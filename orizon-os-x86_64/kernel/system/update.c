/*
 * Orizon OS x86_64 - Update Manager
 *
 * `update` is the in-OS full-upgrade entrypoint. It owns the transaction and
 * records state locally; network protocols are added underneath it.
 */

#include "../include/update.h"
#include "../include/net.h"
#include "../include/string.h"
#include "../include/vfs.h"

#define UPDATE_STATE_PATH "/workspace/.orizon/update-state"
#define UPDATE_LOG_PATH "/workspace/.orizon/update.log"
#define SYSTEM_STATE_PATH "/system/update-state"
#define SYSTEM_PACKAGES_PATH "/system/packages"
#define SYSTEM_SOURCE_PATH "/system/update-source"
#define UPDATE_SOURCE "https://github.com/Orizon-cmd/Orizon-OS"

typedef struct {
  const char *name;
  const char *version;
} update_package_t;

static const update_package_t base_packages[] = {
    {"orizon-core", "core-x86_64"},
    {"orizon-console", "minimal-shell"},
    {"orizon-vfs", "workspace-persistence"},
    {"orizon-net", "ethernet-e1000"},
    {"orizon-updater", "github-bootstrap"},
};

static const char *update_status_text = "update: not run";

static void update_write_file(const char *path, const char *text, int append) {
  file_t *f = vfs_open(path, O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC));
  if (!f) {
    return;
  }
  vfs_write(f, text, strlen(text));
  vfs_close(f);
}

static void update_append_log(const char *line) {
  update_write_file(UPDATE_LOG_PATH, line, 1);
  update_write_file(UPDATE_LOG_PATH, "\n", 1);
}

static void update_set_state(const char *state) {
  update_status_text = state;
  update_write_file(UPDATE_STATE_PATH, state, 0);
  update_write_file(UPDATE_STATE_PATH, "\n", 1);
  update_write_file(SYSTEM_STATE_PATH, state, 0);
  update_write_file(SYSTEM_STATE_PATH, "\n", 1);
  update_append_log(state);
}

static void update_write_package_db(void) {
  char line[128];
  update_write_file("/workspace/.orizon/packages", "", 0);
  update_write_file(SYSTEM_PACKAGES_PATH, "", 0);
  for (size_t i = 0; i < sizeof(base_packages) / sizeof(base_packages[0]); i++) {
    snprintf(line, sizeof(line), "%s %s\n", base_packages[i].name,
             base_packages[i].version);
    update_write_file("/workspace/.orizon/packages", line, 1);
    update_write_file(SYSTEM_PACKAGES_PATH, line, 1);
  }
}

static void append_report(char *report, size_t report_size, const char *line) {
  size_t used;
  if (!report || report_size == 0) {
    return;
  }
  used = strlen(report);
  if (used + 1 >= report_size) {
    return;
  }
  snprintf(report + used, report_size - used, "%s\n", line);
}

int orizon_update_full_upgrade(char *report, size_t report_size) {
  char net_line[256];
  char pkg_line[128];

  if (report && report_size > 0) {
    report[0] = '\0';
  }

  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/system");
  update_write_file(UPDATE_LOG_PATH, "", 0);
  update_write_file(SYSTEM_SOURCE_PATH, UPDATE_SOURCE "\n", 0);

  append_report(report, report_size, "\033[1;36mOrizon full-upgrade\033[0m");
  append_report(report, report_size, "Source: " UPDATE_SOURCE);
  update_append_log("Orizon full-upgrade started");
  update_append_log("Source: " UPDATE_SOURCE);

  update_set_state("update: preparing package database");
  update_write_package_db();
  for (size_t i = 0; i < sizeof(base_packages) / sizeof(base_packages[0]); i++) {
    snprintf(pkg_line, sizeof(pkg_line), "Package target: %s %s",
             base_packages[i].name, base_packages[i].version);
    update_append_log(pkg_line);
  }
  append_report(report, report_size, "[1/4] Local package database ready");

  update_set_state("update: probing ethernet");
  net_init();
  net_format_status(net_line, sizeof(net_line));
  update_append_log(net_line);
  append_report(report, report_size, "[2/4] Ethernet probe");
  append_report(report, report_size, net_line);

  if (!net_link_up()) {
    update_set_state("update: blocked - ethernet link is down");
    append_report(report, report_size,
                  "[3/4] GitHub download skipped: ethernet link is down");
    append_report(report, report_size,
                  "Check VM/device network cable, bridge, or adapter model.");
    vfs_persist_save();
    return -1;
  }

  update_set_state("update: ethernet online, waiting for internet protocols");
  append_report(report, report_size, "[3/4] Ethernet link is up");
  append_report(report, report_size,
                "[4/4] GitHub package download not executed yet");
  append_report(report, report_size,
                "Missing kernel layers: ARP, IPv4, DHCP, DNS, TCP, HTTPS/TLS, boot writer.");
  append_report(report, report_size,
                "No external updater was launched; transaction state was saved.");

  update_append_log("GitHub download pending ARP/IPv4/DHCP/DNS/TCP/HTTPS/TLS");
  update_append_log("Boot/system file writer pending");
  vfs_persist_save();
  return -2;
}

const char *orizon_update_status(void) {
  return update_status_text;
}
