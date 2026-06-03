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
#include "../include/desktop.h"
#include "../include/rsa.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/vfs.h"

#define PKG_MAX_BYTES (48U * 1024U)
#define PKG_MAX_LINE 768U
#define PKG_DB_ROOT "/workspace/.orizon/pkgdb"
#define PKG_DB_INSTALLED "/workspace/.orizon/pkgdb/installed"
#define PKG_DB_STORE "/workspace/.orizon/pkgdb/packages"
#define PKG_DB_REMOVED "/workspace/.orizon/pkgdb/removed"
#define PKG_DB_CACHE "/workspace/.orizon/pkgdb/cache"
#define PKG_DB_HISTORY "/workspace/.orizon/pkgdb/history.log"
#define PKG_DB_TRANSACTION "/workspace/.orizon/pkgdb/transaction.state"
#define PKG_DB_UPGRADE_PLAN "/workspace/.orizon/pkgdb/upgrade.plan"
#define PKG_REMOTE_CACHE_STATUS "/workspace/.orizon/pkgdb/cache/remote.status"
#define PKG_REMOTE_SIG_STATUS "/workspace/.orizon/pkgdb/cache/remote.sig.status"
#define PKG_REMOTE_INDEX_PATH "/workspace/.orizon/package-index"
#define PKG_REMOTE_INDEX_SIG_PATH "/workspace/.orizon/package-index.sig"
#define PKG_WORKSPACE_LIST "/workspace/.orizon/packages"
#define PKG_SYSTEM_LIST "/system/packages"
#define PKG_SYSTEM_INSTALLED "/system/installed"
#define PKG_STATUS_PATH "/system/package-status"
#define PKG_MAX_DEPENDS 8U
#define PKG_MAX_REMOTE_ENTRIES 32U
#define PKG_MANAGER_VERSION "v5"
#define PKG_REPO_SIGNING_KEY_ID "orizon-update-root-2026-05"
#define PKG_REMOTE_SIG_BYTES 256U

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

typedef struct {
  char key_id[64];
  char index_sha256[SHA256_HEX_SIZE];
  uint8_t signature[PKG_REMOTE_SIG_BYTES];
} pkg_remote_signature_t;

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
    {"orizon-packages", "text-payload-v5", "builtin"},
    {"orizon-desktop-base", "hyprland-style-profile-runtime", "optional"},
};

static const uint8_t pkg_repo_root_n[PKG_REMOTE_SIG_BYTES] = {
    0xa7, 0xa0, 0x07, 0xe2, 0x31, 0x2f, 0x12, 0x03, 0x11, 0x85, 0x97, 0x24,
    0xf6, 0xca, 0x3b, 0x77, 0x13, 0xdd, 0x75, 0xdc, 0x23, 0xca, 0x09, 0x45,
    0xca, 0xe6, 0xc4, 0x71, 0x83, 0x84, 0xae, 0x63, 0x31, 0x98, 0xe9, 0x8c,
    0x32, 0x94, 0x6c, 0xcc, 0xb6, 0x59, 0x7e, 0xab, 0x0f, 0x0d, 0xfd, 0xc3,
    0x50, 0x9f, 0x79, 0x53, 0xef, 0x66, 0x5d, 0x1b, 0x58, 0x1f, 0xde, 0x1c,
    0x1d, 0x5f, 0x4e, 0xdc, 0xad, 0xda, 0x1f, 0xc6, 0x5c, 0x66, 0x11, 0x36,
    0x2d, 0x03, 0x3a, 0xd9, 0xd8, 0xb5, 0x95, 0x9c, 0xe9, 0x25, 0x15, 0x7e,
    0xd3, 0x84, 0x9a, 0x6a, 0xb7, 0x2c, 0x5e, 0xb8, 0x55, 0x81, 0xb9, 0xc5,
    0xc5, 0x47, 0x63, 0x63, 0x8a, 0xd9, 0x8d, 0xcf, 0x4f, 0x11, 0x35, 0xee,
    0xeb, 0x9d, 0x73, 0xf8, 0x37, 0x73, 0x22, 0x4f, 0xfc, 0x4e, 0xa4, 0xbb,
    0xc7, 0x9c, 0xd3, 0x6e, 0x66, 0xf3, 0x12, 0x60, 0x1d, 0xcd, 0x66, 0x14,
    0x30, 0x1b, 0x37, 0xc8, 0x2c, 0xe1, 0xe2, 0x7b, 0x78, 0x9f, 0xea, 0xc9,
    0xf2, 0x81, 0x8e, 0x5d, 0x15, 0xcf, 0x3b, 0x6a, 0x78, 0xf2, 0x1a, 0xfc,
    0x06, 0x0a, 0x53, 0x48, 0x03, 0x35, 0x6c, 0x43, 0x79, 0xfe, 0xba, 0xad,
    0xaa, 0xf0, 0x6c, 0x89, 0x61, 0x2c, 0x46, 0x44, 0xab, 0x96, 0x5a, 0x5b,
    0x5f, 0xf1, 0x37, 0x15, 0x29, 0x07, 0x99, 0xef, 0x1a, 0xab, 0xaf, 0xb2,
    0x6b, 0xf7, 0x70, 0x7b, 0x0d, 0xa9, 0xd2, 0x7d, 0x81, 0x15, 0x62, 0x2c,
    0x53, 0x77, 0x0f, 0xb5, 0x09, 0x5c, 0xd8, 0x10, 0x01, 0xee, 0x91, 0xae,
    0x7d, 0x2c, 0xdf, 0xe6, 0x56, 0x03, 0x19, 0x48, 0x46, 0x93, 0xad, 0xef,
    0x94, 0xbb, 0xfc, 0x2b, 0x36, 0xd4, 0x0e, 0x4d, 0x94, 0x5a, 0x25, 0xce,
    0x61, 0x76, 0x16, 0x11, 0x47, 0x5e, 0x97, 0xa0, 0x85, 0xbf, 0x9d, 0xa8,
    0x10, 0x87, 0x7e, 0x87,
};

static char pkg_buf[PKG_MAX_BYTES + 1] __attribute__((aligned(4096)));
static char pkg_rollback_buf[PKG_MAX_BYTES + 1] __attribute__((aligned(4096)));
static char pkg_rollback_meta[1024];
static const char *pkg_status_text = "package manager ready";
static int pkg_initialized = 0;
static unsigned long pkg_transaction_seq = 0;

static int meta_value(const char *text, const char *key, char *out,
                      size_t out_size);

static unsigned long pkg_next_transaction_id(void) {
  pkg_transaction_seq++;
  if (pkg_transaction_seq == 0) {
    pkg_transaction_seq = 1;
  }
  return pkg_transaction_seq;
}

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

static char pkg_ascii_lower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static int pkg_path_contains_ci(const char *path, const char *needle) {
  size_t path_len;
  size_t needle_len;

  if (!path || !needle) {
    return 0;
  }
  path_len = strlen(path);
  needle_len = strlen(needle);
  if (needle_len == 0 || path_len < needle_len) {
    return 0;
  }
  for (size_t i = 0; i + needle_len <= path_len; i++) {
    size_t j = 0;
    while (j < needle_len &&
           pkg_ascii_lower(path[i + j]) == pkg_ascii_lower(needle[j])) {
      j++;
    }
    if (j == needle_len) {
      return 1;
    }
  }
  return 0;
}

static int pkg_path_suffix_ci(const char *path, const char *suffix) {
  size_t path_len;
  size_t suffix_len;

  if (!path || !suffix) {
    return 0;
  }
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (suffix_len == 0 || path_len < suffix_len) {
    return 0;
  }
  return pkg_path_contains_ci(path + path_len - suffix_len, suffix);
}

static int pkg_path_sensitive(const char *path) {
  static const char *const needles[] = {
      "/.ssh/",     "ssh_host_",      "password",  "passwd",
      "private",   "secret",         "token",     "credential",
      "api_key",   "apikey",         "id_rsa",    "id_ed25519",
      "authorized_keys"};
  static const char *const suffixes[] = {
      ".env", ".key", ".private.pem", ".p12", ".pfx"};

  if (!path) {
    return 0;
  }
  if (strcmp(path, "/system/ssh.conf") == 0 ||
      strcmp(path, "/system/ssh_host_rsa.key") == 0) {
    return 1;
  }
  for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
    if (pkg_path_contains_ci(path, needles[i])) {
      return 1;
    }
  }
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
    if (pkg_path_suffix_ci(path, suffixes[i])) {
      return 1;
    }
  }
  return 0;
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

static int pkg_name_is_desktop_alias(const char *name) {
  return name && (strcmp(name, ORIZON_DESKTOP_PACKAGE) == 0 ||
                  strcmp(name, "desktop") == 0 ||
                  strcmp(name, "hypr") == 0 ||
                  strcmp(name, "hyprland") == 0);
}

static int pkg_name_is_desktop_module(const char *name) {
  return name && (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0 ||
                  strcmp(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) == 0 ||
                  strcmp(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) == 0 ||
                  strcmp(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) == 0 ||
                  strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0);
}

static int pkg_name_is_desktop_installable_module(const char *name) {
  return pkg_name_is_desktop_module(name) &&
         strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) != 0;
}

static const char *pkg_desktop_module_role(const char *name) {
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    return "runtime policy/session/settings/logs";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) == 0) {
    return "native tiled terminal client";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) == 0) {
    return "native tiled settings/logs/packages/update viewers";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) == 0) {
    return "Hyprland-style launcher overlay";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
    return "future Waybar-style layer package; not installed now";
  }
  return "unknown desktop module";
}

static const char *pkg_desktop_module_kind(const char *name) {
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    return "runtime";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) == 0 ||
      strcmp(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) == 0 ||
      strcmp(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) == 0) {
    return "app";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
    return "bar";
  }
  return "unknown";
}

static const char *pkg_desktop_module_provides(const char *name) {
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    return "policy,session,settings,logs,module-map";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) == 0) {
    return "terminal-client,tiled-surface,F1";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) == 0) {
    return "settings,logs,packages,update-viewers";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) == 0) {
    return "launcher-overlay,hypr-dispatch,SUPER+D,F3";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
    return "waybar-style-layer,future-package";
  }
  return "unknown";
}

static const char *pkg_desktop_module_command(const char *name) {
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    return "desktop start";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) == 0) {
    return "desktop launch terminal";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) == 0) {
    return "desktop launch settings|logs|packages|update";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) == 0) {
    return "desktop launch launcher";
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
    return "not-installed";
  }
  return "unknown";
}

static const char *pkg_desktop_module_package_path(const char *name) {
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    return ORIZON_DESKTOP_PACKAGE_CORE_PATH;
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) == 0) {
    return ORIZON_DESKTOP_PACKAGE_TERMINAL_PATH;
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) == 0) {
    return ORIZON_DESKTOP_PACKAGE_SETTINGS_PATH;
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) == 0) {
    return ORIZON_DESKTOP_PACKAGE_LAUNCHER_PATH;
  }
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
    return ORIZON_DESKTOP_PACKAGE_WAYBAR_PATH;
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
  if (pkg_path_sensitive(path)) {
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

static int pkg_hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static int pkg_hex_decode_exact(const char *text, uint8_t *out,
                                size_t out_len) {
  size_t len;

  if (!text || !out) {
    return -1;
  }
  len = strlen(text);
  if (len != out_len * 2U) {
    return -1;
  }
  for (size_t i = 0; i < out_len; i++) {
    int hi = pkg_hex_nibble(text[i * 2U]);
    int lo = pkg_hex_nibble(text[i * 2U + 1U]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

static void pkg_write_transaction_state(const char *state, const char *action,
                                        const char *name, const char *version,
                                        unsigned long transaction_id,
                                        const char *rollback,
                                        const char *result) {
  char text[512];

  snprintf(text, sizeof(text),
           "manager %s\n"
           "state %s\n"
           "action %s\n"
           "package %s\n"
           "version %s\n"
           "transaction %s-%lu\n"
           "rollback %s\n"
           "result %s\n",
           PKG_MANAGER_VERSION, state ? state : "unknown",
           action ? action : "unknown", name ? name : "unknown",
           version ? version : "unknown", PKG_MANAGER_VERSION, transaction_id,
           rollback ? rollback : "unknown", result ? result : "unknown");
  pkg_write_blob_internal(PKG_DB_TRANSACTION, text, strlen(text));
}

static void pkg_cache_upgrade_plan(const char *plan) {
  if (plan && plan[0]) {
    pkg_write_blob_internal(PKG_DB_UPGRADE_PLAN, plan, strlen(plan));
  }
}

static void pkg_write_remote_signature_status(const char *status,
                                              const char *reason,
                                              const char *index_hash) {
  char text[640];

  snprintf(text, sizeof(text),
           "detached-signature status=%s\n"
           "reason %s\n"
           "path %s\n"
           "key-id %s\n"
           "algorithm rsa-pkcs1-sha256\n"
           "index-sha256 %s\n"
           "fallback signed-update-manifest-pin\n",
           status ? status : "unknown", reason ? reason : "unknown",
           PKG_REMOTE_INDEX_SIG_PATH, PKG_REPO_SIGNING_KEY_ID,
           index_hash ? index_hash : "unknown");
  pkg_write_blob_internal(PKG_REMOTE_SIG_STATUS, text, strlen(text));
}

static int pkg_parse_remote_signature(const char *text,
                                      pkg_remote_signature_t *sig) {
  line_reader_t reader;
  char line[PKG_MAX_LINE];
  char version[32] = "";
  char algorithm[64] = "";
  int saw_signature = 0;

  if (!text || !sig) {
    return -1;
  }
  memset(sig, 0, sizeof(*sig));
  reader.data = text;
  reader.size = strlen(text);
  reader.pos = 0;
  while (reader_line(&reader, line, sizeof(line))) {
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (pkg_starts_with(line, "signature-version ")) {
      copy_value(version, sizeof(version), line + 18);
    } else if (pkg_starts_with(line, "algorithm ")) {
      copy_value(algorithm, sizeof(algorithm), line + 10);
    } else if (pkg_starts_with(line, "key-id ")) {
      copy_value(sig->key_id, sizeof(sig->key_id), line + 7);
    } else if (pkg_starts_with(line, "package-index-sha256 ")) {
      copy_value(sig->index_sha256, sizeof(sig->index_sha256), line + 21);
    } else if (pkg_starts_with(line, "index-sha256 ")) {
      copy_value(sig->index_sha256, sizeof(sig->index_sha256), line + 13);
    } else if (pkg_starts_with(line, "signature ")) {
      if (pkg_hex_decode_exact(line + 10, sig->signature,
                               sizeof(sig->signature)) < 0) {
        return -1;
      }
      saw_signature = 1;
    } else {
      return -1;
    }
  }
  if (strcmp(version, "1") != 0 ||
      strcmp(algorithm, "rsa-pkcs1-sha256") != 0 ||
      strcmp(sig->key_id, PKG_REPO_SIGNING_KEY_ID) != 0 ||
      !hex_is_valid(sig->index_sha256) || !saw_signature) {
    return -1;
  }
  return 0;
}

static int pkg_validate_remote_signature(char *out, size_t out_size,
                                         int verbose) {
  pkg_remote_signature_t sig;
  uint8_t digest[SHA256_DIGEST_SIZE];
  char index_hash[SHA256_HEX_SIZE];
  size_t index_size = 0;
  size_t sig_size = 0;

  if (pkg_read_file(PKG_REMOTE_INDEX_PATH, pkg_rollback_buf,
                    sizeof(pkg_rollback_buf), &index_size) < 0 ||
      index_size == 0) {
    pkg_write_remote_signature_status("no-index", "cached-index-missing",
                                      NULL);
    if (verbose) {
      pkg_append_line(out, out_size,
                      "package-repo-signature detached=no-index "
                      "fallback=signed-update-manifest-pin");
    }
    return 1;
  }

  sha256_buffer_hex(pkg_rollback_buf, index_size, index_hash);
  if (pkg_read_file(PKG_REMOTE_INDEX_SIG_PATH, pkg_buf, sizeof(pkg_buf),
                    &sig_size) < 0 ||
      sig_size == 0) {
    pkg_write_remote_signature_status("missing", "sidecar-not-cached",
                                      index_hash);
    if (verbose) {
      pkg_append_line(out, out_size,
                      "package-repo-signature detached=missing "
                      "path=" PKG_REMOTE_INDEX_SIG_PATH " "
                      "fallback=signed-update-manifest-pin");
    }
    return 1;
  }

  if (pkg_parse_remote_signature(pkg_buf, &sig) < 0) {
    pkg_write_remote_signature_status("invalid", "sidecar-parse-failed",
                                      index_hash);
    if (verbose) {
      pkg_append_line(out, out_size,
                      "package-repo-signature detached=invalid "
                      "reason=parse-failed");
    }
    return 2;
  }
  if (!hex_equal(sig.index_sha256, index_hash)) {
    pkg_write_remote_signature_status("invalid", "index-sha256-mismatch",
                                      index_hash);
    if (verbose) {
      pkg_append_line(out, out_size,
                      "package-repo-signature detached=invalid "
                      "reason=index-sha256-mismatch");
    }
    return 2;
  }

  sha256_buffer(pkg_rollback_buf, index_size, digest);
  if (rsa_pkcs1v15_sha256_verify(sig.signature, sizeof(sig.signature), digest,
                                 pkg_repo_root_n,
                                 sizeof(pkg_repo_root_n)) != 0) {
    pkg_write_remote_signature_status("invalid", "rsa-verify-failed",
                                      index_hash);
    if (verbose) {
      pkg_append_line(out, out_size,
                      "package-repo-signature detached=invalid "
                      "reason=rsa-verify-failed");
    }
    return 2;
  }

  pkg_write_remote_signature_status("verified", "rsa-sha256-ok", index_hash);
  if (verbose) {
    char line[192];
    snprintf(line, sizeof(line),
             "package-repo-signature detached=verified key-id=%s",
             PKG_REPO_SIGNING_KEY_ID);
    pkg_append_line(out, out_size, line);
    snprintf(line, sizeof(line), "package-index-sha256 %s", index_hash);
    pkg_append_line(out, out_size, line);
  }
  return 0;
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

static int pkg_name_has_suffix(const char *name, const char *suffix) {
  size_t name_len;
  size_t suffix_len;

  if (!name || !suffix) {
    return 0;
  }
  name_len = strlen(name);
  suffix_len = strlen(suffix);
  if (name_len < suffix_len) {
    return 0;
  }
  return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static void pkg_audit_store(int *valid, int *invalid, int *missing_meta) {
  dirent_t entries[64];
  int count;
  char path[MAX_PATH];
  char manifest_path[MAX_PATH];
  char meta_path[MAX_PATH];
  pkg_manifest_t pkg;
  char actual_hash[SHA256_HEX_SIZE];
  size_t size = 0;

  if (valid) {
    *valid = 0;
  }
  if (invalid) {
    *invalid = 0;
  }
  if (missing_meta) {
    *missing_meta = 0;
  }
  count = vfs_readdir(PKG_DB_STORE, entries, 64);
  if (count <= 0) {
    return;
  }
  for (int i = 0; i < count; i++) {
    if (entries[i].type != 0) {
      continue;
    }
    snprintf(path, sizeof(path), PKG_DB_STORE "/%s", entries[i].name);
    if (!pkg_name_has_suffix(entries[i].name, ".opkg") ||
        pkg_read_file(path, pkg_buf, sizeof(pkg_buf), &size) < 0 ||
        parse_manifest(pkg_buf, size, &pkg, actual_hash) != 0) {
      if (invalid) {
        (*invalid)++;
      }
      continue;
    }
    if (valid) {
      (*valid)++;
    }
    if (missing_meta &&
        package_store_paths(pkg.name, manifest_path, sizeof(manifest_path),
                            meta_path, sizeof(meta_path)) == 0 &&
        vfs_stat(meta_path, NULL, NULL) < 0) {
      (*missing_meta)++;
    }
  }
}

static void pkg_audit_installed_meta(int *valid_meta, int *invalid_meta,
                                     int *orphan_meta) {
  dirent_t entries[64];
  int count;
  char meta_path[MAX_PATH];
  char manifest_path[MAX_PATH];
  char store_meta_path[MAX_PATH];
  char name[64];

  if (valid_meta) {
    *valid_meta = 0;
  }
  if (invalid_meta) {
    *invalid_meta = 0;
  }
  if (orphan_meta) {
    *orphan_meta = 0;
  }
  count = vfs_readdir(PKG_DB_INSTALLED, entries, 64);
  if (count <= 0) {
    return;
  }
  for (int i = 0; i < count; i++) {
    if (entries[i].type != 0) {
      continue;
    }
    snprintf(meta_path, sizeof(meta_path), PKG_DB_INSTALLED "/%s",
             entries[i].name);
    if (!pkg_name_has_suffix(entries[i].name, ".meta") ||
        pkg_read_file(meta_path, pkg_buf, sizeof(pkg_buf), NULL) < 0 ||
        meta_value(pkg_buf, "name", name, sizeof(name)) < 0 ||
        package_store_paths(name, manifest_path, sizeof(manifest_path),
                            store_meta_path, sizeof(store_meta_path)) < 0) {
      if (invalid_meta) {
        (*invalid_meta)++;
      }
      continue;
    }
    if (strcmp(store_meta_path, meta_path) != 0) {
      if (invalid_meta) {
        (*invalid_meta)++;
      }
      continue;
    }
    if (valid_meta) {
      (*valid_meta)++;
    }
    if (orphan_meta && vfs_stat(manifest_path, NULL, NULL) < 0) {
      (*orphan_meta)++;
    }
  }
}

static int store_installed_package(const char *source_path,
                                   const pkg_manifest_t *pkg,
                                   const char *actual_hash,
                                   const char *source_data,
                                   size_t source_size,
                                   unsigned long transaction_id) {
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
           "state installed\n"
           "manager %s\n"
           "transaction %s-%lu\n"
           "rollback previous-payload-on-failure\n",
           pkg->name, pkg->version, actual_hash,
           source_path ? source_path : "unknown", PKG_MANAGER_VERSION,
           PKG_MANAGER_VERSION, transaction_id);
  if (pkg_write_blob_internal(meta_path, meta, strlen(meta)) < 0) {
    return -1;
  }
  snprintf(event, sizeof(event),
           "installed %s %s source=%s sha256=%s transaction=%s-%lu "
           "rollback=previous-payload-on-failure result=stored\n",
           pkg->name, pkg->version, source_path ? source_path : "unknown",
           actual_hash, PKG_MANAGER_VERSION, transaction_id);
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

static int package_dependency_missing_count(const pkg_manifest_t *pkg) {
  int missing = 0;

  if (!pkg) {
    return 1;
  }
  for (size_t i = 0; i < pkg->depends_count; i++) {
    if (!package_dependency_satisfied(&pkg->depends[i])) {
      missing++;
    }
  }
  return missing;
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
  unsigned long transaction_id = 0;

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
  transaction_id = pkg_next_transaction_id();

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
  pkg_write_transaction_state("running", had_old ? "upgrade" : "install",
                              pkg.name, pkg.version, transaction_id,
                              "previous-payload-on-failure", "starting");
  snprintf(line, sizeof(line),
           "Transaction %s-%lu action=%s rollback=previous-payload-on-failure",
           PKG_MANAGER_VERSION, transaction_id,
           had_old ? "upgrade" : "install");
  pkg_append_line(report, report_size, line);

  if (replay_payload(&pkg, 1, report, report_size) < 0) {
    pkg_append_line(report, report_size, "pkg: payload install failed");
    remove_payload_files(&pkg, report, report_size);
    if (had_old) {
      replay_payload(&old_pkg, 0, NULL, 0);
      pkg_append_line(report, report_size,
                      "pkg: rollback restored previous package payload");
    }
    snprintf(line, sizeof(line),
             "failed-install %s %s transaction=%s-%lu result=payload-failed",
             pkg.name, pkg.version, PKG_MANAGER_VERSION, transaction_id);
    pkg_append_text_internal(PKG_DB_HISTORY, line);
    pkg_append_text_internal(PKG_DB_HISTORY, "\n");
    pkg_write_transaction_state("failed", had_old ? "upgrade" : "install",
                                pkg.name, pkg.version, transaction_id,
                                "previous-payload-on-failure",
                                "payload-failed");
    return -5;
  }
  if (store_installed_package(source_name, &pkg, actual_hash, data, size,
                              transaction_id) < 0) {
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
    snprintf(line, sizeof(line),
             "failed-install %s %s transaction=%s-%lu result=db-failed",
             pkg.name, pkg.version, PKG_MANAGER_VERSION, transaction_id);
    pkg_append_text_internal(PKG_DB_HISTORY, line);
    pkg_append_text_internal(PKG_DB_HISTORY, "\n");
    pkg_write_transaction_state("failed", had_old ? "upgrade" : "install",
                                pkg.name, pkg.version, transaction_id,
                                "previous-payload-on-failure", "db-failed");
    return -6;
  }
  if (strcmp(pkg.name, ORIZON_DESKTOP_PACKAGE) == 0 ||
      strcmp(pkg.name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    char desktop_status[512];
    if (orizon_desktop_set_enabled(1, desktop_status,
                                   sizeof(desktop_status)) == 0) {
      pkg_append_line(report, report_size,
                      "desktop hook: optional profile enabled");
      pkg_append(report, report_size, desktop_status);
    } else {
      pkg_append_line(report, report_size,
                      "desktop hook: WARN profile enable failed");
    }
  }
  if (had_old) {
    snprintf(line, sizeof(line), "Replaced previous version %s", old_pkg.version);
    pkg_append_line(report, report_size, line);
    snprintf(line, sizeof(line),
             "upgraded %s %s -> %s transaction=%s-%lu rollback=guarded",
             pkg.name, old_pkg.version, pkg.version, PKG_MANAGER_VERSION,
             transaction_id);
    pkg_append_text_internal(PKG_DB_HISTORY, line);
    pkg_append_text_internal(PKG_DB_HISTORY, "\n");
  }
  pkg_write_transaction_state("complete", had_old ? "upgrade" : "install",
                              pkg.name, pkg.version, transaction_id,
                              "previous-payload-on-failure", "stored");
  pkg_status_text = "package installed";
  orizon_pkg_refresh_database();
  vfs_persist_save();
  snprintf(line, sizeof(line), "Installed %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
  return 0;
}

int orizon_pkg_install_named(const char *name, char *report,
                             size_t report_size) {
  char generated[640];
  char install_report[4096];
  char current_version[64];
  char current_origin[32];
  int rc;

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (!pkg_name_is_desktop_alias(name) && !pkg_name_is_desktop_module(name)) {
    pkg_append_line(report, report_size,
                    "pkg install: unknown named package");
    pkg_append_line(report, report_size,
                    "available named packages: " ORIZON_DESKTOP_PACKAGE
                    ", " ORIZON_DESKTOP_PACKAGE_CORE
                    ", " ORIZON_DESKTOP_PACKAGE_TERMINAL
                    ", " ORIZON_DESKTOP_PACKAGE_SETTINGS
                    ", " ORIZON_DESKTOP_PACKAGE_LAUNCHER);
    pkg_append_line(report, report_size,
                    "hint: use pkg install <file> for local .opkg files");
    return -1;
  }

  if (pkg_name_is_desktop_module(name)) {
    const char *path = pkg_desktop_module_package_path(name);
    if (strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
      pkg_append_line(report, report_size,
                      "pkg named install: orizon-waybar is planned later");
      pkg_append_line(report, report_size,
                      "policy: no Waybar/taskbar package is installed now");
      pkg_append_line(report, report_size,
                      "hint: use pkg info orizon-waybar for current status");
      return -2;
    }
    if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) != 0 &&
        package_current_version(ORIZON_DESKTOP_PACKAGE_CORE, current_version,
                                sizeof(current_version), current_origin,
                                sizeof(current_origin)) < 0) {
      pkg_append_line(report, report_size,
                      "pkg named install: installing required desktop core first");
      rc = orizon_pkg_write_desktop_module_sample(ORIZON_DESKTOP_PACKAGE_CORE,
                                                  generated,
                                                  sizeof(generated));
      pkg_append(report, report_size, generated);
      if (rc < 0) {
        pkg_append_line(report, report_size,
                        "pkg install: failed to prepare desktop core package");
        return rc;
      }
      rc = orizon_pkg_install_file(ORIZON_DESKTOP_PACKAGE_CORE_PATH,
                                   install_report, sizeof(install_report));
      pkg_append(report, report_size, install_report);
      if (rc < 0) {
        pkg_append_line(report, report_size,
                        "pkg install: required desktop core failed");
        return rc;
      }
    }
    snprintf(generated, sizeof(generated), "pkg named install: %s\n", name);
    pkg_append(report, report_size, generated);
    rc = orizon_pkg_write_desktop_module_sample(name, generated,
                                                sizeof(generated));
    pkg_append(report, report_size, generated);
    if (rc < 0 || !path) {
      pkg_append_line(report, report_size,
                      "pkg install: failed to prepare desktop module package");
      return rc < 0 ? rc : -3;
    }
    rc = orizon_pkg_install_file(path, install_report, sizeof(install_report));
    pkg_append(report, report_size, install_report);
    return rc;
  }

  pkg_append_line(report, report_size,
                  "pkg named install: " ORIZON_DESKTOP_PACKAGE);
  rc = orizon_pkg_write_desktop_sample(generated, sizeof(generated));
  pkg_append(report, report_size, generated);
  if (rc < 0) {
    pkg_append_line(report, report_size,
                    "pkg install: failed to prepare desktop package");
    return rc;
  }
  rc = orizon_pkg_install_file(ORIZON_DESKTOP_PACKAGE_PATH, install_report,
                               sizeof(install_report));
  pkg_append(report, report_size, install_report);
  return rc;
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
  int sig_rc = 1;
  size_t transaction_size = 0;
  size_t plan_size = 0;
  int transaction_present;
  int plan_present;
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
  sig_rc = pkg_validate_remote_signature(NULL, 0, 0);
  transaction_present = vfs_stat(PKG_DB_TRANSACTION, &transaction_size, NULL) == 0;
  plan_present = vfs_stat(PKG_DB_UPGRADE_PLAN, &plan_size, NULL) == 0;
  pkg_append_line(out, out_size, "Orizon package manager");
  pkg_append_line(out, out_size, pkg_status_text);
  snprintf(line, sizeof(line), "manager %s", PKG_MANAGER_VERSION);
  pkg_append_line(out, out_size, line);
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
                  "format orizon-package 1 manager-v2=yes manager-v3=yes "
                  "manager-v4=yes manager-v5=yes");
  pkg_append_line(out, out_size, "dependencies depends <name> <version|*>");
  pkg_append_line(out, out_size,
                  "scripts post-install pre-remove post-remove");
  pkg_append_line(out, out_size,
                  "script-policy allow=mkdir,touch,write,append,echo,sync "
                  "safe-paths=/system,/home,/packages,/logs,/tmp,/workspace "
                  "sensitive-paths=blocked internal-state=/workspace/.orizon:blocked");
  pkg_append_line(out, out_size,
                  "rollback install-restores-previous-payload remove-cache="
                  PKG_DB_REMOVED);
  pkg_append_line(out, out_size,
                  "remote-index-auth signed-update-manifest-sha256-pinned");
  pkg_append_line(out, out_size,
                  "package-repo-signature detached=prepared "
                  "sidecar=" PKG_REMOTE_INDEX_SIG_PATH " "
                  "fallback=signed-update-manifest-pin");
  snprintf(line, sizeof(line),
           "package-repo-signature-status %s status-file=%s",
           sig_rc == 0 ? "verified" : (sig_rc == 2 ? "fail" : "warn"),
           PKG_REMOTE_SIG_STATUS);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "transaction-state present=%s bytes=%lu path=%s",
           transaction_present ? "yes" : "no",
           (unsigned long)transaction_size, PKG_DB_TRANSACTION);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "upgrade-plan-cache present=%s bytes=%lu path=%s",
           plan_present ? "yes" : "no", (unsigned long)plan_size,
           PKG_DB_UPGRADE_PLAN);
  pkg_append_line(out, out_size, line);
  pkg_append_line(out, out_size,
                  "cache-audit pkg audit; cache-details pkg cache; dry-run pkg simulate <file>");
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
                  "pkg audit, pkg cache, pkg simulate <file>, "
                  "pkg upgrade [plan], pkg rollback <name>");
  pkg_append_line(out, out_size,
                  "network-diagnostics net check; net tcp raw.githubusercontent.com 443; net tls");
  pkg_append_line(out, out_size, "db " PKG_DB_ROOT);
  return 0;
}

int orizon_pkg_cache(char *out, size_t out_size) {
  pkg_remote_validation_t remote_stats;
  char line[192];
  size_t remote_size = 0;
  size_t history_size = 0;
  size_t transaction_size = 0;
  size_t plan_size = 0;
  int remote_rc = -1;
  int sig_rc = 1;
  int remote_cached;
  int history_present;
  int transaction_present;
  int plan_present;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }

  remote_cached = vfs_stat(PKG_REMOTE_INDEX_PATH, &remote_size, NULL) == 0;
  history_present = vfs_stat(PKG_DB_HISTORY, &history_size, NULL) == 0;
  if (remote_cached) {
    remote_rc = pkg_validate_remote_index(&remote_stats, NULL, 0, 0);
  } else {
    memset(&remote_stats, 0, sizeof(remote_stats));
  }
  sig_rc = pkg_validate_remote_signature(NULL, 0, 0);
  transaction_present = vfs_stat(PKG_DB_TRANSACTION, &transaction_size, NULL) == 0;
  plan_present = vfs_stat(PKG_DB_UPGRADE_PLAN, &plan_size, NULL) == 0;

  pkg_append_line(out, out_size, "pkg cache:");
  pkg_append_line(out, out_size, "db-root " PKG_DB_ROOT);
  snprintf(line, sizeof(line), "installed-meta %d",
           pkg_count_regular_files(PKG_DB_INSTALLED));
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "stored-packages %d",
           pkg_count_regular_files(PKG_DB_STORE));
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "removed-snapshots %d",
           pkg_count_regular_files(PKG_DB_REMOVED) / 2);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "cache-files %d",
           pkg_count_regular_files(PKG_DB_CACHE));
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "remote-index cached=%s bytes=%lu path=%s",
           remote_cached ? "yes" : "no", (unsigned long)remote_size,
           PKG_REMOTE_INDEX_PATH);
  pkg_append_line(out, out_size, line);
  if (remote_cached) {
    snprintf(line, sizeof(line),
             "remote-index-valid=%s entries=%d invalid-lines=%d "
             "duplicate-names=%d",
             remote_rc == 0 ? "yes" : "no", remote_stats.valid_entries,
             remote_stats.invalid_lines, remote_stats.duplicate_names);
    pkg_append_line(out, out_size, line);
  }
  snprintf(line, sizeof(line), "remote-status %s", PKG_REMOTE_CACHE_STATUS);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "remote-signature-status %s (%s)",
           sig_rc == 0 ? "verified" : (sig_rc == 2 ? "fail" : "warn"),
           PKG_REMOTE_SIG_STATUS);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "transaction-state present=%s bytes=%lu path=%s",
           transaction_present ? "yes" : "no",
           (unsigned long)transaction_size, PKG_DB_TRANSACTION);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "upgrade-plan-cache present=%s bytes=%lu path=%s",
           plan_present ? "yes" : "no", (unsigned long)plan_size,
           PKG_DB_UPGRADE_PLAN);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "history present=%s bytes=%lu path=%s",
           history_present ? "yes" : "no", (unsigned long)history_size,
           PKG_DB_HISTORY);
  pkg_append_line(out, out_size, line);
  pkg_append_line(out, out_size,
                  "package-repo-signature detached=prepared "
                  "sidecar=" PKG_REMOTE_INDEX_SIG_PATH " "
                  "fallback=signed-update-manifest-pin");
  pkg_append_line(out, out_size, "audit-command pkg audit; doctor pkg doctor");
  return remote_cached && remote_rc != 0 ? 1 : 0;
}

int orizon_pkg_audit(char *out, size_t out_size) {
  pkg_remote_validation_t remote_stats;
  char line[192];
  int valid_store = 0;
  int invalid_store = 0;
  int missing_meta = 0;
  int valid_meta = 0;
  int invalid_meta = 0;
  int orphan_meta = 0;
  int remote_cached;
  int remote_rc = -1;
  int sig_rc = 1;
  int fail;
  int warn;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }

  pkg_audit_store(&valid_store, &invalid_store, &missing_meta);
  pkg_audit_installed_meta(&valid_meta, &invalid_meta, &orphan_meta);
  remote_cached = vfs_stat(PKG_REMOTE_INDEX_PATH, NULL, NULL) == 0;
  if (remote_cached) {
    remote_rc = pkg_validate_remote_index(&remote_stats, NULL, 0, 0);
  } else {
    memset(&remote_stats, 0, sizeof(remote_stats));
  }
  sig_rc = pkg_validate_remote_signature(NULL, 0, 0);

  fail = invalid_store > 0 || invalid_meta > 0 || orphan_meta > 0 ||
         missing_meta > 0 || sig_rc == 2;
  warn = !remote_cached || remote_rc != 0 || sig_rc == 1;

  pkg_append_line(out, out_size, "pkg audit:");
  pkg_append_line(out, out_size,
                  "scope non-destructive; validates package db/cache only");
  snprintf(line, sizeof(line),
           "stored valid=%d invalid=%d missing-meta=%d", valid_store,
           invalid_store, missing_meta);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line),
           "installed-meta valid=%d invalid=%d orphan=%d", valid_meta,
           invalid_meta, orphan_meta);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "rollback-snapshots %d",
           pkg_count_regular_files(PKG_DB_REMOVED) / 2);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "remote-index cached=%s path=%s",
           remote_cached ? "yes" : "no", PKG_REMOTE_INDEX_PATH);
  pkg_append_line(out, out_size, line);
  if (remote_cached) {
    snprintf(line, sizeof(line),
             "remote-index-valid=%s entries=%d invalid-lines=%d "
             "duplicate-names=%d",
             remote_rc == 0 ? "yes" : "no", remote_stats.valid_entries,
             remote_stats.invalid_lines, remote_stats.duplicate_names);
    pkg_append_line(out, out_size, line);
  } else {
    pkg_append_line(out, out_size,
                    "remote-index WARN not cached; run pkg update after install");
  }
  pkg_append_line(out, out_size,
                  "script-policy allow=mkdir,touch,write,append,echo,sync "
                  "sensitive-paths=blocked");
  pkg_append_line(out, out_size,
                  "package-repo-signature detached=prepared "
                  "sidecar=" PKG_REMOTE_INDEX_SIG_PATH " "
                  "fallback=signed-update-manifest-pin");
  snprintf(line, sizeof(line), "package-repo-signature-status %s status-file=%s",
           sig_rc == 0 ? "verified" : (sig_rc == 2 ? "fail" : "warn"),
           PKG_REMOTE_SIG_STATUS);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "summary: %s",
           fail ? "FAIL" : (warn ? "WARN" : "PASS"));
  pkg_append_line(out, out_size, line);
  return fail ? 2 : (warn ? 1 : 0);
}

int orizon_pkg_doctor(char *out, size_t out_size) {
  static const char *const required_dirs[] = {
      PKG_DB_ROOT, PKG_DB_INSTALLED, PKG_DB_STORE, PKG_DB_REMOVED,
      PKG_DB_CACHE, "/workspace/packages"};
  pkg_remote_validation_t remote_stats;
  char line[224];
  char transaction[512];
  char tx_state[48];
  size_t transaction_size = 0;
  size_t plan_size = 0;
  int valid_store = 0;
  int invalid_store = 0;
  int missing_meta = 0;
  int valid_meta = 0;
  int invalid_meta = 0;
  int orphan_meta = 0;
  int remote_cached;
  int remote_rc = -1;
  int sig_rc = 1;
  int fail = 0;
  int warn = 0;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }

  pkg_append_line(out, out_size, "pkg doctor:");
  pkg_append_line(out, out_size,
                  "scope non-destructive; checks v5 package safety state");
  snprintf(line, sizeof(line), "manager %s", PKG_MANAGER_VERSION);
  pkg_append_line(out, out_size, line);

  for (size_t i = 0; i < sizeof(required_dirs) / sizeof(required_dirs[0]);
       i++) {
    int is_dir = 0;
    int ok = vfs_stat(required_dirs[i], NULL, &is_dir) == 0 && is_dir;
    snprintf(line, sizeof(line), "dir %s %s", required_dirs[i],
             ok ? "PASS" : "FAIL");
    pkg_append_line(out, out_size, line);
    if (!ok) {
      fail = 1;
    }
  }

  pkg_audit_store(&valid_store, &invalid_store, &missing_meta);
  pkg_audit_installed_meta(&valid_meta, &invalid_meta, &orphan_meta);
  snprintf(line, sizeof(line),
           "db stored-valid=%d stored-invalid=%d missing-meta=%d "
           "meta-valid=%d meta-invalid=%d orphan-meta=%d",
           valid_store, invalid_store, missing_meta, valid_meta, invalid_meta,
           orphan_meta);
  pkg_append_line(out, out_size, line);
  if (invalid_store > 0 || invalid_meta > 0 || missing_meta > 0 ||
      orphan_meta > 0) {
    fail = 1;
  }

  remote_cached = vfs_stat(PKG_REMOTE_INDEX_PATH, NULL, NULL) == 0;
  if (remote_cached) {
    remote_rc = pkg_validate_remote_index(&remote_stats, NULL, 0, 0);
  } else {
    memset(&remote_stats, 0, sizeof(remote_stats));
  }
  snprintf(line, sizeof(line),
           "remote-index cached=%s valid=%s entries=%d invalid-lines=%d",
           remote_cached ? "yes" : "no",
           remote_cached ? (remote_rc == 0 ? "yes" : "no") : "unknown",
           remote_stats.valid_entries, remote_stats.invalid_lines);
  pkg_append_line(out, out_size, line);
  if (!remote_cached || remote_rc != 0) {
    warn = 1;
  }

  sig_rc = pkg_validate_remote_signature(NULL, 0, 0);
  snprintf(line, sizeof(line),
           "package-repo-signature %s status-file=%s sidecar=%s",
           sig_rc == 0 ? "PASS" : (sig_rc == 2 ? "FAIL" : "WARN"),
           PKG_REMOTE_SIG_STATUS, PKG_REMOTE_INDEX_SIG_PATH);
  pkg_append_line(out, out_size, line);
  if (sig_rc == 2) {
    fail = 1;
  } else if (sig_rc == 1) {
    warn = 1;
  }

  if (pkg_read_file(PKG_DB_TRANSACTION, transaction, sizeof(transaction),
                    &transaction_size) == 0 &&
      transaction_size > 0) {
    if (meta_value(transaction, "state", tx_state, sizeof(tx_state)) < 0) {
      snprintf(tx_state, sizeof(tx_state), "%s", "unknown");
    }
    snprintf(line, sizeof(line), "transaction-state present=yes state=%s path=%s",
             tx_state, PKG_DB_TRANSACTION);
    pkg_append_line(out, out_size, line);
    if (strcmp(tx_state, "failed") == 0 || strcmp(tx_state, "running") == 0 ||
        strcmp(tx_state, "unknown") == 0) {
      warn = 1;
    }
  } else {
    pkg_append_line(out, out_size,
                    "transaction-state present=no state=none-yet");
  }

  if (vfs_stat(PKG_DB_UPGRADE_PLAN, &plan_size, NULL) == 0) {
    snprintf(line, sizeof(line), "upgrade-plan-cache present=yes bytes=%lu path=%s",
             (unsigned long)plan_size, PKG_DB_UPGRADE_PLAN);
  } else {
    snprintf(line, sizeof(line), "upgrade-plan-cache present=no path=%s",
             PKG_DB_UPGRADE_PLAN);
  }
  pkg_append_line(out, out_size, line);

  pkg_append_line(out, out_size,
                  "script-policy PASS allow=mkdir,touch,write,append,echo,sync "
                  "sensitive-paths=blocked");
  pkg_append_line(out, out_size,
                  "limits package-max=48KiB deps-max=8 "
                  "repo-signature-sidecar=prepared");
  snprintf(line, sizeof(line), "summary: %s",
           fail ? "FAIL" : (warn ? "WARN" : "PASS"));
  pkg_append_line(out, out_size, line);
  return fail ? 2 : (warn ? 1 : 0);
}

int orizon_pkg_remote(char *out, size_t out_size) {
  pkg_remote_validation_t validation;
  int remote_matches = 0;
  int validation_rc = -1;
  int sig_rc = 1;

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
  sig_rc = pkg_validate_remote_signature(out, out_size, 1);
  if (sig_rc == 2) {
    pkg_append_line(out, out_size,
                    "package-repo-signature: FAIL sidecar invalid");
  }
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
  int sig_rc;

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
  sig_rc = pkg_validate_remote_signature(out, out_size, 1);
  if (rc < 0) {
    pkg_append_line(out, out_size,
                    "hint: run pkg update after disk install to refresh it");
    return 1;
  }
  if (sig_rc == 2) {
    pkg_append_line(out, out_size,
                    "package-index verification: FAIL detached signature invalid");
    return 1;
  }
  if (rc == 0) {
    pkg_append_line(out, out_size,
                    sig_rc == 0
                        ? "package-index verification: OK detached-signature=verified"
                        : "package-index verification: OK fallback=signed-update-manifest-pin detached-signature=WARN");
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
  int sig_rc = 1;

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
  sig_rc = pkg_validate_remote_signature(out, out_size, 1);
  pkg_append_line(out, out_size,
                  "dependency-resolution per-package-depends-checked-after-download");
  pkg_append_line(out, out_size, "remote-index " PKG_REMOTE_INDEX_PATH);
  pkg_append_line(out, out_size, "plan-cache " PKG_DB_UPGRADE_PLAN);
  validation_rc = pkg_validate_remote_index(&validation, NULL, 0, 0);
  if (validation_rc < 0) {
    pkg_append_line(out, out_size, "cached-index=no");
    pkg_append_line(out, out_size,
                    "action: run pkg update/pkg upgrade after disk install "
                    "to fetch the signed package index");
    pkg_cache_upgrade_plan(out);
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
    pkg_cache_upgrade_plan(out);
    return 1;
  }

  reader.data = pkg_rollback_buf;
  reader.size = size;
  reader.pos = 0;
  while (reader_line(&reader, line, sizeof(line))) {
    pkg_remote_entry_t entry;
    char current_version[64];
    char origin[32];
    char entry_line[320];

    if (!pkg_starts_with(line, "package ") ||
        parse_remote_index_line(line, &entry) < 0) {
      continue;
    }
    if (package_current_version(entry.name, current_version,
                                sizeof(current_version), origin,
                                sizeof(origin)) < 0) {
      snprintf(entry_line, sizeof(entry_line),
               "install %s %s size=%lu sha256=%s deps=validated-on-download",
               entry.name, entry.version, (unsigned long)entry.size,
               entry.sha256);
      pkg_append_line(out, out_size, entry_line);
      install_count++;
      continue;
    }
    if (strcmp(current_version, entry.version) == 0) {
      snprintf(entry_line, sizeof(entry_line),
               "current %s %s source=%s deps=validated-on-download",
               entry.name, entry.version, origin);
      pkg_append_line(out, out_size, entry_line);
      current_count++;
      continue;
    }
    if (strcmp(origin, "builtin") == 0) {
      snprintf(entry_line, sizeof(entry_line),
               "protected-builtin %s %s -> %s use OS update "
               "deps=validated-on-download",
               entry.name, current_version, entry.version);
      pkg_append_line(out, out_size, entry_line);
      protected_count++;
      continue;
    }
    snprintf(entry_line, sizeof(entry_line),
             "upgrade %s %s -> %s deps=validated-on-download",
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
    pkg_cache_upgrade_plan(out);
    return 1;
  }
  if (sig_rc == 2) {
    pkg_append_line(out, out_size,
                    "action: fix package-index.sig before upgrade");
    pkg_cache_upgrade_plan(out);
    return 1;
  }
  if (install_count == 0 && upgrade_count == 0 && protected_count == 0) {
    pkg_append_line(out, out_size, "action: nothing to upgrade");
    pkg_cache_upgrade_plan(out);
    return 0;
  }
  pkg_append_line(out, out_size,
                  "action: pkg upgrade runs signed update/package refresh");
  pkg_cache_upgrade_plan(out);
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
  if (pkg_text_matches_query(ORIZON_DESKTOP_PACKAGE, query) ||
      pkg_text_matches_query("desktop hypr hyprland optional", query)) {
    snprintf(line, sizeof(line),
             "available %s " ORIZON_DESKTOP_PACKAGE_VERSION
             " optional install='pkg install %s'",
             ORIZON_DESKTOP_PACKAGE, ORIZON_DESKTOP_PACKAGE);
    pkg_append_line(out, out_size, line);
    matches++;
  }
  {
    const char *modules[] = {
        ORIZON_DESKTOP_PACKAGE_CORE, ORIZON_DESKTOP_PACKAGE_TERMINAL,
        ORIZON_DESKTOP_PACKAGE_SETTINGS, ORIZON_DESKTOP_PACKAGE_LAUNCHER,
        ORIZON_DESKTOP_PACKAGE_WAYBAR};
    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
      if (!pkg_text_matches_query(modules[i], query) &&
          !pkg_text_matches_query(pkg_desktop_module_role(modules[i]), query) &&
          !pkg_text_matches_query("desktop modules split hyprland waybar",
                                  query)) {
        continue;
      }
      if (strcmp(modules[i], ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
        snprintf(line, sizeof(line),
                 "planned %s " ORIZON_DESKTOP_PACKAGE_VERSION
                 " role='%s' install=no waybar-package-later=yes",
                 modules[i], pkg_desktop_module_role(modules[i]));
      } else {
        snprintf(line, sizeof(line),
                 "prepared %s " ORIZON_DESKTOP_PACKAGE_VERSION
                 " role='%s' sample='pkg sample %s' install='pkg install %s'",
                 modules[i], pkg_desktop_module_role(modules[i]), modules[i],
                 modules[i]);
      }
      pkg_append_line(out, out_size, line);
      matches++;
    }
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
    if (pkg_name_is_desktop_alias(name)) {
      pkg_append_line(out, out_size, "Orizon package");
      pkg_append_line(out, out_size, "name " ORIZON_DESKTOP_PACKAGE);
      pkg_append_line(out, out_size,
                      "version " ORIZON_DESKTOP_PACKAGE_VERSION);
      pkg_append_line(out, out_size, "state available optional");
      pkg_append_line(out, out_size,
                      "type local generated package; not installed yet");
      pkg_append_line(out, out_size,
                      "install pkg install " ORIZON_DESKTOP_PACKAGE);
      pkg_append_line(out, out_size,
                      "sample desktop package writes " ORIZON_DESKTOP_PACKAGE_PATH);
      pkg_append_line(out, out_size,
                      "depends orizon-core core-x86_64; orizon-packages text-payload-v5; orizon-desktop-base hyprland-style-profile-runtime");
      pkg_append_line(out, out_size,
                      "payload /system/desktop.conf /system/desktop-session.conf /system/desktop-settings.conf /system/desktop-state.conf /system/desktop-modules.conf /system/desktop-backend.conf /system/desktop-protocol.conf /system/desktop-binds.conf /system/desktop-autostart.conf /system/desktop-rules.conf /system/desktop-monitors.conf /system/desktop-layers.conf /system/desktop-runtime.conf /home/orizon/.config/hypr/orizon-hypr.conf /system/share/orizon-desktop-hypr.conf");
      pkg_append_line(out, out_size,
                      "split-modules pkg sample orizon-desktop-core|orizon-terminal|orizon-settings|orizon-launcher");
      return 0;
    }
    if (pkg_name_is_desktop_module(name)) {
      pkg_append_line(out, out_size, "Orizon package");
      snprintf(line, sizeof(line), "name %s", name);
      pkg_append_line(out, out_size, line);
      pkg_append_line(out, out_size,
                      "version " ORIZON_DESKTOP_PACKAGE_VERSION);
      pkg_append_line(out, out_size,
                      strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0
                          ? "state planned separate-package"
                          : "state prepared optional-module");
      snprintf(line, sizeof(line), "role %s", pkg_desktop_module_role(name));
      pkg_append_line(out, out_size, line);
      snprintf(line, sizeof(line), "kind %s", pkg_desktop_module_kind(name));
      pkg_append_line(out, out_size, line);
      snprintf(line, sizeof(line), "provides %s",
               pkg_desktop_module_provides(name));
      pkg_append_line(out, out_size, line);
      if (strcmp(name, ORIZON_DESKTOP_PACKAGE_WAYBAR) == 0) {
        pkg_append_line(out, out_size,
                        "install no; planned future separate package only");
        pkg_append_line(out, out_size,
                        "sample no; Waybar/taskbar package is not generated now");
      } else {
        snprintf(line, sizeof(line), "sample pkg sample %s", name);
        pkg_append_line(out, out_size, line);
        snprintf(line, sizeof(line), "install pkg install %s", name);
        pkg_append_line(out, out_size, line);
        snprintf(line, sizeof(line), "package-path %s",
                 pkg_desktop_module_package_path(name));
        pkg_append_line(out, out_size, line);
      }
      pkg_append_line(out, out_size,
                      "compat-bundle pkg install " ORIZON_DESKTOP_PACKAGE);
      pkg_append_line(out, out_size,
                      "module-map " ORIZON_DESKTOP_MODULES_PATH);
      pkg_append_line(out, out_size,
                      "split-status samples-enabled core-auto-installed-for-app-modules");
      pkg_append_line(out, out_size,
                      "waybar-note no Waybar/taskbar package is installed now");
      return 0;
    }
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
  unsigned long transaction_id = 0;

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
  transaction_id = pkg_next_transaction_id();
  pkg_write_transaction_state("running", "remove", pkg.name, pkg.version,
                              transaction_id, "snapshot", "starting");
  if (package_removed_paths(name, removed_manifest_path,
                            sizeof(removed_manifest_path), removed_meta_path,
                            sizeof(removed_meta_path)) < 0 ||
      pkg_write_blob_internal(removed_manifest_path, pkg_buf, size) < 0 ||
      (meta_size > 0 &&
       pkg_write_blob_internal(removed_meta_path, pkg_rollback_meta,
                               meta_size) < 0)) {
    pkg_append_line(report, report_size,
                    "pkg remove: cannot prepare rollback snapshot");
    pkg_write_transaction_state("failed", "remove", pkg.name, pkg.version,
                                transaction_id, "snapshot",
                                "snapshot-failed");
    return -4;
  }

  snprintf(line, sizeof(line), "Removing %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
  snprintf(line, sizeof(line),
           "Transaction %s-%lu action=remove rollback=snapshot",
           PKG_MANAGER_VERSION, transaction_id);
  pkg_append_line(report, report_size, line);
  pkg_append_line(report, report_size,
                  "pkg: rollback snapshot saved for pkg rollback <name>");
  if (run_payload_script(&pkg, "pre-remove", "end-pre-remove", report,
                         report_size) < 0) {
    vfs_delete(removed_manifest_path);
    vfs_delete(removed_meta_path);
    pkg_append_line(report, report_size,
                    "pkg remove: pre-remove failed, package left installed");
    pkg_write_transaction_state("failed", "remove", pkg.name, pkg.version,
                                transaction_id, "snapshot",
                                "pre-remove-failed");
    return -5;
  }
  if (remove_payload_files(&pkg, report, report_size) < 0) {
    pkg_append_line(report, report_size, "pkg remove: payload cleanup failed");
    replay_payload(&pkg, 0, NULL, 0);
    vfs_delete(removed_manifest_path);
    vfs_delete(removed_meta_path);
    pkg_write_transaction_state("failed", "remove", pkg.name, pkg.version,
                                transaction_id, "snapshot",
                                "payload-cleanup-failed");
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
    pkg_write_transaction_state("failed", "remove", pkg.name, pkg.version,
                                transaction_id, "snapshot",
                                "db-cleanup-failed");
    return -7;
  }
  post_remove_result = run_payload_script(&pkg, "post-remove",
                                          "end-post-remove", report,
                                          report_size);
  if (post_remove_result < 0) {
    pkg_append_line(report, report_size,
                    "pkg remove: WARN post-remove script failed");
  }
  if (strcmp(pkg.name, ORIZON_DESKTOP_PACKAGE) == 0 ||
      strcmp(pkg.name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    char desktop_status[512];
    if (orizon_desktop_set_enabled(0, desktop_status,
                                   sizeof(desktop_status)) == 0) {
      pkg_append_line(report, report_size,
                      "desktop hook: optional profile disabled");
      pkg_append(report, report_size, desktop_status);
    } else {
      pkg_append_line(report, report_size,
                      "desktop hook: WARN profile disable failed");
    }
  }
  snprintf(line, sizeof(line),
           "removed %s %s transaction=%s-%lu rollback=available", pkg.name,
           pkg.version, PKG_MANAGER_VERSION, transaction_id);
  pkg_append_text_internal(PKG_DB_HISTORY, line);
  pkg_append_text_internal(PKG_DB_HISTORY, "\n");
  pkg_write_transaction_state("complete", "remove", pkg.name, pkg.version,
                              transaction_id, "snapshot", "removed");
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
  unsigned long transaction_id = 0;

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

  transaction_id = pkg_next_transaction_id();
  pkg_write_transaction_state("running", "rollback", pkg.name, pkg.version,
                              transaction_id, "remove-snapshot", "starting");
  rc = pkg_install_loaded(removed_manifest_path, pkg_rollback_buf, size,
                          report, report_size);
  if (rc != 0) {
    pkg_append_line(report, report_size,
                    "pkg rollback: restore failed, snapshot kept");
    pkg_write_transaction_state("failed", "rollback", pkg.name, pkg.version,
                                transaction_id, "remove-snapshot",
                                "restore-failed");
    return rc;
  }
  snprintf(line, sizeof(line),
           "Rollback transaction %s-%lu source=remove-snapshot",
           PKG_MANAGER_VERSION, transaction_id);
  pkg_append_line(report, report_size, line);
  vfs_delete(removed_manifest_path);
  vfs_delete(removed_meta_path);
  snprintf(line, sizeof(line),
           "rollback %s %s transaction=%s-%lu restored", pkg.name,
           pkg.version, PKG_MANAGER_VERSION, transaction_id);
  pkg_append_text_internal(PKG_DB_HISTORY, line);
  pkg_append_text_internal(PKG_DB_HISTORY, "\n");
  pkg_write_transaction_state("complete", "rollback", pkg.name, pkg.version,
                              transaction_id, "remove-snapshot", "restored");
  pkg_status_text = "package rollback restored";
  orizon_pkg_refresh_database();
  vfs_persist_save();
  snprintf(line, sizeof(line), "Restored %s %s", pkg.name, pkg.version);
  pkg_append_line(report, report_size, line);
  return 0;
}

int orizon_pkg_simulate_file(const char *path, char *out, size_t out_size) {
  pkg_manifest_t pkg;
  char actual_hash[SHA256_HEX_SIZE];
  char current_version[64];
  char origin[32];
  char line[256];
  size_t size = 0;
  int missing_deps;
  int protected_builtin = 0;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (pkg_read_file(path, pkg_buf, sizeof(pkg_buf), &size) < 0 || size == 0) {
    pkg_append_line(out, out_size, "pkg simulate: cannot read package file");
    return -1;
  }
  if (parse_manifest(pkg_buf, size, &pkg, actual_hash) == -2) {
    pkg_append_line(out, out_size, "pkg simulate: sha256 mismatch");
    snprintf(line, sizeof(line), "actual-payload-sha256 %s", actual_hash);
    pkg_append_line(out, out_size, line);
    return -2;
  }
  if (parse_manifest(pkg_buf, size, &pkg, actual_hash) < 0) {
    pkg_append_line(out, out_size, "pkg simulate: invalid package format");
    return -3;
  }

  pkg_append_line(out, out_size, "pkg simulate:");
  snprintf(line, sizeof(line), "source %s", path ? path : "unknown");
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "name %s", pkg.name);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "version %s", pkg.version);
  pkg_append_line(out, out_size, line);
  snprintf(line, sizeof(line), "payload-sha256 %s", actual_hash);
  pkg_append_line(out, out_size, line);
  pkg_append_line(out, out_size, "mode dry-run writes=no");

  if (package_current_version(pkg.name, current_version,
                              sizeof(current_version), origin,
                              sizeof(origin)) < 0) {
    pkg_append_line(out, out_size, "action install");
  } else if (strcmp(origin, "builtin") == 0) {
    protected_builtin = 1;
    snprintf(line, sizeof(line),
             "action protected-builtin current=%s use OS update",
             current_version);
    pkg_append_line(out, out_size, line);
  } else if (strcmp(current_version, pkg.version) == 0) {
    snprintf(line, sizeof(line), "action current version=%s source=%s",
             current_version, origin);
    pkg_append_line(out, out_size, line);
  } else {
    snprintf(line, sizeof(line), "action upgrade %s -> %s",
             current_version, pkg.version);
    pkg_append_line(out, out_size, line);
  }

  append_package_dependencies(&pkg, out, out_size);
  append_package_scripts(&pkg, out, out_size);
  append_payload_files(&pkg, out, out_size);
  pkg_append_line(out, out_size,
                  "script-policy allow=mkdir,touch,write,append,echo,sync "
                  "safe-paths=/system,/home,/packages,/logs,/tmp,/workspace "
                  "sensitive-paths=blocked");
  pkg_append_line(out, out_size,
                  "transaction-preview v5 rollback=previous-payload-on-failure");
  missing_deps = package_dependency_missing_count(&pkg);
  if (protected_builtin) {
    pkg_append_line(out, out_size,
                    "pkg simulate: WARN builtin package is kernel-protected");
    return 1;
  }
  if (missing_deps > 0) {
    snprintf(line, sizeof(line),
             "pkg simulate: WARN missing-dependencies=%d", missing_deps);
    pkg_append_line(out, out_size, line);
    return 1;
  }
  pkg_append_line(out, out_size, "pkg simulate: OK dry-run");
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
  pkg_append_line(out, out_size,
                  "script-policy allow=mkdir,touch,write,append,echo,sync "
                  "safe-paths=/system,/home,/packages,/logs,/tmp,/workspace "
                  "sensitive-paths=blocked");
  pkg_append_line(out, out_size,
                  "transaction-preview v5 rollback=previous-payload-on-failure");
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
  pkg_append_line(out, out_size,
                  "format v5 transaction=v5-N rollback/result fields");
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
           "depends orizon-packages text-payload-v5\n"
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

int orizon_pkg_write_desktop_module_sample(const char *name, char *report,
                                           size_t report_size) {
  const char *path;
  const char *kind;
  const char *role;
  const char *provides;
  const char *command;
  const char *dep_line;
  char hash[SHA256_HEX_SIZE];
  char header[512];
  char line[256];
  char deps[256];

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  if (!pkg_name_is_desktop_module(name)) {
    pkg_append_line(report, report_size,
                    "pkg sample desktop-module: unknown desktop module");
    pkg_append_line(report, report_size,
                    "known: " ORIZON_DESKTOP_PACKAGE_CORE
                    " " ORIZON_DESKTOP_PACKAGE_TERMINAL
                    " " ORIZON_DESKTOP_PACKAGE_SETTINGS
                    " " ORIZON_DESKTOP_PACKAGE_LAUNCHER
                    " " ORIZON_DESKTOP_PACKAGE_WAYBAR);
    return -1;
  }
  if (!pkg_name_is_desktop_installable_module(name)) {
    pkg_append_line(report, report_size,
                    "pkg sample desktop-module: orizon-waybar is planned later");
    pkg_append_line(report, report_size,
                    "policy: Waybar/taskbar package is not generated or installed now");
    return -2;
  }

  path = pkg_desktop_module_package_path(name);
  kind = pkg_desktop_module_kind(name);
  role = pkg_desktop_module_role(name);
  provides = pkg_desktop_module_provides(name);
  command = pkg_desktop_module_command(name);
  if (!path) {
    pkg_append_line(report, report_size,
                    "pkg sample desktop-module: invalid module path");
    return -3;
  }

  pkg_buf[0] = '\0';
  snprintf(line, sizeof(line), "file " ORIZON_DESKTOP_MODULE_DIR "/%s.conf",
           name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "# Orizon desktop module manifest v1");
  snprintf(line, sizeof(line), "module %s", name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "version " ORIZON_DESKTOP_PACKAGE_VERSION);
  snprintf(line, sizeof(line), "kind %s", kind);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  snprintf(line, sizeof(line), "role %s", role);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  snprintf(line, sizeof(line), "provides %s", provides);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  snprintf(line, sizeof(line), "command %s", command);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "status packaged-optional-module");
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "current-bundle " ORIZON_DESKTOP_PACKAGE);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "sample-command pkg sample <module>");
  snprintf(line, sizeof(line), "install-command pkg install %s", name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "floating no");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "manual-window-drag no");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "windows-taskbar no");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "waybar-installed no");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "backend-map " ORIZON_DESKTOP_BACKEND_PATH);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "protocol-map " ORIZON_DESKTOP_PROTOCOL_PATH);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "content-end");

  snprintf(line, sizeof(line), "file /system/share/%s.conf", name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "# Orizon desktop split package hint v1");
  snprintf(line, sizeof(line), "package %s", name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "version " ORIZON_DESKTOP_PACKAGE_VERSION);
  snprintf(line, sizeof(line), "module-kind %s", kind);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  snprintf(line, sizeof(line), "module-provides %s", provides);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  snprintf(line, sizeof(line), "module-command %s", command);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "desktop-core " ORIZON_DESKTOP_PACKAGE_CORE);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "desktop-profile " ORIZON_DESKTOP_PACKAGE);
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "policy tiling-only-no-free-drag-no-windows-taskbar");
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "waybar future-package-not-installed");
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "backend-command desktop backend");
  pkg_append_line(pkg_buf, sizeof(pkg_buf),
                  "protocol-command desktop protocol");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "content-end");

  pkg_append_line(pkg_buf, sizeof(pkg_buf), "post-install");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "mkdir " ORIZON_DESKTOP_MODULE_DIR);
  snprintf(line, sizeof(line),
           "append " ORIZON_DESKTOP_MODULES_PATH
           " module-installed %s version=" ORIZON_DESKTOP_PACKAGE_VERSION
           " kind=%s",
           name, kind);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  snprintf(line, sizeof(line),
           "append " ORIZON_DESKTOP_LOG_PATH
           " %s module installed version=" ORIZON_DESKTOP_PACKAGE_VERSION,
           name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  snprintf(line, sizeof(line), "echo post-install: %s module installed", name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "sync");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "end-post-install");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "pre-remove");
  snprintf(line, sizeof(line), "echo pre-remove: %s module cleanup starting",
           name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "end-pre-remove");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "post-remove");
  snprintf(line, sizeof(line),
           "append " ORIZON_DESKTOP_LOG_PATH " %s module removed", name);
  pkg_append_line(pkg_buf, sizeof(pkg_buf), line);
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0) {
    pkg_append_line(pkg_buf, sizeof(pkg_buf),
                    "write " ORIZON_DESKTOP_CONFIG_PATH " enabled no");
  }
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "sync");
  pkg_append_line(pkg_buf, sizeof(pkg_buf), "end-post-remove");

  sha256_buffer_hex(pkg_buf, strlen(pkg_buf), hash);
  dep_line = strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) == 0
                 ? "depends orizon-desktop-base hyprland-style-profile-runtime\n"
                 : "depends " ORIZON_DESKTOP_PACKAGE_CORE
                   " " ORIZON_DESKTOP_PACKAGE_VERSION "\n";
  snprintf(deps, sizeof(deps), "%s", dep_line);
  snprintf(header, sizeof(header),
           "orizon-package 1\n"
           "name %s\n"
           "version " ORIZON_DESKTOP_PACKAGE_VERSION "\n"
           "depends orizon-core core-x86_64\n"
           "depends orizon-packages text-payload-v5\n"
           "%s"
           "sha256 %s\n"
           "payload:\n",
           name, deps, hash);
  pkg_ensure_dir("/workspace/packages");
  if (pkg_write_blob_internal(path, header, strlen(header)) < 0 ||
      pkg_append_text_internal(path, pkg_buf) < 0) {
    pkg_append_line(report, report_size,
                    "pkg sample desktop-module: cannot write package");
    return -4;
  }
  snprintf(line, sizeof(line), "Desktop module package written to %s", path);
  pkg_append_line(report, report_size, line);
  snprintf(line, sizeof(line), "Run after install: pkg install %s", path);
  pkg_append_line(report, report_size, line);
  snprintf(line, sizeof(line), "Named install: pkg install %s", name);
  pkg_append_line(report, report_size, line);
  if (strcmp(name, ORIZON_DESKTOP_PACKAGE_CORE) != 0) {
    pkg_append_line(report, report_size,
                    "dependency: orizon-desktop-core auto-prepared for named install");
  }
  pkg_append_line(report, report_size,
                  "Waybar note: orizon-waybar remains a future separate package");
  vfs_persist_save();
  return 0;
}

int orizon_pkg_write_desktop_sample(char *report, size_t report_size) {
  static const char desktop_payload[] =
      "file " ORIZON_DESKTOP_CONFIG_PATH "\n"
      "# Orizon desktop policy v1\n"
      "enabled yes\n"
      "profile " ORIZON_DESKTOP_PROFILE "\n"
      "package " ORIZON_DESKTOP_PACKAGE "\n"
      "session orizon-compositor\n"
      "terminal default-open\n"
      "shortcut-open-terminal F1\n"
      "shortcut-close-terminal F2\n"
      "note upstream-hyprland-not-embedded-yet\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_SETTINGS_PATH "\n"
      "# Orizon desktop system settings v1\n"
      "# Created when the optional desktop is selected during install or via package.\n"
      "# This is the central settings layer consumed by the Orizon compositor.\n"
      "scale 1\n"
      "gaps-in 6\n"
      "gaps-out 12\n"
      "border-size 2\n"
      "rounding 8\n"
      "animations yes\n"
      "shadows yes\n"
      "focus-ring yes\n"
      "shadow-range 18\n"
      "animation-ticks 18\n"
      "animation-curve orizon-pop\n"
      "render-profile balanced\n"
      "idle-timeout-seconds 0\n"
      "lock-on-idle no\n"
      "default-terminal orizon-terminal\n"
      "launcher-provider builtin\n"
      "bar-position top\n"
      "keyboard-layout us\n"
      "pointer-profile flat\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_BINDS_PATH "\n"
      "# Orizon generated Hyprland-style binds v1\n"
      "source package-template\n"
      "bind = $mod, RETURN, exec, terminal\n"
      "bind = $mod, Q, killactive\n"
      "bind = $mod, D, exec, orizon-launcher\n"
      "bind = $mod, M, fullscreen\n"
      "bind = $mod, P, pseudo\n"
      "bind = F11, submap, launch\n"
      "submap = launch\n"
      "bind = , t, exec, terminal\n"
      "bind = , s, exec, orizon-settings\n"
      "bind = , l, exec, orizon-logs\n"
      "bind = , p, exec, orizon-packages\n"
      "bind = , u, exec, orizon-update-viewer\n"
      "bind = , d, exec, orizon-launcher\n"
      "bind = , q, killactive\n"
      "submap = default\n"
      "bind = $mod, 1, workspace, 1\n"
      "bind = $mod SHIFT, 1, movetoworkspace, 1\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_AUTOSTART_PATH "\n"
      "# Orizon generated Hyprland-style autostart v1\n"
      "source package-template\n"
      "exec-once = terminal\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_RULES_PATH "\n"
      "# Orizon generated Hyprland-style window rules v1\n"
      "source package-template\n"
      "windowrulev2 = tile,class:^(orizon-.*)$\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_MONITORS_PATH "\n"
      "# Orizon generated Hyprland-style monitor hints v1\n"
      "source package-template\n"
      "monitor = ,preferred,auto,1\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_LAYERS_PATH "\n"
      "# Orizon generated Hyprland-style layer rules v1\n"
      "source package-template\n"
      "layerrule = blur, launcher\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_RUNTIME_PATH "\n"
      "# Orizon generated Hyprland-style runtime state v1\n"
      "source package-template\n"
      "env-count 0\n"
      "workspace-hints 0\n"
      "layout-hints 0\n"
      "input-hints 0\n"
      "decoration-hints 0\n"
      "cursor-hints 0\n"
      "render-hints 0\n"
      "debug-hints 0\n"
      "animation-hints 0\n"
      "submap = default\n"
      "settings-hub yes\n"
      "settings-path " ORIZON_DESKTOP_SETTINGS_PATH "\n"
      "session-path " ORIZON_DESKTOP_SESSION_PATH "\n"
      "user-config-path " ORIZON_DESKTOP_USER_CONFIG_PATH "\n"
      "backend-path " ORIZON_DESKTOP_BACKEND_PATH "\n"
      "protocol-path " ORIZON_DESKTOP_PROTOCOL_PATH "\n"
      "sources 1\n"
      "source = ~/.config/hypr/orizon-local.conf\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_MODULES_PATH "\n"
      "# Orizon desktop modular packaging map v1\n"
      "module " ORIZON_DESKTOP_PACKAGE_CORE
      " state=prepared kind=runtime provides=policy,session,settings,logs,backend-map,protocol-map sample='pkg sample " ORIZON_DESKTOP_PACKAGE_CORE "' install='pkg install " ORIZON_DESKTOP_PACKAGE_CORE "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
      "module " ORIZON_DESKTOP_PACKAGE
      " state=prepared kind=profile provides=hyprland-style-config,dispatchers,tiling,backend-diagnostics current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
      "module " ORIZON_DESKTOP_PACKAGE_TERMINAL
      " state=prepared kind=app provides=terminal-client shortcut=F1 sample='pkg sample " ORIZON_DESKTOP_PACKAGE_TERMINAL "' install='pkg install " ORIZON_DESKTOP_PACKAGE_TERMINAL "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
      "module " ORIZON_DESKTOP_PACKAGE_SETTINGS
      " state=prepared kind=app provides=settings,logs,packages,update-viewers shortcut=F11+s sample='pkg sample " ORIZON_DESKTOP_PACKAGE_SETTINGS "' install='pkg install " ORIZON_DESKTOP_PACKAGE_SETTINGS "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
      "module " ORIZON_DESKTOP_PACKAGE_LAUNCHER
      " state=prepared kind=app provides=launcher-overlay shortcut=SUPER+D/F3 sample='pkg sample " ORIZON_DESKTOP_PACKAGE_LAUNCHER "' install='pkg install " ORIZON_DESKTOP_PACKAGE_LAUNCHER "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
      "module " ORIZON_DESKTOP_PACKAGE_WAYBAR
      " state=planned kind=bar provides=waybar-style-layer package-later=yes installed=no\n"
      "policy no-windows-taskbar\n"
      "policy no-free-drag-window-moving\n"
      "architecture current-backend=framebuffer-vm future-backend=wayland-wlroots protocol=orizon-desktop-ipc-v0\n"
      "install-meta package-current=" ORIZON_DESKTOP_PACKAGE "\n"
      "install-meta package-split-prepared=yes\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_BACKEND_PATH "\n"
      "# Orizon desktop compositor backend map v1\n"
      "api compositor-orchestrator\n"
      "backend-current framebuffer-vm\n"
      "backend-current-file gui/compositor.c\n"
      "backend-future wayland-wlroots\n"
      "render-path software-backbuffer\n"
      "client-model tiled-internal\n"
      "external-wayland-clients no\n"
      "manual-window-drag no\n"
      "taskbar no\n"
      "waybar installed-no future-package\n"
      "vm-ready yes\n"
      "hardware-validated no\n"
      "truth hyprland-style-facade-not-upstream\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_PROTOCOL_PATH "\n"
      "# Orizon desktop internal protocol map v1\n"
      "protocol orizon-desktop-ipc-v0\n"
      "transport internal-kernel-dispatch\n"
      "messages dispatch,spawn-client,close-client,focus-client,workspace,config-keyword,query-state\n"
      "security local-kernel-only\n"
      "wayland no\n"
      "wlroots no\n"
      "xdg-shell no\n"
      "layer-shell prepared-only\n"
      "xwayland no\n"
      "external-clients no\n"
      "status prepared\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_STATE_PATH "\n"
      "# Orizon desktop session manager state v2\n"
      "schema-version 2\n"
      "health PASS\n"
      "desired-state started\n"
      "runtime-state active\n"
      "last-action package-install\n"
      "last-ticks 0\n"
      "boot-mode installed-or-live\n"
      "installed-marker unknown\n"
      "policy enabled\n"
      "autostart-terminal yes\n"
      "focus-follows-mouse no\n"
      "layout dwindle\n"
      "start-count 1\n"
      "stop-count 0\n"
      "restart-count 0\n"
      "reload-count 0\n"
      "recover-count 0\n"
      "crash-count 0\n"
      "crash-recover ready\n"
      "recover-command desktop recover\n"
      "rescue-command desktop rescue\n"
      "config-apply-command desktop config apply\n"
      "config-trace-command desktop config trace\n"
      "settings-doctor-command desktop settings doctor\n"
      "state-path " ORIZON_DESKTOP_STATE_PATH "\n"
      "session-log " ORIZON_DESKTOP_SESSION_LOG_PATH "\n"
      "manual-window-drag no\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_USER_CONFIG_PATH "\n"
      "# Orizon Hyprland-style desktop profile\n"
      "$mod = SUPER\n"
      "$terminal = orizon-terminal\n"
      "$menu = orizon-launcher\n"
      "monitor = ,preferred,auto,1\n"
      "exec-once = terminal\n"
      "input:kb_layout = us\n"
      "input:follow_mouse = 0\n"
      "input:repeat_rate = 40\n"
      "input:repeat_delay = 300\n"
      "input:touchpad:natural_scroll = false\n"
      "general:layout = dwindle\n"
      "general:gaps_in = 6\n"
      "general:gaps_out = 12\n"
      "general:border_size = 2\n"
      "general:col.active_border = rgba(8bd5ffcc)\n"
      "general:col.inactive_border = rgba(2a2f3acc)\n"
      "decoration:rounding = 8\n"
      "decoration:shadow:enabled = true\n"
      "decoration:shadow:range = 18\n"
      "render:focus_ring = true\n"
      "render:profile = balanced\n"
      "decoration:blur:enabled = false\n"
      "animations:enabled = true\n"
      "animations:tick_budget = 18\n"
      "animations:curve = orizon-pop\n"
      "bezier = orizon-pop, 0.16, 1, 0.3, 1\n"
      "animation = windows, 1, 2, orizon-pop\n"
      "cursor:no_hardware_cursors = true\n"
      "render:direct_scanout = false\n"
      "debug:disable_logs = false\n"
      "misc:disable_hyprland_logo = false\n"
      "misc:force_default_wallpaper = 0\n"
      "windowrulev2 = tile,class:^(orizon-.*)$\n"
      "layerrule = blur, launcher\n"
      "source = ~/.config/hypr/orizon-local.conf\n"
      "bind = $mod, RETURN, exec, terminal\n"
      "bind = $mod, Q, killactive\n"
      "bind = $mod, D, exec, orizon-launcher\n"
      "bind = $mod, A, exec, desktop autostart\n"
      "bind = $mod, B, exec, desktop bar toggle\n"
      "bind = $mod, F, exec, desktop focus toggle\n"
      "bind = $mod, M, fullscreen\n"
      "bind = $mod, P, pseudo\n"
      "bind = $mod SHIFT, P, pin\n"
      "bind = $mod, J, togglesplit\n"
      "bind = $mod SHIFT, J, layoutmsg, orientationnext\n"
      "bind = $mod, S, layoutmsg, swapwithmaster\n"
      "bind = $mod SHIFT, S, layoutmsg, focusmaster\n"
      "bindl = , XF86AudioMute, exec, desktop logs\n"
      "bind = $mod, R, submap, resize\n"
      "bind = $mod SHIFT, R, submap, reset\n"
      "bind = $mod, H, movefocus, l\n"
      "bind = $mod, L, movefocus, r\n"
      "bind = $mod, left, movefocus, l\n"
      "bind = $mod, right, movefocus, r\n"
      "bind = $mod, up, movefocus, u\n"
      "bind = $mod, down, movefocus, d\n"
      "bind = $mod SHIFT, left, swapwindow, l\n"
      "bind = $mod SHIFT, right, swapwindow, r\n"
      "bind = $mod SHIFT, up, swapwindow, u\n"
      "bind = $mod SHIFT, down, swapwindow, d\n"
      "bind = $mod, Tab, cyclenext\n"
      "bind = $mod SHIFT, Tab, swapnext\n"
      "bind = $mod, C, exec, desktop session\n"
      "bind = $mod, 1, workspace, 1\n"
      "bind = $mod, 2, workspace, 2\n"
      "bind = $mod, 3, workspace, 3\n"
      "bind = $mod SHIFT, 1, movetoworkspace, 1\n"
      "bind = $mod SHIFT, 2, movetoworkspace, 2\n"
      "bind = $mod SHIFT, 3, movetoworkspace, 3\n"
      "bind = F1, exec, desktop open terminal\n"
      "bind = F2, killactive\n"
      "bind = F4, fullscreen\n"
      "bind = F5, pseudo\n"
      "bind = F9, submap, resize\n"
      "bind = F10, submap, move\n"
      "bind = F11, submap, launch\n"
      "bind = F12, submap, reset\n"
      "submap = resize\n"
      "bind = , right, resizeactive, 5 0\n"
      "bind = , left, resizeactive, -5 0\n"
      "bind = , up, resizeactive, 0 5\n"
      "bind = , down, resizeactive, 0 -5\n"
      "bind = , escape, submap, reset\n"
      "submap = move\n"
      "bind = , right, movefocus, r\n"
      "bind = , left, movefocus, l\n"
      "bind = , 1, movetoworkspace, 1\n"
      "bind = , 2, movetoworkspace, 2\n"
      "bind = , 3, movetoworkspace, 3\n"
      "bind = , escape, submap, reset\n"
      "submap = launch\n"
      "bind = , t, exec, terminal\n"
      "bind = , s, exec, orizon-settings\n"
      "bind = , l, exec, orizon-logs\n"
      "bind = , p, exec, orizon-packages\n"
      "bind = , u, exec, orizon-update-viewer\n"
      "bind = , d, exec, orizon-launcher\n"
      "bind = , q, killactive\n"
      "bind = , escape, submap, reset\n"
      "submap = default\n"
      "dwindle:pseudotile = true\n"
      "dwindle:preserve_split = true\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_TEMPLATE_PATH "\n"
      "# Orizon Hyprland-style package template\n"
      "install = optional\n"
      "open-terminal = F1\n"
      "close-terminal = F2\n"
      "status-command = desktop status\n"
      "autostart-command = desktop autostart\n"
      "profiles-command = desktop profiles\n"
      "preset-command = desktop preset <name>\n"
      "settings-command = desktop settings\n"
      "settings-paths-command = desktop settings paths\n"
      "settings-export-command = desktop settings export\n"
      "settings-sync-command = desktop settings sync\n"
      "modules-command = desktop modules\n"
      "module-map = " ORIZON_DESKTOP_MODULES_PATH "\n"
      "module-core = " ORIZON_DESKTOP_PACKAGE_CORE "\n"
      "module-core-package = " ORIZON_DESKTOP_PACKAGE_CORE_PATH "\n"
      "module-hypr = " ORIZON_DESKTOP_PACKAGE "\n"
      "module-terminal = " ORIZON_DESKTOP_PACKAGE_TERMINAL "\n"
      "module-terminal-package = " ORIZON_DESKTOP_PACKAGE_TERMINAL_PATH "\n"
      "module-settings = " ORIZON_DESKTOP_PACKAGE_SETTINGS "\n"
      "module-settings-package = " ORIZON_DESKTOP_PACKAGE_SETTINGS_PATH "\n"
      "module-launcher = " ORIZON_DESKTOP_PACKAGE_LAUNCHER "\n"
      "module-launcher-package = " ORIZON_DESKTOP_PACKAGE_LAUNCHER_PATH "\n"
      "module-waybar-future = " ORIZON_DESKTOP_PACKAGE_WAYBAR "\n"
      "module-waybar-package-future = " ORIZON_DESKTOP_PACKAGE_WAYBAR_PATH "\n"
      "module-sample-command = pkg sample <orizon-desktop-core|orizon-terminal|orizon-settings|orizon-launcher>\n"
      "module-install-command = pkg install <orizon-desktop-core|orizon-terminal|orizon-settings|orizon-launcher>\n"
      "module-waybar-policy = planned-only-not-installed-now\n"
      "apps-command = desktop apps\n"
      "app-detail-command = desktop app <id>\n"
      "terminal-app-command = desktop launch terminal\n"
      "settings-app-command = desktop launch settings\n"
      "logs-app-command = desktop launch logs\n"
      "packages-app-command = desktop launch packages\n"
      "update-app-command = desktop launch update\n"
      "launcher-app-command = desktop launch launcher\n"
      "launcher-policy = overlay-only-no-taskbar-no-start-menu\n"
      "settings-preset-command = desktop settings preset <name>\n"
      "settings-doctor-command = desktop settings doctor\n"
      "config-doctor-command = desktop config doctor\n"
      "config-apply-command = desktop config apply\n"
      "config-trace-command = desktop config trace\n"
      "session-start-command = desktop start\n"
      "session-stop-command = desktop stop\n"
      "session-restart-command = desktop restart\n"
      "session-reload-command = desktop reload\n"
      "session-recover-command = desktop recover\n"
      "session-rescue-command = desktop rescue\n"
      "session-state-command = desktop state\n"
      "runtime-command = desktop runtime\n"
      "rules-command = desktop rules\n"
      "monitors-command = desktop monitors\n"
      "layers-command = desktop layers\n"
      "keyword-command = desktop keyword <key> <value>\n"
      "devices-command = desktop devices\n"
      "keymap-command = desktop keymap\n"
      "version-command = desktop version\n"
      "systeminfo-command = desktop systeminfo\n"
      "layouts-command = desktop layouts\n"
      "layout-tree-command = desktop layout-tree\n"
      "animations-command = desktop animations\n"
      "decorations-command = desktop decorations\n"
      "render-command = desktop render\n"
      "render-profile-command = desktop settings set render-profile <balanced|performance|cozy>\n"
      "focus-ring-command = desktop settings set focus-ring <yes|no>\n"
      "shadow-range-command = desktop keyword decoration:shadow:range <0-32>\n"
      "animation-budget-command = desktop keyword animations:tick_budget <4-60>\n"
      "backend-command = desktop backend\n"
      "protocol-command = desktop protocol\n"
      "descriptions-command = desktop descriptions\n"
      "instances-command = desktop instances\n"
      "submap-command = desktop submap\n"
      "configerrors-command = desktop configerrors\n"
      "rollinglog-command = desktop rollinglog\n"
      "clients-command = desktop clients\n"
      "client-model-command = desktop client-model\n"
      "activewindow-command = desktop activewindow\n"
      "focus-history-command = desktop focus-history\n"
      "binds-runtime = " ORIZON_DESKTOP_BINDS_PATH "\n"
      "autostart-runtime = " ORIZON_DESKTOP_AUTOSTART_PATH "\n"
      "rules-runtime = " ORIZON_DESKTOP_RULES_PATH "\n"
      "monitors-runtime = " ORIZON_DESKTOP_MONITORS_PATH "\n"
      "layers-runtime = " ORIZON_DESKTOP_LAYERS_PATH "\n"
      "runtime-state = " ORIZON_DESKTOP_RUNTIME_PATH "\n"
      "session-state = " ORIZON_DESKTOP_STATE_PATH "\n"
      "session-log = " ORIZON_DESKTOP_SESSION_LOG_PATH "\n"
      "settings-path = " ORIZON_DESKTOP_SETTINGS_PATH "\n"
      "input-command = desktop input\n"
      "input-layout-command = desktop input layout <fr|us>\n"
      "input-pointer-command = desktop input pointer <flat|natural|precise|accelerated>\n"
      "input-focus-command = desktop input focus <on|off|toggle>\n"
      "focus-command = desktop focus on|off|toggle\n"
      "dispatch-command = desktop dispatch <dispatcher> [args]\n"
      "dispatch-workspace-command = desktop dispatch workspace <1-10|next|empty|+/-n|previous>\n"
      "dispatch-movetoworkspace-command = desktop dispatch movetoworkspace <target>\n"
      "dispatch-movetoworkspacesilent-command = desktop dispatch movetoworkspacesilent <target>\n"
      "dispatch-togglesplit-command = desktop dispatch togglesplit\n"
      "dispatch-layoutmsg-command = desktop dispatch layoutmsg <message>\n"
      "dispatch-focusmaster-command = desktop dispatch focusmaster\n"
      "dispatch-swapwithmaster-command = desktop dispatch swapwithmaster\n"
      "dispatch-swapwindow-command = desktop dispatch swapwindow <l|r|u|d|next|prev>\n"
      "dispatch-movefocus-directional-command = desktop dispatch movefocus <l|r|u|d|next|prev>\n"
      "dispatch-splitratio-command = desktop dispatch layoutmsg splitratio <10-90|+/-n>\n"
      "dispatch-masterratio-command = desktop dispatch layoutmsg masterratio <10-90|+/-n>\n"
      "dispatch-resizeactive-command = desktop dispatch resizeactive <x> <y>\n"
      "dispatch-submap-command = desktop dispatch submap <name|reset>\n"
      "hyprctl-command = desktop hyprctl <command>\n"
      "hyprctl-version-command = desktop hyprctl version\n"
      "hyprctl-systeminfo-command = desktop hyprctl systeminfo\n"
      "hyprctl-backend-command = desktop hyprctl backend\n"
      "hyprctl-protocol-command = desktop hyprctl protocol\n"
      "hyprctl-activeworkspace-command = desktop hyprctl activeworkspace\n"
      "hyprctl-activewindow-command = desktop hyprctl activewindow\n"
      "hyprctl-clients-command = desktop hyprctl clients\n"
      "hyprctl-clientmodel-command = desktop hyprctl clientmodel\n"
      "hyprctl-focushistory-command = desktop hyprctl focushistory\n"
      "hyprctl-layouts-command = desktop hyprctl layouts\n"
      "hyprctl-layouttree-command = desktop hyprctl layouttree\n"
      "hyprctl-animations-command = desktop hyprctl animations\n"
      "hyprctl-decorations-command = desktop hyprctl decorations\n"
      "hyprctl-render-command = desktop hyprctl render\n"
      "hyprctl-descriptions-command = desktop hyprctl descriptions\n"
      "hyprctl-instances-command = desktop hyprctl instances\n"
      "hyprctl-submap-command = desktop hyprctl submap\n"
      "hyprctl-devices-command = desktop hyprctl devices\n"
      "hyprctl-keymap-command = desktop hyprctl keymap\n"
      "hyprctl-cursorpos-command = desktop hyprctl cursorpos\n"
      "hyprctl-splash-command = desktop hyprctl splash\n"
      "hyprctl-configerrors-command = desktop hyprctl configerrors\n"
      "hyprctl-configtrace-command = desktop hyprctl configtrace\n"
      "hyprctl-rollinglog-command = desktop hyprctl rollinglog\n"
      "hyprctl-getoption-command = desktop hyprctl getoption <key>\n"
      "hyprctl-keyword-command = desktop hyprctl keyword <key> <value>\n"
      "content-end\n"
      "file " ORIZON_DESKTOP_SESSION_PATH "\n"
      "# Orizon desktop session v1\n"
      "theme graphite\n"
      "wallpaper aurora\n"
      "layout dwindle\n"
      "bar yes\n"
      "launcher yes\n"
      "autostart-terminal yes\n"
      "focus-follows-mouse no\n"
      "content-end\n"
      "post-install\n"
      "mkdir /workspace/packages\n"
      "append " ORIZON_DESKTOP_LOG_PATH " orizon-desktop-hypr installed\n"
      "append " ORIZON_DESKTOP_SESSION_LOG_PATH " package-install desired-state=started runtime-state=active\n"
      "echo post-install: desktop enabled; run desktop start or reboot\n"
      "end-post-install\n"
      "pre-remove\n"
      "echo pre-remove: orizon-desktop-hypr cleanup starting\n"
      "end-pre-remove\n"
      "post-remove\n"
      "write " ORIZON_DESKTOP_CONFIG_PATH " enabled no\n"
      "write " ORIZON_DESKTOP_STATE_PATH " desired-state stopped\n"
      "append " ORIZON_DESKTOP_LOG_PATH " orizon-desktop-hypr removed\n"
      "append " ORIZON_DESKTOP_SESSION_LOG_PATH " package-remove desired-state=stopped runtime-state=inactive\n"
      "end-post-remove\n";
  char hash[SHA256_HEX_SIZE];
  char header[288];
  char path[] = ORIZON_DESKTOP_PACKAGE_PATH;

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!pkg_initialized) {
    orizon_pkg_init();
  }
  sha256_buffer_hex(desktop_payload, sizeof(desktop_payload) - 1, hash);
  snprintf(header, sizeof(header),
           "orizon-package 1\n"
           "name " ORIZON_DESKTOP_PACKAGE "\n"
           "version " ORIZON_DESKTOP_PACKAGE_VERSION "\n"
           "depends orizon-core core-x86_64\n"
           "depends orizon-packages text-payload-v5\n"
           "depends orizon-desktop-base hyprland-style-profile-runtime\n"
           "sha256 %s\n"
           "payload:\n",
           hash);
  if (pkg_write_blob_internal(path, header, strlen(header)) < 0 ||
      pkg_append_text_internal(path, desktop_payload) < 0) {
    pkg_append_line(report, report_size,
                    "pkg sample desktop: cannot write package");
    return -1;
  }
  pkg_append_line(report, report_size,
                  "Desktop package written to " ORIZON_DESKTOP_PACKAGE_PATH);
  pkg_append_line(report, report_size,
                  "Run after install: pkg install " ORIZON_DESKTOP_PACKAGE_PATH);
  pkg_append_line(report, report_size,
                  "Then: desktop status | desktop modules | desktop settings sync");
  vfs_persist_save();
  return 0;
}
