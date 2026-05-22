/*
 * Orizon OS x86_64 - Installed/live system state helpers
 */

#include "../include/system_state.h"
#include "../include/bootinfo.h"
#include "../include/klog.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"

#define ORIZON_FIRSTBOOT_DONE_PATH "/workspace/.orizon/firstboot-done"
#define ORIZON_INSTALL_MARKER_PATH "/workspace/.orizon/installed"
#define ORIZON_INSTALL_STATE_PATH "/workspace/.orizon/install-state"
#define ORIZON_BOOT_STATE_PATH "/system/boot-state"
#define ORIZON_INIT_LOG_PATH "/logs/init.log"
#define ORIZON_SERVICES_PATH "/system/services.conf"

static const char *system_default_services =
    "# Orizon service policy v1\n"
    "# policy values are descriptive while the init layer is still small.\n"
    "persistence auto\n"
    "bootlog installed\n"
    "network manual\n"
    "ssh manual\n"
    "update-bootguard installed\n";

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
  if (system_write_default_file(ORIZON_SERVICES_PATH, system_default_services,
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_BOOT_STATE_PATH,
                                "boot-state: not-recorded\n", created) < 0) {
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

static const char *system_ok_missing(const char *path) {
  return vfs_exists(path) ? "ok" : "missing";
}

static void system_format_boot_state_text(char *out, size_t out_size,
                                          const char *source) {
  char host[80];
  int installed;

  if (!out || out_size == 0) {
    return;
  }
  orizon_system_hostname(host, sizeof(host));
  installed = orizon_system_is_installed();
  snprintf(out, out_size,
           "boot-state: recorded\n"
           "source: %s\n"
           "mode: %s\n"
           "hostname: %s\n"
           "cmdline: %s\n"
           "uptime-seconds: %lu\n"
           "ticks: %lu\n"
           "persistence: %s\n"
           "firstboot: %s\n"
           "services-policy: " ORIZON_SERVICES_PATH "\n",
           source && source[0] ? source : "system-init",
           installed ? "installed" : "live", host,
           boot_cmdline()[0] ? boot_cmdline() : "(none)",
           (unsigned long)timer_uptime_seconds(),
           (unsigned long)timer_ticks(), vfs_persist_status(),
           installed ? (vfs_exists(ORIZON_FIRSTBOOT_DONE_PATH) ? "done"
                                                               : "pending")
                     : "not-installed");
}

static int system_path_ok(const char *path) {
  return vfs_exists(path);
}

int orizon_system_is_installed(void) {
  char state[256];

  if (vfs_exists(ORIZON_INSTALL_MARKER_PATH)) {
    return 1;
  }
  if (system_read_text_file(ORIZON_INSTALL_STATE_PATH, state, sizeof(state)) >
          0 &&
      strstr(state, "install complete")) {
    return 1;
  }
  return 0;
}

void orizon_system_format_services(char *out, size_t out_size) {
  char line[256];
  size_t used = 0;
  int installed;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  installed = orizon_system_is_installed();
  system_append(out, out_size, &used, "Orizon init/services\n");
  snprintf(line, sizeof(line), "boot-mode: %s\n",
           installed ? "installed" : "live");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "policy: %s %s\n", ORIZON_SERVICES_PATH,
           system_path_ok(ORIZON_SERVICES_PATH) ? "present" : "missing");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "boot-state: %s %s\n",
           ORIZON_BOOT_STATE_PATH,
           system_path_ok(ORIZON_BOOT_STATE_PATH) ? "present" : "missing");
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "services:\n");
  snprintf(line, sizeof(line),
           "  persistence policy=auto state=%s detail=\"%s\"\n",
           vfs_persist_available() ? "active" : "unavailable",
           vfs_persist_status());
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "  bootlog policy=installed state=%s path=" KLOG_BOOT_PATH "\n",
           installed ? (klog_boot_persisted() ? "saved" : "pending")
                     : "live-skip");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "  network policy=manual config=/system/network.conf:%s\n",
           system_ok_missing("/system/network.conf"));
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "  ssh policy=manual config=/system/ssh.conf:%s hostkey=/system/ssh_host_rsa.key:%s\n",
           system_ok_missing("/system/ssh.conf"),
           system_ok_missing("/system/ssh_host_rsa.key"));
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "  update-bootguard policy=installed state=%s\n",
           installed ? "available" : "installed-only");
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "admin:\n");
  system_append(out, out_size, &used,
                "  system init      run idempotent boot tasks and write init log\n");
  system_append(out, out_size, &used,
                "  system doctor    audit installed/live state without writes\n");
  system_append(out, out_size, &used,
                "  system repair    recreate missing defaults and persist state\n");
}

void orizon_system_format_doctor(char *out, size_t out_size) {
  char line[256];
  size_t used = 0;
  int installed;
  int warn = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  installed = orizon_system_is_installed();
  system_append(out, out_size, &used, "Orizon system doctor\n");
  snprintf(line, sizeof(line), "mode: %s\n",
           installed ? "installed" : "live");
  system_append(out, out_size, &used, line);
#define DOCTOR_CHECK(label, ok_expr)                                            \
  do {                                                                         \
    int _ok = (ok_expr);                                                       \
    snprintf(line, sizeof(line), "  %-24s %s\n", (label),                      \
             _ok ? "PASS" : "WARN");                                         \
    system_append(out, out_size, &used, line);                                 \
    if (!_ok) {                                                                \
      warn++;                                                                  \
    }                                                                          \
  } while (0)
  DOCTOR_CHECK("/workspace root", system_path_ok("/workspace"));
  DOCTOR_CHECK("/system root", system_path_ok("/system"));
  DOCTOR_CHECK("/home root", system_path_ok("/home"));
  DOCTOR_CHECK("/packages root", system_path_ok("/packages"));
  DOCTOR_CHECK("/logs root", system_path_ok("/logs"));
  DOCTOR_CHECK("hostname file", system_path_ok(ORIZON_HOSTNAME_PATH));
  DOCTOR_CHECK("network config", system_path_ok("/system/network.conf"));
  DOCTOR_CHECK("services config", system_path_ok(ORIZON_SERVICES_PATH));
  DOCTOR_CHECK("boot state", system_path_ok(ORIZON_BOOT_STATE_PATH));
  DOCTOR_CHECK("init log", system_path_ok(ORIZON_INIT_LOG_PATH));
  DOCTOR_CHECK("persistence", vfs_persist_available());
  if (installed) {
    DOCTOR_CHECK("install marker", system_path_ok(ORIZON_INSTALL_MARKER_PATH));
    DOCTOR_CHECK("firstboot reviewed",
                 system_path_ok(ORIZON_FIRSTBOOT_DONE_PATH));
  }
#undef DOCTOR_CHECK
  snprintf(line, sizeof(line), "summary: %s warnings=%d\n",
           warn == 0 ? "PASS" : "WARN", warn);
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used,
                "next: system repair is safe for missing defaults; use rescue for recovery order.\n");
}

int orizon_system_run_boot_tasks(char *out, size_t out_size) {
  char boot_state[1024];
  char services[1536];
  char line[256];
  size_t used = 0;
  int created = 0;
  int defaults_rc;
  int boot_rc;
  int initlog_rc;
  int bootlog_rc;
  int save_rc;
  int installed;

  defaults_rc = system_ensure_defaults(&created);
  installed = orizon_system_is_installed();
  system_format_boot_state_text(boot_state, sizeof(boot_state), "system-init");
  boot_rc = system_write_text_file(ORIZON_BOOT_STATE_PATH, boot_state);
  orizon_system_format_services(services, sizeof(services));
  initlog_rc = system_write_text_file(ORIZON_INIT_LOG_PATH, boot_state);
  if (initlog_rc == 0) {
    file_t *f = vfs_open(ORIZON_INIT_LOG_PATH, O_WRONLY | O_APPEND);
    if (f) {
      vfs_write(f, "\n", 1);
      vfs_write(f, services, strlen(services));
      vfs_close(f);
    }
  }
  bootlog_rc = klog_persist_boot_if_installed();
  save_rc = vfs_persist_save();

  if (out && out_size) {
    out[0] = '\0';
    snprintf(line, sizeof(line),
             "system init: %s\nmode=%s created-defaults=%d\n",
             defaults_rc == 0 && boot_rc == 0 && initlog_rc == 0 ? "PASS"
                                                                  : "WARN",
             installed ? "installed" : "live", created);
    system_append(out, out_size, &used, line);
    snprintf(line, sizeof(line),
             "boot-state=%s init-log=%s boot-log=%s persistence-save=%s\n",
             boot_rc == 0 ? ORIZON_BOOT_STATE_PATH : "failed",
             initlog_rc == 0 ? ORIZON_INIT_LOG_PATH : "failed",
             bootlog_rc == 0 ? KLOG_BOOT_PATH
                             : (installed ? "pending-or-unavailable"
                                          : "skipped-live"),
             save_rc == 0 ? "ok" : "unavailable");
    system_append(out, out_size, &used, line);
    system_append(out, out_size, &used, "\n");
    system_append(out, out_size, &used, services);
  }
  return defaults_rc == 0 && boot_rc == 0 && initlog_rc == 0 ? 0 : -EIO;
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
           "  hostname=%s network=%s services=%s data-layout=%s install-marker=%s\n",
           system_path_ok(ORIZON_HOSTNAME_PATH) ? "ok" : "missing",
           system_path_ok("/system/network.conf") ? "ok" : "missing",
           system_path_ok(ORIZON_SERVICES_PATH) ? "ok" : "missing",
           system_path_ok("/system/data-layout") ? "ok" : "missing",
           installed ? "present" : "absent");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "init: boot-state=%s init-log=%s boot-log=%s\n",
           system_path_ok(ORIZON_BOOT_STATE_PATH) ? "present" : "missing",
           system_path_ok(ORIZON_INIT_LOG_PATH) ? "present" : "missing",
           klog_boot_persisted() ? "saved" : (installed ? "pending" : "live-skip"));
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "safe commands:\n");
  if (installed) {
    system_append(out, out_size, &used,
                  "  system init, system services, system doctor, update status, bootguard, rollback-status, rescue\n");
  } else {
    system_append(out, out_size, &used,
                  "  system init, system services, system doctor, install-plan, report save, storage diag, rescue\n");
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
           "  3. system doctor             # audit roots/config/init without writes\n"
           "  4. system services           # inspect simple init/service policy\n"
           "  5. persist status && persist slots\n"
           "  6. persist restore previous  # only if the current state is broken\n"
           "  7. system repair             # recreate missing /system,/home,/logs defaults\n"
           "installed-only helpers:\n"
           "  boot-check, repair-boot, update status, bootguard, rollback-status\n"
           "logs:\n"
           "  /logs/init.log, /logs/boot.log, /workspace/.orizon/rescue-report.txt\n"
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
  char services[1536];
  char doctor[1536];
  char line[192];
  size_t used = 0;
  int created = 0;
  int defaults_rc;
  int save_rc;

  defaults_rc = system_ensure_defaults(&created);
  system_format_boot_state_text(status, sizeof(status), "system-repair");
  system_write_text_file(ORIZON_BOOT_STATE_PATH, status);
  orizon_system_format_status(status, sizeof(status));
  orizon_system_format_rescue(rescue, sizeof(rescue));
  orizon_system_format_services(services, sizeof(services));
  orizon_system_format_doctor(doctor, sizeof(doctor));
  system_write_text_file("/system/rescue-last",
                         "system repair executed\n"
                         "next: review system doctor, system services, persist slots\n");
  system_write_text_file(ORIZON_RESCUE_REPORT_PATH, status);
  {
    file_t *f = vfs_open(ORIZON_RESCUE_REPORT_PATH, O_WRONLY | O_APPEND);
    if (f) {
      vfs_write(f, "\n", 1);
      vfs_write(f, services, strlen(services));
      vfs_write(f, "\n", 1);
      vfs_write(f, doctor, strlen(doctor));
      vfs_close(f);
    }
  }
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
    system_append(out, out_size, &used, services);
    system_append(out, out_size, &used, "\n");
    system_append(out, out_size, &used, doctor);
    system_append(out, out_size, &used, "\n");
    system_append(out, out_size, &used, rescue);
  }
  return defaults_rc == 0 ? 0 : -EIO;
}
