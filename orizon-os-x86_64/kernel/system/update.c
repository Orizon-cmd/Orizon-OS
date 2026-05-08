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
#define UPDATE_CHUNK_BYTES 12288U
#define UPDATE_RANGE_RETRIES 5U
#define UPDATE_MANIFEST_MAX 4096U
#define UPDATE_KERNEL_MAX (4U * 1024U * 1024U)
#define UPDATE_EFI_MAX (512U * 1024U)
#define UPDATE_CONF_MAX 4096U

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
static uint8_t update_kernel[UPDATE_KERNEL_MAX] __attribute__((aligned(4096)));
static uint8_t update_efi[UPDATE_EFI_MAX] __attribute__((aligned(4096)));
static char update_limine_conf[UPDATE_CONF_MAX] __attribute__((aligned(4096)));
static uint8_t update_chunk[UPDATE_CHUNK_BYTES] __attribute__((aligned(4096)));

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
  update_write_line(UPDATE_STATE_PATH, state);
  update_write_line(SYSTEM_STATE_PATH, state);
  update_append_log(state);
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

static int build_raw_path(const char *relative, char *out, size_t out_size) {
  const char *rel = relative;
  if (!relative || !out || out_size == 0) {
    return -1;
  }
  while (*rel == '/') {
    rel++;
  }
  return snprintf(out, out_size, UPDATE_RAW_PREFIX "%s", rel) < (int)out_size
             ? 0
             : -1;
}

static int download_range_path(const char *relative, uint64_t start,
                               uint64_t end, void *out, size_t out_cap,
                               size_t *out_len, char *diag,
                               size_t diag_cap) {
  char path[240];
  if (build_raw_path(relative, path, sizeof(path)) < 0) {
    return -1;
  }
  return netstack_https_range_get(UPDATE_RAW_HOST, path, start, end, out,
                                  out_cap, out_len, diag, diag_cap);
}

static int download_artifact(const char *label, const char *relative,
                             size_t expected_size, const char *expected_hash,
                             void *dst, size_t dst_cap, char *report,
                             size_t report_size) {
  size_t done = 0;
  char line[224];
  char diag[192];
  char actual_hash[SHA256_HEX_SIZE];

  if (!relative || !expected_hash || !dst || expected_size == 0 ||
      expected_size > dst_cap) {
    append_report(report, report_size, "update: invalid manifest artifact");
    return -1;
  }

  snprintf(line, sizeof(line), "Downloading %s (%lu bytes)", label,
           (unsigned long)expected_size);
  append_report(report, report_size, line);
  update_append_log(line);

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
      rc = download_range_path(relative, (uint64_t)done,
                               (uint64_t)(done + wanted - 1), update_chunk,
                               sizeof(update_chunk), &got, diag,
                               sizeof(diag));
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
  }

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

int orizon_update_full_upgrade(char *report, size_t report_size) {
  update_manifest_t manifest;
  char net_line[256];
  char line[256];
  char manifest_hash[SHA256_HEX_SIZE];
  char rollback_hash[SHA256_HEX_SIZE];
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
  append_report(report, report_size, "[1/7] Installed package database ready");

  update_set_state("update: probing ethernet");
  net_init();
  net_format_status(net_line, sizeof(net_line));
  append_report(report, report_size, "[2/7] Ethernet probe");
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

  update_set_state("update: configuring ipv4 with dhcp");
  if (netstack_configure_ipv4() != 0) {
    netstack_format_status(net_line, sizeof(net_line));
    update_set_state("update: blocked - dhcp failed");
    append_report(report, report_size, "[3/7] DHCP/IPv4 failed");
    append_report(report, report_size, net_line);
    update_append_log(net_line);
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -2;
  }
  netstack_format_status(net_line, sizeof(net_line));
  append_report(report, report_size, "[3/7] DHCP/IPv4 ready");
  append_report(report, report_size, net_line);
  update_append_log(net_line);

  update_set_state("update: downloading github manifest");
  if (download_range_path(UPDATE_MANIFEST_REMOTE, 0, UPDATE_MANIFEST_MAX - 2,
                          update_manifest_text,
                          sizeof(update_manifest_text) - 1, &manifest_len,
                          net_line, sizeof(net_line)) != 0 ||
      manifest_len == 0) {
    update_set_state("update: blocked - manifest download failed");
    append_report(report, report_size, "[4/7] GitHub manifest download failed");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -3;
  }
  update_manifest_text[manifest_len] = '\0';
  sha256_buffer_hex(update_manifest_text, manifest_len, manifest_hash);
  update_write_blob(UPDATE_PROOF_PATH, update_manifest_text, manifest_len);
  update_write_line(UPDATE_PROOF_HASH_PATH, manifest_hash);
  if (parse_update_manifest(update_manifest_text, &manifest) < 0) {
    update_set_state("update: blocked - invalid manifest");
    append_report(report, report_size, "[4/7] Invalid GitHub update manifest");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -4;
  }
  update_write_blob(UPDATE_MANIFEST_PATH, update_manifest_text, manifest_len);
  update_write_blob(SYSTEM_MANIFEST_PATH, update_manifest_text, manifest_len);
  snprintf(line, sizeof(line), "[4/7] Manifest %s commit %s",
           manifest.version, manifest.commit);
  append_report(report, report_size, line);
  update_append_log(line);

  update_set_state("update: downloading boot payloads");
  append_report(report, report_size, "[5/7] Downloading verified artifacts");
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
  append_report(report, report_size, "[6/7] Rewriting installed boot partition");
  if (orizon_install_update_esp_with_rollback(
          update_kernel, manifest.kernel_size, update_efi, manifest.efi_size,
          boot_kernel_image(), boot_kernel_image_size(), boot_efi_image(),
          boot_efi_image_size(), update_limine_conf, strlen(update_limine_conf),
          update_text, strlen(update_text), report, report_size) != 0) {
    update_set_state("update: blocked - ESP write failed");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -6;
  }

  update_write_blob(UPDATE_LAST_SUCCESS_PATH, update_text, strlen(update_text));
  update_write_blob(UPDATE_ROLLBACK_INFO_PATH, rollback_text,
                    strlen(rollback_text));
  update_write_line(UPDATE_ROLLBACK_STATE_PATH,
                    "rollback available: choose Orizon OS Rollback at boot or run rollback");
  update_set_state("update: complete");
  append_report(report, report_size,
                "[7/7] Update complete. Reboot to start the refreshed system.");
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
