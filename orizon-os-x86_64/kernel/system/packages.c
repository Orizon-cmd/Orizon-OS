/*
 * Orizon OS x86_64 - Minimal Package Manager
 *
 * Package format v1 is deliberately text based:
 *
 *   orizon-package 1
 *   name example
 *   version 1.0.0
 *   sha256 <sha256 of every byte after the payload: line>
 *   payload:
 *   file /system/share/example.txt
 *   hello
 *   content-end
 *   post-install
 *   append /workspace/packages/history.log example installed
 *   end-post-install
 */

#include "../include/packages.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/vfs.h"

#define PKG_MAX_BYTES (48U * 1024U)
#define PKG_MAX_LINE 512U
#define PKG_DB_ROOT "/workspace/.orizon/pkgdb"
#define PKG_DB_INSTALLED "/workspace/.orizon/pkgdb/installed"
#define PKG_DB_STORE "/workspace/.orizon/pkgdb/packages"
#define PKG_DB_HISTORY "/workspace/.orizon/pkgdb/history.log"
#define PKG_WORKSPACE_LIST "/workspace/.orizon/packages"
#define PKG_SYSTEM_LIST "/system/packages"
#define PKG_SYSTEM_INSTALLED "/system/installed"
#define PKG_STATUS_PATH "/system/package-status"

typedef struct {
  const char *name;
  const char *version;
  const char *state;
} builtin_package_t;

typedef struct {
  const char *data;
  size_t size;
  size_t pos;
} line_reader_t;

typedef struct {
  char name[64];
  char version[64];
  char sha256[SHA256_HEX_SIZE];
  const char *payload;
  size_t payload_size;
} pkg_manifest_t;

static const builtin_package_t builtin_packages[] = {
    {"orizon-core", "core-x86_64", "builtin"},
    {"orizon-console", "minimal-shell", "builtin"},
    {"orizon-vfs", "workspace-persistence-ahci-nvme", "builtin"},
    {"orizon-net", "ethernet-e1000-rtl8139", "builtin"},
    {"orizon-ipv4", "dhcp-dns-tcp-bootstrap", "builtin"},
    {"orizon-tls", "github-https-range", "builtin"},
    {"orizon-sha256", "artifact-verification", "builtin"},
    {"orizon-manifest", "github-manifest", "builtin"},
    {"orizon-timer", "pit-100hz", "builtin"},
    {"orizon-scheduler", "process-accounting", "builtin"},
    {"orizon-updater", "installed-esp-writer", "builtin"},
    {"orizon-packages", "text-payload-v1", "builtin"},
};

static char pkg_buf[PKG_MAX_BYTES + 1] __attribute__((aligned(4096)));
static const char *pkg_status_text = "package manager ready";
static int pkg_initialized = 0;

static void pkg_append(char *out, size_t out_size, const char *text) {
  size_t used;
  if (!out || out_size == 0 || !text) {
    return;
  }
  used = strlen(out);
  if (used >= out_size - 1) {
    return;
  }
  snprintf(out + used, out_size - used, "%s", text);
}

static void pkg_append_line(char *out, size_t out_size, const char *line) {
  pkg_append(out, out_size, line);
  pkg_append(out, out_size, "\n");
}

static int pkg_starts_with(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int pkg_path_inside(const char *path, const char *prefix) {
  size_t len = strlen(prefix);
  return strncmp(path, prefix, len) == 0 &&
         (path[len] == '\0' || path[len] == '/');
}

static int pkg_component_is_parent(const char *component, size_t len) {
  return len == 2 && component[0] == '.' && component[1] == '.';
}

static int pkg_name_safe(const char *name) {
  int seen = 0;
  if (!name) {
    return 0;
  }
  while (*name) {
    char c = *name++;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      seen = 1;
      continue;
    }
    return 0;
  }
  return seen;
}

static int pkg_path_safe(const char *path) {
  const char *p;

  if (!path || path[0] != '/' || strlen(path) >= MAX_PATH) {
    return 0;
  }
  if (pkg_path_inside(path, "/workspace/.orizon")) {
    return 0;
  }
  if (!(pkg_path_inside(path, "/system") || pkg_path_inside(path, "/home") ||
        pkg_path_inside(path, "/packages") || pkg_path_inside(path, "/logs") ||
        pkg_path_inside(path, "/tmp") || pkg_path_inside(path, "/workspace"))) {
    return 0;
  }

  p = path;
  while (*p) {
    const char *start;
    size_t len = 0;
    while (*p == '/') {
      p++;
    }
    start = p;
    while (p[len] && p[len] != '/') {
      len++;
    }
    if (pkg_component_is_parent(start, len)) {
      return 0;
    }
    p += len;
  }
  return 1;
}

static int pkg_ensure_dir(const char *path) {
  int is_dir = 0;
  if (vfs_stat(path, NULL, &is_dir) == 0) {
    return is_dir ? 0 : -1;
  }
  return vfs_mkdir(path) >= 0 ? 0 : -1;
}

static int pkg_ensure_parent_dirs(const char *path) {
  char cur[MAX_PATH];
  const char *p;

  if (!path || path[0] != '/') {
    return -1;
  }
  strcpy(cur, "/");
  p = path + 1;
  while (*p) {
    const char *start = p;
    size_t len = 0;
    size_t cur_len;

    while (p[len] && p[len] != '/') {
      len++;
    }
    if (p[len] == '\0') {
      break;
    }
    cur_len = strlen(cur);
    if (cur_len > 1) {
      if (cur_len + 1 >= sizeof(cur)) {
        return -1;
      }
      cur[cur_len++] = '/';
      cur[cur_len] = '\0';
    }
    if (cur_len + len >= sizeof(cur)) {
      return -1;
    }
    for (size_t i = 0; i < len; i++) {
      cur[cur_len + i] = start[i];
    }
    cur[cur_len + len] = '\0';
    if (pkg_ensure_dir(cur) < 0) {
      return -1;
    }
    p += len;
    while (*p == '/') {
      p++;
    }
  }
  return 0;
}

static int pkg_write_blob_internal(const char *path, const void *data,
                                   size_t size) {
  file_t *f;
  if (pkg_ensure_parent_dirs(path) < 0) {
    return -1;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -1;
  }
  if (data && size > 0 && vfs_write(f, data, size) != (ssize_t)size) {
    vfs_close(f);
    return -1;
  }
  vfs_close(f);
  return 0;
}

static int pkg_append_text_internal(const char *path, const char *text) {
  file_t *f;
  if (pkg_ensure_parent_dirs(path) < 0) {
    return -1;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | O_APPEND);
  if (!f) {
    return -1;
  }
  if (text && vfs_write(f, text, strlen(text)) < 0) {
    vfs_close(f);
    return -1;
  }
  vfs_close(f);
  return 0;
}

static int pkg_write_text_checked(const char *path, const char *text,
                                  int append) {
  if (!pkg_path_safe(path)) {
    return -1;
  }
  return append ? pkg_append_text_internal(path, text)
                : pkg_write_blob_internal(path, text, text ? strlen(text) : 0);
}

static int pkg_read_file(const char *path, char *buf, size_t cap,
                         size_t *out_size) {
  file_t *f;
  size_t used = 0;
  ssize_t n;

  if (!path || !buf || cap < 2) {
    return -1;
  }
  f = vfs_open(path, O_RDONLY);
  if (!f) {
    return -1;
  }
  while (used < cap - 1 &&
         (n = vfs_read(f, buf + used, (cap - 1) - used)) > 0) {
    used += (size_t)n;
  }
  vfs_close(f);
  if (n < 0) {
    return -1;
  }
  buf[used] = '\0';
  if (out_size) {
    *out_size = used;
  }
  return 0;
}

static int reader_line(line_reader_t *reader, char *line, size_t line_size) {
  size_t len = 0;
  if (!reader || !line || line_size == 0 || reader->pos >= reader->size) {
    return 0;
  }
  while (reader->pos < reader->size) {
    char c = reader->data[reader->pos++];
    if (c == '\n') {
      break;
    }
    if (len + 1 < line_size) {
      line[len++] = c;
    }
  }
  while (len > 0 && line[len - 1] == '\r') {
    len--;
  }
  line[len] = '\0';
  return 1;
}

static void copy_value(char *dst, size_t dst_size, const char *value) {
  if (!dst || dst_size == 0) {
    return;
  }
  while (value && *value == ' ') {
    value++;
  }
  snprintf(dst, dst_size, "%s", value ? value : "");
}

static int hex_is_valid(const char *hex) {
  if (!hex || strlen(hex) != SHA256_HEX_SIZE - 1) {
    return 0;
  }
  for (size_t i = 0; i < SHA256_HEX_SIZE - 1; i++) {
    char c = hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return 0;
    }
  }
  return 1;
}

static int hex_equal(const char *a, const char *b) {
  if (!hex_is_valid(a) || !hex_is_valid(b)) {
    return 0;
  }
  for (size_t i = 0; i < SHA256_HEX_SIZE - 1; i++) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'F') {
      ca = (char)(ca + ('a' - 'A'));
    }
    if (cb >= 'A' && cb <= 'F') {
      cb = (char)(cb + ('a' - 'A'));
    }
    if (ca != cb) {
      return 0;
    }
  }
  return 1;
}

static int parse_manifest(const char *buf, size_t size, pkg_manifest_t *pkg,
                          char actual_hash[SHA256_HEX_SIZE]) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  int saw_magic = 0;
  int saw_payload = 0;

  if (!buf || !pkg || !actual_hash) {
    return -1;
  }
  memset(pkg, 0, sizeof(*pkg));
  reader.data = buf;
  reader.size = size;
  reader.pos = 0;

  while (reader_line(&reader, line, sizeof(line))) {
    if (strcmp(line, "payload:") == 0) {
      saw_payload = 1;
      pkg->payload = buf + reader.pos;
      pkg->payload_size = size - reader.pos;
      break;
    }
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (strcmp(line, "orizon-package 1") == 0) {
      saw_magic = 1;
    } else if (pkg_starts_with(line, "name ")) {
      copy_value(pkg->name, sizeof(pkg->name), line + 5);
    } else if (pkg_starts_with(line, "version ")) {
      copy_value(pkg->version, sizeof(pkg->version), line + 8);
    } else if (pkg_starts_with(line, "sha256 ")) {
      copy_value(pkg->sha256, sizeof(pkg->sha256), line + 7);
    }
  }

  if (!saw_magic || !saw_payload || !pkg_name_safe(pkg->name) ||
      pkg->version[0] == '\0' || !hex_is_valid(pkg->sha256) ||
      !pkg->payload || pkg->payload_size == 0) {
    return -1;
  }

  sha256_buffer_hex(pkg->payload, pkg->payload_size, actual_hash);
  if (!hex_equal(pkg->sha256, actual_hash)) {
    return -2;
  }
  return 0;
}

static int pkg_script_arg_path_text(const char *args, char *path,
                                    size_t path_size, const char **text) {
  size_t len = 0;
  while (args && *args == ' ') {
    args++;
  }
  if (!args || *args == '\0') {
    return -1;
  }
  while (args[len] && args[len] != ' ') {
    len++;
  }
  if (len == 0 || len >= path_size) {
    return -1;
  }
  for (size_t i = 0; i < len; i++) {
    path[i] = args[i];
  }
  path[len] = '\0';
  args += len;
  while (*args == ' ') {
    args++;
  }
  *text = args;
  return 0;
}

static int run_post_install_line(const char *line, char *report,
                                 size_t report_size) {
  char path[MAX_PATH];
  const char *text;

  if (pkg_starts_with(line, "mkdir ")) {
    copy_value(path, sizeof(path), line + 6);
    if (!pkg_path_safe(path) || pkg_ensure_parent_dirs(path) < 0 ||
        pkg_ensure_dir(path) < 0) {
      return -1;
    }
    return 0;
  }
  if (pkg_starts_with(line, "touch ")) {
    copy_value(path, sizeof(path), line + 6);
    return pkg_write_text_checked(path, "", 1);
  }
  if (pkg_starts_with(line, "write ")) {
    if (pkg_script_arg_path_text(line + 6, path, sizeof(path), &text) < 0) {
      return -1;
    }
    return pkg_write_text_checked(path, text, 0);
  }
  if (pkg_starts_with(line, "append ")) {
    if (pkg_script_arg_path_text(line + 7, path, sizeof(path), &text) < 0) {
      return -1;
    }
    if (pkg_write_text_checked(path, text, 1) < 0 ||
        pkg_write_text_checked(path, "\n", 1) < 0) {
      return -1;
    }
    return 0;
  }
  if (pkg_starts_with(line, "echo ")) {
    pkg_append_line(report, report_size, line + 5);
    return 0;
  }
  if (strcmp(line, "sync") == 0) {
    return vfs_persist_save();
  }
  return -1;
}

static int replay_payload(const pkg_manifest_t *pkg, int run_post,
                          char *report, size_t report_size) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  int installed_files = 0;
  int in_post = 0;

  reader.data = pkg->payload;
  reader.size = pkg->payload_size;
  reader.pos = 0;

  while (reader_line(&reader, line, sizeof(line))) {
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (strcmp(line, "post-install") == 0) {
      in_post = 1;
      continue;
    }
    if (strcmp(line, "end-post-install") == 0) {
      in_post = 0;
      continue;
    }
    if (in_post) {
      if (run_post && run_post_install_line(line, report, report_size) < 0) {
        return -2;
      }
      continue;
    }
    if (pkg_starts_with(line, "file ")) {
      char path[MAX_PATH];
      file_t *f;
      int found_end = 0;

      copy_value(path, sizeof(path), line + 5);
      if (!pkg_path_safe(path) || pkg_ensure_parent_dirs(path) < 0) {
        return -3;
      }
      f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
      if (!f) {
        return -4;
      }
      while (reader_line(&reader, line, sizeof(line))) {
        if (strcmp(line, "content-end") == 0) {
          found_end = 1;
          break;
        }
        if (vfs_write(f, line, strlen(line)) < 0 ||
            vfs_write(f, "\n", 1) < 0) {
          vfs_close(f);
          return -5;
        }
      }
      vfs_close(f);
      if (!found_end) {
        return -6;
      }
      installed_files++;
      continue;
    }
    return -7;
  }

  if (installed_files <= 0) {
    return -8;
  }
  return 0;
}

static int package_store_paths(const char *name, char *manifest_path,
                               size_t manifest_path_size, char *meta_path,
                               size_t meta_path_size) {
  if (!pkg_name_safe(name)) {
    return -1;
  }
  snprintf(manifest_path, manifest_path_size, PKG_DB_STORE "/%s.opkg", name);
  snprintf(meta_path, meta_path_size, PKG_DB_INSTALLED "/%s.meta", name);
  return 0;
}

static int store_installed_package(const char *source_path,
                                   const pkg_manifest_t *pkg,
                                   const char *actual_hash,
                                   const char *source_data,
                                   size_t source_size) {
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char meta[512];

  if (package_store_paths(pkg->name, manifest_path, sizeof(manifest_path),
                          meta_path, sizeof(meta_path)) < 0) {
    return -1;
  }
  if (pkg_write_blob_internal(manifest_path, source_data, source_size) < 0) {
    return -1;
  }
  snprintf(meta, sizeof(meta),
           "name %s\n"
           "version %s\n"
           "sha256 %s\n"
           "source %s\n"
           "state installed\n",
           pkg->name, pkg->version, actual_hash,
           source_path ? source_path : "unknown");
  if (pkg_write_blob_internal(meta_path, meta, strlen(meta)) < 0) {
    return -1;
  }
  pkg_append_text_internal(PKG_DB_HISTORY, meta);
  pkg_append_text_internal(PKG_DB_HISTORY, "\n");
  return 0;
}

static int meta_value(const char *text, const char *key, char *out,
                      size_t out_size) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  size_t key_len = strlen(key);

  reader.data = text;
  reader.size = strlen(text);
  reader.pos = 0;
  while (reader_line(&reader, line, sizeof(line))) {
    if (strncmp(line, key, key_len) == 0 && line[key_len] == ' ') {
      copy_value(out, out_size, line + key_len + 1);
      return 0;
    }
  }
  return -1;
}

static void append_installed_meta_list(char *out, size_t out_size,
                                       const char *prefix_state) {
  dirent_t entries[64];
  int count = vfs_readdir(PKG_DB_INSTALLED, entries, 64);
  char meta_path[MAX_PATH];
  char name[64];
  char version[64];
  char line[192];

  if (count < 0) {
    return;
  }
  for (int i = 0; i < count; i++) {
    if (entries[i].type != 0) {
      continue;
    }
    snprintf(meta_path, sizeof(meta_path), PKG_DB_INSTALLED "/%s",
             entries[i].name);
    if (pkg_read_file(meta_path, pkg_buf, sizeof(pkg_buf), NULL) < 0) {
      continue;
    }
    if (meta_value(pkg_buf, "name", name, sizeof(name)) < 0 ||
        meta_value(pkg_buf, "version", version, sizeof(version)) < 0) {
      continue;
    }
    snprintf(line, sizeof(line), "%s %s %s\n", name, version,
             prefix_state ? prefix_state : "installed");
    pkg_append(out, out_size, line);
  }
}

static void write_builtin_db(char *out, size_t out_size, int include_state) {
  char line[192];
  for (size_t i = 0;
       i < sizeof(builtin_packages) / sizeof(builtin_packages[0]); i++) {
    if (include_state) {
      snprintf(line, sizeof(line), "%s %s %s\n", builtin_packages[i].name,
               builtin_packages[i].version, builtin_packages[i].state);
    } else {
      snprintf(line, sizeof(line), "%s %s\n", builtin_packages[i].name,
               builtin_packages[i].version);
    }
    pkg_append(out, out_size, line);
  }
}

static int replay_stored_packages(void) {
  dirent_t entries[64];
  int count = vfs_readdir(PKG_DB_STORE, entries, 64);
  pkg_manifest_t pkg;
  char hash[SHA256_HEX_SIZE];
  char path[MAX_PATH];
  size_t size = 0;

  if (count < 0) {
    return 0;
  }
  for (int i = 0; i < count; i++) {
    if (entries[i].type != 0) {
      continue;
    }
    snprintf(path, sizeof(path), PKG_DB_STORE "/%s", entries[i].name);
    if (pkg_read_file(path, pkg_buf, sizeof(pkg_buf), &size) < 0) {
      continue;
    }
    if (parse_manifest(pkg_buf, size, &pkg, hash) == 0) {
      replay_payload(&pkg, 0, NULL, 0);
    }
  }
  return 0;
}

int orizon_pkg_refresh_database(void) {
  static char list[8192];
  static char installed[8192];

  list[0] = '\0';
  installed[0] = '\0';
  write_builtin_db(list, sizeof(list), 0);
  write_builtin_db(installed, sizeof(installed), 1);
  append_installed_meta_list(list, sizeof(list), NULL);
  append_installed_meta_list(installed, sizeof(installed), "installed");

  pkg_write_blob_internal(PKG_WORKSPACE_LIST, list, strlen(list));
  pkg_write_blob_internal(PKG_SYSTEM_LIST, list, strlen(list));
  pkg_write_blob_internal(PKG_SYSTEM_INSTALLED, installed, strlen(installed));
  pkg_write_blob_internal(PKG_STATUS_PATH, pkg_status_text,
                          strlen(pkg_status_text));
  return 0;
}

int orizon_pkg_init(void) {
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  pkg_ensure_dir(PKG_DB_ROOT);
  pkg_ensure_dir(PKG_DB_INSTALLED);
  pkg_ensure_dir(PKG_DB_STORE);
  pkg_ensure_dir("/system");
  pkg_ensure_dir("/system/share");
  pkg_ensure_dir("/home");
  pkg_ensure_dir("/home/orizon");
  pkg_ensure_dir("/packages");
  pkg_ensure_dir("/logs");
  pkg_ensure_dir("/workspace/packages");

  replay_stored_packages();
  pkg_initialized = 1;
  pkg_status_text = "package manager ready";
  return orizon_pkg_refresh_database();
}

static int pkg_install_loaded(const char *source_name, const char *data,
                              size_t size, char *report, size_t report_size) {
  pkg_manifest_t pkg;
  char actual_hash[SHA256_HEX_SIZE];
  char line[256];
  int result;

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (!data || size == 0 || size > PKG_MAX_BYTES) {
    pkg_append_line(report, report_size, "pkg: invalid package buffer");
    return -1;
  }
  result = parse_manifest(data, size, &pkg, actual_hash);
  if (result == -2) {
    pkg_append_line(report, report_size, "pkg: sha256 mismatch");
    return -2;
  }
  if (result < 0) {
    pkg_append_line(report, report_size, "pkg: invalid package format");
    return -3;
  }

  snprintf(line, sizeof(line), "Installing %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
  snprintf(line, sizeof(line), "Verified sha256 %s", actual_hash);
  pkg_append_line(report, report_size, line);

  if (replay_payload(&pkg, 1, report, report_size) < 0) {
    pkg_append_line(report, report_size, "pkg: payload install failed");
    return -4;
  }
  if (store_installed_package(source_name, &pkg, actual_hash, data, size) < 0) {
    pkg_append_line(report, report_size, "pkg: cannot update installed db");
    return -5;
  }
  pkg_status_text = "package installed";
  orizon_pkg_refresh_database();
  vfs_persist_save();
  snprintf(line, sizeof(line), "Installed %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
  return 0;
}

int orizon_pkg_install_file(const char *path, char *report, size_t report_size) {
  size_t size = 0;

  if (pkg_read_file(path, pkg_buf, sizeof(pkg_buf), &size) < 0 || size == 0) {
    if (report && report_size > 0) {
      report[0] = '\0';
    }
    pkg_append_line(report, report_size, "pkg: cannot read package file");
    return -1;
  }
  return pkg_install_loaded(path, pkg_buf, size, report, report_size);
}

int orizon_pkg_install_buffer(const char *source_name, const void *data,
                              size_t size, char *report, size_t report_size) {
  if (!data || size == 0 || size > PKG_MAX_BYTES) {
    if (report && report_size > 0) {
      report[0] = '\0';
    }
    pkg_append_line(report, report_size, "pkg: invalid package buffer");
    return -1;
  }
  if (data != pkg_buf) {
    memcpy(pkg_buf, data, size);
    pkg_buf[size] = '\0';
  }
  return pkg_install_loaded(source_name ? source_name : "memory", pkg_buf, size,
                            report, report_size);
}

int orizon_pkg_list(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  } else {
    orizon_pkg_refresh_database();
  }
  if (pkg_read_file(PKG_SYSTEM_INSTALLED, out, out_size, NULL) < 0) {
    pkg_append_line(out, out_size, "pkg: installed database unavailable");
    return -1;
  }
  return 0;
}

int orizon_pkg_status(char *out, size_t out_size) {
  char line[160];
  int installed_count = 0;
  dirent_t entries[64];
  int count;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  count = vfs_readdir(PKG_DB_INSTALLED, entries, 64);
  if (count > 0) {
    for (int i = 0; i < count; i++) {
      if (entries[i].type == 0) {
        installed_count++;
      }
    }
  }
  pkg_append_line(out, out_size, "Orizon package manager");
  pkg_append_line(out, out_size, pkg_status_text);
  snprintf(line, sizeof(line), "builtin-packages %lu",
           (unsigned long)(sizeof(builtin_packages) / sizeof(builtin_packages[0])));
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "installed-packages %d", installed_count);
  pkg_append_line(out, out_size, line);
  pkg_append_line(out, out_size, "format orizon-package 1");
  pkg_append_line(out, out_size, "db " PKG_DB_ROOT);
  return 0;
}

int orizon_pkg_hash_file(const char *path, char *out, size_t out_size) {
  pkg_manifest_t pkg;
  char actual_hash[SHA256_HEX_SIZE];
  size_t size = 0;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (pkg_read_file(path, pkg_buf, sizeof(pkg_buf), &size) < 0 || size == 0) {
    pkg_append_line(out, out_size, "pkg hash: cannot read file");
    return -1;
  }
  if (parse_manifest(pkg_buf, size, &pkg, actual_hash) == -2) {
    pkg_append_line(out, out_size, "pkg hash: payload sha256");
    pkg_append_line(out, out_size, actual_hash);
    pkg_append_line(out, out_size, "declared sha256 does not match");
    return -2;
  }
  if (parse_manifest(pkg_buf, size, &pkg, actual_hash) < 0) {
    line_reader_t reader;
    char line[PKG_MAX_LINE];
    const char *payload = NULL;
    size_t payload_size = 0;
    reader.data = pkg_buf;
    reader.size = size;
    reader.pos = 0;
    while (reader_line(&reader, line, sizeof(line))) {
      if (strcmp(line, "payload:") == 0) {
        payload = pkg_buf + reader.pos;
        payload_size = size - reader.pos;
        break;
      }
    }
    if (!payload) {
      pkg_append_line(out, out_size, "pkg hash: missing payload:");
      return -3;
    }
    sha256_buffer_hex(payload, payload_size, actual_hash);
  }
  pkg_append_line(out, out_size, "payload sha256:");
  pkg_append_line(out, out_size, actual_hash);
  return 0;
}

int orizon_pkg_write_sample(char *report, size_t report_size) {
  static const char sample_payload[] =
      "file /system/share/orizon-hello.txt\n"
      "Hello from the Orizon package manager.\n"
      "This file is restored at boot from the installed package store.\n"
      "content-end\n"
      "post-install\n"
      "mkdir /workspace/packages\n"
      "append /workspace/packages/history.log orizon-hello 0.1.0 installed\n"
      "echo post-install: wrote /workspace/packages/history.log\n"
      "end-post-install\n";
  char hash[SHA256_HEX_SIZE];
  char header[256];
  char path[] = "/workspace/packages/orizon-hello.opkg";

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  sha256_buffer_hex(sample_payload, sizeof(sample_payload) - 1, hash);
  snprintf(header, sizeof(header),
           "orizon-package 1\n"
           "name orizon-hello\n"
           "version 0.1.0\n"
           "sha256 %s\n"
           "payload:\n",
           hash);
  if (pkg_write_blob_internal(path, header, strlen(header)) < 0 ||
      pkg_append_text_internal(path, sample_payload) < 0) {
    pkg_append_line(report, report_size, "pkg sample: cannot write sample");
    return -1;
  }
  pkg_append_line(report, report_size,
                  "Sample package written to /workspace/packages/orizon-hello.opkg");
  pkg_append_line(report, report_size,
                  "Run: pkg install /workspace/packages/orizon-hello.opkg");
  vfs_persist_save();
  return 0;
}
