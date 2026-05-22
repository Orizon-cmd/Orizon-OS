/*
 * Orizon OS x86_64 - Minimal Package Manager
 *
 * Package format v1 is deliberately text based:
 *
 *   orizon-package 1
 *   name example
 *   version 1.0.0
 *   depends orizon-core core-x86_64
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
#define PKG_DB_REMOVED "/workspace/.orizon/pkgdb/removed"
#define PKG_DB_CACHE "/workspace/.orizon/pkgdb/cache"
#define PKG_DB_HISTORY "/workspace/.orizon/pkgdb/history.log"
#define PKG_REMOTE_CACHE_STATUS "/workspace/.orizon/pkgdb/cache/remote.status"
#define PKG_REMOTE_INDEX_PATH "/workspace/.orizon/package-index"
#define PKG_WORKSPACE_LIST "/workspace/.orizon/packages"
#define PKG_SYSTEM_LIST "/system/packages"
#define PKG_SYSTEM_INSTALLED "/system/installed"
#define PKG_STATUS_PATH "/system/package-status"
#define PKG_MAX_DEPENDS 8U
#define PKG_MAX_REMOTE_ENTRIES 32U

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
} pkg_dependency_t;

typedef struct {
  char name[64];
  char version[64];
  char sha256[SHA256_HEX_SIZE];
  pkg_dependency_t depends[PKG_MAX_DEPENDS];
  size_t depends_count;
  const char *payload;
  size_t payload_size;
} pkg_manifest_t;

typedef struct {
  char name[64];
  char version[64];
  char path[160];
  char sha256[SHA256_HEX_SIZE];
  size_t size;
} pkg_remote_entry_t;

typedef struct {
  size_t bytes;
  int total_lines;
  int valid_entries;
  int invalid_lines;
  int duplicate_names;
} pkg_remote_validation_t;

static const builtin_package_t builtin_packages[] = {
    {"orizon-core", "core-x86_64", "builtin"},
    {"orizon-console", "minimal-shell", "builtin"},
    {"orizon-vfs", "workspace-persistence-ahci-nvme-virtio", "builtin"},
    {"orizon-net", "ethernet-e1000-rtl8139-virtio", "builtin"},
    {"orizon-ipv4", "dhcp-static-dns-icmp-bootstrap", "builtin"},
    {"orizon-tls", "github-https-range", "builtin"},
    {"orizon-sha256", "artifact-verification", "builtin"},
    {"orizon-manifest", "github-manifest", "builtin"},
    {"orizon-timer", "pit-100hz", "builtin"},
    {"orizon-scheduler", "process-accounting", "builtin"},
    {"orizon-updater", "installed-esp-writer", "builtin"},
    {"orizon-packages", "text-payload-v3", "builtin"},
};

static char pkg_buf[PKG_MAX_BYTES + 1] __attribute__((aligned(4096)));
static char pkg_rollback_buf[PKG_MAX_BYTES + 1] __attribute__((aligned(4096)));
static char pkg_rollback_meta[1024];
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

static int pkg_version_safe(const char *version) {
  int seen = 0;
  if (!version) {
    return 0;
  }
  while (*version) {
    char c = *version++;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '+' || c == ':' || c == '*') {
      seen = 1;
      continue;
    }
    return 0;
  }
  return seen;
}

static const char *pkg_copy_token(const char *p, char *out, size_t out_size) {
  size_t len = 0;

  if (!p || !out || out_size == 0) {
    return NULL;
  }
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  while (p[len] && p[len] != ' ' && p[len] != '\t' && p[len] != '\r' &&
         p[len] != '\n') {
    len++;
  }
  if (len == 0 || len >= out_size) {
    return NULL;
  }
  memcpy(out, p, len);
  out[len] = '\0';
  return p + len;
}

static int pkg_version_matches(const char *wanted, const char *actual) {
  return wanted && actual &&
         (strcmp(wanted, "*") == 0 || strcmp(wanted, actual) == 0);
}

static const builtin_package_t *find_builtin_package(const char *name) {
  if (!name) {
    return NULL;
  }
  for (size_t i = 0;
       i < sizeof(builtin_packages) / sizeof(builtin_packages[0]); i++) {
    if (strcmp(builtin_packages[i].name, name) == 0) {
      return &builtin_packages[i];
    }
  }
  return NULL;
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

static int pkg_remote_path_safe(const char *path) {
  size_t len;

  if (!path || path[0] == '/' ||
      strncmp(path, "packages/x86_64/", 16) != 0) {
    return 0;
  }
  len = strlen(path);
  if (len <= 5 || strcmp(path + len - 5, ".opkg") != 0) {
    return 0;
  }
  while (*path) {
    char c = *path++;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      return 0;
    }
    if (c == '.' && path[0] == '.') {
      return 0;
    }
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

static int pkg_parse_size_value(const char *text, size_t *out) {
  size_t value = 0;
  int seen = 0;

  if (!text || !out) {
    return -1;
  }
  while (*text == ' ' || *text == '\t') {
    text++;
  }
  while (*text && *text != ' ' && *text != '\t' && *text != '\r' &&
         *text != '\n') {
    if (*text < '0' || *text > '9') {
      return -1;
    }
    value = (value * 10U) + (size_t)(*text - '0');
    if (value > PKG_MAX_BYTES) {
      return -1;
    }
    seen = 1;
    text++;
  }
  if (!seen || value == 0) {
    return -1;
  }
  *out = value;
  return 0;
}

static int parse_remote_index_line(const char *line,
                                   pkg_remote_entry_t *entry) {
  const char *p;
  char size_text[32];

  if (!line || !entry || !pkg_starts_with(line, "package ")) {
    return -1;
  }
  memset(entry, 0, sizeof(*entry));
  p = line + 8;
  p = pkg_copy_token(p, entry->name, sizeof(entry->name));
  if (!p) {
    return -1;
  }
  p = pkg_copy_token(p, entry->version, sizeof(entry->version));
  if (!p) {
    return -1;
  }
  p = pkg_copy_token(p, entry->path, sizeof(entry->path));
  if (!p) {
    return -1;
  }
  p = pkg_copy_token(p, size_text, sizeof(size_text));
  if (!p || pkg_parse_size_value(size_text, &entry->size) < 0) {
    return -1;
  }
  p = pkg_copy_token(p, entry->sha256, sizeof(entry->sha256));
  if (!p || !pkg_name_safe(entry->name) ||
      !pkg_version_safe(entry->version) ||
      !pkg_remote_path_safe(entry->path) || !hex_is_valid(entry->sha256)) {
    return -1;
  }
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p != '\0') {
    return -1;
  }
  return 0;
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
    } else if (pkg_starts_with(line, "depends ")) {
      const char *p;
      if (pkg->depends_count >= PKG_MAX_DEPENDS) {
        return -1;
      }
      p = pkg_copy_token(line + 8, pkg->depends[pkg->depends_count].name,
                         sizeof(pkg->depends[pkg->depends_count].name));
      p = pkg_copy_token(p, pkg->depends[pkg->depends_count].version,
                         sizeof(pkg->depends[pkg->depends_count].version));
      if (!p || !pkg_name_safe(pkg->depends[pkg->depends_count].name) ||
          !pkg_version_safe(pkg->depends[pkg->depends_count].version)) {
        return -1;
      }
      pkg->depends_count++;
    } else if (pkg_starts_with(line, "sha256 ")) {
      copy_value(pkg->sha256, sizeof(pkg->sha256), line + 7);
    }
  }

  if (!saw_magic || !saw_payload || !pkg_name_safe(pkg->name) ||
      !pkg_version_safe(pkg->version) || !hex_is_valid(pkg->sha256) ||
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

static int run_payload_script(const pkg_manifest_t *pkg, const char *begin,
                              const char *end, char *report,
                              size_t report_size) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  int in_file = 0;
  int in_script = 0;
  int saw_script = 0;

  if (!pkg || !begin || !end) {
    return -1;
  }
  reader.data = pkg->payload;
  reader.size = pkg->payload_size;
  reader.pos = 0;

  while (reader_line(&reader, line, sizeof(line))) {
    if (in_file) {
      if (strcmp(line, "content-end") == 0) {
        in_file = 0;
      }
      continue;
    }
    if (!in_script && pkg_starts_with(line, "file ")) {
      in_file = 1;
      continue;
    }
    if (strcmp(line, begin) == 0) {
      in_script = 1;
      saw_script = 1;
      continue;
    }
    if (strcmp(line, end) == 0) {
      if (in_script) {
        return 0;
      }
      continue;
    }
    if (in_script && run_post_install_line(line, report, report_size) < 0) {
      return -2;
    }
  }
  return in_script ? -3 : (saw_script ? 0 : 1);
}

static void append_package_scripts(const pkg_manifest_t *pkg, char *out,
                                   size_t out_size) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  int in_file = 0;
  int post_install = 0;
  int pre_remove = 0;
  int post_remove = 0;
  char scripts[128];

  if (!pkg || !out || out_size == 0) {
    return;
  }
  reader.data = pkg->payload;
  reader.size = pkg->payload_size;
  reader.pos = 0;
  while (reader_line(&reader, line, sizeof(line))) {
    if (in_file) {
      if (strcmp(line, "content-end") == 0) {
        in_file = 0;
      }
      continue;
    }
    if (pkg_starts_with(line, "file ")) {
      in_file = 1;
      continue;
    }
    if (strcmp(line, "post-install") == 0) {
      post_install = 1;
    } else if (strcmp(line, "pre-remove") == 0) {
      pre_remove = 1;
    } else if (strcmp(line, "post-remove") == 0) {
      post_remove = 1;
    }
  }

  scripts[0] = '\0';
  if (post_install) {
    pkg_append(scripts, sizeof(scripts), " post-install");
  }
  if (pre_remove) {
    pkg_append(scripts, sizeof(scripts), " pre-remove");
  }
  if (post_remove) {
    pkg_append(scripts, sizeof(scripts), " post-remove");
  }
  if (scripts[0] == '\0') {
    pkg_append_line(out, out_size, "scripts: none");
  } else {
    char line_out[160];
    snprintf(line_out, sizeof(line_out), "scripts:%s", scripts);
    pkg_append_line(out, out_size, line_out);
  }
}

static int replay_payload(const pkg_manifest_t *pkg, int run_post,
                          char *report, size_t report_size) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  int installed_files = 0;
  int in_post = 0;
  int in_remove_script = 0;

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
    if (strcmp(line, "pre-remove") == 0 ||
        strcmp(line, "post-remove") == 0) {
      in_remove_script = 1;
      continue;
    }
    if (strcmp(line, "end-pre-remove") == 0 ||
        strcmp(line, "end-post-remove") == 0) {
      in_remove_script = 0;
      continue;
    }
    if (in_remove_script) {
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

static int package_removed_paths(const char *name, char *manifest_path,
                                 size_t manifest_path_size, char *meta_path,
                                 size_t meta_path_size) {
  if (!pkg_name_safe(name)) {
    return -1;
  }
  snprintf(manifest_path, manifest_path_size, PKG_DB_REMOVED "/%s.opkg", name);
  snprintf(meta_path, meta_path_size, PKG_DB_REMOVED "/%s.meta", name);
  return 0;
}

static int pkg_count_regular_files(const char *path) {
  dirent_t entries[64];
  int count;
  int files = 0;

  if (!path) {
    return 0;
  }
  count = vfs_readdir(path, entries, 64);
  if (count <= 0) {
    return 0;
  }
  for (int i = 0; i < count; i++) {
    if (entries[i].type == 0) {
      files++;
    }
  }
  return files;
}

static int store_installed_package(const char *source_path,
                                   const pkg_manifest_t *pkg,
                                   const char *actual_hash,
                                   const char *source_data,
                                   size_t source_size) {
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char meta[512];
  char event[256];

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
  snprintf(event, sizeof(event),
           "installed %s %s source=%s sha256=%s transaction=v3\n",
           pkg->name, pkg->version, source_path ? source_path : "unknown",
           actual_hash);
  pkg_append_text_internal(PKG_DB_HISTORY, event);
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

static int package_dependency_satisfied(const pkg_dependency_t *dep) {
  const builtin_package_t *builtin;
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char meta[512];
  char version[64];

  if (!dep || !pkg_name_safe(dep->name) || !pkg_version_safe(dep->version)) {
    return 0;
  }
  builtin = find_builtin_package(dep->name);
  if (builtin && pkg_version_matches(dep->version, builtin->version)) {
    return 1;
  }
  if (package_store_paths(dep->name, manifest_path, sizeof(manifest_path),
                          meta_path, sizeof(meta_path)) < 0 ||
      pkg_read_file(meta_path, meta, sizeof(meta), NULL) < 0 ||
      meta_value(meta, "version", version, sizeof(version)) < 0) {
    return 0;
  }
  return pkg_version_matches(dep->version, version);
}

static int package_current_version(const char *name, char *version,
                                   size_t version_size, char *origin,
                                   size_t origin_size) {
  const builtin_package_t *builtin;
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char meta[512];

  if (!pkg_name_safe(name) || !version || version_size == 0) {
    return -1;
  }
  builtin = find_builtin_package(name);
  if (builtin) {
    snprintf(version, version_size, "%s", builtin->version);
    if (origin && origin_size > 0) {
      snprintf(origin, origin_size, "%s", "builtin");
    }
    return 0;
  }
  if (package_store_paths(name, manifest_path, sizeof(manifest_path),
                          meta_path, sizeof(meta_path)) < 0 ||
      pkg_read_file(meta_path, meta, sizeof(meta), NULL) < 0 ||
      meta_value(meta, "version", version, version_size) < 0) {
    return -1;
  }
  if (origin && origin_size > 0) {
    snprintf(origin, origin_size, "%s", "installed");
  }
  return 0;
}

static int check_package_dependencies(const pkg_manifest_t *pkg, char *report,
                                      size_t report_size) {
  char line[192];
  int missing = 0;

  if (!pkg) {
    return -1;
  }
  if (pkg->depends_count == 0) {
    pkg_append_line(report, report_size, "Dependencies: none");
    return 0;
  }
  for (size_t i = 0; i < pkg->depends_count; i++) {
    const pkg_dependency_t *dep = &pkg->depends[i];
    if (package_dependency_satisfied(dep)) {
      snprintf(line, sizeof(line), "Dependency OK: %s %s", dep->name,
               dep->version);
      pkg_append_line(report, report_size, line);
      continue;
    }
    snprintf(line, sizeof(line), "pkg: missing dependency %s %s", dep->name,
             dep->version);
    pkg_append_line(report, report_size, line);
    missing = 1;
  }
  return missing ? -1 : 0;
}

static void append_package_dependencies(const pkg_manifest_t *pkg, char *out,
                                        size_t out_size) {
  char line[192];

  if (!pkg || !out || out_size == 0) {
    return;
  }
  if (pkg->depends_count == 0) {
    pkg_append_line(out, out_size, "dependencies: none");
    return;
  }
  pkg_append_line(out, out_size, "dependencies:");
  for (size_t i = 0; i < pkg->depends_count; i++) {
    snprintf(line, sizeof(line), "  %s %s %s", pkg->depends[i].name,
             pkg->depends[i].version,
             package_dependency_satisfied(&pkg->depends[i]) ? "ok"
                                                            : "missing");
    pkg_append_line(out, out_size, line);
  }
}

static void append_payload_files(const pkg_manifest_t *pkg, char *out,
                                 size_t out_size) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  int files = 0;
  int in_post = 0;
  int in_file = 0;
  int in_remove_script = 0;

  if (!pkg || !out || out_size == 0) {
    return;
  }
  reader.data = pkg->payload;
  reader.size = pkg->payload_size;
  reader.pos = 0;

  while (reader_line(&reader, line, sizeof(line))) {
    if (in_file) {
      if (strcmp(line, "content-end") == 0) {
        in_file = 0;
      }
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
    if (strcmp(line, "pre-remove") == 0 ||
        strcmp(line, "post-remove") == 0) {
      in_remove_script = 1;
      continue;
    }
    if (strcmp(line, "end-pre-remove") == 0 ||
        strcmp(line, "end-post-remove") == 0) {
      in_remove_script = 0;
      continue;
    }
    if (in_post || in_remove_script || !pkg_starts_with(line, "file ")) {
      continue;
    }
    if (files == 0) {
      pkg_append_line(out, out_size, "files:");
    }
    pkg_append(out, out_size, "  ");
    pkg_append_line(out, out_size, line + 5);
    in_file = 1;
    files++;
  }
  if (files == 0) {
    pkg_append_line(out, out_size, "files: none");
  }
}

static int remove_payload_files(const pkg_manifest_t *pkg, char *report,
                                size_t report_size) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  int removed = 0;
  int skipped = 0;
  int in_post = 0;
  int in_file = 0;
  int in_remove_script = 0;

  if (!pkg) {
    return -1;
  }
  reader.data = pkg->payload;
  reader.size = pkg->payload_size;
  reader.pos = 0;

  while (reader_line(&reader, line, sizeof(line))) {
    if (in_file) {
      if (strcmp(line, "content-end") == 0) {
        in_file = 0;
      }
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
    if (strcmp(line, "pre-remove") == 0 ||
        strcmp(line, "post-remove") == 0) {
      in_remove_script = 1;
      continue;
    }
    if (strcmp(line, "end-pre-remove") == 0 ||
        strcmp(line, "end-post-remove") == 0) {
      in_remove_script = 0;
      continue;
    }
    if (in_post || in_remove_script || !pkg_starts_with(line, "file ")) {
      continue;
    }

    char path[MAX_PATH];
    int is_dir = 0;
    copy_value(path, sizeof(path), line + 5);
    in_file = 1;
    if (!pkg_path_safe(path)) {
      skipped++;
      continue;
    }
    if (vfs_stat(path, NULL, &is_dir) < 0) {
      skipped++;
      continue;
    }
    if (is_dir || vfs_delete(path) < 0) {
      skipped++;
      continue;
    }
    removed++;
  }

  snprintf(line, sizeof(line), "pkg: removed-files %d skipped %d", removed,
           skipped);
  pkg_append_line(report, report_size, line);
  return 0;
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

static int pkg_text_matches_query(const char *text, const char *query) {
  return !query || query[0] == '\0' || (text && strstr(text, query) != NULL);
}

static int append_remote_index_entries(const char *query, char *out,
                                       size_t out_size, int *match_count) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  char entry_line[320];
  size_t size = 0;
  int entries = 0;

  if (match_count) {
    *match_count = 0;
  }
  if (pkg_read_file(PKG_REMOTE_INDEX_PATH, pkg_rollback_buf,
                    sizeof(pkg_rollback_buf), &size) < 0 ||
      size == 0) {
    return -1;
  }
  reader.data = pkg_rollback_buf;
  reader.size = size;
  reader.pos = 0;
  while (reader_line(&reader, line, sizeof(line))) {
    pkg_remote_entry_t entry;
    if (!pkg_starts_with(line, "package ") ||
        parse_remote_index_line(line, &entry) < 0) {
      continue;
    }
    if (!pkg_text_matches_query(entry.name, query) &&
        !pkg_text_matches_query(entry.version, query) &&
        !pkg_text_matches_query(entry.path, query)) {
      continue;
    }
    snprintf(entry_line, sizeof(entry_line),
             "remote %s %s size=%lu sha256=%s path=%s", entry.name,
             entry.version, (unsigned long)entry.size, entry.sha256,
             entry.path);
    pkg_append_line(out, out_size, entry_line);
    entries++;
  }
  if (match_count) {
    *match_count = entries;
  }
  return 0;
}

static int remote_name_seen(char names[PKG_MAX_REMOTE_ENTRIES][64], size_t count,
                            const char *name) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(names[i], name) == 0) {
      return 1;
    }
  }
  return 0;
}

static void pkg_write_remote_cache_status(const pkg_remote_validation_t *stats,
                                          int rc) {
  char status[256];
  if (!stats) {
    return;
  }
  snprintf(status, sizeof(status),
           "remote-index-valid %s\n"
           "entries %d\n"
           "invalid-lines %d\n"
           "duplicate-names %d\n"
           "bytes %lu\n",
           rc == 0 ? "yes" : "no", stats->valid_entries,
           stats->invalid_lines, stats->duplicate_names,
           (unsigned long)stats->bytes);
  pkg_write_blob_internal(PKG_REMOTE_CACHE_STATUS, status, strlen(status));
}

static int pkg_validate_remote_index(pkg_remote_validation_t *stats, char *out,
                                     size_t out_size, int verbose) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  char seen_names[PKG_MAX_REMOTE_ENTRIES][64];
  size_t seen_count = 0;
  int warnings = 0;
  size_t size = 0;
  int rc;

  if (stats) {
    memset(stats, 0, sizeof(*stats));
  }
  memset(seen_names, 0, sizeof(seen_names));
  if (pkg_read_file(PKG_REMOTE_INDEX_PATH, pkg_rollback_buf,
                    sizeof(pkg_rollback_buf), &size) < 0 ||
      size == 0) {
    if (verbose) {
      pkg_append_line(out, out_size, "cached-index=no");
    }
    return -1;
  }
  if (stats) {
    stats->bytes = size;
  }

  reader.data = pkg_rollback_buf;
  reader.size = size;
  reader.pos = 0;
  while (reader_line(&reader, line, sizeof(line))) {
    pkg_remote_entry_t entry;
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (stats) {
      stats->total_lines++;
    }
    if (!pkg_starts_with(line, "package ") ||
        parse_remote_index_line(line, &entry) < 0) {
      if (stats) {
        stats->invalid_lines++;
      }
      if (verbose && warnings < 6) {
        char warn[192];
        snprintf(warn, sizeof(warn), "WARN invalid-index-line %d",
                 stats ? stats->total_lines : warnings + 1);
        pkg_append_line(out, out_size, warn);
        warnings++;
      }
      continue;
    }
    if (remote_name_seen(seen_names, seen_count, entry.name)) {
      if (stats) {
        stats->duplicate_names++;
      }
      if (verbose && warnings < 6) {
        char warn[192];
        snprintf(warn, sizeof(warn), "WARN duplicate-package %s", entry.name);
        pkg_append_line(out, out_size, warn);
        warnings++;
      }
    } else if (seen_count < PKG_MAX_REMOTE_ENTRIES) {
      snprintf(seen_names[seen_count], sizeof(seen_names[seen_count]), "%s",
               entry.name);
      seen_count++;
    } else {
      if (stats) {
        stats->duplicate_names++;
      }
      if (verbose && warnings < 6) {
        pkg_append_line(out, out_size,
                        "WARN remote-index-too-large-for-duplicate-scan");
        warnings++;
      }
    }
    if (stats) {
      stats->valid_entries++;
    }
  }

  rc = (stats && (stats->invalid_lines > 0 || stats->duplicate_names > 0)) ? 1
                                                                           : 0;
  if (verbose && stats) {
    char summary[192];
    snprintf(summary, sizeof(summary), "cached-index=yes bytes=%lu",
             (unsigned long)stats->bytes);
    pkg_append_line(out, out_size, summary);
    snprintf(summary, sizeof(summary),
             "remote-index-valid=%s entries=%d invalid-lines=%d "
             "duplicate-names=%d",
             rc == 0 ? "yes" : "no", stats->valid_entries,
             stats->invalid_lines, stats->duplicate_names);
    pkg_append_line(out, out_size, summary);
    pkg_append_line(out, out_size,
                    "auth signed-update-manifest-sha256-pinned");
    pkg_append_line(out, out_size, "cache-status " PKG_REMOTE_CACHE_STATUS);
  }
  if (stats) {
    pkg_write_remote_cache_status(stats, rc);
  }
  return rc;
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
  pkg_ensure_dir(PKG_DB_REMOVED);
  pkg_ensure_dir(PKG_DB_CACHE);
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
  pkg_manifest_t old_pkg;
  char actual_hash[SHA256_HEX_SIZE];
  char old_hash[SHA256_HEX_SIZE];
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char line[256];
  int result;
  int had_old = 0;
  size_t old_size = 0;
  size_t old_meta_size = 0;

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
  if (check_package_dependencies(&pkg, report, report_size) < 0) {
    return -4;
  }

  if (package_store_paths(pkg.name, manifest_path, sizeof(manifest_path),
                          meta_path, sizeof(meta_path)) < 0) {
    pkg_append_line(report, report_size, "pkg: invalid package store path");
    return -4;
  }
  if (pkg_read_file(manifest_path, pkg_rollback_buf, sizeof(pkg_rollback_buf),
                    &old_size) == 0 &&
      parse_manifest(pkg_rollback_buf, old_size, &old_pkg, old_hash) == 0) {
    had_old = 1;
    pkg_read_file(meta_path, pkg_rollback_meta, sizeof(pkg_rollback_meta),
                  &old_meta_size);
    snprintf(line, sizeof(line),
             "pkg: previous version %s staged for failure rollback",
             old_pkg.version);
    pkg_append_line(report, report_size, line);
  }

  if (replay_payload(&pkg, 1, report, report_size) < 0) {
    pkg_append_line(report, report_size, "pkg: payload install failed");
    remove_payload_files(&pkg, report, report_size);
    if (had_old) {
      replay_payload(&old_pkg, 0, NULL, 0);
      pkg_append_line(report, report_size,
                      "pkg: rollback restored previous package payload");
    }
    return -5;
  }
  if (store_installed_package(source_name, &pkg, actual_hash, data, size) < 0) {
    pkg_append_line(report, report_size, "pkg: cannot update installed db");
    remove_payload_files(&pkg, report, report_size);
    if (had_old) {
      pkg_write_blob_internal(manifest_path, pkg_rollback_buf, old_size);
      if (old_meta_size > 0) {
        pkg_write_blob_internal(meta_path, pkg_rollback_meta, old_meta_size);
      }
      replay_payload(&old_pkg, 0, NULL, 0);
      pkg_append_line(report, report_size,
                      "pkg: rollback restored previous installed metadata");
    } else {
      vfs_delete(meta_path);
      vfs_delete(manifest_path);
    }
    return -6;
  }
  if (had_old) {
    snprintf(line, sizeof(line), "Replaced previous version %s", old_pkg.version);
    pkg_append_line(report, report_size, line);
    snprintf(line, sizeof(line), "upgraded %s %s -> %s rollback=guarded",
             pkg.name, old_pkg.version, pkg.version);
    pkg_append_text_internal(PKG_DB_HISTORY, line);
    pkg_append_text_internal(PKG_DB_HISTORY, "\n");
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
  pkg_remote_validation_t remote_stats;
  int installed_count = 0;
  int removed_count = 0;
  int cached_count = 0;
  int cache_meta_count = 0;
  int remote_cached = 0;
  int remote_valid_rc = -1;
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
  removed_count = pkg_count_regular_files(PKG_DB_REMOVED);
  cached_count = pkg_count_regular_files(PKG_DB_STORE);
  cache_meta_count = pkg_count_regular_files(PKG_DB_CACHE);
  remote_cached = vfs_stat(PKG_REMOTE_INDEX_PATH, NULL, NULL) == 0;
  if (remote_cached) {
    remote_valid_rc = pkg_validate_remote_index(&remote_stats, NULL, 0, 0);
  } else {
    memset(&remote_stats, 0, sizeof(remote_stats));
  }
  pkg_append_line(out, out_size, "Orizon package manager");
  pkg_append_line(out, out_size, pkg_status_text);
  snprintf(line, sizeof(line), "builtin-packages %lu",
           (unsigned long)(sizeof(builtin_packages) / sizeof(builtin_packages[0])));
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "installed-packages %d", installed_count);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "cached-packages %d", cached_count);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "cache-metadata %d", cache_meta_count);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "remove-rollback-snapshots %d",
           removed_count / 2);
  pkg_append_line(out, out_size, line);
  pkg_append_line(out, out_size,
                  "format orizon-package 1 manager-v2=yes manager-v3=yes");
  pkg_append_line(out, out_size, "dependencies depends <name> <version|*>");
  pkg_append_line(out, out_size,
                  "scripts post-install pre-remove post-remove");
  pkg_append_line(out, out_size,
                  "script-policy allow=mkdir,touch,write,append,echo,sync "
                  "safe-paths=/system,/home,/packages,/logs,/tmp,/workspace");
  pkg_append_line(out, out_size,
                  "rollback install-restores-previous-payload remove-cache="
                  PKG_DB_REMOVED);
  pkg_append_line(out, out_size,
                  "remote-index-auth signed-update-manifest-sha256-pinned");
  snprintf(line, sizeof(line), "remote-index cached=%s path=%s",
           remote_cached ? "yes" : "no", PKG_REMOTE_INDEX_PATH);
  pkg_append_line(out, out_size, line);
  if (remote_cached) {
    snprintf(line, sizeof(line),
             "remote-index-valid=%s entries=%d invalid-lines=%d "
             "duplicate-names=%d",
             remote_valid_rc == 0 ? "yes" : "no",
             remote_stats.valid_entries, remote_stats.invalid_lines,
             remote_stats.duplicate_names);
    pkg_append_line(out, out_size, line);
  }
  pkg_append_line(out, out_size,
                  "upgrade-plan pkg upgrade plan; apply pkg upgrade");
  pkg_append_line(out, out_size,
                  "commands pkg search <query>, pkg remote [verify], "
                  "pkg upgrade [plan], pkg rollback <name>");
  pkg_append_line(out, out_size, "db " PKG_DB_ROOT);
  return 0;
}

int orizon_pkg_remote(char *out, size_t out_size) {
  pkg_remote_validation_t validation;
  int remote_matches = 0;
  int validation_rc = -1;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  pkg_append_line(out, out_size, "package remote:");
  pkg_append_line(out, out_size,
                  "auth signed-update-manifest-sha256-pinned");
  pkg_append_line(out, out_size, "path " PKG_REMOTE_INDEX_PATH);
  if (append_remote_index_entries(NULL, out, out_size, &remote_matches) < 0) {
    pkg_append_line(out, out_size, "cached-index=no");
    pkg_append_line(out, out_size,
                    "hint: run pkg update after disk install to refresh it");
    return 1;
  }
  validation_rc = pkg_validate_remote_index(&validation, NULL, 0, 0);
  pkg_append_line(out, out_size, "cached-index=yes");
  if (validation_rc >= 0) {
    char line[192];
    snprintf(line, sizeof(line),
             "remote-index-valid=%s entries=%d invalid-lines=%d "
             "duplicate-names=%d",
             validation_rc == 0 ? "yes" : "no", validation.valid_entries,
             validation.invalid_lines, validation.duplicate_names);
    pkg_append_line(out, out_size, line);
  }
  if (remote_matches == 0) {
    pkg_append_line(out, out_size, "remote packages: none");
  }
  return 0;
}

int orizon_pkg_remote_verify(char *out, size_t out_size) {
  pkg_remote_validation_t validation;
  int rc;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  pkg_append_line(out, out_size, "pkg remote verify:");
  pkg_append_line(out, out_size, "path " PKG_REMOTE_INDEX_PATH);
  rc = pkg_validate_remote_index(&validation, out, out_size, 1);
  if (rc < 0) {
    pkg_append_line(out, out_size,
                    "hint: run pkg update after disk install to refresh it");
    return 1;
  }
  if (rc == 0) {
    pkg_append_line(out, out_size, "package-index verification: OK");
    return 0;
  }
  pkg_append_line(out, out_size,
                  "package-index verification: FAIL invalid cached index");
  return 1;
}

int orizon_pkg_upgrade_plan(char *out, size_t out_size) {
  pkg_remote_validation_t validation;
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  size_t size = 0;
  int validation_rc;
  int install_count = 0;
  int upgrade_count = 0;
  int current_count = 0;
  int protected_count = 0;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }

  pkg_append_line(out, out_size, "pkg upgrade plan:");
  pkg_append_line(out, out_size,
                  "auth signed-update-manifest-sha256-pinned");
  pkg_append_line(out, out_size, "remote-index " PKG_REMOTE_INDEX_PATH);
  validation_rc = pkg_validate_remote_index(&validation, NULL, 0, 0);
  if (validation_rc < 0) {
    pkg_append_line(out, out_size, "cached-index=no");
    pkg_append_line(out, out_size,
                    "action: run pkg update/pkg upgrade after disk install "
                    "to fetch the signed package index");
    return 1;
  }
  snprintf(line, sizeof(line),
           "cached-index=yes valid=%s entries=%d invalid-lines=%d "
           "duplicate-names=%d",
           validation_rc == 0 ? "yes" : "no", validation.valid_entries,
           validation.invalid_lines, validation.duplicate_names);
  pkg_append_line(out, out_size, line);

  if (pkg_read_file(PKG_REMOTE_INDEX_PATH, pkg_rollback_buf,
                    sizeof(pkg_rollback_buf), &size) < 0 ||
      size == 0) {
    pkg_append_line(out, out_size, "pkg upgrade plan: cannot read index");
    return 1;
  }

  reader.data = pkg_rollback_buf;
  reader.size = size;
  reader.pos = 0;
  while (reader_line(&reader, line, sizeof(line))) {
    pkg_remote_entry_t entry;
    char current_version[64];
    char origin[32];
    char entry_line[256];

    if (!pkg_starts_with(line, "package ") ||
        parse_remote_index_line(line, &entry) < 0) {
      continue;
    }
    if (package_current_version(entry.name, current_version,
                                sizeof(current_version), origin,
                                sizeof(origin)) < 0) {
      snprintf(entry_line, sizeof(entry_line),
               "install %s %s size=%lu sha256=%s", entry.name, entry.version,
               (unsigned long)entry.size, entry.sha256);
      pkg_append_line(out, out_size, entry_line);
      install_count++;
      continue;
    }
    if (strcmp(current_version, entry.version) == 0) {
      snprintf(entry_line, sizeof(entry_line), "current %s %s source=%s",
               entry.name, entry.version, origin);
      pkg_append_line(out, out_size, entry_line);
      current_count++;
      continue;
    }
    if (strcmp(origin, "builtin") == 0) {
      snprintf(entry_line, sizeof(entry_line),
               "protected-builtin %s %s -> %s use OS update", entry.name,
               current_version, entry.version);
      pkg_append_line(out, out_size, entry_line);
      protected_count++;
      continue;
    }
    snprintf(entry_line, sizeof(entry_line), "upgrade %s %s -> %s",
             entry.name, current_version, entry.version);
    pkg_append_line(out, out_size, entry_line);
    upgrade_count++;
  }

  snprintf(line, sizeof(line),
           "summary install=%d upgrade=%d current=%d protected=%d",
           install_count, upgrade_count, current_count, protected_count);
  pkg_append_line(out, out_size, line);
  pkg_append_line(out, out_size,
                  "rollback package-transaction=previous-payload-on-failure "
                  "boot-rollback=update-bootguard");
  if (validation_rc != 0) {
    pkg_append_line(out, out_size,
                    "action: fix signed package index before upgrade");
    return 1;
  }
  if (install_count == 0 && upgrade_count == 0 && protected_count == 0) {
    pkg_append_line(out, out_size, "action: nothing to upgrade");
    return 0;
  }
  pkg_append_line(out, out_size,
                  "action: pkg upgrade runs signed update/package refresh");
  return 0;
}

int orizon_pkg_search(const char *query, char *out, size_t out_size) {
  dirent_t entries[64];
  int count;
  int matches = 0;
  int remote_matches = 0;
  char line[256];

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }

  snprintf(line, sizeof(line), "pkg search: %s",
           (query && query[0]) ? query : "<all>");
  pkg_append_line(out, out_size, line);
  for (size_t i = 0;
       i < sizeof(builtin_packages) / sizeof(builtin_packages[0]); i++) {
    if (!pkg_text_matches_query(builtin_packages[i].name, query) &&
        !pkg_text_matches_query(builtin_packages[i].version, query) &&
        !pkg_text_matches_query(builtin_packages[i].state, query)) {
      continue;
    }
    snprintf(line, sizeof(line), "builtin %s %s %s",
             builtin_packages[i].name, builtin_packages[i].version,
             builtin_packages[i].state);
    pkg_append_line(out, out_size, line);
    matches++;
  }

  count = vfs_readdir(PKG_DB_INSTALLED, entries, 64);
  if (count > 0) {
    for (int i = 0; i < count; i++) {
      char meta_path[MAX_PATH];
      char name[64];
      char version[64];
      char source[96];
      if (entries[i].type != 0) {
        continue;
      }
      snprintf(meta_path, sizeof(meta_path), PKG_DB_INSTALLED "/%s",
               entries[i].name);
      if (pkg_read_file(meta_path, pkg_buf, sizeof(pkg_buf), NULL) < 0 ||
          meta_value(pkg_buf, "name", name, sizeof(name)) < 0 ||
          meta_value(pkg_buf, "version", version, sizeof(version)) < 0) {
        continue;
      }
      if (!pkg_text_matches_query(name, query) &&
          !pkg_text_matches_query(version, query) &&
          !pkg_text_matches_query(pkg_buf, query)) {
        continue;
      }
      if (meta_value(pkg_buf, "source", source, sizeof(source)) < 0) {
        strcpy(source, "unknown");
      }
      snprintf(line, sizeof(line), "installed %s %s source=%s", name,
               version, source);
      pkg_append_line(out, out_size, line);
      matches++;
    }
  }

  if (append_remote_index_entries(query, out, out_size, &remote_matches) == 0) {
    matches += remote_matches;
  } else {
    pkg_append_line(out, out_size, "remote-index cached=no");
  }
  snprintf(line, sizeof(line), "matches %d", matches);
  pkg_append_line(out, out_size, line);
  if (matches == 0) {
    pkg_append_line(out, out_size, "pkg search: no match");
  }
  return matches > 0 ? 0 : 1;
}

int orizon_pkg_info(const char *name, char *out, size_t out_size) {
  const builtin_package_t *builtin;
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char actual_hash[SHA256_HEX_SIZE];
  char meta[1024];
  char line[192];
  pkg_manifest_t pkg;
  size_t size = 0;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (!pkg_name_safe(name)) {
    pkg_append_line(out, out_size, "pkg info: invalid package name");
    return -1;
  }

  builtin = find_builtin_package(name);
  if (builtin) {
    pkg_append_line(out, out_size, "Orizon package");
    snprintf(line, sizeof(line), "name %s", builtin->name);
    pkg_append_line(out, out_size, line);
    snprintf(line, sizeof(line), "version %s", builtin->version);
    pkg_append_line(out, out_size, line);
    snprintf(line, sizeof(line), "state %s", builtin->state);
    pkg_append_line(out, out_size, line);
    pkg_append_line(out, out_size,
                    "type builtin, protected by the running kernel");
    return 0;
  }

  if (package_store_paths(name, manifest_path, sizeof(manifest_path),
                          meta_path, sizeof(meta_path)) < 0 ||
      pkg_read_file(meta_path, meta, sizeof(meta), NULL) < 0 ||
      pkg_read_file(manifest_path, pkg_buf, sizeof(pkg_buf), &size) < 0) {
    pkg_append_line(out, out_size, "pkg info: package not installed");
    return -2;
  }

  pkg_append_line(out, out_size, "Orizon package");
  pkg_append_line(out, out_size, meta);
  if (parse_manifest(pkg_buf, size, &pkg, actual_hash) == 0) {
    snprintf(line, sizeof(line), "stored-sha256 %s", actual_hash);
    pkg_append_line(out, out_size, line);
    append_package_dependencies(&pkg, out, out_size);
    append_package_scripts(&pkg, out, out_size);
    append_payload_files(&pkg, out, out_size);
  } else {
    pkg_append_line(out, out_size, "stored-manifest invalid");
  }
  return 0;
}

int orizon_pkg_remove(const char *name, char *report, size_t report_size) {
  const builtin_package_t *builtin;
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char removed_manifest_path[MAX_PATH];
  char removed_meta_path[MAX_PATH];
  char actual_hash[SHA256_HEX_SIZE];
  char line[192];
  pkg_manifest_t pkg;
  size_t size = 0;
  size_t meta_size = 0;
  int post_remove_result;

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (!pkg_name_safe(name)) {
    pkg_append_line(report, report_size, "pkg remove: invalid package name");
    return -1;
  }
  builtin = find_builtin_package(name);
  if (builtin) {
    pkg_append_line(report, report_size,
                    "pkg remove: builtin packages cannot be removed");
    return -2;
  }
  if (package_store_paths(name, manifest_path, sizeof(manifest_path),
                          meta_path, sizeof(meta_path)) < 0 ||
      pkg_read_file(manifest_path, pkg_buf, sizeof(pkg_buf), &size) < 0 ||
      parse_manifest(pkg_buf, size, &pkg, actual_hash) < 0) {
    pkg_append_line(report, report_size, "pkg remove: package not installed");
    return -3;
  }
  if (pkg_read_file(meta_path, pkg_rollback_meta, sizeof(pkg_rollback_meta),
                    &meta_size) < 0) {
    meta_size = 0;
  }
  if (package_removed_paths(name, removed_manifest_path,
                            sizeof(removed_manifest_path), removed_meta_path,
                            sizeof(removed_meta_path)) < 0 ||
      pkg_write_blob_internal(removed_manifest_path, pkg_buf, size) < 0 ||
      (meta_size > 0 &&
       pkg_write_blob_internal(removed_meta_path, pkg_rollback_meta,
                               meta_size) < 0)) {
    pkg_append_line(report, report_size,
                    "pkg remove: cannot prepare rollback snapshot");
    return -4;
  }

  snprintf(line, sizeof(line), "Removing %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
  pkg_append_line(report, report_size,
                  "pkg: rollback snapshot saved for pkg rollback <name>");
  if (run_payload_script(&pkg, "pre-remove", "end-pre-remove", report,
                         report_size) < 0) {
    vfs_delete(removed_manifest_path);
    vfs_delete(removed_meta_path);
    pkg_append_line(report, report_size,
                    "pkg remove: pre-remove failed, package left installed");
    return -5;
  }
  if (remove_payload_files(&pkg, report, report_size) < 0) {
    pkg_append_line(report, report_size, "pkg remove: payload cleanup failed");
    replay_payload(&pkg, 0, NULL, 0);
    vfs_delete(removed_manifest_path);
    vfs_delete(removed_meta_path);
    return -6;
  }
  if (vfs_delete(meta_path) < 0 || vfs_delete(manifest_path) < 0) {
    replay_payload(&pkg, 0, NULL, 0);
    if (meta_size > 0) {
      pkg_write_blob_internal(meta_path, pkg_rollback_meta, meta_size);
    }
    pkg_write_blob_internal(manifest_path, pkg_buf, size);
    vfs_delete(removed_manifest_path);
    vfs_delete(removed_meta_path);
    pkg_append_line(report, report_size,
                    "pkg remove: database cleanup failed, package restored");
    return -7;
  }
  post_remove_result = run_payload_script(&pkg, "post-remove",
                                          "end-post-remove", report,
                                          report_size);
  if (post_remove_result < 0) {
    pkg_append_line(report, report_size,
                    "pkg remove: WARN post-remove script failed");
  }
  snprintf(line, sizeof(line), "removed %s %s rollback=available", pkg.name,
           pkg.version);
  pkg_append_text_internal(PKG_DB_HISTORY, line);
  pkg_append_text_internal(PKG_DB_HISTORY, "\n");
  pkg_status_text = "package removed; rollback available";
  orizon_pkg_refresh_database();
  vfs_persist_save();
  snprintf(line, sizeof(line), "Removed %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
  pkg_append_line(report, report_size, "Rollback: pkg rollback <name>");
  return 0;
}

int orizon_pkg_rollback(const char *name, char *report, size_t report_size) {
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  char removed_manifest_path[MAX_PATH];
  char removed_meta_path[MAX_PATH];
  char actual_hash[SHA256_HEX_SIZE];
  char line[192];
  pkg_manifest_t pkg;
  size_t size = 0;
  int rc;

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (!pkg_name_safe(name)) {
    pkg_append_line(report, report_size, "pkg rollback: invalid package name");
    return -1;
  }
  if (package_store_paths(name, manifest_path, sizeof(manifest_path),
                          meta_path, sizeof(meta_path)) < 0 ||
      package_removed_paths(name, removed_manifest_path,
                            sizeof(removed_manifest_path), removed_meta_path,
                            sizeof(removed_meta_path)) < 0) {
    pkg_append_line(report, report_size, "pkg rollback: invalid paths");
    return -1;
  }
  if (vfs_stat(meta_path, NULL, NULL) == 0) {
    pkg_append_line(report, report_size,
                    "pkg rollback: package is already installed");
    return -2;
  }
  if (pkg_read_file(removed_manifest_path, pkg_rollback_buf,
                    sizeof(pkg_rollback_buf), &size) < 0 ||
      parse_manifest(pkg_rollback_buf, size, &pkg, actual_hash) < 0 ||
      strcmp(pkg.name, name) != 0) {
    pkg_append_line(report, report_size,
                    "pkg rollback: no valid remove snapshot");
    return -3;
  }

  rc = pkg_install_loaded(removed_manifest_path, pkg_rollback_buf, size,
                          report, report_size);
  if (rc != 0) {
    pkg_append_line(report, report_size,
                    "pkg rollback: restore failed, snapshot kept");
    return rc;
  }
  vfs_delete(removed_manifest_path);
  vfs_delete(removed_meta_path);
  snprintf(line, sizeof(line), "rollback %s %s restored", pkg.name,
           pkg.version);
  pkg_append_text_internal(PKG_DB_HISTORY, line);
  pkg_append_text_internal(PKG_DB_HISTORY, "\n");
  pkg_status_text = "package rollback restored";
  orizon_pkg_refresh_database();
  vfs_persist_save();
  snprintf(line, sizeof(line), "Restored %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
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

int orizon_pkg_verify_file(const char *path, char *out, size_t out_size) {
  pkg_manifest_t pkg;
  char actual_hash[SHA256_HEX_SIZE];
  char line[192];
  size_t size = 0;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (pkg_read_file(path, pkg_buf, sizeof(pkg_buf), &size) < 0 || size == 0) {
    pkg_append_line(out, out_size, "pkg verify: cannot read package file");
    return -1;
  }
  if (parse_manifest(pkg_buf, size, &pkg, actual_hash) != 0) {
    pkg_append_line(out, out_size, "pkg verify: invalid package or sha256");
    return -2;
  }
  snprintf(line, sizeof(line), "name %s", pkg.name);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "version %s", pkg.version);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "payload-sha256 %s", actual_hash);
  pkg_append_line(out, out_size, line);
  append_package_dependencies(&pkg, out, out_size);
  append_package_scripts(&pkg, out, out_size);
  append_payload_files(&pkg, out, out_size);
  if (check_package_dependencies(&pkg, out, out_size) == 0) {
    pkg_append_line(out, out_size, "package verify: OK");
    return 0;
  }
  pkg_append_line(out, out_size, "package verify: WARN dependencies missing");
  return 1;
}

int orizon_pkg_history(char *out, size_t out_size) {
  size_t size = 0;
  static char history_buf[4096];

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (pkg_read_file(PKG_DB_HISTORY, history_buf, sizeof(history_buf), &size) <
          0 ||
      size == 0) {
    pkg_append_line(out, out_size, "pkg history: empty");
    return 1;
  }
  pkg_append_line(out, out_size, "pkg history:");
  pkg_append_line(out, out_size, "path " PKG_DB_HISTORY);
  pkg_append(out, out_size, history_buf);
  if (strlen(out) > 0 && out[strlen(out) - 1] != '\n') {
    pkg_append_line(out, out_size, "");
  }
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
      "end-post-install\n"
      "pre-remove\n"
      "echo pre-remove: orizon-hello cleanup starting\n"
      "end-pre-remove\n"
      "post-remove\n"
      "append /workspace/packages/history.log orizon-hello 0.1.0 removed\n"
      "echo post-remove: wrote /workspace/packages/history.log\n"
      "end-post-remove\n";
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
           "depends orizon-core core-x86_64\n"
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
