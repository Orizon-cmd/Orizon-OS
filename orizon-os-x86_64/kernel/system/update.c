/*
 * Orizon OS x86_64 - Update Manager
 *
 * `update` is the in-OS full-upgrade entrypoint. It owns the transaction and
 * records state locally; network protocols are added underneath it.
 */

#include "../include/update.h"
#include "../include/net.h"
#include "../include/netstack.h"
#include "../include/sched.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/vfs.h"

#define UPDATE_STATE_PATH "/workspace/.orizon/update-state"
#define UPDATE_LOG_PATH "/workspace/.orizon/update.log"
#define UPDATE_MANIFEST_PATH "/workspace/.orizon/update-manifest"
#define UPDATE_PLAN_PATH "/workspace/.orizon/update-plan"
#define UPDATE_PROOF_PATH "/workspace/.orizon/github-http-response"
#define UPDATE_PROOF_HASH_PATH "/workspace/.orizon/github-http-response.sha256"
#define SYSTEM_STATE_PATH "/system/update-state"
#define SYSTEM_PACKAGES_PATH "/system/packages"
#define SYSTEM_SOURCE_PATH "/system/update-source"
#define SYSTEM_MANIFEST_PATH "/system/update-manifest"
#define SYSTEM_INSTALLED_PATH "/system/installed"
#define UPDATE_SOURCE "https://github.com/Orizon-cmd/Orizon-OS"
#define UPDATE_CHANNEL "main"

typedef struct {
  const char *name;
  const char *version;
} update_package_t;

static const update_package_t base_packages[] = {
    {"orizon-core", "core-x86_64"},
    {"orizon-console", "minimal-shell"},
    {"orizon-vfs", "workspace-persistence"},
    {"orizon-net", "ethernet-e1000"},
    {"orizon-ipv4", "dhcp-dns-tcp-bootstrap"},
    {"orizon-sha256", "manifest-verification"},
    {"orizon-manifest", "staged-update-plan"},
    {"orizon-timer", "pit-100hz"},
    {"orizon-scheduler", "process-accounting"},
    {"orizon-updater", "github-bootstrap"},
};

static const char update_manifest[] =
    "manifest-version 1\n"
    "os Orizon OS\n"
    "channel " UPDATE_CHANNEL "\n"
    "source " UPDATE_SOURCE "\n"
    "transport github-https\n"
    "package orizon-core core-x86_64 required\n"
    "package orizon-console minimal-shell required\n"
    "package orizon-vfs workspace-persistence required\n"
    "package orizon-net ethernet-e1000 required\n"
    "package orizon-ipv4 dhcp-dns-tcp-bootstrap required\n"
    "package orizon-sha256 manifest-verification required\n"
    "package orizon-manifest staged-update-plan required\n"
    "package orizon-timer pit-100hz required\n"
    "package orizon-scheduler process-accounting required\n"
    "package orizon-updater github-bootstrap required\n"
    "payload Orizon-OS.iso transport=https sha256=pending size=pending\n";

static const char *update_status_text = "update: not run";
static char github_response[4096];

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

static void update_write_line(const char *path, const char *line) {
  update_write_file(path, line, 0);
  update_write_file(path, "\n", 1);
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

static void update_write_manifest(void) {
  update_write_file(UPDATE_MANIFEST_PATH, update_manifest, 0);
  update_write_file(SYSTEM_MANIFEST_PATH, update_manifest, 0);
}

static void update_write_installed_db(void) {
  char line[128];
  update_write_file(SYSTEM_INSTALLED_PATH, "", 0);
  for (size_t i = 0; i < sizeof(base_packages) / sizeof(base_packages[0]); i++) {
    snprintf(line, sizeof(line), "%s %s installed\n", base_packages[i].name,
             base_packages[i].version);
    update_write_file(SYSTEM_INSTALLED_PATH, line, 1);
  }
}

static void update_write_plan(const char *phase, const char *network,
                              const char *proof_hash, size_t proof_len) {
  char line[256];
  update_write_file(UPDATE_PLAN_PATH, "", 0);
  update_write_file(UPDATE_PLAN_PATH, "plan-version 1\n", 1);
  update_write_file(UPDATE_PLAN_PATH, "source " UPDATE_SOURCE "\n", 1);
  update_write_file(UPDATE_PLAN_PATH, "channel " UPDATE_CHANNEL "\n", 1);
  snprintf(line, sizeof(line), "phase %s\n", phase ? phase : "unknown");
  update_write_file(UPDATE_PLAN_PATH, line, 1);
  if (network && *network) {
    snprintf(line, sizeof(line), "network %s\n", network);
    update_write_file(UPDATE_PLAN_PATH, line, 1);
  }
  if (proof_hash && *proof_hash) {
    snprintf(line, sizeof(line), "github-proof-bytes %lu\n",
             (unsigned long)proof_len);
    update_write_file(UPDATE_PLAN_PATH, line, 1);
    snprintf(line, sizeof(line), "github-proof-sha256 %s\n", proof_hash);
    update_write_file(UPDATE_PLAN_PATH, line, 1);
  }
  update_write_file(UPDATE_PLAN_PATH, "next tls-download-manifest\n", 1);
  update_write_file(UPDATE_PLAN_PATH, "install staged-boot-writer\n", 1);
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
  char ip_line[256];
  char stack_line[256];
  char pkg_line[128];
  char proof_hash[SHA256_HEX_SIZE];
  size_t github_len = 0;

  if (report && report_size > 0) {
    report[0] = '\0';
  }

  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/system");
  sched_enter_process("update-manager");
  update_write_file(UPDATE_LOG_PATH, "", 0);
  update_write_file(SYSTEM_SOURCE_PATH, UPDATE_SOURCE "\n", 0);

  append_report(report, report_size, "\033[1;36mOrizon full-upgrade\033[0m");
  append_report(report, report_size, "Source: " UPDATE_SOURCE);
  update_append_log("Orizon full-upgrade started");
  update_append_log("Source: " UPDATE_SOURCE);

  update_set_state("update: preparing package database");
  update_write_package_db();
  update_write_manifest();
  update_write_installed_db();
  update_write_plan("prepare", "", "", 0);
  for (size_t i = 0; i < sizeof(base_packages) / sizeof(base_packages[0]); i++) {
    snprintf(pkg_line, sizeof(pkg_line), "Package target: %s %s",
             base_packages[i].name, base_packages[i].version);
    update_append_log(pkg_line);
  }
  append_report(report, report_size, "[1/7] Local package database ready");
  append_report(report, report_size, "Manifest staged: " UPDATE_MANIFEST_PATH);

  update_set_state("update: probing ethernet");
  net_init();
  net_format_status(net_line, sizeof(net_line));
  update_append_log(net_line);
  append_report(report, report_size, "[2/7] Ethernet probe");
  append_report(report, report_size, net_line);

  if (!net_link_up()) {
    update_set_state("update: blocked - ethernet link is down");
    append_report(report, report_size,
                  "[3/7] GitHub download skipped: ethernet link is down");
    append_report(report, report_size,
                  "Check VM/device network cable, bridge, or adapter model.");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -1;
  }

  update_set_state("update: configuring ipv4 with dhcp");
  append_report(report, report_size, "[3/7] Ethernet link is up");
  if (netstack_configure_ipv4() != 0) {
    netstack_format_status(ip_line, sizeof(ip_line));
    append_report(report, report_size, "[4/7] DHCP/IPv4 failed");
    append_report(report, report_size, ip_line);
    update_append_log(ip_line);
    update_write_plan("blocked-dhcp", ip_line, "", 0);
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -2;
  }

  netstack_format_status(ip_line, sizeof(ip_line));
  update_append_log(ip_line);
  update_write_plan("network-ready", ip_line, "", 0);
  append_report(report, report_size, "[4/7] DHCP/IPv4 ready");
  append_report(report, report_size, ip_line);

  update_set_state("update: downloading github metadata");
  github_response[0] = '\0';
  if (netstack_github_probe(github_response, sizeof(github_response),
                            &github_len) != 0) {
    netstack_format_status(ip_line, sizeof(ip_line));
    append_report(report, report_size, "[5/7] GitHub TCP download failed");
    append_report(report, report_size, ip_line);
    append_report(report, report_size,
                  "Next required layer: stronger TCP retry/TLS diagnostics.");
    update_append_log("GitHub TCP/HTTP probe failed");
    update_append_log(ip_line);
    update_write_plan("blocked-github-tcp", ip_line, "", 0);
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -3;
  }

  sha256_buffer_hex(github_response, github_len, proof_hash);
  update_write_file(UPDATE_PROOF_PATH, github_response, 0);
  update_write_line(UPDATE_PROOF_HASH_PATH, proof_hash);
  netstack_format_status(ip_line, sizeof(ip_line));
  update_append_log(ip_line);
  snprintf(ip_line, sizeof(ip_line), "Downloaded %lu bytes from GitHub edge",
           (unsigned long)github_len);
  update_append_log(ip_line);
  snprintf(pkg_line, sizeof(pkg_line), "GitHub proof sha256 %s", proof_hash);
  update_append_log(pkg_line);
  netstack_format_status(stack_line, sizeof(stack_line));
  update_write_plan("github-proof", stack_line, proof_hash, github_len);
  append_report(report, report_size, "[5/7] GitHub TCP/HTTP response saved");
  append_report(report, report_size, stack_line);
  append_report(report, report_size, ip_line);
  append_report(report, report_size, pkg_line);

  if (strstr(github_response, "Location: https://") ||
      strstr(github_response, "HTTP/1.1 301") ||
      strstr(github_response, "HTTP/1.0 301")) {
    update_set_state("update: github reached, https/tls required");
    append_report(report, report_size,
                  "[6/7] GitHub requires HTTPS/TLS for the real package body");
    append_report(report, report_size,
                  "[7/7] Full package install paused before TLS and boot writer");
    append_report(report, report_size,
                  "Saved proof: /workspace/.orizon/github-http-response");
    update_append_log("GitHub reached over TCP; HTTPS/TLS client is next");
    update_append_log("Boot/system file writer pending");
    vfs_persist_save();
    sched_set_process_state("update-manager", SCHED_SLEEPING);
    sched_enter_process("gui-shell");
    return -4;
  }

  update_set_state("update: github response downloaded");
  append_report(report, report_size,
                "[6/7] GitHub data downloaded without redirect");
  append_report(report, report_size,
                "[7/7] Package installer/boot writer pending");
  update_append_log("GitHub response downloaded");
  update_append_log("Boot/system file writer pending");
  vfs_persist_save();
  sched_set_process_state("update-manager", SCHED_SLEEPING);
  sched_enter_process("gui-shell");
  return 0;
}

const char *orizon_update_status(void) {
  return update_status_text;
}
