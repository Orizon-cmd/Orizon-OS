/*
 * Orizon OS x86_64 - Installed/live system state helpers
 */

#include "../include/system_state.h"
#include "../include/bootinfo.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"

#define ORIZON_FIRSTBOOT_DONE_PATH "/workspace/.orizon/firstboot-done"
#define ORIZON_INSTALL_MARKER_PATH "/workspace/.orizon/installed"
#define ORIZON_INSTALL_STATE_PATH "/workspace/.orizon/install-state"

static void system_append(char *out, size_t out_size, size_t *used,
                          const char *text) {
  size_t len;

  if (!out || !used || !text || *used >= out_size) {
    return;
  }
  len = strlen(text);
  if (*used + len >= out_size) {
    len = out_size - *used - 1;
  }
  if (len > 0) {
    memcpy(out + *used, text, len);
    *used += len;
  }
  out[*used] = '\0';
}

static int system_read_text_file(const char *path, char *buf, size_t cap) {
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
  return (int)used;
}

static int system_write_text_file(const char *path, const char *text) {
  file_t *f;

  if (!path) {
    return -EINVAL;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -EIO;
  }
  if (text && vfs_write(f, text, strlen(text)) < 0) {
    vfs_close(f);
    return -EIO;
  }
  vfs_close(f);
  return 0;
}

static int system_write_default_file(const char *path, const char *text,
                                     int *created) {
  if (vfs_exists(path)) {
    return 0;
  }
  if (created) {
    (*created)++;
  }
  return system_write_text_file(path, text);
}

static void system_trim_first_line(char *buf) {
  if (!buf) {
    return;
  }
  for (size_t i = 0; buf[i]; i++) {
    if (buf[i] == '\n' || buf[i] == '\r') {
      buf[i] = '\0';
      break;
    }
  }
}

static int system_hostname_valid(const char *name) {
  size_t len;

  if (!name) {
    return 0;
  }
  len = strlen(name);
  if (len == 0 || len > 63 || name[0] == '-' || name[len - 1] == '-') {
    return 0;
  }
  for (size_t i = 0; i < len; i++) {
    char c = name[i];
    int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '-';
    if (!ok) {
      return 0;
    }
  }
  return 1;
}

static void system_ensure_dirs(int *created) {
  const char *dirs[] = {
      "/workspace", "/workspace/.orizon", "/home", "/home/orizon",
      "/system", "/system/share", "/system/firmware", "/packages",
      "/logs"};

  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
    if (!vfs_exists(dirs[i]) && created) {
      (*created)++;
    }
    vfs_mkdir(dirs[i]);
  }
}

static int system_ensure_defaults(int *created) {
  int rc = 0;

  system_ensure_dirs(created);
  if (system_write_default_file(ORIZON_HOSTNAME_PATH, "orizon-os\n",
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file("/system/version", "core-x86_64\n",
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file("/system/profile", "minimal-development\n",
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(
          "/system/data-layout",
          "version 1\nroots /system /home /packages /logs /workspace\n",
          created) < 0) {
    rc = -1;
  }
  if (system_write_default_file("/system/network.conf", "mode dhcp\n",
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(
          "/home/orizon/README.txt",
          "Home directory for Orizon OS user files.\n", created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(
          "/packages/README.txt",
          "Local package cache and installed package metadata.\n",
          created) < 0) {
    rc = -1;
  }
  if (system_write_default_file("/logs/README.txt",
                                "Persistent boot, install and update logs.\n",
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(
          "/workspace/.orizon/README.txt",
          "Orizon persistent state: install, update, packages and history.\n",
          created) < 0) {
    rc = -1;
  }
  return rc;
}

static int system_path_ok(const char *path) {
  return vfs_exists(path);
}

int orizon_system_is_installed(void) {
  char state[256];

  if (vfs_exists(ORIZON_INSTALL_MARKER_PATH) || vfs_exists("/system/installed")) {
    return 1;
  }
  if (system_read_text_file(ORIZON_INSTALL_STATE_PATH, state, sizeof(state)) >
          0 &&
      strstr(state, "install complete")) {
    return 1;
  }
  return 0;
}

void orizon_system_hostname(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  if (system_read_text_file(ORIZON_HOSTNAME_PATH, out, out_size) <= 0) {
    snprintf(out, out_size, "orizon-os");
    return;
  }
  system_trim_first_line(out);
  if (!system_hostname_valid(out)) {
    snprintf(out, out_size, "orizon-os");
  }
}

int orizon_system_set_hostname(const char *name, char *status,
                               size_t status_size) {
  char text[96];
  int rc;
  int save_rc = -1;

  if (!system_hostname_valid(name)) {
    if (status && status_size) {
      snprintf(status, status_size,
               "hostname: invalid name; use 1-63 letters, digits or '-'\n");
    }
    return -EINVAL;
  }
  system_ensure_dirs(NULL);
  snprintf(text, sizeof(text), "%s\n", name);
  rc = system_write_text_file(ORIZON_HOSTNAME_PATH, text);
  if (rc == 0) {
    system_write_text_file("/workspace/.orizon/hostname", text);
    save_rc = vfs_persist_save();
  }
  if (status && status_size) {
    snprintf(status, status_size, "hostname: %s\npersisted=%s\n", name,
             rc == 0 && save_rc == 0 ? "yes" : "memory-only");
  }
  return rc;
}

void orizon_system_format_status(char *out, size_t out_size) {
  char host[80];
  char line[256];
  size_t used = 0;
  int installed;
  const char *firstboot;
  const char *mode;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_system_hostname(host, sizeof(host));
  installed = orizon_system_is_installed();
  firstboot = installed ? (vfs_exists(ORIZON_FIRSTBOOT_DONE_PATH) ? "done"
                                                                  : "pending")
                        : "not-installed";
  mode = installed ? "installed" : "live";

  system_append(out, out_size, &used, "Orizon system status\n");
  snprintf(line, sizeof(line), "boot-mode: %s\n", mode);
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "hostname: %s\n", host);
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "first-boot: %s\n", firstboot);
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "cmdline: %s\n",
           boot_cmdline()[0] ? boot_cmdline() : "(none)");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "uptime: %lus ticks=%lu hz=%lu\n",
           (unsigned long)timer_uptime_seconds(),
           (unsigned long)timer_ticks(), (unsigned long)timer_hz());
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "storage: %s\n",
           storage_available() ? storage_status() : "unavailable");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "persistence: %s\n", vfs_persist_status());
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "roots:\n");
  snprintf(line, sizeof(line),
           "  /workspace=%s /home=%s /system=%s /packages=%s /logs=%s\n",
           system_path_ok("/workspace") ? "ok" : "missing",
           system_path_ok("/home") ? "ok" : "missing",
           system_path_ok("/system") ? "ok" : "missing",
           system_path_ok("/packages") ? "ok" : "missing",
           system_path_ok("/logs") ? "ok" : "missing");
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "files:\n");
  snprintf(line, sizeof(line),
           "  hostname=%s network=%s data-layout=%s install-marker=%s\n",
           system_path_ok(ORIZON_HOSTNAME_PATH) ? "ok" : "missing",
           system_path_ok("/system/network.conf") ? "ok" : "missing",
           system_path_ok("/system/data-layout") ? "ok" : "missing",
           installed ? "present" : "absent");
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "safe commands:\n");
  if (installed) {
    system_append(out, out_size, &used,
                  "  update status, bootguard, rollback-status, system repair, rescue\n");
  } else {
    system_append(out, out_size, &used,
                  "  install-plan, report save, storage diag, system repair, rescue\n");
  }
  system_append(out, out_size, &used,
                "notes: system repair is non-destructive and only recreates missing defaults.\n");
}

void orizon_system_format_rescue(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon rescue mode\n"
           "write-scope: non-destructive VFS defaults only unless another command says otherwise\n"
           "recommended order:\n"
           "  1. system status\n"
           "  2. report save\n"
           "  3. persist status && persist slots\n"
           "  4. persist restore previous  # only if the current state is broken\n"
           "  5. system repair             # recreate missing /system,/home,/logs defaults\n"
           "installed-only helpers:\n"
           "  boot-check, repair-boot, update status, bootguard, rollback-status\n"
           "report: " ORIZON_RESCUE_REPORT_PATH "\n");
}

int orizon_system_mark_firstboot_done(char *out, size_t out_size) {
  int rc;

  system_ensure_dirs(NULL);
  rc = system_write_text_file(ORIZON_FIRSTBOOT_DONE_PATH,
                              "first boot confirmed\n");
  if (rc == 0) {
    vfs_persist_save();
  }
  if (out && out_size) {
    snprintf(out, out_size, "firstboot: %s\npersisted=%s\n",
             rc == 0 ? "done" : "failed",
             rc == 0 && vfs_persist_available() ? "yes" : "memory-only");
  }
  return rc;
}

int orizon_system_repair(char *out, size_t out_size) {
  char status[2048];
  char rescue[1024];
  char line[192];
  size_t used = 0;
  int created = 0;
  int defaults_rc;
  int save_rc;

  defaults_rc = system_ensure_defaults(&created);
  orizon_system_format_status(status, sizeof(status));
  orizon_system_format_rescue(rescue, sizeof(rescue));
  system_write_text_file("/system/rescue-last", "system repair executed\n");
  system_write_text_file(ORIZON_RESCUE_REPORT_PATH, status);
  save_rc = vfs_persist_save();

  if (out && out_size) {
    out[0] = '\0';
    snprintf(line, sizeof(line),
             "system repair: %s\ncreated-defaults=%d persistence-save=%s\n",
             defaults_rc == 0 ? "PASS" : "WARN", created,
             save_rc == 0 ? "ok" : "unavailable");
    system_append(out, out_size, &used, line);
    system_append(out, out_size, &used,
                  "report: " ORIZON_RESCUE_REPORT_PATH "\n\n");
    system_append(out, out_size, &used, status);
    system_append(out, out_size, &used, "\n");
    system_append(out, out_size, &used, rescue);
  }
  return defaults_rc == 0 ? 0 : -EIO;
}
