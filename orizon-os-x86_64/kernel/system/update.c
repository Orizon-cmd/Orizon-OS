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
#include "../include/rsa.h"
#include "../include/sched.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"

#define UPDATE_STATE_PATH "/workspace/.orizon/update-state"
#define UPDATE_LOG_PATH "/workspace/.orizon/update.log"
#define UPDATE_MANIFEST_PATH "/workspace/.orizon/update-manifest"
#define UPDATE_MANIFEST_SIG_PATH "/workspace/.orizon/update-manifest.sig"
#define UPDATE_PACKAGE_INDEX_PATH "/workspace/.orizon/package-index"
#define UPDATE_PROOF_PATH "/workspace/.orizon/github-https-manifest"
#define UPDATE_PROOF_HASH_PATH "/workspace/.orizon/github-https-manifest.sha256"
#define UPDATE_LAST_SUCCESS_PATH "/workspace/.orizon/last-update"
#define UPDATE_ROLLBACK_STATE_PATH "/workspace/.orizon/rollback-state"
#define UPDATE_ROLLBACK_INFO_PATH "/workspace/.orizon/rollback-info"
#define UPDATE_BOOT_GUARD_PATH "/workspace/.orizon/boot-guard"
#define UPDATE_BOOT_GUARD_STATUS_PATH "/system/boot-guard"
#define UPDATE_KERNEL_CACHE_PATH "/workspace/.orizon/update-kernel.part"
#define UPDATE_EFI_CACHE_PATH "/workspace/.orizon/update-efi.part"
#define UPDATE_LIMINE_CACHE_PATH "/workspace/.orizon/update-limine.part"
#define UPDATE_LIMINE_NORMAL_PATH "/workspace/.orizon/update-limine.normal"
#define UPDATE_LIMINE_FALLBACK_PATH "/workspace/.orizon/update-limine.fallback"
#define SYSTEM_STATE_PATH "/system/update-state"
#define SYSTEM_SOURCE_PATH "/system/update-source"
#define SYSTEM_MANIFEST_PATH "/system/update-manifest"
#define SYSTEM_MANIFEST_SIG_PATH "/system/update-manifest.sig"
#define UPDATE_SOURCE "https://github.com/Orizon-cmd/Orizon-OS"
#define UPDATE_CHANNEL "main"
#define UPDATE_RAW_HOST "raw.githubusercontent.com"
#define UPDATE_RAW_PREFIX "/Orizon-cmd/Orizon-OS/main/"
#define UPDATE_MANIFEST_REMOTE "updates/x86_64/manifest.txt"
#define UPDATE_MANIFEST_SIG_REMOTE "updates/x86_64/manifest.sig"
#define UPDATE_MANIFEST_SIGNING_KEY_ID "orizon-update-root-2026-05"
#define UPDATE_PACKAGE_SOURCE "https://github.com/Orizon-cmd/Orizon-Packages"
#define UPDATE_CHUNK_BYTES 65536U
#define UPDATE_RANGE_RETRIES 5U
#define UPDATE_METADATA_PAIR_RETRIES 3U
#define UPDATE_MANIFEST_MAX 4096U
#define UPDATE_MANIFEST_SIG_MAX 1024U
#define UPDATE_MANIFEST_SIG_BYTES 256U
#define UPDATE_BOOT_GUARD_ATTEMPTS 2U
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
  char channel[32];
  char source[96];
  char kernel_path[160];
  char kernel_sha256[SHA256_HEX_SIZE];
  size_t kernel_size;
  char efi_path[160];
  char efi_sha256[SHA256_HEX_SIZE];
  size_t efi_size;
  char limine_path[160];
  char limine_sha256[SHA256_HEX_SIZE];
  size_t limine_size;
  char package_source[96];
  char package_commit[64];
  char package_index_path[180];
  char package_index_sha256[SHA256_HEX_SIZE];
  size_t package_index_size;
} update_manifest_t;

typedef struct {
  char key_id[64];
  char manifest_sha256[SHA256_HEX_SIZE];
  uint8_t signature[UPDATE_MANIFEST_SIG_BYTES];
} update_manifest_signature_t;

static const char *update_status_text = "update: not run";
static char update_manifest_text[UPDATE_MANIFEST_MAX];
static char update_manifest_sig_text[UPDATE_MANIFEST_SIG_MAX];
static char update_package_index_text[UPDATE_PACKAGE_INDEX_MAX];
static char update_installed_db_text[UPDATE_INSTALLED_DB_MAX];
static uint8_t update_package_blob[UPDATE_PACKAGE_MAX] __attribute__((aligned(4096)));
static uint8_t update_kernel[UPDATE_KERNEL_MAX] __attribute__((aligned(4096)));
static uint8_t update_efi[UPDATE_EFI_MAX] __attribute__((aligned(4096)));
static char update_limine_conf[UPDATE_CONF_MAX] __attribute__((aligned(4096)));
static char update_limine_guard_conf[UPDATE_CONF_MAX] __attribute__((aligned(4096)));
static char update_limine_fallback_conf[UPDATE_CONF_MAX] __attribute__((aligned(4096)));
static uint8_t update_chunk[UPDATE_CHUNK_BYTES] __attribute__((aligned(4096)));
static orizon_update_progress_fn update_progress_fn = NULL;
static void *update_progress_ctx = NULL;

static const uint8_t update_manifest_root_n[UPDATE_MANIFEST_SIG_BYTES] = {
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

static const char rollback_limine_entry[] =
    "\n"
    "/Orizon OS Rollback\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/KROLLBK.ELF\n"
    "    cmdline: orizon.safe=1 rollback\n"
    "    resolution: 1024x768x32\n"
    "    module_path: boot():/EFI/BOOT/BOOTX64.ROL\n"
    "    module_cmdline: orizon-bootx64 rollback\n";

static const char rollback_restore_limine_conf[] =
    "# Limine Configuration File\n"
    "# Orizon OS x86_64 rollback restore\n"
    "\n"
    "timeout: 5\n"
    "interface_resolution: 1024x768\n"
    "interface_branding: Orizon OS\n"
    "default_entry: 1\n"
    "\n"
    "/Orizon OS\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.safe=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Minimal display debug\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.minimal=1 orizon.notimer=1 orizon.nohw=1 orizon.noinput=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Lenovo hardware probe\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.safe=1 orizon.i2chid=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Native display\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.safe=1 orizon.native=1\n";

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

static int update_read_blob(const char *path, void *buf, size_t cap,
                            size_t *out_len) {
  file_t *f;
  size_t used = 0;
  ssize_t n = 0;

  if (!path || !buf || cap == 0) {
    return -1;
  }
  f = vfs_open(path, O_RDONLY);
  if (!f) {
    return -1;
  }
  while (used < cap && (n = vfs_read(f, (uint8_t *)buf + used, cap - used)) > 0) {
    used += (size_t)n;
  }
  vfs_close(f);
  if (n < 0) {
    return -1;
  }
  if (out_len) {
    *out_len = used;
  }
  return 0;
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

static uint64_t update_elapsed_ms(uint64_t start_ticks) {
  uint64_t hz = timer_hz();
  if (hz == 0) {
    return 0;
  }
  return ((timer_ticks() - start_ticks) * 1000ULL) / hz;
}

static void append_timing(char *report, size_t report_size, const char *label,
                          uint64_t start_ticks) {
  char line[160];
  snprintf(line, sizeof(line), "Time: %s %lu ms", label,
           (unsigned long)update_elapsed_ms(start_ticks));
  append_report(report, report_size, line);
  update_append_log(line);
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

static int sha256_text_valid(const char *text);
static int parse_size_value(const char *text, size_t *out);
static int manifest_copy_value(const char *manifest, const char *key, char *out,
                               size_t out_size);

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

static unsigned limine_entry_index_named(const char *conf, const char *name) {
  const char *p = conf;
  unsigned index = 0;

  if (!conf || !name) {
    return 0;
  }
  while (*p) {
    const char *line = p;
    size_t len = 0;
    while (p[len] && p[len] != '\n' && p[len] != '\r') {
      len++;
    }
    if (len > 1 && line[0] == '/') {
      index++;
      if (strlen(name) == len - 1 && strncmp(line + 1, name, len - 1) == 0) {
        return index;
      }
    }
    p += len;
    while (*p == '\n' || *p == '\r') {
      p++;
    }
  }
  return 0;
}

static int limine_write_default_entry(const char *src, char *dst, size_t cap,
                                      unsigned default_entry) {
  const char *p = src;
  size_t out = 0;
  char line[48];
  int wrote_default = 0;

  if (!src || !dst || cap == 0 || default_entry == 0) {
    return -1;
  }

  snprintf(line, sizeof(line), "default_entry: %lu\n",
           (unsigned long)default_entry);
  if (strlen(line) + 1 >= cap) {
    return -1;
  }
  memcpy(dst, line, strlen(line));
  out = strlen(line);
  wrote_default = 1;

  while (*p) {
    const char *line_start = p;
    size_t len = 0;
    while (p[len] && p[len] != '\n' && p[len] != '\r') {
      len++;
    }
    if (!(len >= 14 && strncmp(line_start, "default_entry:", 14) == 0)) {
      if (out + len + 2 >= cap) {
        return -1;
      }
      memcpy(dst + out, line_start, len);
      out += len;
      dst[out++] = '\n';
    }
    p += len;
    while (*p == '\n' || *p == '\r') {
      p++;
    }
  }
  if (!wrote_default || out >= cap) {
    return -1;
  }
  dst[out] = '\0';
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

static void update_boot_guard_write(const char *text) {
  update_write_blob(UPDATE_BOOT_GUARD_PATH, text, strlen(text));
  update_write_blob(UPDATE_BOOT_GUARD_STATUS_PATH, text, strlen(text));
}

static unsigned update_boot_guard_attempts_left(const char *guard) {
  char value[24];
  size_t attempts = UPDATE_BOOT_GUARD_ATTEMPTS;

  if (guard && manifest_copy_value(guard, "attempts-left", value,
                                   sizeof(value)) == 0 &&
      parse_size_value(value, &attempts) == 0) {
    if (attempts > UPDATE_BOOT_GUARD_ATTEMPTS) {
      attempts = UPDATE_BOOT_GUARD_ATTEMPTS;
    }
    return (unsigned)attempts;
  }
  return UPDATE_BOOT_GUARD_ATTEMPTS;
}

static int update_boot_guard_install_limine_config(const char *path,
                                                   const char *label) {
  size_t conf_len = 0;
  static char install_report[1024];
  char line[192];

  if (!path || !label ||
      update_read_blob(path, update_limine_guard_conf,
                       sizeof(update_limine_guard_conf) - 1,
                       &conf_len) != 0 ||
      conf_len == 0 || conf_len >= sizeof(update_limine_guard_conf)) {
    snprintf(line, sizeof(line),
             "boot-guard: %s Limine config unavailable", label);
    update_append_log(line);
    return -1;
  }

  update_limine_guard_conf[conf_len] = '\0';
  install_report[0] = '\0';
  if (orizon_install_update_limine_config(update_limine_guard_conf, conf_len,
                                          install_report,
                                          sizeof(install_report)) != 0) {
    snprintf(line, sizeof(line),
             "boot-guard: %s Limine config install failed", label);
    update_append_log(line);
    return -1;
  }
  snprintf(line, sizeof(line), "boot-guard: %s Limine config installed",
           label);
  update_append_log(line);
  return 0;
}

static void update_boot_guard_arm(const update_manifest_t *manifest,
                                  const char *rollback_hash) {
  char guard[900];

  if (!manifest || !rollback_hash || !sha256_text_valid(rollback_hash)) {
    return;
  }
  snprintf(guard, sizeof(guard),
           "boot-guard-version 1\n"
           "state pending\n"
           "source %s\n"
           "channel %s\n"
           "updated-version %s\n"
           "updated-commit %s\n"
           "expected-kernel-sha256 %s\n"
           "rollback-kernel-sha256 %s\n"
           "attempts-left %lu\n"
           "action boot-count-shell-validation\n",
           UPDATE_SOURCE, UPDATE_CHANNEL, manifest->version, manifest->commit,
           manifest->kernel_sha256, rollback_hash,
           (unsigned long)UPDATE_BOOT_GUARD_ATTEMPTS);
  update_boot_guard_write(guard);
  update_write_line(UPDATE_ROLLBACK_STATE_PATH,
                    "rollback available: update pending boot validation");
  update_append_log("boot-guard: armed pending updated boot validation with boot-count");
}

static void update_boot_guard_mark_current(const update_manifest_t *manifest) {
  char guard[512];

  if (!manifest) {
    return;
  }
  snprintf(guard, sizeof(guard),
           "boot-guard-version 1\n"
           "state current\n"
           "source %s\n"
           "channel %s\n"
           "updated-version %s\n"
           "updated-commit %s\n"
           "expected-kernel-sha256 %s\n"
           "action none\n",
           UPDATE_SOURCE, UPDATE_CHANNEL, manifest->version, manifest->commit,
           manifest->kernel_sha256);
  update_boot_guard_write(guard);
}

static void boot_guard_append(char *out, size_t out_size, const char *line) {
  size_t used;
  if (!out || out_size == 0 || !line) {
    return;
  }
  used = strlen(out);
  if (used + 1 >= out_size) {
    return;
  }
  snprintf(out + used, out_size - used, "%s\n", line);
}

static void update_boot_guard_write_state(const char *state,
                                          const char *detail,
                                          const char *current_hash,
                                          const char *expected_hash,
                                          const char *rollback_hash) {
  char guard[900];

  snprintf(guard, sizeof(guard),
           "boot-guard-version 1\n"
           "state %s\n"
           "detail %s\n"
           "current-kernel-sha256 %s\n"
           "expected-kernel-sha256 %s\n"
           "rollback-kernel-sha256 %s\n",
           state ? state : "unknown", detail ? detail : "none",
           current_hash ? current_hash : "unknown",
           expected_hash ? expected_hash : "unknown",
           rollback_hash ? rollback_hash : "unknown");
  update_boot_guard_write(guard);
  update_append_log(detail ? detail : "boot-guard: state updated");
}

static void update_boot_guard_write_testing(unsigned attempts_left,
                                            const char *detail,
                                            const char *current_hash,
                                            const char *expected_hash,
                                            const char *rollback_hash,
                                            int fallback_armed) {
  char guard[1000];

  snprintf(guard, sizeof(guard),
           "boot-guard-version 1\n"
           "state testing\n"
           "detail %s\n"
           "current-kernel-sha256 %s\n"
           "expected-kernel-sha256 %s\n"
           "rollback-kernel-sha256 %s\n"
           "attempts-left %lu\n"
           "fallback-armed %s\n"
           "action validate-at-shell-ready\n",
           detail ? detail : "boot-guard: updated kernel entered Orizon",
           current_hash ? current_hash : "unknown",
           expected_hash ? expected_hash : "unknown",
           rollback_hash ? rollback_hash : "unknown",
           (unsigned long)attempts_left,
           fallback_armed ? "yes" : "no");
  update_boot_guard_write(guard);
  update_append_log(detail ? detail : "boot-guard: updated kernel testing");
}

void orizon_update_boot_guard_check(void) {
  char guard[1024];
  char state[32];
  char expected_hash[SHA256_HEX_SIZE];
  char rollback_hash[SHA256_HEX_SIZE];
  char current_hash[SHA256_HEX_SIZE];
  unsigned attempts_left = UPDATE_BOOT_GUARD_ATTEMPTS;
  int fallback_armed = 0;
  static char rollback_report[4096];

  if (!update_installed_marker_present() ||
      update_read_file(UPDATE_BOOT_GUARD_PATH, guard, sizeof(guard), NULL) < 0 ||
      manifest_copy_value(guard, "state", state, sizeof(state)) < 0 ||
      (strcmp(state, "pending") != 0 && strcmp(state, "testing") != 0)) {
    return;
  }

  if (manifest_copy_value(guard, "expected-kernel-sha256", expected_hash,
                          sizeof(expected_hash)) < 0 ||
      manifest_copy_value(guard, "rollback-kernel-sha256", rollback_hash,
                          sizeof(rollback_hash)) < 0 ||
      !sha256_text_valid(expected_hash) ||
      !sha256_text_valid(rollback_hash)) {
    update_boot_guard_write_state(
        "blocked", "boot-guard: pending metadata is invalid", "unknown",
        "unknown", "unknown");
    vfs_persist_save();
    return;
  }

  if (!boot_payloads_ready()) {
    update_boot_guard_write_state(
        "pending", "boot-guard: boot payload capture unavailable", "unknown",
        expected_hash, rollback_hash);
    vfs_persist_save();
    return;
  }

  sha256_buffer_hex(boot_kernel_image(), boot_kernel_image_size(),
                    current_hash);

  if (boot_cmdline_has("rollback")) {
    update_boot_guard_write_state(
        "rollback-booted",
        "boot-guard: rollback entry booted; restoring main boot slot",
        current_hash, expected_hash, rollback_hash);
    rollback_report[0] = '\0';
    if (orizon_update_rollback(rollback_report, sizeof(rollback_report)) == 0) {
      update_boot_guard_write_state(
          "auto-restored",
          "boot-guard: rollback payload restored as main boot slot",
          current_hash, expected_hash, rollback_hash);
    } else {
      update_boot_guard_write_state(
          "restore-failed",
          "boot-guard: rollback booted but automatic restore failed",
          current_hash, expected_hash, rollback_hash);
    }
    vfs_persist_save();
    return;
  }

  if (hex_equal(current_hash, expected_hash)) {
    attempts_left = update_boot_guard_attempts_left(guard);
    if (attempts_left == 0) {
      fallback_armed =
          (update_boot_guard_install_limine_config(UPDATE_LIMINE_FALLBACK_PATH,
                                                   "fallback") == 0);
      update_boot_guard_write_testing(
          0,
          fallback_armed
              ? "boot-guard: validation attempts exhausted; rollback fallback remains armed until shell confirms"
              : "boot-guard: validation attempts exhausted; rollback fallback arm failed",
          current_hash, expected_hash, rollback_hash, fallback_armed);
      update_write_line(
          UPDATE_ROLLBACK_STATE_PATH,
          fallback_armed
              ? "rollback armed: update validation attempts exhausted; shell must restore normal boot"
              : "rollback available: update validation attempts exhausted; fallback config could not be armed");
      vfs_persist_save();
      return;
    }
    if (attempts_left > 0) {
      attempts_left--;
    }
    fallback_armed =
        (update_boot_guard_install_limine_config(UPDATE_LIMINE_FALLBACK_PATH,
                                                 "fallback") == 0);
    update_boot_guard_write_testing(
        attempts_left,
        fallback_armed
            ? "boot-guard: updated kernel entered Orizon; rollback fallback armed until shell is ready"
            : "boot-guard: updated kernel entered Orizon; rollback fallback arm failed",
        current_hash, expected_hash, rollback_hash, fallback_armed);
    update_write_line(
        UPDATE_ROLLBACK_STATE_PATH,
        fallback_armed
            ? "rollback armed: updated boot testing; fallback default will run if shell is not reached"
            : "rollback available: updated boot testing; fallback config could not be armed");
    vfs_persist_save();
    return;
  }

  if (hex_equal(current_hash, rollback_hash)) {
    update_boot_guard_write_state(
        "rollback-running",
        "boot-guard: rollback kernel is running; run rollback to restore main",
        current_hash, expected_hash, rollback_hash);
    vfs_persist_save();
    return;
  }

  update_boot_guard_write_state(
      "unexpected-payload",
      "boot-guard: booted kernel does not match update or rollback hash",
      current_hash, expected_hash, rollback_hash);
  vfs_persist_save();
}

void orizon_update_boot_guard_shell_ready(void) {
  char guard[1024];
  char state[32];
  char expected_hash[SHA256_HEX_SIZE];
  char rollback_hash[SHA256_HEX_SIZE];
  char current_hash[SHA256_HEX_SIZE];

  if (!update_installed_marker_present() ||
      update_read_file(UPDATE_BOOT_GUARD_PATH, guard, sizeof(guard), NULL) < 0 ||
      manifest_copy_value(guard, "state", state, sizeof(state)) < 0 ||
      (strcmp(state, "pending") != 0 && strcmp(state, "testing") != 0)) {
    return;
  }

  if (manifest_copy_value(guard, "expected-kernel-sha256", expected_hash,
                          sizeof(expected_hash)) < 0 ||
      manifest_copy_value(guard, "rollback-kernel-sha256", rollback_hash,
                          sizeof(rollback_hash)) < 0 ||
      !sha256_text_valid(expected_hash) ||
      !sha256_text_valid(rollback_hash) || !boot_payloads_ready()) {
    return;
  }

  sha256_buffer_hex(boot_kernel_image(), boot_kernel_image_size(),
                    current_hash);
  if (!hex_equal(current_hash, expected_hash)) {
    return;
  }

  if (update_boot_guard_install_limine_config(UPDATE_LIMINE_NORMAL_PATH,
                                              "normal") != 0) {
    update_boot_guard_write_state(
        "restore-normal-failed",
        "boot-guard: updated kernel reached shell but normal boot restore failed",
        current_hash, expected_hash, rollback_hash);
    update_write_line(
        UPDATE_ROLLBACK_STATE_PATH,
        "rollback default remains armed: updated shell reached but normal boot restore failed");
    vfs_persist_save();
    return;
  }

  update_boot_guard_write_state(
      "validated",
      "boot-guard: updated kernel reached shell; normal boot restored",
      current_hash, expected_hash, rollback_hash);
  update_write_line(
      UPDATE_ROLLBACK_STATE_PATH,
      "rollback available: updated boot validated; run rollback if needed");
  vfs_persist_save();
}

void orizon_update_boot_guard_status(char *out, size_t out_size) {
  char buf[1024];
  char current_hash[SHA256_HEX_SIZE];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  boot_guard_append(out, out_size, "Orizon boot guard");
  if (update_read_file(UPDATE_BOOT_GUARD_PATH, buf, sizeof(buf), NULL) == 0) {
    boot_guard_append(out, out_size, buf);
  } else {
    boot_guard_append(out, out_size, "state none");
  }
  if (boot_payloads_ready()) {
    sha256_buffer_hex(boot_kernel_image(), boot_kernel_image_size(),
                      current_hash);
    snprintf(buf, sizeof(buf), "running-kernel-sha256 %s", current_hash);
    boot_guard_append(out, out_size, buf);
  } else {
    boot_guard_append(out, out_size, "running-kernel-sha256 unavailable");
  }
  snprintf(buf, sizeof(buf), "firmware %s", boot_firmware_type_name());
  boot_guard_append(out, out_size, buf);
  if (boot_efi_system_table()) {
    snprintf(buf, sizeof(buf), "efi-system-table 0x%016lx",
             (unsigned long)(uintptr_t)boot_efi_system_table());
    boot_guard_append(out, out_size, buf);
  } else {
    boot_guard_append(out, out_size, "efi-system-table unavailable");
  }
  boot_guard_append(
      out, out_size,
      "nvram-bootnext prepared=no reason=efi-runtime-writer-not-implemented");
  if (update_read_file(UPDATE_ROLLBACK_STATE_PATH, buf, sizeof(buf), NULL) ==
      0) {
    boot_guard_append(out, out_size, "rollback-state:");
    boot_guard_append(out, out_size, buf);
  }
}

int orizon_update_boot_guard_confirm(char *report, size_t report_size) {
  char current_hash[SHA256_HEX_SIZE];

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  if (!update_installed_marker_present()) {
    append_report(report, report_size,
                  "bootguard: unavailable in live boot. Install Orizon OS first.");
    return -1;
  }
  if (!boot_payloads_ready()) {
    append_report(report, report_size,
                  "bootguard: boot payload capture unavailable");
    return -2;
  }
  sha256_buffer_hex(boot_kernel_image(), boot_kernel_image_size(),
                    current_hash);
  if (update_boot_guard_install_limine_config(UPDATE_LIMINE_NORMAL_PATH,
                                              "normal") != 0) {
    append_report(report, report_size,
                  "bootguard: current boot confirmed, but normal boot config restore failed");
  }
  update_boot_guard_write_state(
      "validated-manual",
      "boot-guard: current boot manually confirmed by operator",
      current_hash, current_hash, "unchanged");
  update_write_line(
      UPDATE_ROLLBACK_STATE_PATH,
      "rollback available: current boot manually confirmed; run rollback if needed");
  append_report(report, report_size,
                "bootguard: current boot marked valid");
  append_report(report, report_size, current_hash);
  vfs_persist_save();
  return 0;
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

static int hex_nibble(char c) {
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

static int hex_decode_exact(const char *text, uint8_t *out, size_t out_len) {
  if (!text || !out || strlen(text) != out_len * 2U) {
    return -1;
  }
  for (size_t i = 0; i < out_len; i++) {
    int hi = hex_nibble(text[i * 2U]);
    int lo = hex_nibble(text[i * 2U + 1U]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

static int parse_manifest_signature(const char *text,
                                    update_manifest_signature_t *sig) {
  char signature_hex[UPDATE_MANIFEST_SIG_BYTES * 2U + 1U];

  if (!text || !sig || !strstr(text, "signature-version 1") ||
      !strstr(text, "algorithm rsa-pkcs1-sha256")) {
    return -1;
  }
  memset(sig, 0, sizeof(*sig));
  if (manifest_copy_value(text, "key-id", sig->key_id,
                          sizeof(sig->key_id)) < 0 ||
      manifest_copy_value(text, "manifest-sha256", sig->manifest_sha256,
                          sizeof(sig->manifest_sha256)) < 0 ||
      manifest_copy_value(text, "signature", signature_hex,
                          sizeof(signature_hex)) < 0) {
    return -1;
  }
  if (strcmp(sig->key_id, UPDATE_MANIFEST_SIGNING_KEY_ID) != 0 ||
      !sha256_text_valid(sig->manifest_sha256) ||
      hex_decode_exact(signature_hex, sig->signature,
                       sizeof(sig->signature)) != 0) {
    return -1;
  }
  return 0;
}

static int verify_update_manifest_signature(const char *manifest,
                                            size_t manifest_len,
                                            const char *sig_text,
                                            const char *manifest_hash,
                                            char *report,
                                            size_t report_size) {
  update_manifest_signature_t sig;
  uint8_t digest[SHA256_DIGEST_SIZE];
  char line[192];

  if (!manifest || !sig_text || !manifest_hash ||
      parse_manifest_signature(sig_text, &sig) != 0) {
    append_report(report, report_size,
                  "update: invalid manifest signature metadata");
    return -1;
  }
  if (!hex_equal(sig.manifest_sha256, manifest_hash)) {
    append_report(report, report_size,
                  "update: manifest signature hash mismatch");
    return -1;
  }
  sha256_buffer(manifest, manifest_len, digest);
  if (rsa_pkcs1v15_sha256_verify(sig.signature, sizeof(sig.signature), digest,
                                 update_manifest_root_n,
                                 sizeof(update_manifest_root_n)) != 0) {
    append_report(report, report_size,
                  "update: manifest signature verification failed");
    return -1;
  }
  snprintf(line, sizeof(line), "Manifest signature verified key-id=%s",
           sig.key_id);
  append_report(report, report_size, line);
  update_append_log(line);
  return 0;
}

static int update_manifest_signature_matches_hash(const char *sig_text,
                                                  const char *manifest_hash) {
  update_manifest_signature_t sig;

  if (!sig_text || !manifest_hash ||
      parse_manifest_signature(sig_text, &sig) != 0) {
    return -1;
  }
  return hex_equal(sig.manifest_sha256, manifest_hash) ? 1 : 0;
}

static int update_token_safe(const char *text) {
  int seen = 0;
  if (!text) {
    return 0;
  }
  while (*text) {
    char c = *text++;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == ':') {
      seen = 1;
      continue;
    }
    return 0;
  }
  return seen;
}

static int update_path_safe(const char *path, const char *prefix,
                            const char *suffix) {
  size_t suffix_len;
  size_t path_len;

  if (!path || !prefix || !suffix || path[0] == '/' ||
      strncmp(path, prefix, strlen(prefix)) != 0) {
    return 0;
  }
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (path_len <= suffix_len ||
      strcmp(path + path_len - suffix_len, suffix) != 0) {
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
      manifest_copy_value(text, "channel", manifest->channel,
                          sizeof(manifest->channel)) < 0 ||
      manifest_copy_value(text, "source", manifest->source,
                          sizeof(manifest->source)) < 0 ||
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
      manifest_size_value(text, "limine-size", &manifest->limine_size) < 0 ||
      manifest_copy_value(text, "package-source", manifest->package_source,
                          sizeof(manifest->package_source)) < 0 ||
      manifest_copy_value(text, "package-commit", manifest->package_commit,
                          sizeof(manifest->package_commit)) < 0 ||
      manifest_copy_value(text, "package-index-path",
                          manifest->package_index_path,
                          sizeof(manifest->package_index_path)) < 0 ||
      manifest_size_value(text, "package-index-size",
                          &manifest->package_index_size) < 0 ||
      manifest_copy_value(text, "package-index-sha256",
                          manifest->package_index_sha256,
                          sizeof(manifest->package_index_sha256)) < 0) {
    return -1;
  }
  if (manifest->kernel_size == 0 || manifest->kernel_size > UPDATE_KERNEL_MAX ||
      manifest->efi_size == 0 || manifest->efi_size > UPDATE_EFI_MAX ||
      manifest->limine_size == 0 || manifest->limine_size >= UPDATE_CONF_MAX ||
      manifest->package_index_size == 0 ||
      manifest->package_index_size >= UPDATE_PACKAGE_INDEX_MAX) {
    return -1;
  }
  if (!update_token_safe(manifest->version) ||
      !update_token_safe(manifest->commit) ||
      strcmp(manifest->channel, UPDATE_CHANNEL) != 0 ||
      !(strcmp(manifest->source, UPDATE_SOURCE) == 0 ||
        strcmp(manifest->source, UPDATE_SOURCE ".git") == 0) ||
      !(strcmp(manifest->package_source, UPDATE_PACKAGE_SOURCE) == 0 ||
        strcmp(manifest->package_source, UPDATE_PACKAGE_SOURCE ".git") == 0) ||
      !update_token_safe(manifest->package_commit) ||
      !update_path_safe(manifest->kernel_path, "updates/x86_64/", ".elf") ||
      !update_path_safe(manifest->efi_path, "updates/x86_64/", ".EFI") ||
      !update_path_safe(manifest->limine_path, "updates/x86_64/", ".conf") ||
      !update_path_safe(manifest->package_index_path, "packages/x86_64/",
                        ".txt") ||
      !sha256_text_valid(manifest->kernel_sha256) ||
      !sha256_text_valid(manifest->efi_sha256) ||
      !sha256_text_valid(manifest->limine_sha256) ||
      !sha256_text_valid(manifest->package_index_sha256)) {
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

static int download_text_with_retries(const char *label, const char *prefix,
                                      const char *relative, void *out,
                                      size_t out_cap, size_t *out_len,
                                      char *diag, size_t diag_cap) {
  char line[224];
  size_t got = 0;
  int rc = -1;

  if (!label || !relative || !out || out_cap == 0 || !out_len) {
    return -1;
  }
  for (unsigned attempt = 1; attempt <= UPDATE_RANGE_RETRIES; attempt++) {
    got = 0;
    if (diag && diag_cap > 0) {
      diag[0] = '\0';
    }
    rc = download_range_prefixed(prefix, relative, 0, (uint64_t)(out_cap - 1),
                                 out, out_cap, &got, diag, diag_cap);
    if (rc == 0 && got > 0 && got < out_cap) {
      *out_len = got;
      return 0;
    }
    snprintf(line, sizeof(line),
             "update: retry %lu/%lu for %s manifest/index rc=%d got=%lu %s",
             (unsigned long)attempt, (unsigned long)UPDATE_RANGE_RETRIES,
             label, rc, (unsigned long)got,
             (diag && diag[0]) ? diag : "");
    update_append_log(line);
  }
  *out_len = got;
  return rc ? rc : -1;
}

static int download_verified_blob(const char *label, const char *prefix,
                                  const char *relative, size_t expected_size,
                                  const char *expected_hash, void *dst,
                                  size_t dst_cap, const char *cache_path,
                                  char *report, size_t report_size) {
  size_t done = 0;
  char line[224];
  char diag[192];
  char actual_hash[SHA256_HEX_SIZE];
  unsigned next_percent = 0;
  uint64_t started_ticks = timer_ticks();
  size_t cached = 0;

  if (!relative || !expected_hash || !dst || expected_size == 0 ||
      expected_size > dst_cap) {
    append_report(report, report_size, "update: invalid manifest artifact");
    return -1;
  }

  if (cache_path && update_read_blob(cache_path, dst, dst_cap, &cached) == 0 &&
      cached > 0) {
    if (cached == expected_size) {
      sha256_buffer_hex(dst, expected_size, actual_hash);
      if (hex_equal(actual_hash, expected_hash)) {
        snprintf(line, sizeof(line), "%s reused cached sha256 %s", label,
                 actual_hash);
        append_report(report, report_size, line);
        update_append_log(line);
        return 0;
      }
      update_append_log("update: cached artifact hash mismatch, refetching");
      update_write_blob(cache_path, "", 0);
      cached = 0;
    } else if (cached < expected_size) {
      done = cached;
      snprintf(line, sizeof(line), "Resume: %s from %lu/%lu bytes", label,
               (unsigned long)done, (unsigned long)expected_size);
      append_report(report, report_size, line);
      update_append_log(line);
      goto ranged_chunks;
    } else {
      update_write_blob(cache_path, "", 0);
      cached = 0;
    }
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

ranged_chunks:
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
    if (cache_path) {
      update_write_blob(cache_path, dst, done);
    }

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
    if (cache_path) {
      update_write_blob(cache_path, "", 0);
    }
    return -3;
  }

  snprintf(line, sizeof(line), "%s verified sha256 %s", label, actual_hash);
  append_report(report, report_size, line);
  update_append_log(line);
  if (cache_path) {
    update_write_blob(cache_path, dst, expected_size);
  }
  append_timing(report, report_size, label, started_ticks);
  return 0;
}

static int download_artifact(const char *label, const char *relative,
                             size_t expected_size, const char *expected_hash,
                             void *dst, size_t dst_cap, char *report,
                             size_t report_size) {
  const char *cache_path = NULL;
  if (strcmp(label, "kernel.elf") == 0) {
    cache_path = UPDATE_KERNEL_CACHE_PATH;
  } else if (strcmp(label, "BOOTX64.EFI") == 0) {
    cache_path = UPDATE_EFI_CACHE_PATH;
  } else if (strcmp(label, "limine.conf") == 0) {
    cache_path = UPDATE_LIMINE_CACHE_PATH;
  }
  return download_verified_blob(label, UPDATE_RAW_PREFIX, relative,
                                expected_size, expected_hash, dst, dst_cap,
                                cache_path, report, report_size);
}

static int build_package_raw_prefix(const update_manifest_t *manifest, char *out,
                                    size_t out_size) {
  if (!manifest || !out || out_size == 0 ||
      !update_token_safe(manifest->package_commit)) {
    return -1;
  }
  return snprintf(out, out_size, "/Orizon-cmd/Orizon-Packages/%s/",
                  manifest->package_commit) < (int)out_size
             ? 0
             : -1;
}

static int update_install_remote_packages(const update_manifest_t *manifest,
                                          char *report, size_t report_size) {
  update_package_index_t index;
  char line[256];
  char package_prefix[160];
  char pkg_report[2048];
  size_t installed_len = 0;
  size_t installed_count = 0;
  size_t skipped_count = 0;

  if (!manifest || build_package_raw_prefix(manifest, package_prefix,
                                            sizeof(package_prefix)) != 0) {
    update_set_state("update: blocked - invalid package source");
    append_report(report, report_size, "update: invalid package source");
    return -1;
  }

  update_set_state("update: downloading package index");
  append_report(report, report_size,
                "[6/8] Checking Orizon package repository");
  append_report(report, report_size, "Package source: " UPDATE_PACKAGE_SOURCE);
  snprintf(line, sizeof(line), "Package commit: %s", manifest->package_commit);
  append_report(report, report_size, line);
  if (download_verified_blob(
          "package-index", package_prefix, manifest->package_index_path,
          manifest->package_index_size, manifest->package_index_sha256,
          update_package_index_text, sizeof(update_package_index_text) - 1,
          UPDATE_PACKAGE_INDEX_PATH, report, report_size) != 0) {
    update_set_state("update: blocked - package index download failed");
    append_report(report, report_size, "update: package index download failed");
    return -1;
  }
  update_package_index_text[manifest->package_index_size] = '\0';
  snprintf(line, sizeof(line), "Get: package index [%lu bytes]",
           (unsigned long)manifest->package_index_size);
  append_report(report, report_size, line);

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
    if (download_verified_blob(entry->name, package_prefix, entry->path,
                               entry->size, entry->sha256,
                               update_package_blob, sizeof(update_package_blob),
                               NULL, report, report_size) < 0) {
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
  size_t manifest_sig_len = 0;
  int metadata_verified = 0;
  char update_text[512];
  char rollback_text[512];
  uint64_t total_started_ticks = timer_ticks();

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
  for (unsigned metadata_attempt = 1;
       metadata_attempt <= UPDATE_METADATA_PAIR_RETRIES; metadata_attempt++) {
    manifest_len = 0;
    manifest_sig_len = 0;
    update_manifest_text[0] = '\0';
    update_manifest_sig_text[0] = '\0';
    net_line[0] = '\0';
    if (download_text_with_retries("github-manifest", UPDATE_RAW_PREFIX,
                                   UPDATE_MANIFEST_REMOTE,
                                   update_manifest_text,
                                   sizeof(update_manifest_text) - 1,
                                   &manifest_len, net_line,
                                   sizeof(net_line)) != 0 ||
        manifest_len == 0) {
      if (metadata_attempt < UPDATE_METADATA_PAIR_RETRIES) {
        snprintf(line, sizeof(line),
                 "[4/8] GitHub manifest download retry %lu/%lu",
                 (unsigned long)(metadata_attempt + 1),
                 (unsigned long)UPDATE_METADATA_PAIR_RETRIES);
        append_report(report, report_size, line);
        update_append_log(line);
        update_set_state("update: retrying github manifest");
        continue;
      }
      update_set_state("update: blocked - manifest download failed");
      append_report(report, report_size,
                    "[4/8] GitHub manifest download failed");
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
    if (download_text_with_retries("github-manifest-signature",
                                   UPDATE_RAW_PREFIX,
                                   UPDATE_MANIFEST_SIG_REMOTE,
                                   update_manifest_sig_text,
                                   sizeof(update_manifest_sig_text) - 1,
                                   &manifest_sig_len, net_line,
                                   sizeof(net_line)) != 0 ||
        manifest_sig_len == 0) {
      if (metadata_attempt < UPDATE_METADATA_PAIR_RETRIES) {
        snprintf(line, sizeof(line),
                 "[4/8] GitHub manifest signature retry %lu/%lu",
                 (unsigned long)(metadata_attempt + 1),
                 (unsigned long)UPDATE_METADATA_PAIR_RETRIES);
        append_report(report, report_size, line);
        update_append_log(line);
        update_set_state("update: retrying github manifest signature");
        continue;
      }
      update_set_state("update: blocked - manifest signature download failed");
      append_report(report, report_size,
                    "[4/8] GitHub manifest signature download failed");
      if (net_line[0]) {
        append_report(report, report_size, net_line);
      }
      vfs_persist_save();
      sched_set_process_state("update-manager", SCHED_SLEEPING);
      sched_enter_process("gui-shell");
      return -4;
    }
    update_manifest_sig_text[manifest_sig_len] = '\0';
    if (update_manifest_signature_matches_hash(update_manifest_sig_text,
                                               manifest_hash) == 0 &&
        metadata_attempt < UPDATE_METADATA_PAIR_RETRIES) {
      snprintf(line, sizeof(line),
               "[4/8] Manifest/signature pair mismatch, retry %lu/%lu",
               (unsigned long)(metadata_attempt + 1),
               (unsigned long)UPDATE_METADATA_PAIR_RETRIES);
      append_report(report, report_size, line);
      update_append_log(line);
      update_set_state("update: retrying manifest signature pair");
      continue;
    }
    if (verify_update_manifest_signature(update_manifest_text, manifest_len,
                                         update_manifest_sig_text,
                                         manifest_hash, report,
                                         report_size) == 0) {
      metadata_verified = 1;
      break;
    }
    if (metadata_attempt < UPDATE_METADATA_PAIR_RETRIES) {
      snprintf(line, sizeof(line),
               "[4/8] Manifest signature invalid, retry %lu/%lu",
               (unsigned long)(metadata_attempt + 1),
               (unsigned long)UPDATE_METADATA_PAIR_RETRIES);
      append_report(report, report_size, line);
      update_append_log(line);
      update_set_state("update: retrying manifest signature verification");
      continue;
    }
    update_set_state("update: blocked - manifest signature invalid");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -4;
  }
  if (!metadata_verified) {
    update_set_state("update: blocked - manifest signature invalid");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -4;
  }
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
  update_write_blob(UPDATE_MANIFEST_SIG_PATH, update_manifest_sig_text,
                    manifest_sig_len);
  update_write_blob(SYSTEM_MANIFEST_SIG_PATH, update_manifest_sig_text,
                    manifest_sig_len);
  snprintf(line, sizeof(line), "[4/8] Manifest %s commit %s",
           manifest.version, manifest.commit);
  append_report(report, report_size, line);
  update_append_log(line);

  if (hex_equal(rollback_hash, manifest.kernel_sha256) &&
      hex_equal(current_efi_hash, manifest.efi_sha256)) {
    update_set_state("update: boot payload already current");
    append_report(report, report_size,
                  "[5/8] Boot payload already current, skipping ESP rewrite");
    if (update_install_remote_packages(&manifest, report, report_size) < 0) {
      vfs_persist_save();
      sched_set_process_state("update-manager", SCHED_SLEEPING);
      sched_enter_process("gui-shell");
      return -6;
    }
    snprintf(update_text, sizeof(update_text),
             "Orizon OS already current\nsource=%s\nchannel=%s\nversion=%s\n"
             "commit=%s\nkernel-sha256=%s\nefi-sha256=%s\nelapsed-ms=%lu\n",
             UPDATE_SOURCE, UPDATE_CHANNEL, manifest.version, manifest.commit,
             manifest.kernel_sha256, manifest.efi_sha256,
             (unsigned long)update_elapsed_ms(total_started_ticks));
    update_write_blob(UPDATE_LAST_SUCCESS_PATH, update_text,
                      strlen(update_text));
    update_boot_guard_mark_current(&manifest);
    update_set_state("update: complete");
    append_report(report, report_size,
                  "[7/8] Installed ESP already matches GitHub");
    append_report(report, report_size,
                  "[8/8] Update complete. No reboot required for boot payloads.");
    append_timing(report, report_size, "full-upgrade total",
                  total_started_ticks);
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
  {
    unsigned rollback_entry =
        limine_entry_index_named(update_limine_conf, "Orizon OS Rollback");
    size_t normal_len = 0;
    size_t fallback_len = 0;

    if (rollback_entry == 0 ||
        limine_write_default_entry(update_limine_conf, update_limine_guard_conf,
                                   sizeof(update_limine_guard_conf), 1) != 0 ||
        limine_write_default_entry(update_limine_conf,
                                   update_limine_fallback_conf,
                                   sizeof(update_limine_fallback_conf),
                                   rollback_entry) != 0) {
      update_set_state("update: blocked - boot guard config failed");
      append_report(report, report_size,
                    "update: cannot prepare boot-count rollback configs");
      vfs_persist_save();
      sched_set_process_state("update-manager", SCHED_SLEEPING);
      sched_enter_process("gui-shell");
      return -5;
    }
    normal_len = strlen(update_limine_guard_conf);
    fallback_len = strlen(update_limine_fallback_conf);
    memcpy(update_limine_conf, update_limine_guard_conf, normal_len + 1);
    update_write_blob(UPDATE_LIMINE_NORMAL_PATH, update_limine_guard_conf,
                      normal_len);
    update_write_blob(UPDATE_LIMINE_FALLBACK_PATH,
                      update_limine_fallback_conf, fallback_len);
  }

  if (update_install_remote_packages(&manifest, report, report_size) < 0) {
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -6;
  }

  snprintf(update_text, sizeof(update_text),
           "Orizon OS updated\nsource=%s\nchannel=%s\nversion=%s\ncommit=%s\n"
           "kernel-sha256=%s\nrollback-kernel-sha256=%s\nelapsed-ms=%lu\n",
           UPDATE_SOURCE, UPDATE_CHANNEL, manifest.version, manifest.commit,
           manifest.kernel_sha256, rollback_hash,
           (unsigned long)update_elapsed_ms(total_started_ticks));
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
  update_boot_guard_arm(&manifest, rollback_hash);
  update_write_line(UPDATE_ROLLBACK_STATE_PATH,
                    "rollback available: update pending boot validation; choose Orizon OS Rollback at boot or run rollback if needed");
  update_set_state("update: complete");
  append_report(report, report_size,
                "[8/8] Update complete. Reboot to start the refreshed system.");
  append_report(report, report_size,
                "Rollback ready: boot 'Orizon OS Rollback' or run rollback to restore it.");
  append_timing(report, report_size, "full-upgrade total",
                total_started_ticks);
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

static void update_status_append(char *out, size_t out_size, const char *line) {
  size_t used;

  if (!out || out_size == 0 || !line) {
    return;
  }
  used = strlen(out);
  if (used + 1 >= out_size) {
    return;
  }
  snprintf(out + used, out_size - used, "%s\n", line);
}

void orizon_update_format_status(char *out, size_t out_size) {
  char manifest[UPDATE_MANIFEST_MAX];
  char sig[UPDATE_MANIFEST_SIG_MAX];
  char line[256];
  char value[128];
  char manifest_hash[SHA256_HEX_SIZE];
  size_t manifest_len = 0;
  size_t sig_len = 0;
  int manifest_ok;
  int sig_ok;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  update_status_append(out, out_size, "update status:");
  snprintf(line, sizeof(line), "  state: %s", update_status_text);
  update_status_append(out, out_size, line);
  snprintf(line, sizeof(line), "  boot-mode: %s",
           update_installed_marker_present() ? "installed" : "live-iso");
  update_status_append(out, out_size, line);
  snprintf(line, sizeof(line), "  update-source: %s channel=%s",
           UPDATE_SOURCE, UPDATE_CHANNEL);
  update_status_append(out, out_size, line);

  manifest_ok =
      update_read_file(UPDATE_MANIFEST_PATH, manifest, sizeof(manifest),
                       &manifest_len) == 0 &&
      manifest_len > 0;
  sig_ok = update_read_file(UPDATE_MANIFEST_SIG_PATH, sig, sizeof(sig),
                            &sig_len) == 0 &&
           sig_len > 0;
  snprintf(line, sizeof(line), "  manifest: %s path=%s bytes=%lu",
           manifest_ok ? "present" : "missing", UPDATE_MANIFEST_PATH,
           (unsigned long)manifest_len);
  update_status_append(out, out_size, line);
  snprintf(line, sizeof(line), "  manifest.sig: %s path=%s bytes=%lu",
           sig_ok ? "present" : "missing", UPDATE_MANIFEST_SIG_PATH,
           (unsigned long)sig_len);
  update_status_append(out, out_size, line);

  if (manifest_ok) {
    sha256_buffer_hex(manifest, manifest_len, manifest_hash);
    snprintf(line, sizeof(line), "  manifest-sha256: %s", manifest_hash);
    update_status_append(out, out_size, line);
    if (manifest_copy_value(manifest, "version", value, sizeof(value)) == 0) {
      snprintf(line, sizeof(line), "  manifest-version: %s", value);
      update_status_append(out, out_size, line);
    }
    if (manifest_copy_value(manifest, "commit", value, sizeof(value)) == 0) {
      snprintf(line, sizeof(line), "  manifest-commit: %s", value);
      update_status_append(out, out_size, line);
    }
    if (manifest_copy_value(manifest, "kernel-sha256", value,
                            sizeof(value)) == 0) {
      snprintf(line, sizeof(line), "  kernel-sha256: %s", value);
      update_status_append(out, out_size, line);
    }
    if (manifest_copy_value(manifest, "iso-sha256", value, sizeof(value)) ==
        0) {
      snprintf(line, sizeof(line), "  iso-sha256: %s", value);
      update_status_append(out, out_size, line);
    }
  }

  if (sig_ok) {
    char sig_hash[SHA256_HEX_SIZE];
    if (manifest_copy_value(sig, "key-id", value, sizeof(value)) == 0) {
      snprintf(line, sizeof(line), "  signature-key-id: %s", value);
      update_status_append(out, out_size, line);
    }
    if (manifest_copy_value(sig, "manifest-sha256", sig_hash,
                            sizeof(sig_hash)) == 0) {
      snprintf(line, sizeof(line), "  signature-manifest-match: %s",
               manifest_ok && hex_equal(sig_hash, manifest_hash) ? "yes"
                                                                 : "no");
      update_status_append(out, out_size, line);
    }
  }

  update_status_append(
      out, out_size,
      "  tls-root-trust: embedded root trust used by HTTPS/TLS probe");
  snprintf(line, sizeof(line), "  retry-https: retries=%lu range-cache=yes",
           (unsigned long)UPDATE_RANGE_RETRIES);
  update_status_append(out, out_size, line);
  snprintf(line, sizeof(line),
           "  resume-cache: kernel=%s efi=%s limine=%s",
           vfs_exists(UPDATE_KERNEL_CACHE_PATH) ? "present" : "empty",
           vfs_exists(UPDATE_EFI_CACHE_PATH) ? "present" : "empty",
           vfs_exists(UPDATE_LIMINE_CACHE_PATH) ? "present" : "empty");
  update_status_append(out, out_size, line);
  snprintf(line, sizeof(line), "  bootguard-pending: %s",
           vfs_exists(UPDATE_BOOT_GUARD_PATH) ? "yes" : "no");
  update_status_append(out, out_size, line);
  snprintf(line, sizeof(line), "  rollback-ready: %s",
           vfs_exists(UPDATE_ROLLBACK_INFO_PATH) ? "yes" : "no");
  update_status_append(out, out_size, line);
  update_status_append(
      out, out_size,
      "  nvram-bootnext: prepared=no reason=efi-runtime-writer-not-implemented");
  update_status_append(
      out, out_size,
      update_installed_marker_present()
          ? "  action: update may run on installed Orizon"
          : "  action: live ISO can inspect status; install required before update");
}

const char *orizon_update_status(void) {
  return update_status_text;
}
