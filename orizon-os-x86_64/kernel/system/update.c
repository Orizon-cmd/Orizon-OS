/*
 * Orizon OS x86_64 - Installed System Update Manager
 *
 * The updater is intentionally installed-disk only. Live boot can prepare and
 * install the OS; once installed, `update` owns a full boot-payload refresh:
 * fetch manifest, download artifacts from GitHub, verify SHA-256, rewrite the
 * ESP, and preserve the Orizon data partition used by /workspace.
 */

#include "../include/update.h"
#include "../include/bootinfo.h"
#include "../include/install.h"
#include "../include/klog.h"
#include "../include/net.h"
#include "../include/netstack.h"
#include "../include/packages.h"
#include "../include/sched.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/vfs.h"

#define UPDATE_STATE_PATH "/workspace/.orizon/update-state"
#define UPDATE_LOG_PATH "/workspace/.orizon/update.log"
#define UPDATE_MANIFEST_PATH "/workspace/.orizon/update-manifest"
#define UPDATE_PACKAGE_INDEX_PATH "/workspace/.orizon/package-index"
#define UPDATE_PROOF_PATH "/workspace/.orizon/github-https-manifest"
#define UPDATE_PROOF_HASH_PATH "/workspace/.orizon/github-https-manifest.sha256"
#define UPDATE_LAST_SUCCESS_PATH "/workspace/.orizon/last-update"
#define UPDATE_ROLLBACK_STATE_PATH "/workspace/.orizon/rollback-state"
#define UPDATE_ROLLBACK_INFO_PATH "/workspace/.orizon/rollback-info"
#define SYSTEM_STATE_PATH "/system/update-state"
#define SYSTEM_SOURCE_PATH "/system/update-source"
#define SYSTEM_MANIFEST_PATH "/system/update-manifest"
#define UPDATE_SOURCE "https://github.com/Orizon-cmd/Orizon-OS"
#define UPDATE_CHANNEL "main"
#define UPDATE_RAW_HOST "raw.githubusercontent.com"
#define UPDATE_RAW_PREFIX "/Orizon-cmd/Orizon-OS/main/"
#define UPDATE_MANIFEST_REMOTE "updates/x86_64/manifest.txt"
#define UPDATE_PACKAGE_SOURCE "https://github.com/Orizon-cmd/Orizon-Packages"
#define UPDATE_PACKAGE_RAW_PREFIX "/Orizon-cmd/Orizon-Packages/main/"
#define UPDATE_PACKAGE_INDEX_REMOTE "packages/x86_64/index.txt"
#define UPDATE_CHUNK_BYTES 65536U
#define UPDATE_RANGE_RETRIES 5U
#define UPDATE_MANIFEST_MAX 4096U
#define UPDATE_PACKAGE_INDEX_MAX 8192U
#define UPDATE_PACKAGE_MAX (48U * 1024U)
#define UPDATE_PACKAGE_MAX_ENTRIES 16U
#define UPDATE_INSTALLED_DB_MAX 8192U
#define UPDATE_KERNEL_MAX (4U * 1024U * 1024U)
#define UPDATE_EFI_MAX (512U * 1024U)
#define UPDATE_CONF_MAX 4096U

typedef struct {
  char name[64];
  char version[64];
  char path[180];
  char sha256[SHA256_HEX_SIZE];
  size_t size;
} update_package_index_entry_t;

typedef struct {
  update_package_index_entry_t entries[UPDATE_PACKAGE_MAX_ENTRIES];
  size_t count;
} update_package_index_t;

typedef struct {
  char version[64];
  char commit[64];
  char kernel_path[160];
  char kernel_sha256[SHA256_HEX_SIZE];
  size_t kernel_size;
  char efi_path[160];
  char efi_sha256[SHA256_HEX_SIZE];
  size_t efi_size;
  char limine_path[160];
  char limine_sha256[SHA256_HEX_SIZE];
  size_t limine_size;
} update_manifest_t;

static const char *update_status_text = "update: not run";
static char update_manifest_text[UPDATE_MANIFEST_MAX];
static char update_package_index_text[UPDATE_PACKAGE_INDEX_MAX];
static char update_installed_db_text[UPDATE_INSTALLED_DB_MAX];
static uint8_t update_package_blob[UPDATE_PACKAGE_MAX] __attribute__((aligned(4096)));
static uint8_t update_kernel[UPDATE_KERNEL_MAX] __attribute__((aligned(4096)));
static uint8_t update_efi[UPDATE_EFI_MAX] __attribute__((aligned(4096)));
static char update_limine_conf[UPDATE_CONF_MAX] __attribute__((aligned(4096)));
static uint8_t update_chunk[UPDATE_CHUNK_BYTES] __attribute__((aligned(4096)));
static orizon_update_progress_fn update_progress_fn = NULL;
static void *update_progress_ctx = NULL;

static const char rollback_limine_entry[] =
    "\n"
    "/Orizon OS Rollback\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/KROLLBK.ELF\n"
    "    module_path: boot():/EFI/BOOT/BOOTX64.ROL\n"
    "    module_cmdline: orizon-bootx64 rollback\n";

static const char rollback_restore_limine_conf[] =
    "# Limine Configuration File\n"
    "# Orizon OS x86_64 rollback restore\n"
    "\n"
    "timeout: 5\n"
    "default_entry: 1\n"
    "\n"
    "/Orizon OS\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    module_path: boot():/EFI/BOOT/BOOTX64.EFI\n"
    "    module_cmdline: orizon-bootx64\n";

static void update_write_file(const char *path, const char *text, int append) {
  file_t *f = vfs_open(path, O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC));
  if (!f) {
    return;
  }
  if (text) {
    vfs_write(f, text, strlen(text));
  }
  vfs_close(f);
}

void orizon_update_set_progress(orizon_update_progress_fn fn, void *ctx) {
  update_progress_fn = fn;
  update_progress_ctx = ctx;
}

static void update_progress_line(const char *line) {
  if (update_progress_fn && line) {
    update_progress_fn(line, update_progress_ctx);
  }
}

static void update_write_blob(const char *path, const void *data, size_t size) {
  file_t *f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return;
  }
  if (data && size > 0) {
    vfs_write(f, data, size);
  }
  vfs_close(f);
}

static int update_read_file(const char *path, char *buf, size_t cap,
                            size_t *out_len) {
  file_t *f;
  size_t used = 0;
  ssize_t n = 0;

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
  if (out_len) {
    *out_len = used;
  }
  return 0;
}

static void update_append_log(const char *line) {
  update_write_file(UPDATE_LOG_PATH, line, 1);
  update_write_file(UPDATE_LOG_PATH, "\n", 1);
}

static void update_write_line(const char *path, const char *line) {
  update_write_file(path, line, 0);
  update_write_file(path, "\n", 1);
}

static void update_set_state(const char *state) {
  update_status_text = state;
  klog_info("update", state);
  update_write_line(UPDATE_STATE_PATH, state);
  update_write_line(SYSTEM_STATE_PATH, state);
  update_append_log(state);
  update_progress_line(state);
}

static void append_report(char *report, size_t report_size, const char *line) {
  size_t used;
  if (!report || report_size == 0 || !line) {
    return;
  }
  used = strlen(report);
  if (used + 1 >= report_size) {
    return;
  }
  snprintf(report + used, report_size - used, "%s\n", line);
  update_progress_line(line);
}

static void append_report_block(char *report, size_t report_size,
                                const char *text) {
  const char *p = text;
  char line[256];

  if (!text) {
    return;
  }
  while (*p) {
    size_t len = 0;
    while (p[len] && p[len] != '\n') {
      len++;
    }
    if (len > 0) {
      size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
      memcpy(line, p, copy);
      line[copy] = '\0';
      append_report(report, report_size, line);
    }
    p += len;
    if (*p == '\n') {
      p++;
    }
  }
}

static void update_emit_report_tail(const char *report, size_t start) {
  char line[256];
  size_t pos = start;

  if (!report || !update_progress_fn) {
    return;
  }
  while (report[pos]) {
    size_t len = 0;
    while (report[pos + len] && report[pos + len] != '\n') {
      len++;
    }
    if (len > 0) {
      size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
      memcpy(line, report + pos, copy);
      line[copy] = '\0';
      update_progress_line(line);
    }
    pos += len;
    if (report[pos] == '\n') {
      pos++;
    }
  }
}

static int update_installed_marker_present(void) {
  return vfs_exists("/workspace/.orizon/installed");
}

static int append_limine_rollback_entry(char *conf, size_t cap) {
  size_t used;
  if (!conf || cap == 0) {
    return -1;
  }
  if (strstr(conf, "KROLLBK.ELF") || strstr(conf, "Orizon OS Rollback")) {
    return 0;
  }
  used = strlen(conf);
  if (used + strlen(rollback_limine_entry) + 1 >= cap) {
    return -1;
  }
  snprintf(conf + used, cap - used, "%s", rollback_limine_entry);
  return 0;
}

static int hex_equal(const char *a, const char *b) {
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

static int parse_size_value(const char *text, size_t *out) {
  size_t value = 0;
  int seen = 0;
  while (*text >= '0' && *text <= '9') {
    value = value * 10 + (size_t)(*text - '0');
    text++;
    seen = 1;
  }
  if (!seen || (*text != '\0' && *text != '\n' && *text != '\r' && *text != ' ')) {
    return -1;
  }
  *out = value;
  return 0;
}

static int manifest_copy_value(const char *manifest, const char *key, char *out,
                               size_t out_size) {
  size_t key_len = strlen(key);
  const char *p = manifest;

  if (!manifest || !key || !out || out_size == 0) {
    return -1;
  }
  while (*p) {
    const char *line = p;
    size_t len = 0;
    while (p[len] && p[len] != '\n') {
      len++;
    }
    if (len > key_len && strncmp(line, key, key_len) == 0 &&
        line[key_len] == ' ') {
      size_t value_len = len - key_len - 1;
      if (value_len >= out_size) {
        value_len = out_size - 1;
      }
      memcpy(out, line + key_len + 1, value_len);
      out[value_len] = '\0';
      return 0;
    }
    p += len;
    if (*p == '\n') {
      p++;
    }
  }
  return -1;
}

static int manifest_size_value(const char *manifest, const char *key,
                               size_t *out) {
  char value[32];
  if (manifest_copy_value(manifest, key, value, sizeof(value)) < 0) {
    return -1;
  }
  return parse_size_value(value, out);
}

static int parse_update_manifest(const char *text, update_manifest_t *manifest) {
  if (!text || !manifest || !strstr(text, "manifest-version 1") ||
      !strstr(text, "os Orizon OS")) {
    return -1;
  }
  memset(manifest, 0, sizeof(*manifest));
  if (manifest_copy_value(text, "version", manifest->version,
                          sizeof(manifest->version)) < 0 ||
      manifest_copy_value(text, "commit", manifest->commit,
                          sizeof(manifest->commit)) < 0 ||
      manifest_copy_value(text, "kernel-path", manifest->kernel_path,
                          sizeof(manifest->kernel_path)) < 0 ||
      manifest_copy_value(text, "kernel-sha256", manifest->kernel_sha256,
                          sizeof(manifest->kernel_sha256)) < 0 ||
      manifest_size_value(text, "kernel-size", &manifest->kernel_size) < 0 ||
      manifest_copy_value(text, "efi-path", manifest->efi_path,
                          sizeof(manifest->efi_path)) < 0 ||
      manifest_copy_value(text, "efi-sha256", manifest->efi_sha256,
                          sizeof(manifest->efi_sha256)) < 0 ||
      manifest_size_value(text, "efi-size", &manifest->efi_size) < 0 ||
      manifest_copy_value(text, "limine-path", manifest->limine_path,
                          sizeof(manifest->limine_path)) < 0 ||
      manifest_copy_value(text, "limine-sha256", manifest->limine_sha256,
                          sizeof(manifest->limine_sha256)) < 0 ||
      manifest_size_value(text, "limine-size", &manifest->limine_size) < 0) {
    return -1;
  }
  if (manifest->kernel_size == 0 || manifest->kernel_size > UPDATE_KERNEL_MAX ||
      manifest->efi_size == 0 || manifest->efi_size > UPDATE_EFI_MAX ||
      manifest->limine_size == 0 || manifest->limine_size >= UPDATE_CONF_MAX) {
    return -1;
  }
  return 0;
}

static const char *copy_token(const char *p, char *out, size_t out_size) {
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

static int sha256_text_valid(const char *text) {
  if (!text || strlen(text) != SHA256_HEX_SIZE - 1) {
    return 0;
  }
  for (size_t i = 0; i < SHA256_HEX_SIZE - 1; i++) {
    char c = text[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return 0;
    }
  }
  return 1;
}

static int package_name_safe(const char *name) {
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

static int package_path_safe(const char *path) {
  if (!path || path[0] == '/' || !strstr(path, ".opkg") ||
      strncmp(path, "packages/x86_64/", 16) != 0) {
    return 0;
  }
  while (*path) {
    if (path[0] == '.' && path[1] == '.') {
      return 0;
    }
    path++;
  }
  return 1;
}

static int parse_package_index_line(const char *line,
                                    update_package_index_entry_t *entry) {
  const char *p = line + 8;
  char size_text[32];

  memset(entry, 0, sizeof(*entry));
  p = copy_token(p, entry->name, sizeof(entry->name));
  if (!p) {
    return -1;
  }
  p = copy_token(p, entry->version, sizeof(entry->version));
  if (!p) {
    return -1;
  }
  p = copy_token(p, entry->path, sizeof(entry->path));
  if (!p) {
    return -1;
  }
  p = copy_token(p, size_text, sizeof(size_text));
  if (!p || parse_size_value(size_text, &entry->size) < 0) {
    return -1;
  }
  p = copy_token(p, entry->sha256, sizeof(entry->sha256));
  if (!p || !package_name_safe(entry->name) ||
      !package_path_safe(entry->path) || entry->version[0] == '\0' ||
      entry->size == 0 || entry->size > UPDATE_PACKAGE_MAX ||
      !sha256_text_valid(entry->sha256)) {
    return -1;
  }
  return 0;
}

static int parse_package_index(const char *text,
                               update_package_index_t *index) {
  const char *p = text;

  if (!text || !index || !strstr(text, "index-version 1") ||
      !strstr(text, "os Orizon OS")) {
    return -1;
  }
  memset(index, 0, sizeof(*index));
  while (*p) {
    const char *line = p;
    size_t len = 0;
    char copy[320];

    while (p[len] && p[len] != '\n') {
      len++;
    }
    if (len > 0 && len < sizeof(copy)) {
      memcpy(copy, line, len);
      copy[len] = '\0';
      if (len > 8 && strncmp(copy, "package ", 8) == 0) {
        if (index->count >= UPDATE_PACKAGE_MAX_ENTRIES ||
            parse_package_index_line(copy, &index->entries[index->count]) < 0) {
          return -1;
        }
        index->count++;
      }
    }
    p += len;
    if (*p == '\n') {
      p++;
    }
  }
  return 0;
}

static int installed_package_version_is(const char *name, const char *version) {
  const char *p = update_installed_db_text;
  size_t name_len = strlen(name);
  size_t version_len = strlen(version);

  while (*p) {
    const char *line = p;
    size_t len = 0;
    while (p[len] && p[len] != '\n') {
      len++;
    }
    if (len > name_len + 1 + version_len &&
        strncmp(line, name, name_len) == 0 && line[name_len] == ' ' &&
        strncmp(line + name_len + 1, version, version_len) == 0 &&
        (line[name_len + 1 + version_len] == ' ' ||
         line[name_len + 1 + version_len] == '\r')) {
      return 1;
    }
    p += len;
    if (*p == '\n') {
      p++;
    }
  }
  return 0;
}

static int build_prefixed_raw_path(const char *prefix, const char *relative,
                                   char *out, size_t out_size) {
  const char *rel = relative;
  if (!prefix || !relative || !out || out_size == 0) {
    return -1;
  }
  while (*rel == '/') {
    rel++;
  }
  return snprintf(out, out_size, "%s%s", prefix, rel) < (int)out_size
             ? 0
             : -1;
}

static int download_range_prefixed(const char *prefix, const char *relative,
                                   uint64_t start, uint64_t end, void *out,
                                   size_t out_cap, size_t *out_len, char *diag,
                                   size_t diag_cap) {
  char path[240];
  if (build_prefixed_raw_path(prefix, relative, path, sizeof(path)) < 0) {
    return -1;
  }
  return netstack_https_range_get(UPDATE_RAW_HOST, path, start, end, out,
                                  out_cap, out_len, diag, diag_cap);
}

static int download_range_path(const char *relative, uint64_t start,
                               uint64_t end, void *out, size_t out_cap,
                               size_t *out_len, char *diag,
                               size_t diag_cap) {
  return download_range_prefixed(UPDATE_RAW_PREFIX, relative, start, end, out,
                                 out_cap, out_len, diag, diag_cap);
}

static int download_verified_blob(const char *label, const char *prefix,
                                  const char *relative, size_t expected_size,
                                  const char *expected_hash, void *dst,
                                  size_t dst_cap, char *report,
                                  size_t report_size) {
  size_t done = 0;
  char line[224];
  char diag[192];
  char actual_hash[SHA256_HEX_SIZE];
  unsigned next_percent = 0;

  if (!relative || !expected_hash || !dst || expected_size == 0 ||
      expected_size > dst_cap) {
    append_report(report, report_size, "update: invalid manifest artifact");
    return -1;
  }

  snprintf(line, sizeof(line), "Downloading %s (%lu bytes)", label,
           (unsigned long)expected_size);
  append_report(report, report_size, line);
  update_append_log(line);

  snprintf(line, sizeof(line), "Get: %s single HTTPS request [%lu KiB]", label,
           (unsigned long)((expected_size + 1023) / 1024));
  update_progress_line(line);
  update_append_log(line);
  diag[0] = '\0';
  if (download_range_prefixed(prefix, relative, 0,
                              (uint64_t)(expected_size - 1), dst, dst_cap,
                              &done, diag, sizeof(diag)) == 0 &&
      done == expected_size) {
    snprintf(line, sizeof(line), "Get: %s 100%% [%lu/%lu KiB]", label,
             (unsigned long)((done + 1023) / 1024),
             (unsigned long)((expected_size + 1023) / 1024));
    update_progress_line(line);
    update_append_log("update: single HTTPS range request complete");
    goto verify_hash;
  }

  snprintf(line, sizeof(line),
           "update: single range incomplete for %s got=%lu expected=%lu %s",
           label, (unsigned long)done, (unsigned long)expected_size, diag);
  update_append_log(line);
  append_report(report, report_size,
                "update: fast download fallback to ranged chunks");
  done = 0;

  while (done < expected_size) {
    size_t wanted = expected_size - done;
    size_t got = 0;
    int rc = -1;
    int ok = 0;
    if (wanted > UPDATE_CHUNK_BYTES) {
      wanted = UPDATE_CHUNK_BYTES;
    }

    for (unsigned attempt = 1; attempt <= UPDATE_RANGE_RETRIES; attempt++) {
      got = 0;
      diag[0] = '\0';
      rc = download_range_prefixed(prefix, relative, (uint64_t)done,
                                   (uint64_t)(done + wanted - 1),
                                   update_chunk, sizeof(update_chunk), &got,
                                   diag, sizeof(diag));
      if (rc == 0 && got > 0 && got <= wanted) {
        ok = 1;
        break;
      }
      snprintf(line, sizeof(line),
               "update: retry %lu/%lu for %s at %lu rc=%d got=%lu %s",
               (unsigned long)attempt, (unsigned long)UPDATE_RANGE_RETRIES,
               label, (unsigned long)done, rc, (unsigned long)got, diag);
      update_append_log(line);
    }

    if (!ok) {
      snprintf(line, sizeof(line),
               "update: download failed for %s at %lu rc=%d got=%lu",
               label, (unsigned long)done, rc, (unsigned long)got);
      append_report(report, report_size, line);
      update_append_log(line);
      if (diag[0]) {
        append_report(report, report_size, diag);
        update_append_log(diag);
      }
      return -2;
    }
    memcpy((uint8_t *)dst + done, update_chunk, got);
    done += got;

    if (update_progress_fn) {
      unsigned percent =
          (unsigned)((done * 100ULL) / (uint64_t)expected_size);
      if (percent >= next_percent || done == expected_size) {
        snprintf(line, sizeof(line), "Get: %s %u%% [%lu/%lu KiB]", label,
                 percent, (unsigned long)((done + 1023) / 1024),
                 (unsigned long)((expected_size + 1023) / 1024));
        update_progress_line(line);
        while (next_percent <= percent && next_percent < 100) {
          next_percent += 10;
        }
      }
    }
  }

verify_hash:
  sha256_buffer_hex(dst, expected_size, actual_hash);
  if (!hex_equal(actual_hash, expected_hash)) {
    snprintf(line, sizeof(line), "update: sha256 mismatch for %s", label);
    append_report(report, report_size, line);
    update_append_log(line);
    snprintf(line, sizeof(line), "expected %s", expected_hash);
    update_append_log(line);
    snprintf(line, sizeof(line), "actual   %s", actual_hash);
    update_append_log(line);
    return -3;
  }

  snprintf(line, sizeof(line), "%s verified sha256 %s", label, actual_hash);
  append_report(report, report_size, line);
  update_append_log(line);
  return 0;
}

static int download_artifact(const char *label, const char *relative,
                             size_t expected_size, const char *expected_hash,
                             void *dst, size_t dst_cap, char *report,
                             size_t report_size) {
  return download_verified_blob(label, UPDATE_RAW_PREFIX, relative,
                                expected_size, expected_hash, dst, dst_cap,
                                report, report_size);
}

static int update_install_remote_packages(char *report, size_t report_size) {
  update_package_index_t index;
  char line[256];
  char pkg_report[2048];
  size_t index_len = 0;
  size_t installed_len = 0;
  size_t installed_count = 0;
  size_t skipped_count = 0;
  char index_hash[SHA256_HEX_SIZE];

  update_set_state("update: downloading package index");
  append_report(report, report_size,
                "[6/8] Checking Orizon package repository");
  append_report(report, report_size, "Package source: " UPDATE_PACKAGE_SOURCE);
  if (download_range_prefixed(UPDATE_PACKAGE_RAW_PREFIX,
                              UPDATE_PACKAGE_INDEX_REMOTE, 0,
                              UPDATE_PACKAGE_INDEX_MAX - 2,
                              update_package_index_text,
                              sizeof(update_package_index_text) - 1,
                              &index_len, line, sizeof(line)) != 0 ||
      index_len == 0) {
    update_set_state("update: blocked - package index download failed");
    append_report(report, report_size, "update: package index download failed");
    return -1;
  }
  update_package_index_text[index_len] = '\0';
  sha256_buffer_hex(update_package_index_text, index_len, index_hash);
  update_write_blob(UPDATE_PACKAGE_INDEX_PATH, update_package_index_text,
                    index_len);
  snprintf(line, sizeof(line), "Get: package index [%lu bytes]",
           (unsigned long)index_len);
  append_report(report, report_size, line);
  snprintf(line, sizeof(line), "package-index sha256 %s", index_hash);
  update_append_log(line);

  if (parse_package_index(update_package_index_text, &index) < 0) {
    update_set_state("update: blocked - invalid package index");
    append_report(report, report_size, "update: invalid package index");
    return -2;
  }

  orizon_pkg_refresh_database();
  if (update_read_file("/system/installed", update_installed_db_text,
                       sizeof(update_installed_db_text), &installed_len) < 0) {
    update_installed_db_text[0] = '\0';
  }

  for (size_t i = 0; i < index.count; i++) {
    const update_package_index_entry_t *entry = &index.entries[i];
    if (installed_package_version_is(entry->name, entry->version)) {
      skipped_count++;
      snprintf(line, sizeof(line), "Skip: %s %s already installed",
               entry->name, entry->version);
      append_report(report, report_size, line);
      continue;
    }

    snprintf(line, sizeof(line), "Inst: %s %s", entry->name, entry->version);
    append_report(report, report_size, line);
    if (download_verified_blob(entry->name, UPDATE_PACKAGE_RAW_PREFIX,
                               entry->path, entry->size, entry->sha256,
                               update_package_blob, sizeof(update_package_blob),
                               report, report_size) < 0) {
      update_set_state("update: blocked - package download failed");
      return -3;
    }

    if (orizon_pkg_install_buffer(entry->path, update_package_blob, entry->size,
                                  pkg_report, sizeof(pkg_report)) != 0) {
      append_report_block(report, report_size, pkg_report);
      update_set_state("update: blocked - package install failed");
      return -4;
    }
    append_report_block(report, report_size, pkg_report);
    installed_count++;
  }

  orizon_pkg_refresh_database();
  snprintf(line, sizeof(line), "Packages: %lu installed, %lu already current",
           (unsigned long)installed_count, (unsigned long)skipped_count);
  append_report(report, report_size, line);
  return 0;
}

int orizon_update_full_upgrade(char *report, size_t report_size) {
  update_manifest_t manifest;
  char net_line[256];
  char line[256];
  char manifest_hash[SHA256_HEX_SIZE];
  char rollback_hash[SHA256_HEX_SIZE];
  char current_efi_hash[SHA256_HEX_SIZE];
  size_t manifest_len = 0;
  char update_text[512];
  char rollback_text[512];

  if (report && report_size > 0) {
    report[0] = '\0';
  }

  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/system");

  if (!update_installed_marker_present()) {
    append_report(report, report_size,
                  "update: unavailable in live boot. Install Orizon OS first.");
    return -10;
  }
  if (!boot_payloads_ready()) {
    append_report(report, report_size,
                  "update: boot payload capture unavailable, rollback unsafe");
    return -11;
  }
  sha256_buffer_hex(boot_kernel_image(), boot_kernel_image_size(),
                    rollback_hash);
  sha256_buffer_hex(boot_efi_image(), boot_efi_image_size(),
                    current_efi_hash);

  sched_enter_process("update-manager");
  update_write_file(UPDATE_LOG_PATH, "", 0);
  update_write_file(SYSTEM_SOURCE_PATH, UPDATE_SOURCE "\n", 0);

  append_report(report, report_size, "\033[1;36mOrizon full-upgrade\033[0m");
  append_report(report, report_size, "Source: " UPDATE_SOURCE);
  update_append_log("Orizon full-upgrade started");
  update_append_log("Source: " UPDATE_SOURCE);

  update_set_state("update: preparing installed package database");
  orizon_pkg_init();
  orizon_pkg_refresh_database();
  append_report(report, report_size, "[1/8] Installed package database ready");

  update_set_state("update: probing ethernet");
  net_init();
  net_format_status(net_line, sizeof(net_line));
  append_report(report, report_size, "[2/8] Ethernet probe");
  append_report(report, report_size, net_line);
  update_append_log(net_line);
  if (!net_link_up()) {
    update_set_state("update: blocked - ethernet link is down");
    append_report(report, report_size, "update: ethernet link is down");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -1;
  }

  update_set_state("update: configuring ipv4 (dhcp/static)");
  if (netstack_configure_ipv4() != 0) {
    netstack_format_status(net_line, sizeof(net_line));
    update_set_state("update: blocked - ipv4 failed");
    append_report(report, report_size, "[3/8] IPv4 failed");
    append_report(report, report_size, net_line);
    append_report(report, report_size,
                  "Hint: run 'net', 'net dhcp' or configure static IPv4 with "
                  "'net config ip <ip> gateway <gw> dns <dns>'.");
    update_append_log(net_line);
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -2;
  }
  netstack_format_status(net_line, sizeof(net_line));
  append_report(report, report_size, "[3/8] IPv4 ready");
  append_report(report, report_size, net_line);
  update_append_log(net_line);

  update_set_state("update: downloading github manifest");
  if (download_range_path(UPDATE_MANIFEST_REMOTE, 0, UPDATE_MANIFEST_MAX - 2,
                          update_manifest_text,
                          sizeof(update_manifest_text) - 1, &manifest_len,
                          net_line, sizeof(net_line)) != 0 ||
      manifest_len == 0) {
    update_set_state("update: blocked - manifest download failed");
    append_report(report, report_size, "[4/8] GitHub manifest download failed");
    netstack_format_status(net_line, sizeof(net_line));
    append_report(report, report_size, net_line);
    append_report(report, report_size,
                  "Hint: check DNS/gateway with 'dns raw.githubusercontent.com', "
                  "'route' and 'ping 8.8.8.8'.");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -3;
  }
  update_manifest_text[manifest_len] = '\0';
  snprintf(line, sizeof(line), "Get: manifest.txt [%lu bytes]",
           (unsigned long)manifest_len);
  update_progress_line(line);
  sha256_buffer_hex(update_manifest_text, manifest_len, manifest_hash);
  update_write_blob(UPDATE_PROOF_PATH, update_manifest_text, manifest_len);
  update_write_line(UPDATE_PROOF_HASH_PATH, manifest_hash);
  if (parse_update_manifest(update_manifest_text, &manifest) < 0) {
    update_set_state("update: blocked - invalid manifest");
    append_report(report, report_size, "[4/8] Invalid GitHub update manifest");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -4;
  }
  update_write_blob(UPDATE_MANIFEST_PATH, update_manifest_text, manifest_len);
  update_write_blob(SYSTEM_MANIFEST_PATH, update_manifest_text, manifest_len);
  snprintf(line, sizeof(line), "[4/8] Manifest %s commit %s",
           manifest.version, manifest.commit);
  append_report(report, report_size, line);
  update_append_log(line);

  if (hex_equal(rollback_hash, manifest.kernel_sha256) &&
      hex_equal(current_efi_hash, manifest.efi_sha256)) {
    update_set_state("update: boot payload already current");
    append_report(report, report_size,
                  "[5/8] Boot payload already current, skipping ESP rewrite");
    if (update_install_remote_packages(report, report_size) < 0) {
      vfs_persist_save();
      sched_set_process_state("update-manager", SCHED_SLEEPING);
      sched_enter_process("gui-shell");
      return -6;
    }
    snprintf(update_text, sizeof(update_text),
             "Orizon OS already current\nsource=%s\nchannel=%s\nversion=%s\n"
             "commit=%s\nkernel-sha256=%s\nefi-sha256=%s\n",
             UPDATE_SOURCE, UPDATE_CHANNEL, manifest.version, manifest.commit,
             manifest.kernel_sha256, manifest.efi_sha256);
    update_write_blob(UPDATE_LAST_SUCCESS_PATH, update_text,
                      strlen(update_text));
    update_set_state("update: complete");
    append_report(report, report_size,
                  "[7/8] Installed ESP already matches GitHub");
    append_report(report, report_size,
                  "[8/8] Update complete. No reboot required for boot payloads.");
    update_append_log("Update complete: already current");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return 0;
  }

  update_set_state("update: downloading boot payloads");
  append_report(report, report_size, "[5/8] Downloading verified artifacts");
  if (download_artifact("kernel.elf", manifest.kernel_path,
                        manifest.kernel_size, manifest.kernel_sha256,
                        update_kernel, sizeof(update_kernel), report,
                        report_size) < 0 ||
      download_artifact("BOOTX64.EFI", manifest.efi_path, manifest.efi_size,
                        manifest.efi_sha256, update_efi, sizeof(update_efi),
                        report, report_size) < 0 ||
      download_artifact("limine.conf", manifest.limine_path,
                        manifest.limine_size, manifest.limine_sha256,
                        update_limine_conf, sizeof(update_limine_conf) - 1,
                        report, report_size) < 0) {
    update_set_state("update: blocked - artifact verification failed");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -5;
  }
  update_limine_conf[manifest.limine_size] = '\0';
  if (append_limine_rollback_entry(update_limine_conf,
                                   sizeof(update_limine_conf)) < 0) {
    update_set_state("update: blocked - rollback config failed");
    append_report(report, report_size,
                  "update: cannot add rollback boot entry");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -5;
  }

  if (update_install_remote_packages(report, report_size) < 0) {
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -6;
  }

  snprintf(update_text, sizeof(update_text),
           "Orizon OS updated\nsource=%s\nchannel=%s\nversion=%s\ncommit=%s\n"
           "kernel-sha256=%s\nrollback-kernel-sha256=%s\n",
           UPDATE_SOURCE, UPDATE_CHANNEL, manifest.version, manifest.commit,
           manifest.kernel_sha256, rollback_hash);
  snprintf(rollback_text, sizeof(rollback_text),
           "rollback-version 1\n"
           "state available\n"
           "source %s\n"
           "channel %s\n"
           "updated-version %s\n"
           "updated-commit %s\n"
           "rollback-kernel-sha256 %s\n"
           "boot-entry Orizon OS Rollback\n"
           "restore-command rollback\n",
           UPDATE_SOURCE, UPDATE_CHANNEL, manifest.version, manifest.commit,
           rollback_hash);

  update_set_state("update: writing installed ESP");
  append_report(report, report_size, "[7/8] Rewriting installed boot partition");
  size_t esp_report_start = report ? strlen(report) : 0;
  if (orizon_install_update_esp_with_rollback(
          update_kernel, manifest.kernel_size, update_efi, manifest.efi_size,
          boot_kernel_image(), boot_kernel_image_size(), boot_efi_image(),
          boot_efi_image_size(), update_limine_conf, strlen(update_limine_conf),
          update_text, strlen(update_text), report, report_size) != 0) {
    update_emit_report_tail(report, esp_report_start);
    update_set_state("update: blocked - ESP write failed");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -7;
  }
  update_emit_report_tail(report, esp_report_start);

  update_write_blob(UPDATE_LAST_SUCCESS_PATH, update_text, strlen(update_text));
  update_write_blob(UPDATE_ROLLBACK_INFO_PATH, rollback_text,
                    strlen(rollback_text));
  update_write_line(UPDATE_ROLLBACK_STATE_PATH,
                    "rollback available: choose Orizon OS Rollback at boot or run rollback");
  update_set_state("update: complete");
  append_report(report, report_size,
                "[8/8] Update complete. Reboot to start the refreshed system.");
  append_report(report, report_size,
                "Rollback ready: boot 'Orizon OS Rollback' or run rollback to restore it.");
  update_append_log("Update complete");
  vfs_persist_save();
  sched_set_process_state("update-manager", SCHED_SLEEPING);
  sched_enter_process("gui-shell");
  return 0;
}

int orizon_update_rollback(char *report, size_t report_size) {
  char rollback_hash[SHA256_HEX_SIZE];
  char rollback_text[512];

  if (report && report_size > 0) {
    report[0] = '\0';
  }

  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/system");

  if (!update_installed_marker_present()) {
    append_report(report, report_size,
                  "rollback: unavailable in live boot. Install Orizon OS first.");
    return -10;
  }
  if (!boot_payloads_ready()) {
    append_report(report, report_size,
                  "rollback: boot payload capture unavailable");
    return -11;
  }

  sched_enter_process("update-manager");
  update_set_state("rollback: restoring currently booted payload");
  append_report(report, report_size, "\033[1;36mOrizon rollback\033[0m");
  append_report(report, report_size,
                "Restoring the currently booted kernel/loader as the main boot slot.");

  sha256_buffer_hex(boot_kernel_image(), boot_kernel_image_size(),
                    rollback_hash);
  snprintf(rollback_text, sizeof(rollback_text),
           "Orizon OS rollback restored\nsource=currently-booted-payload\n"
           "kernel-sha256=%s\n",
           rollback_hash);

  if (orizon_install_update_esp(boot_kernel_image(), boot_kernel_image_size(),
                                boot_efi_image(), boot_efi_image_size(),
                                rollback_restore_limine_conf,
                                sizeof(rollback_restore_limine_conf) - 1,
                                rollback_text, strlen(rollback_text), report,
                                report_size) != 0) {
    update_set_state("rollback: ESP restore failed");
    append_report(report, report_size, "rollback: ESP restore failed");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -1;
  }

  update_write_blob(UPDATE_ROLLBACK_INFO_PATH, rollback_text,
                    strlen(rollback_text));
  update_write_line(UPDATE_ROLLBACK_STATE_PATH,
                    "rollback restored: reboot to use restored main slot");
  update_set_state("rollback: complete");
  append_report(report, report_size,
                "Rollback complete. Reboot to use the restored main boot slot.");
  vfs_persist_save();
  sched_set_process_state("update-manager", SCHED_SLEEPING);
  sched_enter_process("gui-shell");
  return 0;
}

const char *orizon_update_status(void) {
  return update_status_text;
}
