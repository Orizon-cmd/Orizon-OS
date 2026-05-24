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
#define ORIZON_SERVICE_STATE_PATH "/system/service-state"
#define ORIZON_SERVICE_LOG_PATH "/logs/service.log"
#define ORIZON_MOTD_PATH "/system/motd"
#define ORIZON_ISSUE_PATH "/system/issue"
#define ORIZON_FSTAB_PATH "/system/fstab"
#define ORIZON_RESCUE_CONF_PATH "/system/rescue.conf"
#define ORIZON_ADMIN_GUIDE_PATH "/system/admin-guide.txt"
#define ORIZON_HOME_PROFILE_PATH "/home/orizon/.profile"
#define ORIZON_OS_RELEASE_PATH "/system/os-release"
#define ORIZON_MACHINE_ID_PATH "/system/machine-id"
#define ORIZON_ADMIN_NOTES_PATH "/system/admin-notes.txt"
#define ORIZON_SYSTEM_SNAPSHOT_PATH "/workspace/.orizon/system-snapshot.txt"
#define ORIZON_ADMIN_BACKUP_PATH "/workspace/.orizon/admin-backup.txt"

static const char *system_default_services =
    "# Orizon service policy v1\n"
    "# policy values are descriptive while the init layer is still small.\n"
    "persistence auto\n"
    "bootlog installed\n"
    "network manual\n"
    "ssh manual\n"
    "package-db installed\n"
    "update-bootguard installed\n";

static const char *system_default_motd =
    "Welcome to Orizon OS.\n"
    "Start with: system status, system health, system firstboot, system services.\n"
    "Export evidence with: system snapshot, system backup, report save.\n"
    "Use rescue for the safe recovery checklist.\n";

static const char *system_default_fstab =
    "# Orizon mount map v1\n"
    "# This records intended persistent roots; it is not a POSIX mount table yet.\n"
    "rootfs / memory defaults 0 0\n"
    "persist /workspace persistent optional 0 0\n"
    "persist /home persistent optional 0 0\n"
    "persist /system persistent optional 0 0\n"
    "persist /packages persistent optional 0 0\n"
    "persist /logs persistent optional 0 0\n";

static const char *system_default_rescue_conf =
    "# Orizon rescue policy v1\n"
    "repair-defaults yes\n"
    "allow-disk-layout-writes no\n"
    "prefer-previous-snapshot yes\n"
    "export-report /workspace/.orizon/rescue-report.txt\n";

static const char *system_default_admin_guide =
    "Orizon admin quickstart\n"
    "1. system status\n"
    "2. system health\n"
    "3. system firstboot\n"
    "4. system services\n"
    "5. system logs\n"
    "6. system snapshot\n"
    "7. system backup\n"
    "8. system doctor\n"
    "9. rescue\n";

static const char *system_default_profile =
    "# Orizon shell profile\n"
    "# The console uses the built-in Orizon shell; this file records user defaults.\n"
    "HOME=/home/orizon\n"
    "WORKSPACE=/workspace\n";

static const char *system_default_os_release =
    "NAME=Orizon OS\n"
    "ID=orizon\n"
    "VERSION=core-x86_64\n"
    "VERSION_ID=core-x86_64\n"
    "VARIANT=Core Development Base\n"
    "VALIDATION=vm-zimaos\n";

static const char *system_default_admin_notes =
    "Orizon admin notes\n"
    "- system snapshot writes a shareable state report.\n"
    "- system backup exports non-secret system configuration.\n"
    "- system repair recreates missing defaults without changing disk layout.\n"
    "- Lenovo and real hardware validation must be stated separately.\n";

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

static void system_generate_machine_id(char *out, size_t out_size) {
  unsigned long a;
  unsigned long b;

  if (!out || out_size == 0) {
    return;
  }
  a = ((unsigned long)timer_ticks()) ^ 0x4f52495aUL;
  b = ((unsigned long)timer_uptime_seconds()) ^ ((unsigned long)timer_hz()) ^
      0x4f53564dUL;
  snprintf(out, out_size, "orizon-%08lx-%08lx\n", a, b);
}

static void system_append_file_preview(char *out, size_t out_size, size_t *used,
                                       const char *label, const char *path) {
  char buf[768];
  char line[160];
  int n;

  snprintf(line, sizeof(line), "%s: %s\n", label, path);
  system_append(out, out_size, used, line);
  n = system_read_text_file(path, buf, sizeof(buf));
  if (n <= 0) {
    system_append(out, out_size, used, "  (not available)\n");
    return;
  }
  system_append(out, out_size, used, buf);
  if (buf[strlen(buf) - 1] != '\n') {
    system_append(out, out_size, used, "\n");
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
  char machine_id[80];
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
  if (system_write_default_file(ORIZON_OS_RELEASE_PATH,
                                system_default_os_release, created) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_MACHINE_ID_PATH)) {
    system_generate_machine_id(machine_id, sizeof(machine_id));
    if (created) {
      (*created)++;
    }
    if (system_write_text_file(ORIZON_MACHINE_ID_PATH, machine_id) < 0) {
      rc = -1;
    }
  }
  if (system_write_default_file(
          "/system/data-layout",
          "version 1\nroots /system /home /packages /logs /workspace\n",
          created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_FSTAB_PATH, system_default_fstab,
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_MOTD_PATH, system_default_motd,
                                created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_ISSUE_PATH,
                                "Orizon OS Core Development Base\n",
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
  if (system_write_default_file(ORIZON_SERVICE_STATE_PATH,
                                "service-state: not-recorded\n", created) <
      0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_RESCUE_CONF_PATH,
                                system_default_rescue_conf, created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_ADMIN_GUIDE_PATH,
                                system_default_admin_guide, created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_ADMIN_NOTES_PATH,
                                system_default_admin_notes, created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_BOOT_STATE_PATH,
                                "boot-state: not-recorded\n", created) < 0) {
    rc = -1;
  }
  if (system_write_default_file(ORIZON_HOME_PROFILE_PATH,
                                system_default_profile, created) < 0) {
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

static void system_format_service_state_text(char *out, size_t out_size,
                                             const char *source) {
  char host[80];
  int installed;

  if (!out || out_size == 0) {
    return;
  }
  orizon_system_hostname(host, sizeof(host));
  installed = orizon_system_is_installed();
  snprintf(out, out_size,
           "service-state: recorded\n"
           "source: %s\n"
           "mode: %s\n"
           "hostname: %s\n"
           "uptime-seconds: %lu\n"
           "services:\n"
           "  persistence policy=auto state=%s\n"
           "  bootlog policy=installed state=%s\n"
           "  network policy=manual state=configured-on-demand\n"
           "  ssh policy=manual state=configured-on-demand\n"
           "  package-db policy=installed state=%s\n"
           "  update-bootguard policy=installed state=%s\n"
           "  firstboot policy=installed state=%s\n",
           source && source[0] ? source : "system-init",
           installed ? "installed" : "live", host,
           (unsigned long)timer_uptime_seconds(),
           vfs_persist_available() ? "active" : "memory-only",
           installed ? (klog_boot_persisted() ? "saved" : "pending")
                     : "live-skip",
           vfs_exists("/system/installed") ? "present" : "live-or-pending",
           installed ? "available" : "installed-only",
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
  snprintf(line, sizeof(line), "service-state: %s %s\n",
           ORIZON_SERVICE_STATE_PATH,
           system_path_ok(ORIZON_SERVICE_STATE_PATH) ? "present" : "missing");
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
  snprintf(line, sizeof(line), "  package-db policy=installed state=%s\n",
           vfs_exists("/system/installed") ? "present" : "live-or-pending");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "  update-bootguard policy=installed state=%s\n",
           installed ? "available" : "installed-only");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "  firstboot policy=installed state=%s\n",
           installed ? (vfs_exists(ORIZON_FIRSTBOOT_DONE_PATH) ? "done"
                                                               : "pending")
                     : "not-installed");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "logs: init=%s service=%s boot=%s\n",
           system_ok_missing(ORIZON_INIT_LOG_PATH),
           system_ok_missing(ORIZON_SERVICE_LOG_PATH),
           system_ok_missing(KLOG_BOOT_PATH));
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "admin:\n");
  system_append(out, out_size, &used,
                "  system health    show concise PASS/WARN installed-state summary\n");
  system_append(out, out_size, &used,
                "  system snapshot  write /workspace/.orizon/system-snapshot.txt\n");
  system_append(out, out_size, &used,
                "  system backup    export non-secret config to admin-backup.txt\n");
  system_append(out, out_size, &used,
                "  system init      run idempotent boot tasks and write init log\n");
  system_append(out, out_size, &used,
                "  system doctor    audit installed/live state without writes\n");
  system_append(out, out_size, &used,
                "  system repair    recreate missing defaults and persist state\n");
  system_append(out, out_size, &used,
                "  system logs      show boot-state, service-state and init logs\n");
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
  DOCTOR_CHECK("machine id", system_path_ok(ORIZON_MACHINE_ID_PATH));
  DOCTOR_CHECK("os-release", system_path_ok(ORIZON_OS_RELEASE_PATH));
  DOCTOR_CHECK("motd file", system_path_ok(ORIZON_MOTD_PATH));
  DOCTOR_CHECK("issue file", system_path_ok(ORIZON_ISSUE_PATH));
  DOCTOR_CHECK("fstab map", system_path_ok(ORIZON_FSTAB_PATH));
  DOCTOR_CHECK("network config", system_path_ok("/system/network.conf"));
  DOCTOR_CHECK("services config", system_path_ok(ORIZON_SERVICES_PATH));
  DOCTOR_CHECK("service state", system_path_ok(ORIZON_SERVICE_STATE_PATH));
  DOCTOR_CHECK("rescue config", system_path_ok(ORIZON_RESCUE_CONF_PATH));
  DOCTOR_CHECK("admin guide", system_path_ok(ORIZON_ADMIN_GUIDE_PATH));
  DOCTOR_CHECK("admin notes", system_path_ok(ORIZON_ADMIN_NOTES_PATH));
  DOCTOR_CHECK("home profile", system_path_ok(ORIZON_HOME_PROFILE_PATH));
  DOCTOR_CHECK("boot state", system_path_ok(ORIZON_BOOT_STATE_PATH));
  DOCTOR_CHECK("init log", system_path_ok(ORIZON_INIT_LOG_PATH));
  DOCTOR_CHECK("service log", system_path_ok(ORIZON_SERVICE_LOG_PATH));
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
  char service_state[1024];
  char services[1536];
  char line[256];
  size_t used = 0;
  int created = 0;
  int defaults_rc;
  int boot_rc;
  int service_state_rc;
  int initlog_rc;
  int servicelog_rc;
  int bootlog_rc;
  int save_rc;
  int installed;

  defaults_rc = system_ensure_defaults(&created);
  installed = orizon_system_is_installed();
  system_format_boot_state_text(boot_state, sizeof(boot_state), "system-init");
  system_format_service_state_text(service_state, sizeof(service_state),
                                   "system-init");
  boot_rc = system_write_text_file(ORIZON_BOOT_STATE_PATH, boot_state);
  service_state_rc =
      system_write_text_file(ORIZON_SERVICE_STATE_PATH, service_state);
  orizon_system_format_services(services, sizeof(services));
  initlog_rc = system_write_text_file(ORIZON_INIT_LOG_PATH, boot_state);
  if (initlog_rc == 0) {
    file_t *f = vfs_open(ORIZON_INIT_LOG_PATH, O_WRONLY | O_APPEND);
    if (f) {
      vfs_write(f, "\n", 1);
      vfs_write(f, service_state, strlen(service_state));
      vfs_write(f, "\n", 1);
      vfs_write(f, services, strlen(services));
      vfs_close(f);
    }
  }
  servicelog_rc = system_write_text_file(ORIZON_SERVICE_LOG_PATH, service_state);
  bootlog_rc = klog_persist_boot_if_installed();
  save_rc = vfs_persist_save();

  if (out && out_size) {
    out[0] = '\0';
    snprintf(line, sizeof(line),
             "system init: %s\nmode=%s created-defaults=%d\n",
             defaults_rc == 0 && boot_rc == 0 && service_state_rc == 0 &&
                     initlog_rc == 0 && servicelog_rc == 0
                 ? "PASS"
                 : "WARN",
             installed ? "installed" : "live", created);
    system_append(out, out_size, &used, line);
    snprintf(line, sizeof(line),
             "boot-state=%s service-state=%s init-log=%s service-log=%s "
             "boot-log=%s persistence-save=%s\n",
             boot_rc == 0 ? ORIZON_BOOT_STATE_PATH : "failed",
             service_state_rc == 0 ? ORIZON_SERVICE_STATE_PATH : "failed",
             initlog_rc == 0 ? ORIZON_INIT_LOG_PATH : "failed",
             servicelog_rc == 0 ? ORIZON_SERVICE_LOG_PATH : "failed",
             bootlog_rc == 0 ? KLOG_BOOT_PATH
                             : (installed ? "pending-or-unavailable"
                                          : "skipped-live"),
             save_rc == 0 ? "ok" : "unavailable");
    system_append(out, out_size, &used, line);
    system_append(out, out_size, &used, "\n");
    system_append(out, out_size, &used, services);
  }
  return defaults_rc == 0 && boot_rc == 0 && service_state_rc == 0 &&
                 initlog_rc == 0 && servicelog_rc == 0
             ? 0
             : -EIO;
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
           "  hostname=%s machine-id=%s os-release=%s motd=%s fstab=%s\n",
           system_path_ok(ORIZON_HOSTNAME_PATH) ? "ok" : "missing",
           system_path_ok(ORIZON_MACHINE_ID_PATH) ? "ok" : "missing",
           system_path_ok(ORIZON_OS_RELEASE_PATH) ? "ok" : "missing",
           system_path_ok(ORIZON_MOTD_PATH) ? "ok" : "missing",
           system_path_ok(ORIZON_FSTAB_PATH) ? "ok" : "missing");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "  network=%s services=%s admin-notes=%s data-layout=%s "
           "install-marker=%s\n",
           system_path_ok("/system/network.conf") ? "ok" : "missing",
           system_path_ok(ORIZON_SERVICES_PATH) ? "ok" : "missing",
           system_path_ok(ORIZON_ADMIN_NOTES_PATH) ? "ok" : "missing",
           system_path_ok("/system/data-layout") ? "ok" : "missing",
           installed ? "present" : "absent");
  system_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "init: boot-state=%s service-state=%s init-log=%s service-log=%s "
           "boot-log=%s\n",
           system_path_ok(ORIZON_BOOT_STATE_PATH) ? "present" : "missing",
           system_path_ok(ORIZON_SERVICE_STATE_PATH) ? "present" : "missing",
           system_path_ok(ORIZON_INIT_LOG_PATH) ? "present" : "missing",
           system_path_ok(ORIZON_SERVICE_LOG_PATH) ? "present" : "missing",
           klog_boot_persisted() ? "saved" : (installed ? "pending" : "live-skip"));
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "safe commands:\n");
  if (installed) {
    system_append(out, out_size, &used,
                  "  system health, system snapshot, system backup, system init, system services, system logs, system firstboot, system doctor, update status, bootguard, rollback-status, rescue\n");
  } else {
    system_append(out, out_size, &used,
                  "  system health, system snapshot, system backup, system init, system services, system logs, system firstboot, system doctor, install-plan, report save, storage diag, rescue\n");
  }
  system_append(out, out_size, &used,
                "notes: system repair is non-destructive and only recreates missing defaults.\n");
}

void orizon_system_format_firstboot(char *out, size_t out_size) {
  char host[80];
  char line[192];
  size_t used = 0;
  int installed;
  int done;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_system_hostname(host, sizeof(host));
  installed = orizon_system_is_installed();
  done = vfs_exists(ORIZON_FIRSTBOOT_DONE_PATH);
  system_append(out, out_size, &used, "Orizon first boot\n");
  snprintf(line, sizeof(line), "mode: %s\nhostname: %s\nstate: %s\n",
           installed ? "installed" : "live", host,
           installed ? (done ? "done" : "pending") : "not-installed");
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "checklist:\n");
  system_append(out, out_size, &used,
                "  1. system status     # confirm live vs installed\n");
  system_append(out, out_size, &used,
                "  2. system health     # quick PASS/WARN state summary\n");
  system_append(out, out_size, &used,
                "  3. system services   # confirm boot service policy\n");
  system_append(out, out_size, &used,
                "  4. system logs       # inspect boot-state and init logs\n");
  system_append(out, out_size, &used,
                "  5. system snapshot   # write shareable admin state\n");
  system_append(out, out_size, &used,
                "  6. system backup     # export non-secret config\n");
  system_append(out, out_size, &used,
                "  7. system doctor     # audit required roots and config\n");
  system_append(out, out_size, &used,
                "  8. report save       # export hardware/network evidence\n");
  if (installed && !done) {
    system_append(out, out_size, &used,
                  "finish: run 'firstboot done' after reviewing the checklist.\n");
  } else if (installed) {
    system_append(out, out_size, &used,
                  "finish: first boot already confirmed; rerun system init after repairs.\n");
  } else {
    system_append(out, out_size, &used,
                  "note: firstboot can only be completed after installing to a VM disk.\n");
  }
}

void orizon_system_format_logs(char *out, size_t out_size) {
  char line[256];
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  system_append(out, out_size, &used, "Orizon system logs\n");
  snprintf(line, sizeof(line),
           "paths: boot-state=%s service-state=%s init-log=%s service-log=%s "
           "boot-log=%s\n",
           system_ok_missing(ORIZON_BOOT_STATE_PATH),
           system_ok_missing(ORIZON_SERVICE_STATE_PATH),
           system_ok_missing(ORIZON_INIT_LOG_PATH),
           system_ok_missing(ORIZON_SERVICE_LOG_PATH),
           system_ok_missing(KLOG_BOOT_PATH));
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used, "\n");
  system_append_file_preview(out, out_size, &used, "boot-state",
                             ORIZON_BOOT_STATE_PATH);
  system_append(out, out_size, &used, "\n");
  system_append_file_preview(out, out_size, &used, "service-state",
                             ORIZON_SERVICE_STATE_PATH);
  system_append(out, out_size, &used, "\n");
  system_append_file_preview(out, out_size, &used, "init-log",
                             ORIZON_INIT_LOG_PATH);
  system_append(out, out_size, &used, "\n");
  system_append_file_preview(out, out_size, &used, "service-log",
                             ORIZON_SERVICE_LOG_PATH);
}

void orizon_system_format_health(char *out, size_t out_size) {
  char host[80];
  char line[256];
  size_t used = 0;
  int installed;
  int warn = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_system_hostname(host, sizeof(host));
  installed = orizon_system_is_installed();
  system_append(out, out_size, &used, "Orizon system health\n");
  snprintf(line, sizeof(line), "mode: %s\nhostname: %s\n",
           installed ? "installed" : "live", host);
  system_append(out, out_size, &used, line);
#define HEALTH_CHECK(label, ok_expr)                                           \
  do {                                                                         \
    int _ok = (ok_expr);                                                       \
    snprintf(line, sizeof(line), "  %-24s %s\n", (label),                    \
             _ok ? "PASS" : "WARN");                                        \
    system_append(out, out_size, &used, line);                                 \
    if (!_ok) {                                                                \
      warn++;                                                                  \
    }                                                                          \
  } while (0)
  HEALTH_CHECK("workspace root", system_path_ok("/workspace"));
  HEALTH_CHECK("system root", system_path_ok("/system"));
  HEALTH_CHECK("home root", system_path_ok("/home"));
  HEALTH_CHECK("packages root", system_path_ok("/packages"));
  HEALTH_CHECK("logs root", system_path_ok("/logs"));
  HEALTH_CHECK("hostname", system_path_ok(ORIZON_HOSTNAME_PATH));
  HEALTH_CHECK("machine-id", system_path_ok(ORIZON_MACHINE_ID_PATH));
  HEALTH_CHECK("os-release", system_path_ok(ORIZON_OS_RELEASE_PATH));
  HEALTH_CHECK("service policy", system_path_ok(ORIZON_SERVICES_PATH));
  HEALTH_CHECK("boot state", system_path_ok(ORIZON_BOOT_STATE_PATH));
  HEALTH_CHECK("service state", system_path_ok(ORIZON_SERVICE_STATE_PATH));
  HEALTH_CHECK("init log", system_path_ok(ORIZON_INIT_LOG_PATH));
  HEALTH_CHECK("service log", system_path_ok(ORIZON_SERVICE_LOG_PATH));
  HEALTH_CHECK("admin notes", system_path_ok(ORIZON_ADMIN_NOTES_PATH));
  HEALTH_CHECK("persistence", vfs_persist_available());
  if (installed) {
    HEALTH_CHECK("install marker", system_path_ok(ORIZON_INSTALL_MARKER_PATH));
    HEALTH_CHECK("firstboot done", system_path_ok(ORIZON_FIRSTBOOT_DONE_PATH));
  } else {
    system_append(out, out_size, &used,
                  "  firstboot done           SKIP live ISO\n");
  }
#undef HEALTH_CHECK
  snprintf(line, sizeof(line), "summary: %s warnings=%d\n",
           warn == 0 ? "PASS" : "WARN", warn);
  system_append(out, out_size, &used, line);
  system_append(out, out_size, &used,
                "next: system snapshot | system backup | system doctor | system repair\n");
}

int orizon_system_write_snapshot(char *out, size_t out_size) {
  static char snapshot[8192];
  static char status[2048];
  static char health[2048];
  static char services[2048];
  static char doctor[2048];
  static char firstboot[1536];
  static char logs[2048];
  char line[256];
  size_t used = 0;
  int created = 0;
  int defaults_rc;
  int write_rc;
  int save_rc;

  defaults_rc = system_ensure_defaults(&created);
  orizon_system_format_status(status, sizeof(status));
  orizon_system_format_health(health, sizeof(health));
  orizon_system_format_services(services, sizeof(services));
  orizon_system_format_doctor(doctor, sizeof(doctor));
  orizon_system_format_firstboot(firstboot, sizeof(firstboot));
  orizon_system_format_logs(logs, sizeof(logs));

  snapshot[0] = '\0';
  system_append(snapshot, sizeof(snapshot), &used, "Orizon system snapshot\n");
  snprintf(line, sizeof(line),
           "scope: VM/ZimaOS-safe installed/live state, no hardware validation claim\n"
           "created-defaults: %d\n",
           created);
  system_append(snapshot, sizeof(snapshot), &used, line);
  system_append(snapshot, sizeof(snapshot), &used, "\n== status ==\n");
  system_append(snapshot, sizeof(snapshot), &used, status);
  system_append(snapshot, sizeof(snapshot), &used, "\n== health ==\n");
  system_append(snapshot, sizeof(snapshot), &used, health);
  system_append(snapshot, sizeof(snapshot), &used, "\n== services ==\n");
  system_append(snapshot, sizeof(snapshot), &used, services);
  system_append(snapshot, sizeof(snapshot), &used, "\n== doctor ==\n");
  system_append(snapshot, sizeof(snapshot), &used, doctor);
  system_append(snapshot, sizeof(snapshot), &used, "\n== firstboot ==\n");
  system_append(snapshot, sizeof(snapshot), &used, firstboot);
  system_append(snapshot, sizeof(snapshot), &used, "\n== logs ==\n");
  system_append(snapshot, sizeof(snapshot), &used, logs);

  write_rc = system_write_text_file(ORIZON_SYSTEM_SNAPSHOT_PATH, snapshot);
  save_rc = vfs_persist_save();
  if (out && out_size) {
    snprintf(out, out_size,
             "system snapshot: %s\npath: " ORIZON_SYSTEM_SNAPSHOT_PATH
             "\nbytes: %lu\ncreated-defaults: %d\npersistence-save: %s\n"
             "read: cat " ORIZON_SYSTEM_SNAPSHOT_PATH "\n",
             defaults_rc == 0 && write_rc == 0 ? "PASS" : "WARN",
             (unsigned long)strlen(snapshot), created,
             save_rc == 0 ? "ok" : "unavailable");
  }
  return defaults_rc == 0 && write_rc == 0 ? 0 : -EIO;
}

int orizon_system_write_admin_backup(char *out, size_t out_size) {
  static char backup[8192];
  char line[256];
  size_t used = 0;
  int created = 0;
  int defaults_rc;
  int write_rc;
  int save_rc;

  defaults_rc = system_ensure_defaults(&created);
  backup[0] = '\0';
  system_append(backup, sizeof(backup), &used, "Orizon admin backup\n");
  system_append(backup, sizeof(backup), &used,
                "scope: non-secret system configuration only\n");
  system_append(backup, sizeof(backup), &used,
                "excluded: SSH private keys, update private keys, package payload secrets, disk data\n");
  snprintf(line, sizeof(line), "created-defaults: %d\n\n", created);
  system_append(backup, sizeof(backup), &used, line);
  system_append_file_preview(backup, sizeof(backup), &used, "os-release",
                             ORIZON_OS_RELEASE_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "machine-id",
                             ORIZON_MACHINE_ID_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "hostname",
                             ORIZON_HOSTNAME_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "profile",
                             "/system/profile");
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "data-layout",
                             "/system/data-layout");
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "fstab",
                             ORIZON_FSTAB_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "network",
                             "/system/network.conf");
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "services",
                             ORIZON_SERVICES_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "service-state",
                             ORIZON_SERVICE_STATE_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "rescue",
                             ORIZON_RESCUE_CONF_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "admin-guide",
                             ORIZON_ADMIN_GUIDE_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "admin-notes",
                             ORIZON_ADMIN_NOTES_PATH);
  system_append(backup, sizeof(backup), &used, "\n");
  system_append_file_preview(backup, sizeof(backup), &used, "home-profile",
                             ORIZON_HOME_PROFILE_PATH);

  write_rc = system_write_text_file(ORIZON_ADMIN_BACKUP_PATH, backup);
  save_rc = vfs_persist_save();
  if (out && out_size) {
    snprintf(out, out_size,
             "system backup: %s\npath: " ORIZON_ADMIN_BACKUP_PATH
             "\nbytes: %lu\ncreated-defaults: %d\npersistence-save: %s\n"
             "read: cat " ORIZON_ADMIN_BACKUP_PATH "\n",
             defaults_rc == 0 && write_rc == 0 ? "PASS" : "WARN",
             (unsigned long)strlen(backup), created,
             save_rc == 0 ? "ok" : "unavailable");
  }
  return defaults_rc == 0 && write_rc == 0 ? 0 : -EIO;
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
           "  2. system health\n"
           "  3. system snapshot           # write shareable admin state\n"
           "  4. system backup             # export non-secret config\n"
           "  5. report save\n"
           "  6. system doctor             # audit roots/config/init without writes\n"
           "  7. system services           # inspect simple init/service policy\n"
           "  8. system logs               # inspect boot/service/init evidence\n"
           "  9. persist status && persist slots\n"
           " 10. persist restore previous  # only if the current state is broken\n"
           " 11. system repair             # recreate missing /system,/home,/logs defaults\n"
           "installed-only helpers:\n"
           "  boot-check, repair-boot, update status, bootguard, rollback-status\n"
           "logs:\n"
           "  /logs/init.log, /logs/service.log, /logs/boot.log, /workspace/.orizon/rescue-report.txt\n"
           "  /workspace/.orizon/system-snapshot.txt, /workspace/.orizon/admin-backup.txt\n"
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
  char service_state[1024];
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
  system_format_service_state_text(service_state, sizeof(service_state),
                                   "system-repair");
  system_write_text_file(ORIZON_SERVICE_STATE_PATH, service_state);
  system_write_text_file(ORIZON_SERVICE_LOG_PATH, service_state);
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
