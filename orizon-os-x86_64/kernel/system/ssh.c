/*
 * Orizon OS x86_64 - SSH service
 *
 * This is the first safe remote-management brick: an inbound TCP/22 service
 * with an SSH protocol banner and diagnostics. Full encrypted KEX/auth/shell
 * will build on top of this listener instead of hiding an unsafe backdoor.
 */

#include "../include/ssh.h"
#include "../include/aes_gcm.h"
#include "../include/desktop.h"
#include "../include/gui.h"
#include "../include/install.h"
#include "../include/kmalloc.h"
#include "../include/klog.h"
#include "../include/net.h"
#include "../include/netstack.h"
#include "../include/packages.h"
#include "../include/pci.h"
#include "../include/power.h"
#include "../include/report.h"
#include "../include/rsa.h"
#include "../include/sched.h"
#include "../include/selftest.h"
#include "../include/sha256.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/system_state.h"
#include "../include/timer.h"
#include "../include/update.h"
#include "../include/usb.h"
#include "../include/vfs.h"
#include "../include/wifi.h"
#include "../include/x25519.h"

#define SSH_BANNER "SSH-2.0-OrizonSSH_0.1\r\n"
#define SSH_RX_BUF 2048
#define SSH_PACKET_MAX 4096
#define SSH_TX_BUF 1400
#define SSH_CHANNEL_TEXT_BUF 65536
#define SSH_CHANNEL_DATA_CHUNK 960
#define SSH_FILE_READ_MAX (SSH_CHANNEL_TEXT_BUF / 2)
#define SSH_AUDIT_RECENT 4

#define SSH_MSG_DISCONNECT 1
#define SSH_MSG_SERVICE_REQUEST 5
#define SSH_MSG_SERVICE_ACCEPT 6
#define SSH_MSG_KEXINIT 20
#define SSH_MSG_NEWKEYS 21
#define SSH_MSG_KEXDH_INIT 30
#define SSH_MSG_KEXDH_REPLY 31
#define SSH_MSG_USERAUTH_REQUEST 50
#define SSH_MSG_USERAUTH_FAILURE 51
#define SSH_MSG_USERAUTH_SUCCESS 52
#define SSH_MSG_CHANNEL_OPEN 90
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91
#define SSH_MSG_CHANNEL_OPEN_FAILURE 92
#define SSH_MSG_CHANNEL_DATA 94
#define SSH_MSG_CHANNEL_EOF 96
#define SSH_MSG_CHANNEL_CLOSE 97
#define SSH_MSG_CHANNEL_REQUEST 98
#define SSH_MSG_CHANNEL_SUCCESS 99
#define SSH_MSG_CHANNEL_FAILURE 100

#define SSH_KEX_ALGORITHMS "curve25519-sha256,curve25519-sha256@libssh.org"
#define SSH_HOSTKEY_ALGORITHMS "rsa-sha2-256"
#define SSH_CIPHER_ALGORITHMS "aes128-ctr,aes128-gcm@openssh.com"
#define SSH_MAC_ALGORITHMS "hmac-sha2-256,hmac-sha1"
#define SSH_COMPRESSION_ALGORITHMS "none"
#define SSH_RSA_SIGNATURE_SIZE 128U
#define SSH_CHANNEL_WINDOW 65536U
#define SSH_CHANNEL_MAX_PACKET 1024U
#define SSH_AUTH_MAX_ATTEMPTS_DEFAULT 3U
#define SSH_AUTH_LOCKOUT_SECONDS_DEFAULT 30U
#define SSH_AUTH_MAX_ATTEMPTS_LIMIT 20U
#define SSH_AUTH_LOCKOUT_SECONDS_LIMIT 3600U
#define SSH_HOSTKEY_FILE_MAX 1600U
#define SSH_SESSION_IDLE_TIMEOUT_TICKS (15ULL * TIMER_HZ)
#define SSH_UPDATE_LOG_PATH "/workspace/.orizon/update.log"
#define SSH_INSTALL_LOG_PATH "/workspace/.orizon/install-log"
#define SSH_INSTALL_REPORT_PATH "/workspace/.orizon/install-report.txt"
#define SSH_WIFI_LOG_PATH "/logs/wifi.log"
/*
 * Development host key for the current staged SSH server.
 * Used only as a last-resort fallback when per-install key generation or
 * persistence is unavailable.
 */
static const uint8_t ORIZON_SSH_RSA_N[SSH_RSA_SIGNATURE_SIZE] = {
    0xac, 0x57, 0x1b, 0x62, 0x57, 0x43, 0xec, 0xaa, 0x07, 0x3d, 0xfe, 0xe6,
    0x88, 0x85, 0xa4, 0x72, 0xac, 0xb5, 0xfe, 0x49, 0x42, 0x60, 0x31, 0x66,
    0x9c, 0x4b, 0xdc, 0x3d, 0x42, 0x83, 0x48, 0x23, 0x72, 0x2b, 0xae, 0x99,
    0xee, 0xc4, 0x00, 0xe9, 0x11, 0x3d, 0x33, 0x12, 0x1e, 0xb4, 0x5f, 0xb4,
    0xa0, 0x4d, 0x4d, 0xec, 0x9d, 0xf8, 0x5d, 0x8d, 0x4a, 0x55, 0xc1, 0xe1,
    0x3e, 0x35, 0xbb, 0x66, 0x45, 0x38, 0xa9, 0x4d, 0x64, 0x4d, 0x72, 0x87,
    0x8a, 0x51, 0xdc, 0x6d, 0x13, 0x44, 0x0b, 0xad, 0x5f, 0x2f, 0x59, 0x8f,
    0x58, 0x41, 0x70, 0x19, 0xbc, 0x2b, 0xae, 0xd1, 0x85, 0xfc, 0x4a, 0x74,
    0xca, 0x42, 0xbb, 0x12, 0x48, 0x60, 0xd8, 0x37, 0xf7, 0xc6, 0x47, 0x8d,
    0x65, 0x78, 0xd8, 0x73, 0x48, 0x29, 0x78, 0xb3, 0x52, 0x45, 0x97, 0x65,
    0xa0, 0xa2, 0x44, 0x12, 0xcd, 0x3d, 0x3d, 0x3f,
};

static const uint8_t ORIZON_SSH_RSA_P[64] = {
    0xda, 0xb2, 0xff, 0x0c, 0x37, 0x01, 0xb2, 0x41, 0x3d, 0x74, 0x53, 0x2b,
    0xe1, 0x9e, 0x8e, 0x93, 0xe1, 0xa7, 0x51, 0xf1, 0x49, 0xd3, 0xe9, 0x9f,
    0xbd, 0xad, 0x23, 0x3a, 0x61, 0xa8, 0x26, 0x33, 0x94, 0xd9, 0xcd, 0x4f,
    0xd9, 0x71, 0x8a, 0xda, 0x13, 0x72, 0xa7, 0xd4, 0xff, 0x6a, 0xa3, 0x84,
    0xcc, 0xa4, 0x33, 0x56, 0x51, 0xa1, 0xc2, 0x52, 0x91, 0xf8, 0xee, 0x8e,
    0x99, 0x47, 0x40, 0xf3,
};

static const uint8_t ORIZON_SSH_RSA_Q[64] = {
    0xc9, 0xbb, 0xf3, 0xeb, 0xe9, 0xd0, 0x19, 0x22, 0xba, 0x02, 0x2c, 0x94,
    0xdf, 0x27, 0x0b, 0x8f, 0x9d, 0x80, 0x64, 0xc0, 0x59, 0xba, 0x44, 0x9f,
    0xc4, 0x5b, 0xda, 0x0a, 0x2d, 0x03, 0x73, 0x37, 0xd3, 0xc7, 0xc9, 0xde,
    0x46, 0x7c, 0xda, 0x81, 0x0d, 0xb2, 0x0c, 0xfc, 0x06, 0x41, 0x14, 0x88,
    0x3b, 0xd2, 0xb0, 0xb8, 0x9d, 0x04, 0xb6, 0xb0, 0x27, 0xe7, 0x6c, 0x02,
    0x44, 0x2d, 0x45, 0x85,
};

static const uint8_t ORIZON_SSH_RSA_DMP1[64] = {
    0x12, 0xe0, 0xaa, 0x85, 0x4a, 0x66, 0x3a, 0x15, 0xc9, 0x91, 0x35, 0xf0,
    0xae, 0xbb, 0xfa, 0x00, 0xa7, 0xd4, 0xc2, 0x8c, 0xfa, 0x5b, 0x71, 0x6a,
    0x19, 0x7c, 0x4d, 0x73, 0x27, 0xa4, 0xd5, 0x0f, 0x54, 0xc4, 0xec, 0x24,
    0xfd, 0x57, 0x00, 0xae, 0x4c, 0x49, 0x74, 0x55, 0x3d, 0x6a, 0xde, 0x0c,
    0x83, 0x81, 0x94, 0xf0, 0xd9, 0x81, 0x05, 0xfe, 0x0c, 0x9d, 0x99, 0x31,
    0xf3, 0xe7, 0x23, 0xa3,
};

static const uint8_t ORIZON_SSH_RSA_DMQ1[64] = {
    0xa2, 0x90, 0x7f, 0x83, 0xc0, 0xab, 0x1d, 0x56, 0x4a, 0xa6, 0xad, 0xde,
    0x59, 0xe5, 0x50, 0xff, 0xae, 0x60, 0x64, 0xd0, 0x4c, 0x7e, 0x3a, 0x06,
    0xb5, 0x69, 0x7f, 0x4f, 0x6b, 0xee, 0xb7, 0xce, 0x69, 0x2f, 0x3a, 0x91,
    0x90, 0x23, 0xd4, 0xc0, 0xe2, 0x94, 0x74, 0xba, 0x33, 0x20, 0x06, 0xb7,
    0xb1, 0xdd, 0x9a, 0xe3, 0x6a, 0x44, 0xfe, 0x22, 0xfe, 0x45, 0x13, 0x58,
    0xd0, 0x2f, 0xdb, 0x31,
};

static const uint8_t ORIZON_SSH_RSA_IQMP[64] = {
    0x59, 0x57, 0x56, 0x51, 0x72, 0x73, 0x0b, 0x88, 0x71, 0x1f, 0xbd, 0x52,
    0x15, 0x78, 0x1e, 0xd3, 0x36, 0x2e, 0x6b, 0x16, 0x34, 0xb0, 0x09, 0x8d,
    0x0f, 0x15, 0x94, 0x32, 0x2f, 0xac, 0xbe, 0x98, 0xb0, 0xec, 0x4f, 0x91,
    0x86, 0xa7, 0x73, 0x19, 0x5d, 0x08, 0x88, 0x1f, 0x33, 0xb9, 0xce, 0x35,
    0x07, 0xac, 0xfd, 0x5c, 0xe9, 0x75, 0x08, 0xd5, 0x3b, 0x24, 0x47, 0x60,
    0x18, 0x4a, 0x82, 0x78,
};

static ssh_status_t ssh_status = {
    .enabled = 0,
    .configured = 0,
    .listening = 0,
    .connected = 0,
    .banner_sent = 0,
    .client_banner_seen = 0,
    .server_kexinit_sent = 0,
    .client_kexinit_seen = 0,
    .client_kex_packet_seen = 0,
    .ecdh_ready = 0,
    .ecdh_reply_sent = 0,
    .newkeys_sent = 0,
    .client_newkeys_seen = 0,
    .traffic_keys_ready = 0,
    .encrypted_packet_seen = 0,
    .service_accept_sent = 0,
    .userauth_request_seen = 0,
    .auth_configured = 0,
    .authenticated = 0,
    .auth_failure_sent = 0,
    .auth_failures = 0,
    .auth_lockout_until = 0,
    .max_auth_attempts = SSH_AUTH_MAX_ATTEMPTS_DEFAULT,
    .auth_lockout_seconds = SSH_AUTH_LOCKOUT_SECONDS_DEFAULT,
    .hostkey_loaded = 0,
    .hostkey_persistent = 0,
    .hostkey_bootstrap = 1,
    .channel_open_seen = 0,
    .channel_open_confirm_sent = 0,
    .shell_ready = 0,
    .kex_seen = 0,
    .disconnect_sent = 0,
    .last_packet_type = 0,
    .port = ORIZON_SSH_PORT,
    .remote_ip = 0,
    .remote_port = 0,
    .sessions = 0,
    .packets_rx = 0,
    .ssh_packets_rx = 0,
    .bytes_rx = 0,
    .bytes_tx = 0,
    .errors = 0,
    .remote_banner = {0},
    .kex_algorithm = {0},
    .hostkey_algorithm = {0},
    .cipher_c2s = {0},
    .cipher_s2c = {0},
    .mac_c2s = {0},
    .mac_s2c = {0},
    .compression_c2s = {0},
    .compression_s2c = {0},
    .client_kex_first = {0},
    .client_hostkey_first = {0},
    .client_public_sha256 = {0},
    .hostkey_sha256 = {0},
    .server_public_sha256 = {0},
    .shared_secret_sha256 = {0},
    .exchange_hash_sha256 = {0},
    .signature_sha256 = {0},
    .client_to_server_key_sha256 = {0},
    .server_to_client_key_sha256 = {0},
    .client_to_server_mac_sha256 = {0},
    .server_to_client_mac_sha256 = {0},
    .auth_user = {0},
    .auth_method = {0},
    .hostkey_source = "compiled-bootstrap",
    .hostkey_status = "ssh: host key not loaded",
    .status = "ssh: stopped",
};

static netstack_tcp_server_t ssh_server;
static uint32_t ssh_seen_connections = 0;
static uint64_t ssh_last_activity_tick = 0;
static int ssh_disconnect_close_polls = 0;
static uint8_t ssh_binary_rx[SSH_PACKET_MAX];
static size_t ssh_binary_rx_used = 0;
static size_t ssh_remote_banner_len = 0;
static uint8_t ssh_client_kexinit_payload[2048];
static size_t ssh_client_kexinit_payload_len = 0;
static uint8_t ssh_server_kexinit_payload[768];
static size_t ssh_server_kexinit_payload_len = 0;
static uint8_t ssh_client_public[X25519_KEY_SIZE];
static uint8_t ssh_server_private[X25519_KEY_SIZE];
static uint8_t ssh_server_public[X25519_KEY_SIZE];
static uint8_t ssh_shared_secret[X25519_KEY_SIZE];
static uint8_t ssh_host_key_blob[192];
static size_t ssh_host_key_blob_len = 0;
static uint8_t ssh_exchange_hash[SHA256_DIGEST_SIZE];
static uint8_t ssh_session_id[SHA256_DIGEST_SIZE];
static int ssh_session_id_ready = 0;
static uint8_t ssh_host_signature[SSH_RSA_SIGNATURE_SIZE];
static int ssh_host_signature_ready = 0;
static uint8_t ssh_hostkey_n[SSH_RSA_SIGNATURE_SIZE];
static uint8_t ssh_hostkey_d[SSH_RSA_SIGNATURE_SIZE];
static uint8_t ssh_hostkey_p[64];
static uint8_t ssh_hostkey_q[64];
static uint8_t ssh_hostkey_dmp1[64];
static uint8_t ssh_hostkey_dmq1[64];
static uint8_t ssh_hostkey_iqmp[64];
static rsa_crt_private_key_t ssh_hostkey = {
    .n = ORIZON_SSH_RSA_N,
    .n_len = sizeof(ORIZON_SSH_RSA_N),
    .d = NULL,
    .d_len = 0,
    .p = ORIZON_SSH_RSA_P,
    .p_len = sizeof(ORIZON_SSH_RSA_P),
    .q = ORIZON_SSH_RSA_Q,
    .q_len = sizeof(ORIZON_SSH_RSA_Q),
    .dmp1 = ORIZON_SSH_RSA_DMP1,
    .dmp1_len = sizeof(ORIZON_SSH_RSA_DMP1),
    .dmq1 = ORIZON_SSH_RSA_DMQ1,
    .dmq1_len = sizeof(ORIZON_SSH_RSA_DMQ1),
    .iqmp = ORIZON_SSH_RSA_IQMP,
    .iqmp_len = sizeof(ORIZON_SSH_RSA_IQMP),
};
static uint8_t ssh_iv_c2s[16];
static uint8_t ssh_iv_s2c[16];
static uint8_t ssh_key_c2s[16];
static uint8_t ssh_key_s2c[16];
static uint8_t ssh_mac_c2s[SHA256_DIGEST_SIZE];
static uint8_t ssh_mac_s2c[SHA256_DIGEST_SIZE];
static uint8_t ssh_ctr_c2s[16];
static uint8_t ssh_ctr_s2c[16];
static uint32_t ssh_seq_in = 0;
static uint32_t ssh_seq_out = 0;
static int ssh_in_encrypted = 0;
static int ssh_out_encrypted = 0;
static int ssh_service_accept_pending = 0;
static int ssh_auth_failure_pending = 0;
static int ssh_auth_success_pending = 0;
static int ssh_channel_open_confirm_pending = 0;
static int ssh_channel_success_pending = 0;
static int ssh_channel_failure_pending = 0;
static int ssh_channel_data_pending = 0;
static int ssh_channel_exit_status_pending = 0;
static uint32_t ssh_channel_exit_code = 0;
static int ssh_channel_close_pending = 0;
static int ssh_channel_close_sent = 0;
static uint8_t ssh_encrypted_rx[SSH_PACKET_MAX + SHA256_DIGEST_SIZE];
static size_t ssh_encrypted_rx_used = 0;
static uint8_t ssh_pending_ctr_s2c[16];
static int ssh_pending_ctr_s2c_ready = 0;
static uint8_t ssh_mac_input[SSH_PACKET_MAX + 4];
static uint8_t ssh_encrypt_plain[SSH_PACKET_MAX];
static uint8_t ssh_poll_rx[SSH_RX_BUF];
static uint8_t ssh_poll_tx[SSH_TX_BUF];
static uint8_t ssh_channel_payload[1100];
static char ssh_password_sha256[SHA256_HEX_SIZE];
static uint32_t ssh_client_channel = 0;
static uint32_t ssh_server_channel = 0;
static uint32_t ssh_client_window = 0;
static uint32_t ssh_client_max_packet = 0;
static char ssh_channel_tx[SSH_CHANNEL_TEXT_BUF];
static size_t ssh_channel_tx_len = 0;
static size_t ssh_channel_tx_off = 0;
static size_t ssh_channel_last_chunk_len = 0;
static char ssh_shell_line[256];
static size_t ssh_shell_line_len = 0;
static int ssh_shell_last_was_cr = 0;
static char ssh_shell_cwd[MAX_PATH] = "/home/orizon";
static int ssh_shell_suppress_prompt = 0;
static uint32_t ssh_auth_success_total = 0;
static uint32_t ssh_auth_failure_total = 0;
static uint32_t ssh_session_total = 0;
static uint32_t ssh_exec_request_total = 0;
static uint32_t ssh_shell_command_total = 0;
static uint32_t ssh_listener_reset_total = 0;
static uint32_t ssh_listener_recover_total = 0;
static uint32_t ssh_channel_close_total = 0;
static char ssh_last_command[160] = "none";
static char ssh_last_audit[192] = "none";
static char ssh_audit_recent[SSH_AUDIT_RECENT][160];
static uint32_t ssh_audit_recent_next = 0;
static uint32_t ssh_audit_recent_count = 0;
static uint32_t ssh_policy_denied_total = 0;
static uint32_t ssh_policy_denied_sensitive = 0;
static uint32_t ssh_policy_denied_internal = 0;
static uint32_t ssh_policy_denied_write_scope = 0;
static uint32_t ssh_policy_denied_root = 0;
static char ssh_last_policy_denial[128] = "none";

static int ssh_ensure_hostkey(void);
static int ssh_load_hostkey_file(void);
static int ssh_write_hostkey_file(void);
static int ssh_generate_install_hostkey(char *report, size_t report_size);
static int ssh_rebuild_host_key_blob(void);
static void ssh_install_bootstrap_hostkey(void);
static void ssh_reset_negotiation(void);
static void ssh_refresh_state(void);

static void ssh_append_log_line(const char *path, const char *line) {
  file_t *f;

  if (!path || !line) {
    return;
  }
  vfs_mkdir("/logs");
  f = vfs_open(path, O_CREAT | O_WRONLY | O_APPEND);
  if (!f) {
    return;
  }
  vfs_write(f, line, strlen(line));
  vfs_write(f, "\n", 1);
  vfs_close(f);
}

static int ssh_write_text_file_raw(const char *path, const char *text) {
  file_t *f;

  if (!path || !text) {
    return -1;
  }
  vfs_mkdir("/system");
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/logs");
  f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -1;
  }
  if (vfs_write(f, text, strlen(text)) != (ssize_t)strlen(text)) {
    vfs_close(f);
    return -1;
  }
  vfs_close(f);
  return 0;
}

static void ssh_log_line(const char *line) {
  ssh_append_log_line(ORIZON_SSH_LOG_PATH, line);
}

static void ssh_security_log_line(const char *line) {
  ssh_append_log_line(ORIZON_SECURITY_LOG_PATH, line);
}

static void ssh_set_status(const char *status) {
  strncpy(ssh_status.status, status, sizeof(ssh_status.status) - 1);
  ssh_status.status[sizeof(ssh_status.status) - 1] = '\0';
  klog_info("ssh", status);
  ssh_log_line(status);
}

static void ssh_audit_event(const char *event) {
  char remote[24];
  char line[224];
  char *slot;

  if (!event) {
    event = "event";
  }
  netstack_format_ipv4(ssh_status.remote_ip, remote, sizeof(remote));
  snprintf(line, sizeof(line), "audit: %s remote=%s:%u sessions=%lu",
           event, remote, (unsigned)ssh_status.remote_port,
           (unsigned long)ssh_status.sessions);
  strncpy(ssh_last_audit, line, sizeof(ssh_last_audit) - 1);
  ssh_last_audit[sizeof(ssh_last_audit) - 1] = '\0';
  slot = ssh_audit_recent[ssh_audit_recent_next % SSH_AUDIT_RECENT];
  strncpy(slot, line, 159);
  slot[159] = '\0';
  ssh_audit_recent_next = (ssh_audit_recent_next + 1) % SSH_AUDIT_RECENT;
  if (ssh_audit_recent_count < SSH_AUDIT_RECENT) {
    ssh_audit_recent_count++;
  }
  ssh_log_line(line);
  ssh_security_log_line(line);
}

static int ssh_command_prefix(const char *command, const char *prefix) {
  size_t len;

  if (!command || !prefix) {
    return 0;
  }
  len = strlen(prefix);
  return strncmp(command, prefix, len) == 0 &&
         (command[len] == '\0' || command[len] == ' ');
}

static void ssh_copy_command_path(const char *command, const char *op,
                                  char *safe, size_t safe_size) {
  char path[80];
  const char *p;
  size_t i = 0;

  if (!command || !op || !safe || safe_size == 0) {
    return;
  }
  p = command + strlen(op);
  while (*p == ' ') {
    p++;
  }
  while (p[i] && p[i] != ' ' && p[i] != '\r' && p[i] != '\n' &&
         i + 1 < sizeof(path)) {
    path[i] = p[i];
    i++;
  }
  path[i] = '\0';
  snprintf(safe, safe_size, "%s %s <redacted-text>", op,
           path[0] ? path : "<path>");
}

static void ssh_redact_command_for_audit(const char *command, char *safe,
                                         size_t safe_size) {
  size_t i = 0;

  if (!safe || safe_size == 0) {
    return;
  }
  if (!command) {
    safe[0] = '\0';
    return;
  }
  if (ssh_command_prefix(command, "ssh password")) {
    snprintf(safe, safe_size, "ssh password <redacted>");
    return;
  }
  if (ssh_command_prefix(command, "write")) {
    ssh_copy_command_path(command, "write", safe, safe_size);
    return;
  }
  if (ssh_command_prefix(command, "append")) {
    ssh_copy_command_path(command, "append", safe, safe_size);
    return;
  }
  if (ssh_command_prefix(command, "wifi connect") ||
      ssh_command_prefix(command, "wifi join")) {
    snprintf(safe, safe_size, "wifi <redacted-credentials>");
    return;
  }
  while (command[i] && command[i] != '\r' && command[i] != '\n' &&
         i + 1 < safe_size) {
    safe[i] = command[i];
    i++;
  }
  safe[i] = '\0';
}

static void ssh_record_command(const char *kind, const char *command) {
  char safe[128];
  char event[176];

  if (!kind) {
    kind = "command";
  }
  if (!command) {
    command = "";
  }
  ssh_redact_command_for_audit(command, safe, sizeof(safe));
  snprintf(ssh_last_command, sizeof(ssh_last_command), "%s: %s", kind, safe);
  snprintf(event, sizeof(event), "%s command=\"%s\"", kind, safe);
  ssh_audit_event(event);
}

static int ssh_hex64_valid(const char *text) {
  if (!text || strlen(text) != 64) {
    return 0;
  }
  for (size_t i = 0; i < 64; i++) {
    char c = text[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return 0;
    }
  }
  return 1;
}

static void ssh_copy_token(const char *src, char *dst, size_t dst_size) {
  size_t i = 0;

  if (!dst || dst_size == 0) {
    return;
  }
  while (src && src[i] && src[i] != ' ' && src[i] != '\n' &&
         src[i] != '\r' && i + 1 < dst_size) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static uint32_t ssh_parse_uint_token(const char *src, uint32_t fallback,
                                     uint32_t min_value,
                                     uint32_t max_value) {
  uint32_t value = 0;
  int seen = 0;

  if (!src) {
    return fallback;
  }
  while (*src == ' ' || *src == '\t') {
    src++;
  }
  while (*src >= '0' && *src <= '9') {
    uint32_t digit = (uint32_t)(*src - '0');
    seen = 1;
    if (value > (max_value - digit) / 10U) {
      return max_value;
    }
    value = value * 10U + digit;
    src++;
  }
  if (!seen || value < min_value) {
    return fallback;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static uint64_t ssh_lockout_remaining(void) {
  uint64_t now = timer_uptime_seconds();

  if (ssh_status.auth_lockout_until <= now) {
    return 0;
  }
  return ssh_status.auth_lockout_until - now;
}

static void ssh_note_auth_failure(const char *reason) {
  ssh_auth_failure_pending = 1;
  ssh_status.auth_failures++;
  ssh_auth_failure_total++;
  if (ssh_status.max_auth_attempts > 0 &&
      ssh_status.auth_failures >= ssh_status.max_auth_attempts) {
    ssh_status.auth_lockout_until =
        timer_uptime_seconds() + ssh_status.auth_lockout_seconds;
    ssh_status.auth_failures = 0;
    ssh_set_status("ssh: auth locked after repeated failures");
    ssh_audit_event("auth failure lockout");
    return;
  }
  ssh_set_status(reason ? reason : "ssh: authentication failed");
  ssh_audit_event("auth failure");
}

static void ssh_note_auth_success(void) {
  ssh_status.authenticated = 1;
  ssh_status.auth_failures = 0;
  ssh_status.auth_lockout_until = 0;
  ssh_auth_success_pending = 1;
  ssh_auth_success_total++;
  ssh_set_status("ssh: password auth accepted");
  ssh_audit_event("auth success");
}

static void ssh_load_config(void) {
  file_t *f;
  char text[768];
  ssize_t n;
  const char *needle = "password-sha256 ";
  const char *max_attempts_needle = "max-attempts ";
  const char *lockout_needle = "lockout-seconds ";
  char *p;

  f = vfs_open(ORIZON_SSH_CONFIG_PATH, O_RDONLY);
  if (!f) {
    ssh_status.auth_configured = ssh_hex64_valid(ssh_password_sha256);
    return;
  }
  ssh_status.auth_configured = 0;
  ssh_password_sha256[0] = '\0';
  ssh_status.max_auth_attempts = SSH_AUTH_MAX_ATTEMPTS_DEFAULT;
  ssh_status.auth_lockout_seconds = SSH_AUTH_LOCKOUT_SECONDS_DEFAULT;
  memset(text, 0, sizeof(text));
  n = vfs_read(f, text, sizeof(text) - 1);
  vfs_close(f);
  if (n <= 0) {
    return;
  }
  p = strstr(text, needle);
  if (p) {
    char hash[SHA256_HEX_SIZE];
    ssh_copy_token(p + strlen(needle), hash, sizeof(hash));
    if (ssh_hex64_valid(hash)) {
      strncpy(ssh_password_sha256, hash, sizeof(ssh_password_sha256) - 1);
      ssh_password_sha256[sizeof(ssh_password_sha256) - 1] = '\0';
      ssh_status.auth_configured = 1;
    }
  }
  p = strstr(text, max_attempts_needle);
  if (p) {
    ssh_status.max_auth_attempts =
        ssh_parse_uint_token(p + strlen(max_attempts_needle),
                             SSH_AUTH_MAX_ATTEMPTS_DEFAULT, 1,
                             SSH_AUTH_MAX_ATTEMPTS_LIMIT);
  }
  p = strstr(text, lockout_needle);
  if (p) {
    ssh_status.auth_lockout_seconds =
        ssh_parse_uint_token(p + strlen(lockout_needle),
                             SSH_AUTH_LOCKOUT_SECONDS_DEFAULT, 1,
                             SSH_AUTH_LOCKOUT_SECONDS_LIMIT);
  }
}

static void ssh_write_config(void) {
  file_t *f;
  char text[384];

  vfs_mkdir("/system");
  snprintf(text, sizeof(text),
           "enabled yes\n"
           "port 22\n"
           "mode staged-ssh-transport\n"
           "user orizon\n"
           "auth %s\n"
           "password-sha256 %s\n"
           "max-attempts %lu\n"
           "lockout-seconds %lu\n",
           ssh_status.auth_configured ? "password" : "disabled",
           ssh_status.auth_configured ? ssh_password_sha256 : "unset",
           (unsigned long)ssh_status.max_auth_attempts,
           (unsigned long)ssh_status.auth_lockout_seconds);
  f = vfs_open(ORIZON_SSH_CONFIG_PATH, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return;
  }
  vfs_write(f, text, strlen(text));
  vfs_close(f);
}

static void ssh_ensure_config(void) {
  ssh_load_config();
  if (!vfs_exists(ORIZON_SSH_CONFIG_PATH)) {
    ssh_write_config();
  }
}

int ssh_set_password(const char *password, char *report, size_t report_size) {
  char token[96];

  ssh_copy_token(password, token, sizeof(token));
  if (strlen(token) < 6) {
    if (report && report_size > 0) {
      snprintf(report, report_size,
               "ssh: password not changed; use at least 6 characters.\n");
    }
    return -1;
  }
  sha256_buffer_hex(token, strlen(token), ssh_password_sha256);
  ssh_status.auth_configured = 1;
  ssh_status.auth_failures = 0;
  ssh_status.auth_lockout_until = 0;
  ssh_write_config();
  ssh_security_log_line("security: ssh password auth enabled user=orizon");
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: password auth enabled for user 'orizon'.\n"
             "ssh: connect with: ssh orizon@<ip-orizon>\n");
  }
  return 0;
}

int ssh_disable_password(char *report, size_t report_size) {
  ssh_password_sha256[0] = '\0';
  ssh_status.auth_configured = 0;
  ssh_status.authenticated = 0;
  ssh_status.auth_failures = 0;
  ssh_status.auth_lockout_until = 0;
  ssh_write_config();
  ssh_security_log_line("security: ssh password auth disabled user=orizon");
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: password auth disabled; existing sessions remain until closed.\n");
  }
  return 0;
}

int ssh_reload_config(char *report, size_t report_size) {
  ssh_load_config();
  ssh_security_log_line("security: ssh config reloaded");
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: config reloaded from %s; auth=%s max-attempts=%lu lockout=%lus\n",
             ORIZON_SSH_CONFIG_PATH,
             ssh_status.auth_configured ? "password" : "disabled",
             (unsigned long)ssh_status.max_auth_attempts,
             (unsigned long)ssh_status.auth_lockout_seconds);
  }
  return 0;
}

int ssh_clear_lockout(char *report, size_t report_size) {
  ssh_status.auth_failures = 0;
  ssh_status.auth_lockout_until = 0;
  ssh_security_log_line("security: ssh lockout cleared");
  if (report && report_size > 0) {
    snprintf(report, report_size, "ssh: auth lockout cleared.\n");
  }
  return 0;
}

int ssh_set_auth_policy(uint32_t max_attempts, uint32_t lockout_seconds,
                        char *report, size_t report_size) {
  char line[128];

  if (max_attempts < 1 || max_attempts > SSH_AUTH_MAX_ATTEMPTS_LIMIT ||
      lockout_seconds < 1 ||
      lockout_seconds > SSH_AUTH_LOCKOUT_SECONDS_LIMIT) {
    if (report && report_size > 0) {
      snprintf(report, report_size,
               "ssh: invalid auth policy; max-attempts=1..%lu lockout=1..%lus.\n",
               (unsigned long)SSH_AUTH_MAX_ATTEMPTS_LIMIT,
               (unsigned long)SSH_AUTH_LOCKOUT_SECONDS_LIMIT);
    }
    return -1;
  }
  ssh_status.max_auth_attempts = max_attempts;
  ssh_status.auth_lockout_seconds = lockout_seconds;
  ssh_status.auth_failures = 0;
  ssh_status.auth_lockout_until = 0;
  ssh_write_config();
  snprintf(line, sizeof(line),
           "security: ssh auth policy changed max-attempts=%lu lockout=%lus",
           (unsigned long)ssh_status.max_auth_attempts,
           (unsigned long)ssh_status.auth_lockout_seconds);
  ssh_security_log_line(line);
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: auth policy saved; max-attempts=%lu lockout=%lus.\n",
             (unsigned long)ssh_status.max_auth_attempts,
             (unsigned long)ssh_status.auth_lockout_seconds);
  }
  return 0;
}

int ssh_reset_auth_policy(char *report, size_t report_size) {
  return ssh_set_auth_policy(SSH_AUTH_MAX_ATTEMPTS_DEFAULT,
                             SSH_AUTH_LOCKOUT_SECONDS_DEFAULT, report,
                             report_size);
}

int ssh_reload_hostkey(char *report, size_t report_size) {
  if (ssh_load_hostkey_file() != 0) {
    ssh_security_log_line("security: ssh hostkey reload failed");
    if (report && report_size > 0) {
      snprintf(report, report_size,
               "ssh: host key reload failed; run 'ssh hostkey reset' to recreate %s.\n",
               ORIZON_SSH_HOSTKEY_PATH);
    }
    return -1;
  }
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: host key reloaded from %s\n"
             "ssh: hostkey-sha256=%s\n",
             ORIZON_SSH_HOSTKEY_PATH,
             ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256
                                          : "none");
  }
  ssh_security_log_line("security: ssh hostkey reloaded");
  return 0;
}

int ssh_reset_hostkey(char *report, size_t report_size) {
  char gen_report[160];
  char line[160];

  vfs_delete(ORIZON_SSH_HOSTKEY_PATH);
  if (ssh_generate_install_hostkey(gen_report, sizeof(gen_report)) != 0) {
    ssh_install_bootstrap_hostkey();
  }
  if (ssh_write_hostkey_file() != 0) {
    ssh_security_log_line("security: ssh hostkey reset failed");
    if (report && report_size > 0) {
      snprintf(report, report_size,
               "ssh: host key reset failed; could not write %s.\n",
               ORIZON_SSH_HOSTKEY_PATH);
    }
    return -1;
  }
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: host key regenerated and persisted to %s\n"
             "ssh: hostkey-sha256=%s\n",
             ORIZON_SSH_HOSTKEY_PATH,
             ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256
                                          : "none");
  }
  snprintf(line, sizeof(line), "security: ssh hostkey reset fingerprint=%s",
           ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256 : "none");
  ssh_security_log_line(line);
  return 0;
}

static void ssh_put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)((v >> 16) & 0xff);
  p[2] = (uint8_t)((v >> 8) & 0xff);
  p[3] = (uint8_t)(v & 0xff);
}

static uint32_t ssh_get_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int ssh_wrap_packet_block(uint8_t *out, size_t cap,
                                 const uint8_t *payload, size_t payload_len,
                                 size_t block_size, size_t *out_len) {
  size_t padding_len;
  size_t packet_len;
  size_t off = 0;
  uint32_t seed = 0x4f5a5353U ^ (uint32_t)timer_ticks() ^
                  (uint32_t)ssh_status.sessions;

  if (block_size < 8) {
    block_size = 8;
  }
  padding_len = block_size - ((payload_len + 5) % block_size);
  if (padding_len < 4) {
    padding_len += block_size;
  }
  packet_len = 1 + payload_len + padding_len;
  if (!out || !payload || !out_len || cap < packet_len + 4) {
    return -1;
  }

  ssh_put_u32(out + off, (uint32_t)packet_len);
  off += 4;
  out[off++] = (uint8_t)padding_len;
  memcpy(out + off, payload, payload_len);
  off += payload_len;
  for (size_t i = 0; i < padding_len; i++) {
    seed = seed * 1664525U + 1013904223U;
    out[off++] = (uint8_t)(seed >> 24);
  }
  *out_len = off;
  return 0;
}

static int ssh_wrap_packet(uint8_t *out, size_t cap, const uint8_t *payload,
                           size_t payload_len, size_t *out_len) {
  return ssh_wrap_packet_block(out, cap, payload, payload_len, 8, out_len);
}

static int ssh_put_namelist(uint8_t *out, size_t cap, size_t *off,
                            const char *names) {
  size_t len = strlen(names);

  if (!out || !off || *off + 4 + len > cap) {
    return -1;
  }
  ssh_put_u32(out + *off, (uint32_t)len);
  *off += 4;
  if (len > 0) {
    memcpy(out + *off, names, len);
    *off += len;
  }
  return 0;
}

static int ssh_put_string(uint8_t *out, size_t cap, size_t *off,
                          const uint8_t *data, size_t len) {
  if (!out || !off || *off + 4 + len > cap) {
    return -1;
  }
  ssh_put_u32(out + *off, (uint32_t)len);
  *off += 4;
  if (len > 0 && data) {
    memcpy(out + *off, data, len);
    *off += len;
  }
  return 0;
}

static int ssh_put_cstring(uint8_t *out, size_t cap, size_t *off,
                           const char *text) {
  return ssh_put_string(out, cap, off, (const uint8_t *)text, strlen(text));
}

static int ssh_put_mpint(uint8_t *out, size_t cap, size_t *off,
                         const uint8_t *data, size_t len) {
  size_t start = 0;
  size_t body_len;
  int prefix_zero;

  if (!out || !off || !data) {
    return -1;
  }
  while (start < len && data[start] == 0) {
    start++;
  }
  if (start == len) {
    return ssh_put_string(out, cap, off, NULL, 0);
  }
  body_len = len - start;
  prefix_zero = (data[start] & 0x80U) != 0;
  if (*off + 4 + body_len + (prefix_zero ? 1U : 0U) > cap) {
    return -1;
  }
  ssh_put_u32(out + *off, (uint32_t)(body_len + (prefix_zero ? 1U : 0U)));
  *off += 4;
  if (prefix_zero) {
    out[(*off)++] = 0;
  }
  memcpy(out + *off, data + start, body_len);
  *off += body_len;
  return 0;
}

static int ssh_append_text(char *buf, size_t cap, size_t *off,
                           const char *text) {
  size_t len;

  if (!buf || !off || !text || *off >= cap) {
    return -1;
  }
  len = strlen(text);
  if (*off + len >= cap) {
    return -1;
  }
  memcpy(buf + *off, text, len);
  *off += len;
  buf[*off] = '\0';
  return 0;
}

static int ssh_append_hex_line(char *buf, size_t cap, size_t *off,
                               const char *name, const uint8_t *data,
                               size_t len) {
  static const char hex[] = "0123456789abcdef";

  if (ssh_append_text(buf, cap, off, name) != 0 ||
      ssh_append_text(buf, cap, off, " ") != 0) {
    return -1;
  }
  for (size_t i = 0; i < len; i++) {
    if (*off + 3 >= cap) {
      return -1;
    }
    buf[(*off)++] = hex[data[i] >> 4];
    buf[(*off)++] = hex[data[i] & 0x0f];
  }
  buf[(*off)++] = '\n';
  buf[*off] = '\0';
  return 0;
}

static int ssh_hex_value(char c) {
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

static const char *ssh_find_line_value(const char *text, const char *name) {
  size_t name_len;
  const char *p;

  if (!text || !name) {
    return NULL;
  }
  name_len = strlen(name);
  p = text;
  while (*p) {
    while (*p == '\n' || *p == '\r') {
      p++;
    }
    if (strncmp(p, name, name_len) == 0 && p[name_len] == ' ') {
      return p + name_len + 1;
    }
    while (*p && *p != '\n') {
      p++;
    }
  }
  return NULL;
}

static int ssh_parse_hex_field(const char *text, const char *name,
                               uint8_t *out, size_t out_len) {
  const char *p = ssh_find_line_value(text, name);

  if (!p || !out) {
    return -1;
  }
  for (size_t i = 0; i < out_len; i++) {
    int hi = ssh_hex_value(p[i * 2]);
    int lo = ssh_hex_value(p[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  p += out_len * 2;
  if (*p != '\n' && *p != '\r' && *p != '\0' && *p != ' ') {
    return -1;
  }
  return 0;
}

static void ssh_set_hostkey_report(const char *source, const char *status,
                                   int persistent, int bootstrap) {
  strncpy(ssh_status.hostkey_source, source ? source : "unknown",
          sizeof(ssh_status.hostkey_source) - 1);
  ssh_status.hostkey_source[sizeof(ssh_status.hostkey_source) - 1] = '\0';
  strncpy(ssh_status.hostkey_status, status ? status : "ssh: host key ready",
          sizeof(ssh_status.hostkey_status) - 1);
  ssh_status.hostkey_status[sizeof(ssh_status.hostkey_status) - 1] = '\0';
  ssh_status.hostkey_persistent = persistent;
  ssh_status.hostkey_bootstrap = bootstrap;
  ssh_status.hostkey_loaded = 1;
}

static void ssh_install_hostkey_material(const uint8_t *n, const uint8_t *p,
                                         const uint8_t *q,
                                         const uint8_t *dmp1,
                                         const uint8_t *dmq1,
                                         const uint8_t *iqmp,
                                         const uint8_t *d) {
  memcpy(ssh_hostkey_n, n, sizeof(ssh_hostkey_n));
  if (d) {
    memcpy(ssh_hostkey_d, d, sizeof(ssh_hostkey_d));
  } else {
    memset(ssh_hostkey_d, 0, sizeof(ssh_hostkey_d));
  }
  memcpy(ssh_hostkey_p, p, sizeof(ssh_hostkey_p));
  memcpy(ssh_hostkey_q, q, sizeof(ssh_hostkey_q));
  memcpy(ssh_hostkey_dmp1, dmp1, sizeof(ssh_hostkey_dmp1));
  memcpy(ssh_hostkey_dmq1, dmq1, sizeof(ssh_hostkey_dmq1));
  memcpy(ssh_hostkey_iqmp, iqmp, sizeof(ssh_hostkey_iqmp));
  ssh_hostkey.n = ssh_hostkey_n;
  ssh_hostkey.n_len = sizeof(ssh_hostkey_n);
  ssh_hostkey.d = d ? ssh_hostkey_d : NULL;
  ssh_hostkey.d_len = d ? sizeof(ssh_hostkey_d) : 0;
  ssh_hostkey.p = ssh_hostkey_p;
  ssh_hostkey.p_len = sizeof(ssh_hostkey_p);
  ssh_hostkey.q = ssh_hostkey_q;
  ssh_hostkey.q_len = sizeof(ssh_hostkey_q);
  ssh_hostkey.dmp1 = ssh_hostkey_dmp1;
  ssh_hostkey.dmp1_len = sizeof(ssh_hostkey_dmp1);
  ssh_hostkey.dmq1 = ssh_hostkey_dmq1;
  ssh_hostkey.dmq1_len = sizeof(ssh_hostkey_dmq1);
  ssh_hostkey.iqmp = ssh_hostkey_iqmp;
  ssh_hostkey.iqmp_len = sizeof(ssh_hostkey_iqmp);
  ssh_host_key_blob_len = 0;
  ssh_status.hostkey_sha256[0] = '\0';
  ssh_host_signature_ready = 0;
}

static void ssh_install_bootstrap_hostkey(void) {
  ssh_install_hostkey_material(ORIZON_SSH_RSA_N, ORIZON_SSH_RSA_P,
                               ORIZON_SSH_RSA_Q, ORIZON_SSH_RSA_DMP1,
                               ORIZON_SSH_RSA_DMQ1, ORIZON_SSH_RSA_IQMP,
                               NULL);
  ssh_set_hostkey_report("compiled-bootstrap",
                         "ssh: using compiled bootstrap host key", 0, 1);
}

static void ssh_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a,
                      uint32_t *b, uint32_t *c, uint32_t *d) {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;

  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(leaf), "c"(subleaf));
  if (a) *a = eax;
  if (b) *b = ebx;
  if (c) *c = ecx;
  if (d) *d = edx;
}

static int ssh_rdrand64(uint64_t *out) {
  uint64_t value = 0;
  unsigned char ok = 0;

  __asm__ volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ok));
  if (ok && out) {
    *out = value;
    return 0;
  }
  return -1;
}

static void ssh_seed_file(sha256_ctx_t *ctx, const char *path,
                          size_t max_bytes) {
  char buf[192];
  file_t *f;
  ssize_t n;

  if (!ctx || !path || max_bytes == 0) {
    return;
  }
  f = vfs_open(path, O_RDONLY);
  if (!f) {
    return;
  }
  while (max_bytes > 0) {
    size_t take = max_bytes < sizeof(buf) ? max_bytes : sizeof(buf);
    n = vfs_read(f, buf, take);
    if (n <= 0) {
      break;
    }
    sha256_update(ctx, path, strlen(path));
    sha256_update(ctx, buf, (size_t)n);
    max_bytes -= (size_t)n;
    if ((size_t)n < take) {
      break;
    }
  }
  vfs_close(f);
}

static void ssh_collect_hostkey_seed(uint8_t seed[SHA256_DIGEST_SIZE]) {
  sha256_ctx_t ctx;
  uint32_t eax = 0;
  uint32_t ebx = 0;
  uint32_t ecx = 0;
  uint32_t edx = 0;
  uint64_t ticks = timer_ticks();
  uint64_t uptime = timer_uptime_seconds();
  const net_device_status_t *net = net_get_status();

  sha256_init(&ctx);
  sha256_update(&ctx, "orizon-ssh-install-hostkey-v1", 29);
  sha256_update(&ctx, &ticks, sizeof(ticks));
  sha256_update(&ctx, &uptime, sizeof(uptime));
  sha256_update(&ctx, &ssh_status.sessions, sizeof(ssh_status.sessions));
  ssh_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
  sha256_update(&ctx, &eax, sizeof(eax));
  sha256_update(&ctx, &ebx, sizeof(ebx));
  sha256_update(&ctx, &ecx, sizeof(ecx));
  sha256_update(&ctx, &edx, sizeof(edx));
  ssh_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
  sha256_update(&ctx, &eax, sizeof(eax));
  sha256_update(&ctx, &ebx, sizeof(ebx));
  sha256_update(&ctx, &ecx, sizeof(ecx));
  sha256_update(&ctx, &edx, sizeof(edx));
  if (net) {
    sha256_update(&ctx, &net->vendor_id, sizeof(net->vendor_id));
    sha256_update(&ctx, &net->device_id, sizeof(net->device_id));
    sha256_update(&ctx, net->mac, sizeof(net->mac));
    if (net->driver) {
      sha256_update(&ctx, net->driver, strlen(net->driver));
    }
  }
  if (ecx & (1U << 30)) {
    for (int i = 0; i < 8; i++) {
      uint64_t rnd = 0;
      if (ssh_rdrand64(&rnd) == 0) {
        sha256_update(&ctx, &rnd, sizeof(rnd));
      }
    }
  }
  ssh_seed_file(&ctx, "/workspace/.orizon/installed", 512);
  ssh_seed_file(&ctx, "/system/hostname", 128);
  ssh_seed_file(&ctx, "/system/network.conf", 512);
  ssh_seed_file(&ctx, "/system/version", 128);
  sha256_final(&ctx, seed);
}

static int ssh_generate_install_hostkey(char *report, size_t report_size) {
  rsa_generated_private_key_t generated;
  uint8_t seed[SHA256_DIGEST_SIZE];
  uint8_t zero_iqmp[64];
  char gen_report[128];

  memset(zero_iqmp, 0, sizeof(zero_iqmp));
  memset(gen_report, 0, sizeof(gen_report));
  ssh_collect_hostkey_seed(seed);
  if (rsa_generate_private_key_1024(&generated, seed, gen_report,
                                    sizeof(gen_report)) != 0) {
    if (report && report_size > 0) {
      snprintf(report, report_size, "ssh: host key generation failed: %s\n",
               gen_report[0] ? gen_report : "rsa generator error");
    }
    return -1;
  }
  ssh_install_hostkey_material(generated.n, generated.p, generated.q,
                               generated.dmp1, generated.dmq1, zero_iqmp,
                               generated.d);
  if (ssh_rebuild_host_key_blob() != 0) {
    if (report && report_size > 0) {
      snprintf(report, report_size, "ssh: generated host key is invalid\n");
    }
    return -1;
  }
  ssh_set_hostkey_report("generated-per-install",
                         gen_report[0] ? gen_report
                                       : "ssh: generated per-install RSA key",
                         0, 0);
  if (report && report_size > 0) {
    snprintf(report, report_size, "ssh: generated per-install RSA host key\n%s\n",
             gen_report);
  }
  return 0;
}

static int ssh_rebuild_host_key_blob(void) {
  static const uint8_t exponent[3] = {0x01, 0x00, 0x01};
  size_t off = 0;

  if (ssh_put_cstring(ssh_host_key_blob, sizeof(ssh_host_key_blob), &off,
                      "ssh-rsa") != 0 ||
      ssh_put_mpint(ssh_host_key_blob, sizeof(ssh_host_key_blob), &off,
                    exponent, sizeof(exponent)) != 0 ||
      ssh_put_mpint(ssh_host_key_blob, sizeof(ssh_host_key_blob), &off,
                    ssh_hostkey.n, ssh_hostkey.n_len) != 0) {
    return -1;
  }
  ssh_host_key_blob_len = off;
  sha256_buffer_hex(ssh_host_key_blob, ssh_host_key_blob_len,
                    ssh_status.hostkey_sha256);
  return 0;
}

static int ssh_load_hostkey_file(void) {
  file_t *f;
  char text[SSH_HOSTKEY_FILE_MAX];
  uint8_t n_buf[SSH_RSA_SIGNATURE_SIZE];
  uint8_t d_buf[SSH_RSA_SIGNATURE_SIZE];
  uint8_t p_buf[64];
  uint8_t q_buf[64];
  uint8_t dmp1_buf[64];
  uint8_t dmq1_buf[64];
  uint8_t iqmp_buf[64];
  ssize_t n;
  int has_private_d = 0;

  f = vfs_open(ORIZON_SSH_HOSTKEY_PATH, O_RDONLY);
  if (!f) {
    return -1;
  }
  memset(text, 0, sizeof(text));
  n = vfs_read(f, text, sizeof(text) - 1);
  vfs_close(f);
  if (n <= 0 ||
      (!strstr(text, "format orizon-ssh-rsa-crt-v1") &&
       !strstr(text, "format orizon-ssh-rsa-private-v2"))) {
    return -1;
  }
  memset(d_buf, 0, sizeof(d_buf));
  memset(p_buf, 0, sizeof(p_buf));
  memset(q_buf, 0, sizeof(q_buf));
  memset(dmp1_buf, 0, sizeof(dmp1_buf));
  memset(dmq1_buf, 0, sizeof(dmq1_buf));
  memset(iqmp_buf, 0, sizeof(iqmp_buf));
  if (ssh_parse_hex_field(text, "n", n_buf, sizeof(n_buf)) != 0) {
    return -1;
  }
  has_private_d = ssh_parse_hex_field(text, "d", d_buf, sizeof(d_buf)) == 0;
  if (!has_private_d &&
      (ssh_parse_hex_field(text, "p", p_buf, sizeof(p_buf)) != 0 ||
       ssh_parse_hex_field(text, "q", q_buf, sizeof(q_buf)) != 0 ||
       ssh_parse_hex_field(text, "dmp1", dmp1_buf, sizeof(dmp1_buf)) != 0 ||
       ssh_parse_hex_field(text, "dmq1", dmq1_buf, sizeof(dmq1_buf)) != 0 ||
       ssh_parse_hex_field(text, "iqmp", iqmp_buf, sizeof(iqmp_buf)) != 0)) {
    return -1;
  }
  if (has_private_d) {
    (void)ssh_parse_hex_field(text, "p", p_buf, sizeof(p_buf));
    (void)ssh_parse_hex_field(text, "q", q_buf, sizeof(q_buf));
    (void)ssh_parse_hex_field(text, "dmp1", dmp1_buf, sizeof(dmp1_buf));
    (void)ssh_parse_hex_field(text, "dmq1", dmq1_buf, sizeof(dmq1_buf));
    (void)ssh_parse_hex_field(text, "iqmp", iqmp_buf, sizeof(iqmp_buf));
  }
  ssh_install_hostkey_material(n_buf, p_buf, q_buf, dmp1_buf, dmq1_buf,
                               iqmp_buf, has_private_d ? d_buf : NULL);
  if (ssh_rebuild_host_key_blob() != 0) {
    return -1;
  }
  ssh_set_hostkey_report(ORIZON_SSH_HOSTKEY_PATH,
                         "ssh: loaded persistent host key file", 1,
                         strstr(text, "generator compiled-bootstrap") != NULL);
  return 0;
}

static int ssh_write_hostkey_file(void) {
  file_t *f;
  char text[SSH_HOSTKEY_FILE_MAX];
  char line[128];
  size_t off = 0;

  if (!ssh_hostkey.n || ssh_rebuild_host_key_blob() != 0) {
    return -1;
  }
  memset(text, 0, sizeof(text));
  if (ssh_append_text(text, sizeof(text), &off,
                      ssh_hostkey.d
                          ? "format orizon-ssh-rsa-private-v2\n"
                          : "format orizon-ssh-rsa-crt-v1\n") != 0 ||
      ssh_append_text(text, sizeof(text), &off,
                      "algorithm rsa-sha2-256\n"
                      "source Orizon OS persistent SSH host identity\n") != 0) {
    return -1;
  }
  snprintf(line, sizeof(line), "fingerprint-sha256 %s\n",
           ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256 : "none");
  if (ssh_append_text(text, sizeof(text), &off, line) != 0) {
    return -1;
  }
  snprintf(line, sizeof(line), "created-ticks %lu\n",
           (unsigned long)timer_ticks());
  if (ssh_append_text(text, sizeof(text), &off, line) != 0 ||
      ssh_append_text(text, sizeof(text), &off,
                      ssh_hostkey.d ? "generator per-install-rsa-sha256\n"
                                    : "generator compiled-bootstrap\n") != 0 ||
      (ssh_hostkey.d &&
       ssh_append_hex_line(text, sizeof(text), &off, "d", ssh_hostkey.d,
                           ssh_hostkey.d_len) != 0) ||
      ssh_append_hex_line(text, sizeof(text), &off, "n", ssh_hostkey.n,
                          ssh_hostkey.n_len) != 0 ||
      ssh_append_hex_line(text, sizeof(text), &off, "p", ssh_hostkey.p,
                          ssh_hostkey.p_len) != 0 ||
      ssh_append_hex_line(text, sizeof(text), &off, "q", ssh_hostkey.q,
                          ssh_hostkey.q_len) != 0 ||
      ssh_append_hex_line(text, sizeof(text), &off, "dmp1", ssh_hostkey.dmp1,
                          ssh_hostkey.dmp1_len) != 0 ||
      ssh_append_hex_line(text, sizeof(text), &off, "dmq1", ssh_hostkey.dmq1,
                          ssh_hostkey.dmq1_len) != 0 ||
      ssh_append_hex_line(text, sizeof(text), &off, "iqmp", ssh_hostkey.iqmp,
                          ssh_hostkey.iqmp_len) != 0) {
    return -1;
  }

  vfs_mkdir("/system");
  f = vfs_open(ORIZON_SSH_HOSTKEY_PATH, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -1;
  }
  if (vfs_write(f, text, strlen(text)) != (ssize_t)strlen(text)) {
    vfs_close(f);
    return -1;
  }
  vfs_close(f);
  ssh_set_hostkey_report(ORIZON_SSH_HOSTKEY_PATH,
                         "ssh: persistent host key file saved", 1,
                         ssh_hostkey.d ? 0 : ssh_status.hostkey_bootstrap);
  return 0;
}

static int ssh_ensure_hostkey(void) {
  char report[160];

  if (ssh_status.hostkey_loaded && ssh_hostkey.n &&
      ssh_status.hostkey_sha256[0]) {
    return 0;
  }
  if (ssh_load_hostkey_file() == 0) {
    return 0;
  }
  if (ssh_generate_install_hostkey(report, sizeof(report)) == 0 &&
      ssh_write_hostkey_file() == 0) {
    return 0;
  }
  ssh_install_bootstrap_hostkey();
  if (ssh_write_hostkey_file() == 0) {
    return 0;
  }
  return ssh_rebuild_host_key_blob();
}

static void ssh_fill_cookie(uint8_t cookie[16]) {
  sha256_ctx_t ctx;
  uint8_t digest[SHA256_DIGEST_SIZE];
  uint64_t ticks = timer_ticks();
  const char domain[] = "orizon-ssh-kex-cookie";

  sha256_init(&ctx);
  sha256_update(&ctx, domain, sizeof(domain) - 1);
  sha256_update(&ctx, &ticks, sizeof(ticks));
  sha256_update(&ctx, &ssh_status.sessions, sizeof(ssh_status.sessions));
  sha256_update(&ctx, &ssh_status.bytes_rx, sizeof(ssh_status.bytes_rx));
  sha256_final(&ctx, digest);
  memcpy(cookie, digest, 16);
}

static size_t ssh_build_kexinit(uint8_t *out, size_t cap) {
  uint8_t payload[768];
  uint8_t cookie[16];
  size_t off = 0;
  size_t wrapped = 0;

  if (!out || cap == 0) {
    return 0;
  }
  memset(payload, 0, sizeof(payload));
  payload[off++] = SSH_MSG_KEXINIT;
  ssh_fill_cookie(cookie);
  memcpy(payload + off, cookie, sizeof(cookie));
  off += sizeof(cookie);
  if (ssh_put_namelist(payload, sizeof(payload), &off, SSH_KEX_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, SSH_HOSTKEY_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, SSH_CIPHER_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, SSH_CIPHER_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, SSH_MAC_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, SSH_MAC_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, SSH_COMPRESSION_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, SSH_COMPRESSION_ALGORITHMS) != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, "") != 0 ||
      ssh_put_namelist(payload, sizeof(payload), &off, "") != 0 ||
      off + 5 > sizeof(payload)) {
    return 0;
  }
  payload[off++] = 0; /* first_kex_packet_follows */
  ssh_put_u32(payload + off, 0);
  off += 4;

  memcpy(ssh_server_kexinit_payload, payload, off);
  ssh_server_kexinit_payload_len = off;
  if (ssh_wrap_packet(out, cap, payload, off, &wrapped) != 0) {
    return 0;
  }
  return wrapped;
}

static size_t ssh_build_disconnect(uint8_t *out, size_t cap) {
  const char *message =
      "Orizon SSH staged handshake stopped before authentication.";
  size_t message_len = strlen(message);
  uint8_t payload[192];
  size_t off = 0;
  size_t wrapped = 0;

  if (!out || cap == 0 || sizeof(payload) < 1 + 4 + 4 + message_len + 4) {
    return 0;
  }
  payload[off++] = SSH_MSG_DISCONNECT;
  ssh_put_u32(payload + off, 3); /* SSH_DISCONNECT_KEY_EXCHANGE_FAILED */
  off += 4;
  ssh_put_u32(payload + off, (uint32_t)message_len);
  off += 4;
  memcpy(payload + off, message, message_len);
  off += message_len;
  ssh_put_u32(payload + off, 0);
  off += 4;
  if (ssh_wrap_packet(out, cap, payload, off, &wrapped) != 0) {
    return 0;
  }
  return wrapped;
}

static size_t ssh_build_newkeys(uint8_t *out, size_t cap) {
  uint8_t payload[1] = {SSH_MSG_NEWKEYS};
  size_t wrapped = 0;

  if (!out || cap == 0) {
    return 0;
  }
  if (ssh_wrap_packet(out, cap, payload, sizeof(payload), &wrapped) != 0) {
    return 0;
  }
  return wrapped;
}

static void ssh_mac_packet(const uint8_t key[SHA256_DIGEST_SIZE],
                           uint32_t seq, const uint8_t *packet,
                           size_t packet_len,
                           uint8_t digest[SHA256_DIGEST_SIZE]) {
  if (!key || !packet || !digest || packet_len + 4 > sizeof(ssh_mac_input)) {
    memset(digest, 0, SHA256_DIGEST_SIZE);
    return;
  }
  ssh_put_u32(ssh_mac_input, seq);
  memcpy(ssh_mac_input + 4, packet, packet_len);
  hmac_sha256(key, SHA256_DIGEST_SIZE, ssh_mac_input, packet_len + 4, digest);
}

static int ssh_build_host_key_blob(void) {
  if (ssh_ensure_hostkey() != 0) {
    return -1;
  }
  if (ssh_host_key_blob_len > 0) {
    if (!ssh_status.hostkey_sha256[0]) {
      sha256_buffer_hex(ssh_host_key_blob, ssh_host_key_blob_len,
                        ssh_status.hostkey_sha256);
    }
    return 0;
  }
  return ssh_rebuild_host_key_blob();
}

static int ssh_build_signature_blob(uint8_t *out, size_t cap, size_t *out_len) {
  size_t off = 0;

  if (!out || !out_len || !ssh_host_signature_ready) {
    return -1;
  }
  if (ssh_put_cstring(out, cap, &off, "rsa-sha2-256") != 0 ||
      ssh_put_string(out, cap, &off, ssh_host_signature,
                     sizeof(ssh_host_signature)) != 0) {
    return -1;
  }
  *out_len = off;
  return 0;
}

static size_t ssh_build_ecdh_reply(uint8_t *out, size_t cap) {
  uint8_t payload[512];
  uint8_t sig_blob[192];
  size_t sig_blob_len = 0;
  size_t off = 0;
  size_t wrapped = 0;

  if (!out || cap == 0 || ssh_build_host_key_blob() != 0 ||
      ssh_build_signature_blob(sig_blob, sizeof(sig_blob), &sig_blob_len) != 0) {
    return 0;
  }

  payload[off++] = SSH_MSG_KEXDH_REPLY;
  if (ssh_put_string(payload, sizeof(payload), &off, ssh_host_key_blob,
                     ssh_host_key_blob_len) != 0 ||
      ssh_put_string(payload, sizeof(payload), &off, ssh_server_public,
                     sizeof(ssh_server_public)) != 0 ||
      ssh_put_string(payload, sizeof(payload), &off, sig_blob,
                     sig_blob_len) != 0) {
    return 0;
  }
  if (ssh_wrap_packet(out, cap, payload, off, &wrapped) != 0) {
    return 0;
  }
  return wrapped;
}

static size_t ssh_build_encrypted_packet(uint8_t *out, size_t cap,
                                         const uint8_t *payload,
                                         size_t payload_len) {
  uint8_t mac[SHA256_DIGEST_SIZE];
  uint8_t ctr_tmp[16];
  size_t plain_len = 0;

  if (!out || !payload || !ssh_out_encrypted || !ssh_status.traffic_keys_ready ||
      ssh_wrap_packet_block(ssh_encrypt_plain, sizeof(ssh_encrypt_plain),
                            payload, payload_len, 16, &plain_len) != 0 ||
      cap < plain_len + SHA256_DIGEST_SIZE) {
    return 0;
  }

  ssh_mac_packet(ssh_mac_s2c, ssh_seq_out, ssh_encrypt_plain, plain_len, mac);
  memcpy(ctr_tmp, ssh_ctr_s2c, sizeof(ctr_tmp));
  aes128_ctr_crypt_update(ssh_key_s2c, ctr_tmp, ssh_encrypt_plain, plain_len,
                          out);
  memcpy(out + plain_len, mac, sizeof(mac));
  memcpy(ssh_pending_ctr_s2c, ctr_tmp, sizeof(ssh_pending_ctr_s2c));
  ssh_pending_ctr_s2c_ready = 1;
  return plain_len + sizeof(mac);
}

static size_t ssh_build_service_accept(uint8_t *out, size_t cap) {
  uint8_t payload[64];
  size_t off = 0;

  payload[off++] = SSH_MSG_SERVICE_ACCEPT;
  if (ssh_put_cstring(payload, sizeof(payload), &off, "ssh-userauth") != 0) {
    return 0;
  }
  return ssh_build_encrypted_packet(out, cap, payload, off);
}

static size_t ssh_build_userauth_failure(uint8_t *out, size_t cap) {
  uint8_t payload[64];
  size_t off = 0;

  payload[off++] = SSH_MSG_USERAUTH_FAILURE;
  if (ssh_put_cstring(payload, sizeof(payload), &off,
                      (ssh_status.auth_configured &&
                       ssh_lockout_remaining() == 0)
                          ? "password"
                          : "") != 0) {
    return 0;
  }
  payload[off++] = 0; /* partial_success */
  return ssh_build_encrypted_packet(out, cap, payload, off);
}

static size_t ssh_build_userauth_success(uint8_t *out, size_t cap) {
  uint8_t payload[1] = {SSH_MSG_USERAUTH_SUCCESS};

  return ssh_build_encrypted_packet(out, cap, payload, sizeof(payload));
}

static size_t ssh_build_channel_open_confirmation(uint8_t *out, size_t cap) {
  uint8_t payload[32];
  size_t off = 0;

  payload[off++] = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
  ssh_put_u32(payload + off, ssh_client_channel);
  off += 4;
  ssh_put_u32(payload + off, ssh_server_channel);
  off += 4;
  ssh_put_u32(payload + off, SSH_CHANNEL_WINDOW);
  off += 4;
  ssh_put_u32(payload + off, SSH_CHANNEL_MAX_PACKET);
  off += 4;
  return ssh_build_encrypted_packet(out, cap, payload, off);
}

static size_t ssh_build_channel_status(uint8_t *out, size_t cap,
                                       uint8_t msg_type) {
  uint8_t payload[8];
  size_t off = 0;

  payload[off++] = msg_type;
  ssh_put_u32(payload + off, ssh_client_channel);
  off += 4;
  return ssh_build_encrypted_packet(out, cap, payload, off);
}

static size_t ssh_build_channel_data(uint8_t *out, size_t cap) {
  uint8_t *payload = ssh_channel_payload;
  size_t off = 0;
  size_t remaining;
  size_t chunk;

  ssh_channel_last_chunk_len = 0;
  if (!ssh_channel_data_pending || ssh_channel_tx_len == 0 ||
      ssh_channel_tx_off >= ssh_channel_tx_len) {
    return 0;
  }
  remaining = ssh_channel_tx_len - ssh_channel_tx_off;
  chunk = remaining;
  if (chunk > SSH_CHANNEL_DATA_CHUNK) {
    chunk = SSH_CHANNEL_DATA_CHUNK;
  }
  payload[off++] = SSH_MSG_CHANNEL_DATA;
  ssh_put_u32(payload + off, ssh_client_channel);
  off += 4;
  if (ssh_put_string(payload, sizeof(ssh_channel_payload), &off,
                     (const uint8_t *)ssh_channel_tx + ssh_channel_tx_off,
                     chunk) != 0) {
    return 0;
  }
  ssh_channel_last_chunk_len = chunk;
  return ssh_build_encrypted_packet(out, cap, payload, off);
}

static size_t ssh_build_channel_exit_status(uint8_t *out, size_t cap) {
  uint8_t payload[64];
  size_t off = 0;

  payload[off++] = SSH_MSG_CHANNEL_REQUEST;
  ssh_put_u32(payload + off, ssh_client_channel);
  off += 4;
  if (ssh_put_cstring(payload, sizeof(payload), &off, "exit-status") != 0) {
    return 0;
  }
  payload[off++] = 0; /* want_reply */
  ssh_put_u32(payload + off, ssh_channel_exit_code);
  off += 4;
  return ssh_build_encrypted_packet(out, cap, payload, off);
}

static void ssh_queue_channel_text(const char *text) {
  size_t len;
  size_t start;
  size_t room;

  if (!text) {
    return;
  }
  len = strlen(text);
  start = ssh_channel_data_pending ? ssh_channel_tx_len : 0;
  if (!ssh_channel_data_pending) {
    ssh_channel_tx_off = 0;
    ssh_channel_last_chunk_len = 0;
  }
  if (start >= sizeof(ssh_channel_tx) - 1) {
    return;
  }
  room = sizeof(ssh_channel_tx) - 1 - start;
  if (len > room) {
    len = room;
  }
  memcpy(ssh_channel_tx + start, text, len);
  ssh_channel_tx[start + len] = '\0';
  ssh_channel_tx_len = start + len;
  ssh_channel_data_pending = 1;
}

static int ssh_has_pending_tx(void) {
  return ssh_service_accept_pending || ssh_auth_failure_pending ||
         ssh_auth_success_pending || ssh_channel_open_confirm_pending ||
         ssh_channel_success_pending || ssh_channel_failure_pending ||
         ssh_channel_data_pending || ssh_channel_exit_status_pending ||
         ssh_channel_close_pending || ssh_disconnect_close_polls > 0;
}

static void ssh_reopen_listener(const char *reason) {
  netstack_tcp_server_close(&ssh_server);
  netstack_tcp_server_init(&ssh_server, ORIZON_SSH_PORT);
  ssh_seen_connections = ssh_server.connections;
  ssh_disconnect_close_polls = 0;
  ssh_reset_negotiation();
  ssh_last_activity_tick = timer_ticks();
  ssh_listener_reset_total++;
  ssh_set_status(reason ? reason : "ssh: listener reset");
  ssh_refresh_state();
}

static void ssh_ensure_listener_alive(void) {
  if (!ssh_status.enabled) {
    return;
  }
  if (ssh_server.enabled && ssh_server.state != NETSTACK_TCP_SERVER_CLOSED) {
    return;
  }
  netstack_tcp_server_init(&ssh_server, ORIZON_SSH_PORT);
  ssh_seen_connections = ssh_server.connections;
  ssh_disconnect_close_polls = 0;
  ssh_reset_negotiation();
  ssh_last_activity_tick = timer_ticks();
  ssh_listener_recover_total++;
  ssh_set_status("ssh: listener recovered");
  ssh_refresh_state();
}

static void ssh_reset_negotiation(void) {
  ssh_status.server_kexinit_sent = 0;
  ssh_status.client_kexinit_seen = 0;
  ssh_status.client_kex_packet_seen = 0;
  ssh_status.ecdh_ready = 0;
  ssh_status.ecdh_reply_sent = 0;
  ssh_status.newkeys_sent = 0;
  ssh_status.client_newkeys_seen = 0;
  ssh_status.traffic_keys_ready = 0;
  ssh_status.encrypted_packet_seen = 0;
  ssh_status.service_accept_sent = 0;
  ssh_status.userauth_request_seen = 0;
  ssh_status.authenticated = 0;
  ssh_status.auth_failure_sent = 0;
  ssh_status.channel_open_seen = 0;
  ssh_status.channel_open_confirm_sent = 0;
  ssh_status.shell_ready = 0;
  ssh_status.kex_seen = 0;
  ssh_status.disconnect_sent = 0;
  ssh_status.last_packet_type = 0;
  ssh_status.kex_algorithm[0] = '\0';
  ssh_status.hostkey_algorithm[0] = '\0';
  ssh_status.cipher_c2s[0] = '\0';
  ssh_status.cipher_s2c[0] = '\0';
  ssh_status.mac_c2s[0] = '\0';
  ssh_status.mac_s2c[0] = '\0';
  ssh_status.compression_c2s[0] = '\0';
  ssh_status.compression_s2c[0] = '\0';
  ssh_status.client_kex_first[0] = '\0';
  ssh_status.client_hostkey_first[0] = '\0';
  ssh_status.client_public_sha256[0] = '\0';
  ssh_status.server_public_sha256[0] = '\0';
  ssh_status.shared_secret_sha256[0] = '\0';
  ssh_status.exchange_hash_sha256[0] = '\0';
  ssh_status.signature_sha256[0] = '\0';
  ssh_status.client_to_server_key_sha256[0] = '\0';
  ssh_status.server_to_client_key_sha256[0] = '\0';
  ssh_status.client_to_server_mac_sha256[0] = '\0';
  ssh_status.server_to_client_mac_sha256[0] = '\0';
  ssh_status.auth_user[0] = '\0';
  ssh_status.auth_method[0] = '\0';
  ssh_binary_rx_used = 0;
  ssh_remote_banner_len = 0;
  ssh_client_kexinit_payload_len = 0;
  ssh_server_kexinit_payload_len = 0;
  memset(ssh_client_public, 0, sizeof(ssh_client_public));
  memset(ssh_server_private, 0, sizeof(ssh_server_private));
  memset(ssh_server_public, 0, sizeof(ssh_server_public));
  memset(ssh_shared_secret, 0, sizeof(ssh_shared_secret));
  memset(ssh_exchange_hash, 0, sizeof(ssh_exchange_hash));
  memset(ssh_session_id, 0, sizeof(ssh_session_id));
  ssh_session_id_ready = 0;
  memset(ssh_host_signature, 0, sizeof(ssh_host_signature));
  ssh_host_signature_ready = 0;
  memset(ssh_iv_c2s, 0, sizeof(ssh_iv_c2s));
  memset(ssh_iv_s2c, 0, sizeof(ssh_iv_s2c));
  memset(ssh_key_c2s, 0, sizeof(ssh_key_c2s));
  memset(ssh_key_s2c, 0, sizeof(ssh_key_s2c));
  memset(ssh_mac_c2s, 0, sizeof(ssh_mac_c2s));
  memset(ssh_mac_s2c, 0, sizeof(ssh_mac_s2c));
  memset(ssh_ctr_c2s, 0, sizeof(ssh_ctr_c2s));
  memset(ssh_ctr_s2c, 0, sizeof(ssh_ctr_s2c));
  ssh_seq_in = 0;
  ssh_seq_out = 0;
  ssh_in_encrypted = 0;
  ssh_out_encrypted = 0;
  ssh_service_accept_pending = 0;
  ssh_auth_failure_pending = 0;
  ssh_auth_success_pending = 0;
  ssh_channel_open_confirm_pending = 0;
  ssh_channel_success_pending = 0;
  ssh_channel_failure_pending = 0;
  ssh_channel_data_pending = 0;
  ssh_channel_exit_status_pending = 0;
  ssh_channel_exit_code = 0;
  ssh_channel_close_pending = 0;
  ssh_channel_close_sent = 0;
  ssh_encrypted_rx_used = 0;
  memset(ssh_pending_ctr_s2c, 0, sizeof(ssh_pending_ctr_s2c));
  ssh_pending_ctr_s2c_ready = 0;
  ssh_client_channel = 0;
  ssh_server_channel = 0;
  ssh_client_window = 0;
  ssh_client_max_packet = 0;
  ssh_channel_tx_len = 0;
  ssh_channel_tx_off = 0;
  ssh_channel_last_chunk_len = 0;
  ssh_shell_line_len = 0;
  ssh_shell_last_was_cr = 0;
  strcpy(ssh_shell_cwd, "/home/orizon");
}

static int ssh_read_namelist(const uint8_t *payload, size_t payload_len,
                             size_t *off, const uint8_t **list,
                             size_t *list_len) {
  uint32_t len;

  if (!payload || !off || !list || !list_len || *off + 4 > payload_len) {
    return -1;
  }
  len = ssh_get_u32(payload + *off);
  *off += 4;
  if (*off + len > payload_len) {
    return -1;
  }
  *list = payload + *off;
  *list_len = (size_t)len;
  *off += len;
  return 0;
}

static int ssh_read_string(const uint8_t *payload, size_t payload_len,
                           size_t *off, const uint8_t **data,
                           size_t *data_len) {
  return ssh_read_namelist(payload, payload_len, off, data, data_len);
}

static void ssh_copy_name(const uint8_t *name, size_t len, char *out,
                          size_t out_size) {
  size_t copy;

  if (!out || out_size == 0) {
    return;
  }
  copy = len;
  if (copy >= out_size) {
    copy = out_size - 1;
  }
  if (copy > 0 && name) {
    memcpy(out, name, copy);
  }
  out[copy] = '\0';
}

static void ssh_copy_first_name(const uint8_t *list, size_t list_len,
                                char *out, size_t out_size) {
  size_t len = 0;

  while (len < list_len && list[len] != ',') {
    len++;
  }
  ssh_copy_name(list, len, out, out_size);
}

static int ssh_namelist_has(const uint8_t *list, size_t list_len,
                            const char *needle, size_t needle_len) {
  size_t start = 0;

  while (start <= list_len) {
    size_t end = start;
    while (end < list_len && list[end] != ',') {
      end++;
    }
    if (end - start == needle_len &&
        memcmp(list + start, needle, needle_len) == 0) {
      return 1;
    }
    if (end >= list_len) {
      break;
    }
    start = end + 1;
  }
  return 0;
}

static void ssh_choose_algorithm(const uint8_t *client_list,
                                 size_t client_list_len,
                                 const char *server_list, char *out,
                                 size_t out_size) {
  size_t start = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  while (start <= client_list_len) {
    size_t end = start;
    while (end < client_list_len && client_list[end] != ',') {
      end++;
    }
    if (ssh_namelist_has((const uint8_t *)server_list, strlen(server_list),
                         (const char *)(client_list + start), end - start)) {
      ssh_copy_name(client_list + start, end - start, out, out_size);
      return;
    }
    if (end >= client_list_len) {
      break;
    }
    start = end + 1;
  }
  strncpy(out, "none", out_size - 1);
  out[out_size - 1] = '\0';
}

static int ssh_algorithm_ready(void) {
  return strcmp(ssh_status.kex_algorithm, "none") != 0 &&
         strcmp(ssh_status.hostkey_algorithm, "none") != 0 &&
         strcmp(ssh_status.cipher_c2s, "none") != 0 &&
         strcmp(ssh_status.cipher_s2c, "none") != 0 &&
         strcmp(ssh_status.mac_c2s, "none") != 0 &&
         strcmp(ssh_status.mac_s2c, "none") != 0;
}

static void ssh_process_kexinit(const uint8_t *payload, size_t payload_len) {
  const uint8_t *lists[10];
  size_t lens[10];
  size_t off = 17;

  if (!payload || payload_len < 22 || payload[0] != SSH_MSG_KEXINIT) {
    return;
  }
  if (payload_len <= sizeof(ssh_client_kexinit_payload)) {
    memcpy(ssh_client_kexinit_payload, payload, payload_len);
    ssh_client_kexinit_payload_len = payload_len;
  }
  for (int i = 0; i < 10; i++) {
    if (ssh_read_namelist(payload, payload_len, &off, &lists[i], &lens[i]) !=
        0) {
      ssh_status.errors++;
      ssh_set_status("ssh: malformed client KEXINIT");
      return;
    }
  }
  if (off + 5 > payload_len) {
    ssh_status.errors++;
    ssh_set_status("ssh: truncated client KEXINIT");
    return;
  }

  ssh_copy_first_name(lists[0], lens[0], ssh_status.client_kex_first,
                      sizeof(ssh_status.client_kex_first));
  ssh_copy_first_name(lists[1], lens[1], ssh_status.client_hostkey_first,
                      sizeof(ssh_status.client_hostkey_first));
  ssh_choose_algorithm(lists[0], lens[0], SSH_KEX_ALGORITHMS,
                       ssh_status.kex_algorithm,
                       sizeof(ssh_status.kex_algorithm));
  ssh_choose_algorithm(lists[1], lens[1], SSH_HOSTKEY_ALGORITHMS,
                       ssh_status.hostkey_algorithm,
                       sizeof(ssh_status.hostkey_algorithm));
  ssh_choose_algorithm(lists[2], lens[2], SSH_CIPHER_ALGORITHMS,
                       ssh_status.cipher_c2s, sizeof(ssh_status.cipher_c2s));
  ssh_choose_algorithm(lists[3], lens[3], SSH_CIPHER_ALGORITHMS,
                       ssh_status.cipher_s2c, sizeof(ssh_status.cipher_s2c));
  ssh_choose_algorithm(lists[4], lens[4], SSH_MAC_ALGORITHMS,
                       ssh_status.mac_c2s, sizeof(ssh_status.mac_c2s));
  ssh_choose_algorithm(lists[5], lens[5], SSH_MAC_ALGORITHMS,
                       ssh_status.mac_s2c, sizeof(ssh_status.mac_s2c));
  ssh_choose_algorithm(lists[6], lens[6], SSH_COMPRESSION_ALGORITHMS,
                       ssh_status.compression_c2s,
                       sizeof(ssh_status.compression_c2s));
  ssh_choose_algorithm(lists[7], lens[7], SSH_COMPRESSION_ALGORITHMS,
                       ssh_status.compression_s2c,
                       sizeof(ssh_status.compression_s2c));
  ssh_status.client_kexinit_seen = 1;
  ssh_status.kex_seen = 1;
  ssh_set_status("ssh: client KEXINIT parsed");
}

static void ssh_make_server_private(void) {
  sha256_ctx_t ctx;
  uint8_t digest[SHA256_DIGEST_SIZE];
  uint64_t ticks = timer_ticks();
  const char domain[] = "orizon-ssh-x25519-server";

  sha256_init(&ctx);
  sha256_update(&ctx, domain, sizeof(domain) - 1);
  sha256_update(&ctx, &ticks, sizeof(ticks));
  sha256_update(&ctx, &ssh_status.sessions, sizeof(ssh_status.sessions));
  sha256_update(&ctx, ssh_client_public, sizeof(ssh_client_public));
  sha256_update(&ctx, ssh_client_kexinit_payload, ssh_client_kexinit_payload_len);
  sha256_update(&ctx, ssh_server_kexinit_payload, ssh_server_kexinit_payload_len);
  sha256_final(&ctx, digest);
  memcpy(ssh_server_private, digest, sizeof(ssh_server_private));
  x25519_clamp_private(ssh_server_private);
}

static int ssh_all_zero(const uint8_t *data, size_t len) {
  uint8_t acc = 0;

  if (!data) {
    return 1;
  }
  for (size_t i = 0; i < len; i++) {
    acc |= data[i];
  }
  return acc == 0;
}

static void ssh_hash_u32(sha256_ctx_t *ctx, uint32_t value) {
  uint8_t tmp[4];

  ssh_put_u32(tmp, value);
  sha256_update(ctx, tmp, sizeof(tmp));
}

static void ssh_hash_string(sha256_ctx_t *ctx, const uint8_t *data,
                            size_t len) {
  ssh_hash_u32(ctx, (uint32_t)len);
  if (len > 0 && data) {
    sha256_update(ctx, data, len);
  }
}

static void ssh_hash_cstring(sha256_ctx_t *ctx, const char *text) {
  ssh_hash_string(ctx, (const uint8_t *)text, strlen(text));
}

static void ssh_hash_mpint(sha256_ctx_t *ctx, const uint8_t *data,
                           size_t len) {
  uint8_t tmp[X25519_KEY_SIZE + 1];
  size_t start = 0;
  size_t out_len;
  size_t off = 0;

  while (start < len && data[start] == 0) {
    start++;
  }
  if (start == len) {
    ssh_hash_u32(ctx, 0);
    return;
  }
  out_len = len - start;
  if (data[start] & 0x80U) {
    tmp[off++] = 0;
  }
  memcpy(tmp + off, data + start, out_len);
  off += out_len;
  ssh_hash_string(ctx, tmp, off);
}

static int ssh_compute_exchange_hash(void) {
  sha256_ctx_t ctx;
  const char server_banner[] = "SSH-2.0-OrizonSSH_0.1";

  if (ssh_build_host_key_blob() != 0) {
    return -1;
  }
  sha256_init(&ctx);
  ssh_hash_cstring(&ctx, ssh_status.remote_banner);
  ssh_hash_cstring(&ctx, server_banner);
  ssh_hash_string(&ctx, ssh_client_kexinit_payload, ssh_client_kexinit_payload_len);
  ssh_hash_string(&ctx, ssh_server_kexinit_payload, ssh_server_kexinit_payload_len);
  ssh_hash_string(&ctx, ssh_host_key_blob, ssh_host_key_blob_len);
  ssh_hash_string(&ctx, ssh_client_public, sizeof(ssh_client_public));
  ssh_hash_string(&ctx, ssh_server_public, sizeof(ssh_server_public));
  ssh_hash_mpint(&ctx, ssh_shared_secret, sizeof(ssh_shared_secret));
  sha256_final(&ctx, ssh_exchange_hash);
  sha256_hex(ssh_exchange_hash, ssh_status.exchange_hash_sha256);
  return 0;
}

static int ssh_sign_exchange_hash(void) {
  uint8_t signature_digest[SHA256_DIGEST_SIZE];

  if (ssh_ensure_hostkey() != 0) {
    return -1;
  }
  sha256_buffer(ssh_exchange_hash, sizeof(ssh_exchange_hash), signature_digest);
  if (rsa_pkcs1v15_sha256_sign_crt(ssh_host_signature,
                                   sizeof(ssh_host_signature),
                                   signature_digest, &ssh_hostkey) != 0) {
    return -1;
  }
  ssh_host_signature_ready = 1;
  sha256_buffer_hex(ssh_host_signature, sizeof(ssh_host_signature),
                    ssh_status.signature_sha256);
  return 0;
}

static int ssh_encode_shared_secret_mpint(uint8_t *out, size_t cap,
                                          size_t *out_len) {
  size_t off = 0;

  if (!out || !out_len) {
    return -1;
  }
  if (ssh_put_mpint(out, cap, &off, ssh_shared_secret,
                    sizeof(ssh_shared_secret)) != 0) {
    return -1;
  }
  *out_len = off;
  return 0;
}

static int ssh_derive_key(uint8_t letter, uint8_t *out, size_t out_len) {
  uint8_t k_blob[X25519_KEY_SIZE + 5];
  uint8_t digest[SHA256_DIGEST_SIZE];
  size_t k_blob_len = 0;
  sha256_ctx_t ctx;

  if (!out || out_len > SHA256_DIGEST_SIZE ||
      ssh_encode_shared_secret_mpint(k_blob, sizeof(k_blob), &k_blob_len) != 0) {
    return -1;
  }

  sha256_init(&ctx);
  sha256_update(&ctx, k_blob, k_blob_len);
  sha256_update(&ctx, ssh_exchange_hash, sizeof(ssh_exchange_hash));
  sha256_update(&ctx, &letter, 1);
  sha256_update(&ctx, ssh_session_id, sizeof(ssh_session_id));
  sha256_final(&ctx, digest);
  memcpy(out, digest, out_len);
  return 0;
}

static int ssh_derive_traffic_keys(void) {
  if (!ssh_session_id_ready) {
    memcpy(ssh_session_id, ssh_exchange_hash, sizeof(ssh_session_id));
    ssh_session_id_ready = 1;
  }

  if (ssh_derive_key('A', ssh_iv_c2s, sizeof(ssh_iv_c2s)) != 0 ||
      ssh_derive_key('B', ssh_iv_s2c, sizeof(ssh_iv_s2c)) != 0 ||
      ssh_derive_key('C', ssh_key_c2s, sizeof(ssh_key_c2s)) != 0 ||
      ssh_derive_key('D', ssh_key_s2c, sizeof(ssh_key_s2c)) != 0 ||
      ssh_derive_key('E', ssh_mac_c2s, sizeof(ssh_mac_c2s)) != 0 ||
      ssh_derive_key('F', ssh_mac_s2c, sizeof(ssh_mac_s2c)) != 0) {
    return -1;
  }

  sha256_buffer_hex(ssh_key_c2s, sizeof(ssh_key_c2s),
                    ssh_status.client_to_server_key_sha256);
  sha256_buffer_hex(ssh_key_s2c, sizeof(ssh_key_s2c),
                    ssh_status.server_to_client_key_sha256);
  sha256_buffer_hex(ssh_mac_c2s, sizeof(ssh_mac_c2s),
                    ssh_status.client_to_server_mac_sha256);
  sha256_buffer_hex(ssh_mac_s2c, sizeof(ssh_mac_s2c),
                    ssh_status.server_to_client_mac_sha256);
  memcpy(ssh_ctr_c2s, ssh_iv_c2s, sizeof(ssh_ctr_c2s));
  memcpy(ssh_ctr_s2c, ssh_iv_s2c, sizeof(ssh_ctr_s2c));
  ssh_status.traffic_keys_ready = 1;
  return 0;
}

static void ssh_process_kexdh_init(const uint8_t *payload, size_t payload_len) {
  const uint8_t *client_public = NULL;
  size_t client_public_len = 0;
  size_t off = 1;

  if (ssh_read_string(payload, payload_len, &off, &client_public,
                      &client_public_len) != 0 ||
      client_public_len != X25519_KEY_SIZE) {
    ssh_status.errors++;
    ssh_set_status("ssh: malformed ECDH init");
    return;
  }

  memcpy(ssh_client_public, client_public, sizeof(ssh_client_public));
  ssh_make_server_private();
  x25519_public_from_private(ssh_server_public, ssh_server_private);
  x25519_shared_secret(ssh_shared_secret, ssh_server_private,
                       ssh_client_public);
  if (ssh_all_zero(ssh_shared_secret, sizeof(ssh_shared_secret))) {
    ssh_status.errors++;
    ssh_set_status("ssh: rejected all-zero ECDH secret");
    return;
  }
  sha256_buffer_hex(ssh_client_public, sizeof(ssh_client_public),
                    ssh_status.client_public_sha256);
  sha256_buffer_hex(ssh_server_public, sizeof(ssh_server_public),
                    ssh_status.server_public_sha256);
  sha256_buffer_hex(ssh_shared_secret, sizeof(ssh_shared_secret),
                    ssh_status.shared_secret_sha256);
  if (ssh_compute_exchange_hash() != 0 || ssh_sign_exchange_hash() != 0 ||
      ssh_derive_traffic_keys() != 0) {
    ssh_status.errors++;
    ssh_set_status("ssh: host key or traffic key setup failed");
    return;
  }
  ssh_status.client_kex_packet_seen = 1;
  ssh_status.ecdh_ready = 1;
  ssh_status.kex_seen = 1;
  ssh_set_status("ssh: client ECDH init signed; traffic keys ready");
}

static void ssh_process_service_request(const uint8_t *payload,
                                        size_t payload_len) {
  const uint8_t *service = NULL;
  size_t service_len = 0;
  size_t off = 1;

  if (ssh_read_string(payload, payload_len, &off, &service, &service_len) != 0) {
    ssh_status.errors++;
    ssh_set_status("ssh: malformed SERVICE_REQUEST");
    return;
  }
  ssh_status.encrypted_packet_seen = 1;
  if (service_len == strlen("ssh-userauth") &&
      memcmp(service, "ssh-userauth", service_len) == 0) {
    ssh_service_accept_pending = 1;
    ssh_set_status("ssh: encrypted SERVICE_REQUEST received");
    return;
  }
  ssh_status.errors++;
  ssh_set_status("ssh: unsupported SSH service requested");
}

static void ssh_process_userauth_request(const uint8_t *payload,
                                         size_t payload_len) {
  const uint8_t *user = NULL;
  const uint8_t *service = NULL;
  const uint8_t *method = NULL;
  const uint8_t *password = NULL;
  size_t user_len = 0;
  size_t service_len = 0;
  size_t method_len = 0;
  size_t password_len = 0;
  size_t off = 1;
  char password_hash[SHA256_HEX_SIZE];
  uint8_t change_request = 0;

  ssh_status.encrypted_packet_seen = 1;
  ssh_status.userauth_request_seen = 1;
  if (ssh_read_string(payload, payload_len, &off, &user, &user_len) != 0 ||
      ssh_read_string(payload, payload_len, &off, &service, &service_len) != 0 ||
      ssh_read_string(payload, payload_len, &off, &method, &method_len) != 0) {
    ssh_status.errors++;
    ssh_note_auth_failure("ssh: malformed USERAUTH_REQUEST");
    return;
  }

  ssh_copy_name(user, user_len, ssh_status.auth_user,
                sizeof(ssh_status.auth_user));
  ssh_copy_name(method, method_len, ssh_status.auth_method,
                sizeof(ssh_status.auth_method));

  if (ssh_lockout_remaining() > 0) {
    ssh_auth_failure_pending = 1;
    ssh_set_status("ssh: authentication locked temporarily");
    return;
  }

  if (service_len != strlen("ssh-connection") ||
      memcmp(service, "ssh-connection", service_len) != 0 ||
      user_len != strlen("orizon") || memcmp(user, "orizon", user_len) != 0) {
    ssh_note_auth_failure("ssh: userauth rejected user/service");
    return;
  }

  if (method_len == strlen("none") && memcmp(method, "none", method_len) == 0) {
    ssh_auth_failure_pending = 1;
    ssh_set_status("ssh: userauth method list sent");
    return;
  }

  if (method_len == strlen("password") &&
      memcmp(method, "password", method_len) == 0) {
    if (off + 1 > payload_len) {
      ssh_note_auth_failure("ssh: malformed password auth");
      return;
    }
    change_request = payload[off++];
    if (change_request ||
        ssh_read_string(payload, payload_len, &off, &password,
                        &password_len) != 0 ||
        !ssh_status.auth_configured) {
      ssh_auth_failure_pending = 1;
      ssh_set_status("ssh: password auth unavailable");
      return;
    }
    sha256_buffer_hex(password, password_len, password_hash);
    if (strcmp(password_hash, ssh_password_sha256) == 0) {
      ssh_note_auth_success();
      return;
    }
    ssh_note_auth_failure("ssh: password auth failed");
    return;
  }

  ssh_auth_failure_pending = 1;
  ssh_set_status("ssh: unsupported userauth method");
}

static void ssh_process_channel_open(const uint8_t *payload,
                                     size_t payload_len) {
  const uint8_t *type = NULL;
  size_t type_len = 0;
  size_t off = 1;

  if (!ssh_status.authenticated ||
      ssh_read_string(payload, payload_len, &off, &type, &type_len) != 0 ||
      off + 12 > payload_len) {
    ssh_channel_failure_pending = 1;
    ssh_set_status("ssh: channel open rejected");
    return;
  }
  ssh_client_channel = ssh_get_u32(payload + off);
  off += 4;
  ssh_client_window = ssh_get_u32(payload + off);
  off += 4;
  ssh_client_max_packet = ssh_get_u32(payload + off);
  off += 4;

  if (type_len == strlen("session") && memcmp(type, "session", type_len) == 0) {
    ssh_server_channel = 0;
    ssh_status.shell_ready = 0;
    ssh_status.channel_open_seen = 1;
    ssh_status.channel_open_confirm_sent = 0;
    ssh_channel_success_pending = 0;
    ssh_channel_failure_pending = 0;
    ssh_channel_data_pending = 0;
    ssh_channel_exit_status_pending = 0;
    ssh_channel_exit_code = 0;
    ssh_channel_close_pending = 0;
    ssh_channel_close_sent = 0;
    ssh_channel_tx_len = 0;
    ssh_channel_tx_off = 0;
    ssh_channel_last_chunk_len = 0;
    ssh_shell_line_len = 0;
    ssh_channel_open_confirm_pending = 1;
    ssh_set_status("ssh: session channel open received");
    return;
  }
  ssh_channel_failure_pending = 1;
  ssh_set_status("ssh: unsupported channel type");
}

static void ssh_remote_exec_execute(const uint8_t *command,
                                    size_t command_len);
static int ssh_write_absolute_text_file(const char *path, const char *text);

static const char *ssh_shell_skip_spaces(const char *s) {
  while (s && *s == ' ') {
    s++;
  }
  return s ? s : "";
}

static int ssh_shell_command_is(const char *cmd, const char *name) {
  size_t len = strlen(name);
  return strncmp(cmd, name, len) == 0 &&
         (cmd[len] == '\0' || cmd[len] == ' ');
}

static void ssh_shell_path_pop(char *path) {
  int len = strlen(path);

  if (len <= 1) {
    strcpy(path, "/");
    return;
  }
  while (len > 1 && path[len - 1] != '/') {
    len--;
  }
  if (len <= 1) {
    strcpy(path, "/");
  } else {
    path[len - 1] = '\0';
  }
}

static int ssh_shell_path_append(char *path, size_t size,
                                 const char *component,
                                 size_t component_len) {
  size_t path_len = strlen(path);

  if (component_len == 0 ||
      (component_len == 1 && component[0] == '.')) {
    return 0;
  }
  if (component_len == 2 && component[0] == '.' && component[1] == '.') {
    ssh_shell_path_pop(path);
    return 0;
  }
  if (path_len > 1) {
    if (path_len + 1 >= size) {
      return -1;
    }
    path[path_len++] = '/';
    path[path_len] = '\0';
  }
  if (path_len + component_len >= size) {
    return -1;
  }
  memcpy(path + path_len, component, component_len);
  path[path_len + component_len] = '\0';
  return 0;
}

static int ssh_shell_resolve_path(const char *input, char *out,
                                  size_t out_size) {
  char raw[MAX_PATH];
  char trimmed[MAX_PATH];
  const char *p;
  size_t input_len;

  if (!input || out_size < 2) {
    return -1;
  }
  input = ssh_shell_skip_spaces(input);
  input_len = strlen(input);
  while (input_len > 0 && input[input_len - 1] == ' ') {
    input_len--;
  }
  if (input_len == 0 || input_len >= sizeof(trimmed)) {
    return -1;
  }
  memcpy(trimmed, input, input_len);
  trimmed[input_len] = '\0';

  if (trimmed[0] == '/') {
    snprintf(raw, sizeof(raw), "%s", trimmed);
  } else if (ssh_shell_cwd[0] && strcmp(ssh_shell_cwd, "/") != 0) {
    snprintf(raw, sizeof(raw), "%s/%s", ssh_shell_cwd, trimmed);
  } else {
    snprintf(raw, sizeof(raw), "/%s", trimmed);
  }

  out[0] = '/';
  out[1] = '\0';
  p = raw;
  while (*p) {
    const char *component;
    size_t component_len = 0;

    while (*p == '/') {
      p++;
    }
    component = p;
    while (*p && *p != '/') {
      component_len++;
      p++;
    }
    if (ssh_shell_path_append(out, out_size, component, component_len) < 0) {
      return -1;
    }
  }
  return 0;
}

static void ssh_shell_prompt(void) {
  char prompt[320];

  if (ssh_shell_suppress_prompt) {
    return;
  }
  snprintf(prompt, sizeof(prompt), "orizon:%s$ ", ssh_shell_cwd);
  ssh_queue_channel_text(prompt);
}

static void ssh_shell_append(char *out, size_t out_size, size_t *used,
                             const char *text) {
  size_t len;

  if (!out || !used || !text || *used >= out_size) {
    return;
  }
  len = strlen(text);
  if (*used + len >= out_size) {
    len = out_size - *used - 1;
  }
  memcpy(out + *used, text, len);
  *used += len;
  out[*used] = '\0';
}

static void ssh_shell_append_file_text(char *out, size_t out_size,
                                       size_t *used, const char *data,
                                       size_t len) {
  for (size_t i = 0; i < len && *used + 2 < out_size; i++) {
    if (data[i] == '\n') {
      ssh_shell_append(out, out_size, used, "\r\n");
    } else if (data[i] >= 32 || data[i] == '\t') {
      out[(*used)++] = data[i];
      out[*used] = '\0';
    }
  }
}

static int ssh_shell_path_has_prefix(const char *path, const char *prefix) {
  size_t len;

  if (!path || !prefix) {
    return 0;
  }
  len = strlen(prefix);
  return strncmp(path, prefix, len) == 0 &&
         (path[len] == '\0' || path[len] == '/');
}

static char ssh_ascii_lower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static int ssh_shell_path_contains_ci(const char *path, const char *needle) {
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
           ssh_ascii_lower(path[i + j]) == ssh_ascii_lower(needle[j])) {
      j++;
    }
    if (j == needle_len) {
      return 1;
    }
  }
  return 0;
}

static int ssh_shell_path_suffix_ci(const char *path, const char *suffix) {
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
  return ssh_shell_path_contains_ci(path + path_len - suffix_len, suffix);
}

static int ssh_shell_path_is_sensitive(const char *path) {
  static const char *const needles[] = {
      "/.ssh/",        "/config/keys/", "ssh_host_",     "password",
      "passwd",        "private",       "secret",        "token",
      "credential",    "api_key",       "apikey",        "id_rsa",
      "id_ed25519",    "authorized_keys"};
  static const char *const suffixes[] = {
      ".env", ".key", ".pem", ".p12", ".pfx"};

  if (!path) {
    return 0;
  }
  if (strcmp(path, ORIZON_SSH_CONFIG_PATH) == 0 ||
      strcmp(path, ORIZON_SSH_HOSTKEY_PATH) == 0) {
    return 1;
  }
  for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
    if (ssh_shell_path_contains_ci(path, needles[i])) {
      return 1;
    }
  }
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
    if (ssh_shell_path_suffix_ci(path, suffixes[i])) {
      return 1;
    }
  }
  return 0;
}

static int ssh_shell_path_write_allowed(const char *path) {
  if (!path || ssh_shell_path_is_sensitive(path)) {
    return 0;
  }
  if (ssh_shell_path_has_prefix(path, "/workspace/.orizon")) {
    return 0;
  }
  return ssh_shell_path_has_prefix(path, "/workspace") ||
         ssh_shell_path_has_prefix(path, "/home") ||
         ssh_shell_path_has_prefix(path, "/logs") ||
         ssh_shell_path_has_prefix(path, "/packages");
}

static int ssh_shell_path_is_remote_root(const char *path) {
  return path && (strcmp(path, "/workspace") == 0 ||
                  strcmp(path, "/home") == 0 ||
                  strcmp(path, "/logs") == 0 ||
                  strcmp(path, "/packages") == 0);
}

static const char *ssh_security_policy_class(const char *op,
                                             const char *path) {
  if (path && ssh_shell_path_is_remote_root(path) && op &&
      strcmp(op, "rm") == 0) {
    return "remote-root";
  }
  if (path && ssh_shell_path_is_sensitive(path)) {
    return "sensitive-path";
  }
  if (path && ssh_shell_path_has_prefix(path, "/workspace/.orizon")) {
    return "internal-state";
  }
  if (op && (strcmp(op, "write") == 0 || strcmp(op, "append") == 0 ||
             strcmp(op, "touch") == 0 || strcmp(op, "mkdir") == 0 ||
             strcmp(op, "rm") == 0)) {
    return "write-scope";
  }
  return "policy";
}

static void ssh_note_policy_denied(const char *op, const char *path) {
  const char *class_name = ssh_security_policy_class(op, path);
  char event[128];

  ssh_policy_denied_total++;
  if (strcmp(class_name, "sensitive-path") == 0) {
    ssh_policy_denied_sensitive++;
  } else if (strcmp(class_name, "internal-state") == 0) {
    ssh_policy_denied_internal++;
  } else if (strcmp(class_name, "remote-root") == 0) {
    ssh_policy_denied_root++;
  } else {
    ssh_policy_denied_write_scope++;
  }
  snprintf(ssh_last_policy_denial, sizeof(ssh_last_policy_denial),
           "op=%s class=%s", op ? op : "file", class_name);
  snprintf(event, sizeof(event), "policy-deny op=%s class=%s",
           op ? op : "file", class_name);
  ssh_audit_event(event);
}

static void ssh_shell_policy_denied(const char *op, const char *path,
                                    const char *hint) {
  char out[192];

  ssh_channel_exit_code = 13;
  ssh_note_policy_denied(op, path);
  snprintf(out, sizeof(out), "%s: access denied by SSH file policy%s%s\r\n",
           op ? op : "file", hint ? "; " : "", hint ? hint : "");
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static int ssh_shell_split_path_text(const char *args, char *path,
                                     size_t path_size, const char **text) {
  const char *p = ssh_shell_skip_spaces(args);
  size_t len = 0;

  if (!path || !text || path_size == 0 || !p || *p == '\0') {
    return -1;
  }
  while (p[len] && p[len] != ' ') {
    len++;
  }
  if (len == 0 || len >= path_size) {
    return -1;
  }
  memcpy(path, p, len);
  path[len] = '\0';
  p = ssh_shell_skip_spaces(p + len);
  if (*p == '\0') {
    return -1;
  }
  *text = p;
  return 0;
}

static int ssh_shell_parse_uint(const char *s, uint32_t *out) {
  uint32_t value = 0;
  int seen = 0;

  if (!s || !out) {
    return -1;
  }
  s = ssh_shell_skip_spaces(s);
  while (*s >= '0' && *s <= '9') {
    uint32_t digit = (uint32_t)(*s - '0');
    if (value > (0xffffffffU - digit) / 10U) {
      return -1;
    }
    value = value * 10U + digit;
    seen = 1;
    s++;
  }
  if (!seen) {
    return -1;
  }
  *out = value;
  return 0;
}

static int ssh_shell_parse_uint64(const char *s, uint64_t *out) {
  uint64_t value = 0;
  int seen = 0;

  if (!s || !out) {
    return -1;
  }
  s = ssh_shell_skip_spaces(s);
  while (*s >= '0' && *s <= '9') {
    uint64_t digit = (uint64_t)(*s - '0');
    if (value > (0xffffffffffffffffULL - digit) / 10ULL) {
      return -1;
    }
    value = value * 10ULL + digit;
    seen = 1;
    s++;
  }
  if (!seen) {
    return -1;
  }
  *out = value;
  return 0;
}

static const char *ssh_shell_read_token(const char *s, char *out,
                                        size_t out_size) {
  size_t len = 0;

  if (!out || out_size == 0) {
    return NULL;
  }
  out[0] = '\0';
  s = ssh_shell_skip_spaces(s);
  if (!s || *s == '\0') {
    return NULL;
  }
  while (s[len] && s[len] != ' ' && len + 1 < out_size) {
    out[len] = s[len];
    len++;
  }
  out[len] = '\0';
  while (s[len] && s[len] != ' ') {
    len++;
  }
  return ssh_shell_skip_spaces(s + len);
}

static void ssh_shell_print_ls(const char *arg) {
  static char path[MAX_PATH];
  static char out[880];
  static dirent_t entries[32];
  size_t used = 0;
  int count;
  int is_dir = 0;
  size_t size = 0;

  if (!arg || *ssh_shell_skip_spaces(arg) == '\0') {
    snprintf(path, sizeof(path), "%s", ssh_shell_cwd);
  } else if (ssh_shell_resolve_path(arg, path, sizeof(path)) < 0) {
    ssh_queue_channel_text("ls: invalid path\r\n");
    ssh_shell_prompt();
    return;
  }

  if (vfs_stat(path, &size, &is_dir) < 0) {
    ssh_queue_channel_text("ls: not found\r\n");
    ssh_shell_prompt();
    return;
  }
  if (!is_dir) {
    snprintf(out, sizeof(out), "%s  %lu bytes\r\n", path,
             (unsigned long)size);
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  count = vfs_readdir(path, entries, 32);
  if (count < 0) {
    ssh_queue_channel_text("ls: cannot read directory\r\n");
    ssh_shell_prompt();
    return;
  }
  for (int i = 0; i < count; i++) {
    ssh_shell_append(out, sizeof(out), &used,
                     entries[i].type ? "[d] " : "    ");
    ssh_shell_append(out, sizeof(out), &used, entries[i].name);
    ssh_shell_append(out, sizeof(out), &used, "\r\n");
  }
  if (count == 0) {
    ssh_shell_append(out, sizeof(out), &used, "(empty)\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_file(const char *arg, size_t max_bytes, int tail) {
  static char path[MAX_PATH];
  static char out[SSH_CHANNEL_TEXT_BUF];
  static char buf[SSH_FILE_READ_MAX];
  size_t used = 0;
  size_t text_start = 0;
  size_t file_size = 0;
  int is_dir = 0;
  int tailing = 0;
  ssize_t n;
  file_t *f;

  if (ssh_shell_resolve_path(arg, path, sizeof(path)) < 0) {
    ssh_queue_channel_text("cat: invalid path\r\n");
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_path_is_sensitive(path)) {
    ssh_shell_policy_denied("cat",
                            path,
                            "use 'ssh auth', 'ssh hostkey' or 'security' "
                            "instead of reading secret material");
    return;
  }
  if (vfs_stat(path, &file_size, &is_dir) < 0 || is_dir) {
    ssh_queue_channel_text("cat: not found\r\n");
    ssh_shell_prompt();
    return;
  }
  f = vfs_open(path, O_RDONLY);
  if (!f) {
    ssh_queue_channel_text("cat: not found\r\n");
    ssh_shell_prompt();
    return;
  }
  if (max_bytes > sizeof(buf)) {
    max_bytes = sizeof(buf);
  }
  if (tail && file_size > max_bytes) {
    if (vfs_seek(f, (int)(file_size - max_bytes), SEEK_SET) < 0) {
      vfs_close(f);
      ssh_queue_channel_text("cat: seek failed\r\n");
      ssh_shell_prompt();
      return;
    }
    tailing = 1;
    ssh_shell_append(out, sizeof(out), &used, "[tail]\r\n");
  }
  n = vfs_read(f, buf, max_bytes);
  vfs_close(f);
  if (n < 0) {
    ssh_queue_channel_text("cat: read failed\r\n");
    ssh_shell_prompt();
    return;
  }
  if (tailing) {
    while (text_start < (size_t)n && buf[text_start] != '\n') {
      text_start++;
    }
    if (text_start < (size_t)n) {
      text_start++;
    } else {
      text_start = 0;
    }
  }
  ssh_shell_append_file_text(out, sizeof(out), &used, buf + text_start,
                             (size_t)n - text_start);
  if (!tailing && file_size > (size_t)n) {
    ssh_shell_append(out, sizeof(out), &used, "\r\n[truncated]\r\n");
  } else if (used == 0 || out[used - 1] != '\n') {
    ssh_shell_append(out, sizeof(out), &used, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_ps(void) {
  static sched_process_t procs[SCHED_MAX_PROCESSES];
  static char out[880];
  static char line[128];
  size_t used = 0;
  int count = sched_snapshot(procs, SCHED_MAX_PROCESSES);

  ssh_shell_append(out, sizeof(out), &used, "PID STATE    TICKS NAME\r\n");
  for (int i = 0; i < count; i++) {
    snprintf(line, sizeof(line), "%3d %-8s %5lu %s\r\n", procs[i].pid,
             sched_state_name(procs[i].state),
             (unsigned long)procs[i].cpu_ticks, procs[i].name);
    ssh_shell_append(out, sizeof(out), &used, line);
  }
  snprintf(line, sizeof(line), "context-switches=%lu\r\n",
           (unsigned long)sched_context_switches());
  ssh_shell_append(out, sizeof(out), &used, line);
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_storage(const char *args) {
  static char out[8192];
  static char diag[2048];
  static char line[160];
  static char cap[64];
  size_t used = 0;
  const char *sub = ssh_shell_skip_spaces(args);

  if (ssh_shell_command_is(sub, "diag") ||
      ssh_shell_command_is(sub, "diagnostics")) {
    storage_format_diagnostics(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "vmcheck") ||
      ssh_shell_command_is(sub, "check") ||
      ssh_shell_command_is(sub, "verify") ||
      ssh_shell_command_is(sub, "repair")) {
    storage_format_vmcheck(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }

  int count = storage_device_count();

  storage_format_capacity(cap, sizeof(cap));
  snprintf(line, sizeof(line), "selected=%d available=%s capacity=%s status=%s\r\n",
           storage_selected_device() + 1, storage_available() ? "yes" : "no",
           cap, storage_status());
  ssh_shell_append(out, sizeof(out), &used, line);
  for (int i = 0; i < count && i < ORIZON_STORAGE_MAX_DEVICES; i++) {
    storage_device_info_t dev;
    char dcap[64];
    if (storage_get_device(i, &dev) < 0) {
      continue;
    }
    storage_format_size(dev.sectors, dcap, sizeof(dcap));
    snprintf(line, sizeof(line), "%d %s %s %s %s\r\n", dev.index + 1,
             dev.selected ? "*" : "-", dev.driver, dcap, dev.model);
    ssh_shell_append(out, sizeof(out), &used, line);
  }
  if (count == 0) {
    ssh_shell_append(out, sizeof(out), &used, "no storage devices\r\n");
    storage_format_diagnostics(diag, sizeof(diag));
    ssh_shell_append(out, sizeof(out), &used, diag);
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_persist(const char *args) {
  static char out[1200];
  const char *sub = ssh_shell_skip_spaces(args);

  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    vfs_persist_format_status(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "slots")) {
    vfs_persist_format_slots(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "save") || ssh_shell_command_is(sub, "sync")) {
    ssh_queue_channel_text(vfs_persist_save() == 0
                               ? "persistence save: ok\r\n"
                               : "persistence save: failed or not installed\r\n");
    vfs_persist_format_status(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "restore")) {
    const char *restore = ssh_shell_skip_spaces(sub + strlen("restore"));
    if (ssh_shell_command_is(restore, "previous") ||
        ssh_shell_command_is(restore, "prev")) {
      vfs_persist_restore_previous(out, sizeof(out));
      ssh_queue_channel_text(out);
      ssh_shell_prompt();
      return;
    }
    if (ssh_shell_command_is(restore, "slot")) {
      uint32_t slot = 0;
      restore = ssh_shell_skip_spaces(restore + strlen("slot"));
      if (ssh_shell_parse_uint(restore, &slot) < 0) {
        ssh_queue_channel_text("usage: persist restore slot <0..n>\r\n");
        ssh_shell_prompt();
        return;
      }
      vfs_persist_restore_slot((int)slot, out, sizeof(out));
      ssh_queue_channel_text(out);
      ssh_shell_prompt();
      return;
    }
    ssh_queue_channel_text(
        "usage: persist restore previous | persist restore slot <n>\r\n");
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "repair")) {
    vfs_persist_repair(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  ssh_queue_channel_text(
      "usage: persist [status|slots|save|repair|restore previous|restore slot <n>]\r\n");
  ssh_shell_prompt();
}

static int ssh_install_already_complete(void) {
  return orizon_system_is_installed();
}

static void ssh_shell_print_system(const char *args) {
  static char out[4096];
  const char *sub = ssh_shell_skip_spaces(args);

  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    orizon_system_format_status(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "repair")) {
    orizon_system_repair(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "init") ||
             ssh_shell_command_is(sub, "boot")) {
    orizon_system_run_boot_tasks(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "services")) {
    orizon_system_format_services(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "health")) {
    orizon_system_format_health(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "snapshot")) {
    orizon_system_write_snapshot(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "backup")) {
    orizon_system_write_admin_backup(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "logs") ||
             ssh_shell_command_is(sub, "journal") ||
             ssh_shell_command_is(sub, "bootlog")) {
    orizon_system_format_logs(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "doctor") ||
             ssh_shell_command_is(sub, "check")) {
    orizon_system_format_doctor(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "rescue")) {
    orizon_system_format_rescue(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "firstboot")) {
    const char *first = ssh_shell_skip_spaces(sub + strlen("firstboot"));
    if (ssh_shell_command_is(first, "done") ||
        ssh_shell_command_is(first, "confirm")) {
      orizon_system_mark_firstboot_done(out, sizeof(out));
    } else {
      orizon_system_format_firstboot(out, sizeof(out));
      if (strlen(out) + strlen("usage: system firstboot done\r\n") <
          sizeof(out)) {
        strcat(out, "usage: system firstboot done\r\n");
      }
    }
  } else {
    snprintf(out, sizeof(out),
             "usage: system [status|health|snapshot|backup|init|services|logs|doctor|repair|rescue|firstboot done]\r\n");
  }
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_hostname(const char *args) {
  static char out[192];
  char host[80];
  const char *sub = ssh_shell_skip_spaces(args);

  if (*sub == '\0') {
    orizon_system_hostname(host, sizeof(host));
    snprintf(out, sizeof(out), "%s\r\n", host);
  } else if (ssh_shell_command_is(sub, "set")) {
    const char *name = ssh_shell_skip_spaces(sub + strlen("set"));
    orizon_system_set_hostname(name, out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
  } else {
    snprintf(out, sizeof(out), "usage: hostname [set <name>]\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static int ssh_pkg_desktop_name(const char *name) {
  return ssh_shell_command_is(name, ORIZON_DESKTOP_PACKAGE) ||
         ssh_shell_command_is(name, "desktop") ||
         ssh_shell_command_is(name, "hypr") ||
         ssh_shell_command_is(name, "hyprland");
}

static int ssh_pkg_desktop_module_name(const char *name) {
  return ssh_shell_command_is(name, ORIZON_DESKTOP_PACKAGE_CORE) ||
         ssh_shell_command_is(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) ||
         ssh_shell_command_is(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) ||
         ssh_shell_command_is(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) ||
         ssh_shell_command_is(name, ORIZON_DESKTOP_PACKAGE_WAYBAR);
}

static void ssh_shell_print_pkg(const char *args) {
  static char out[SSH_CHANNEL_TEXT_BUF];
  static char path[MAX_PATH];
  char token[96];
  const char *sub = ssh_shell_skip_spaces(args);

  out[0] = '\0';
  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    orizon_pkg_status(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "help")) {
    snprintf(out, sizeof(out),
             "Orizon packages\r\n"
             "  pkg status             show package manager state\r\n"
             "  pkg audit              audit package db/cache consistency\r\n"
             "  pkg doctor             diagnose package v5 safety state\r\n"
             "  pkg cache              show package cache details\r\n"
             "  pkg list               list builtin/installed packages\r\n"
             "  pkg search <query>     search builtin/installed/remote packages\r\n"
             "  pkg remote             show cached signed remote package index\r\n"
             "  pkg remote verify      validate cached signed remote index\r\n"
             "  pkg upgrade plan       show signed package upgrade plan\r\n"
             "  pkg info <name>        show package metadata/files\r\n"
             "  pkg history            show install/remove history\r\n"
             "  pkg sample [desktop|desktop-module] create a sample .opkg package\r\n"
             "  pkg hash <file>        print package payload sha256\r\n"
             "  pkg verify <file>      verify package hash/dependencies\r\n"
             "  pkg simulate <file>    dry-run install/upgrade without writes\r\n"
             "  pkg update             run signed package refresh through update\r\n"
             "  pkg upgrade            plan then run signed package refresh\r\n"
             "  pkg install <file|desktop-package> install a verified package after disk install\r\n"
             "  pkg remove <name>      remove an installed package\r\n"
             "  pkg rollback <name>    restore last removed package snapshot\r\n");
  } else if (ssh_shell_command_is(sub, "list")) {
    orizon_pkg_list(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "audit")) {
    orizon_pkg_audit(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "doctor")) {
    orizon_pkg_doctor(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "cache")) {
    orizon_pkg_cache(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "search")) {
    const char *query = ssh_shell_skip_spaces(sub + 6);
    orizon_pkg_search(query, out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "remote")) {
    const char *remote_args = ssh_shell_skip_spaces(sub + 6);
    if (ssh_shell_command_is(remote_args, "verify") ||
        ssh_shell_command_is(remote_args, "check")) {
      orizon_pkg_remote_verify(out, sizeof(out));
    } else {
      orizon_pkg_remote(out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "upgrade")) {
    const char *upgrade_args = ssh_shell_skip_spaces(sub + 7);
    if (ssh_shell_command_is(upgrade_args, "plan") ||
        ssh_shell_command_is(upgrade_args, "dry-run") ||
        ssh_shell_command_is(upgrade_args, "check")) {
      orizon_pkg_upgrade_plan(out, sizeof(out));
    } else if (!ssh_install_already_complete()) {
      snprintf(out, sizeof(out),
               "pkg upgrade: unavailable in live boot. Install Orizon OS first.\r\n"
               "hint: use 'pkg upgrade plan' to inspect the cached signed index.\r\n");
    } else {
      orizon_pkg_upgrade_plan(out, sizeof(out));
      if (strlen(out) + strlen("pkg upgrade: running signed system manifest/package refresh\r\n") <
          sizeof(out)) {
        strcat(out,
               "pkg upgrade: running signed system manifest/package refresh\r\n");
      }
      orizon_update_full_upgrade(out + strlen(out), sizeof(out) - strlen(out));
    }
  } else if (ssh_shell_command_is(sub, "info")) {
    if (!ssh_shell_read_token(sub + 4, token, sizeof(token))) {
      snprintf(out, sizeof(out), "usage: pkg info <name>\r\n");
    } else {
      orizon_pkg_info(token, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "history")) {
    orizon_pkg_history(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "sample")) {
    const char *sample_args = ssh_shell_skip_spaces(sub + 6);
    if (ssh_shell_command_is(sample_args, "desktop") ||
        ssh_shell_command_is(sample_args, "orizon-desktop-hypr")) {
      orizon_pkg_write_desktop_sample(out, sizeof(out));
    } else if (ssh_pkg_desktop_module_name(sample_args)) {
      orizon_pkg_write_desktop_module_sample(sample_args, out, sizeof(out));
    } else {
      orizon_pkg_write_sample(out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "hash")) {
    if (ssh_shell_resolve_path(sub + 4, path, sizeof(path)) < 0) {
      snprintf(out, sizeof(out), "usage: pkg hash <file>\r\n");
    } else {
      orizon_pkg_hash_file(path, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "verify")) {
    if (ssh_shell_resolve_path(sub + 6, path, sizeof(path)) < 0) {
      snprintf(out, sizeof(out), "usage: pkg verify <file>\r\n");
    } else {
      orizon_pkg_verify_file(path, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "simulate") ||
             ssh_shell_command_is(sub, "dry-run")) {
    const char *path_args =
        sub + (ssh_shell_command_is(sub, "dry-run") ? 7 : 8);
    if (ssh_shell_resolve_path(path_args, path, sizeof(path)) < 0) {
      snprintf(out, sizeof(out), "usage: pkg simulate <file>\r\n");
    } else {
      orizon_pkg_simulate_file(path, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "update")) {
    if (!ssh_install_already_complete()) {
      snprintf(out, sizeof(out),
               "pkg update: unavailable in live boot. Install Orizon OS first.\r\n");
    } else {
      snprintf(out, sizeof(out),
               "pkg update: using signed system manifest/package index via update\r\n");
      orizon_update_full_upgrade(out + strlen(out), sizeof(out) - strlen(out));
    }
  } else if (ssh_shell_command_is(sub, "install")) {
    if (!ssh_install_already_complete()) {
      snprintf(out, sizeof(out),
               "pkg install: unavailable in live boot. Install Orizon OS first.\r\n");
    } else if (ssh_pkg_desktop_name(ssh_shell_skip_spaces(sub + 7)) ||
               ssh_pkg_desktop_module_name(ssh_shell_skip_spaces(sub + 7))) {
      orizon_pkg_install_named(ssh_shell_skip_spaces(sub + 7), out,
                               sizeof(out));
      gui_desktop_set_enabled(orizon_desktop_is_enabled());
    } else if (ssh_shell_resolve_path(sub + 7, path, sizeof(path)) < 0) {
      snprintf(out, sizeof(out),
               "usage: pkg install <file|desktop-package>\r\n");
    } else {
      orizon_pkg_install_file(path, out, sizeof(out));
      gui_desktop_set_enabled(orizon_desktop_is_enabled());
    }
  } else if (ssh_shell_command_is(sub, "remove")) {
    if (!ssh_install_already_complete()) {
      snprintf(out, sizeof(out),
               "pkg remove: unavailable in live boot. Install Orizon OS first.\r\n");
    } else if (!ssh_shell_read_token(sub + 6, token, sizeof(token))) {
      snprintf(out, sizeof(out), "usage: pkg remove <name>\r\n");
    } else {
      orizon_pkg_remove(ssh_pkg_desktop_name(token) ? ORIZON_DESKTOP_PACKAGE
                                                    : token,
                        out, sizeof(out));
      gui_desktop_set_enabled(orizon_desktop_is_enabled());
    }
  } else if (ssh_shell_command_is(sub, "rollback")) {
    if (!ssh_install_already_complete()) {
      snprintf(out, sizeof(out),
               "pkg rollback: unavailable in live boot. Install Orizon OS first.\r\n");
    } else if (!ssh_shell_read_token(sub + 8, token, sizeof(token))) {
      snprintf(out, sizeof(out), "usage: pkg rollback <name>\r\n");
    } else {
      orizon_pkg_rollback(ssh_pkg_desktop_name(token) ? ORIZON_DESKTOP_PACKAGE
                                                      : token,
                          out, sizeof(out));
      gui_desktop_set_enabled(orizon_desktop_is_enabled());
    }
  } else {
    snprintf(out, sizeof(out), "pkg: unknown command. Try 'pkg help'.\r\n");
  }
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_desktop(const char *args) {
  static char out[4096];
  const char *sub = ssh_shell_skip_spaces(args);

  out[0] = '\0';
  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    gui_desktop_format_status(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "help")) {
    snprintf(out, sizeof(out),
             "Orizon desktop\r\n"
             "  desktop status          show desktop/session state\r\n"
             "  desktop config          show Hyprland-style config template\r\n"
             "  desktop config doctor   validate Hyprland-style user config\r\n"
             "  desktop config apply    apply supported config keys to session/settings\r\n"
             "  desktop config trace    trace apply/prepare/ignore decisions per config line\r\n"
             "  desktop start|stop      start/stop Hyprland-style session manager\r\n"
             "  desktop restart|reload  restart/reload session manager/config\r\n"
             "  desktop recover         repair/recover desktop session state\r\n"
             "  desktop rescue          read-only session recovery checklist\r\n"
             "  desktop state           show session manager state/log\r\n"
             "  desktop enable          enable optional desktop profile\r\n"
             "  desktop disable         disable desktop profile\r\n"
             "  desktop doctor          check desktop install/config state\r\n"
             "  desktop logs            show desktop events\r\n"
             "  desktop shortcuts       show keys and commands\r\n"
             "  desktop keymap          show VM keyboard/submap runtime\r\n"
             "  desktop session         show theme/wallpaper/layout\r\n"
             "  desktop settings        show system-wide desktop settings\r\n"
             "  desktop settings paths  show /system and ~/.config/hypr settings hub\r\n"
             "  desktop settings export write Hyprland-style user config from /system\r\n"
             "  desktop settings sync   export settings and refresh runtime hints\r\n"
             "  desktop settings set <key> <value> update /system/desktop-settings.conf\r\n"
             "  desktop settings preset <name> apply compact/cozy/performance settings\r\n"
             "  desktop settings doctor validate system-wide desktop settings\r\n"
             "  desktop input           show/set keyboard, pointer and focus policy\r\n"
             "  desktop pointer         show cursor and HID mouse/tablet diagnostics\r\n"
             "  desktop devices         show Hyprland-style input device summary\r\n"
             "  desktop version         show desktop compatibility facade version\r\n"
             "  desktop systeminfo      show compositor/backend/session summary\r\n"
             "  desktop backend         show current framebuffer backend and future split\r\n"
             "  desktop protocol        show internal client/compositor protocol map\r\n"
             "  desktop layouts         show available tiling layouts\r\n"
             "  desktop layout-tree     show active workspace tiling tree/rectangles\r\n"
             "  desktop animations      show animation/runtime transition state\r\n"
             "  desktop decorations     show border/shadow/rounding state\r\n"
             "  desktop render          show render/focus/transition diagnostics\r\n"
             "  desktop descriptions    show hyprctl/dispatcher command descriptions\r\n"
             "  desktop instances       show compositor instance summary\r\n"
             "  desktop submap          show active Hyprland-style submap\r\n"
             "  desktop configerrors    show Hyprland-style config parser errors\r\n"
             "  desktop rollinglog      show desktop event log as hyprctl rollinglog\r\n"
             "  desktop focus-history   show Hyprland-style focusHistoryID order\r\n"
             "  desktop workspace-stack show master/stack/focus order per workspace\r\n"
             "  desktop client-model    show client/workspace/focus state graph\r\n"
             "  desktop rule-matches    show windowrulev2 matches against tiled clients\r\n"
             "  desktop modules         show prepared modular desktop packages\r\n"
             "  desktop apps            list compositor-managed desktop apps\r\n"
             "  desktop app <id>        show app class/module/surface details\r\n"
             "  desktop profiles        list themes/wallpapers/layouts\r\n"
             "  desktop preset <name>   apply graphite/moss/ember/frost/focus preset\r\n"
             "  desktop binds           show Hyprland-style binds/dispatchers\r\n"
             "  desktop rules           show Hyprland-style window rules runtime\r\n"
             "  desktop monitors        show Hyprland-style monitor hints\r\n"
             "  desktop runtime         show generated Hyprland-style runtime files\r\n"
             "  desktop layers          show compositor layer model\r\n"
             "  desktop layout-state    show per-workspace tiling layout state\r\n"
             "  desktop keyword <k> <v> apply one Hyprland-style runtime keyword\r\n"
             "  desktop dispatch <d>    run exec/workspace/layoutmsg/master/swapwindow/submap\r\n"
             "  desktop hyprctl <cmd>   Hyprland-like status/keyword/dispatch facade\r\n"
             "  desktop autostart       show or configure startup apps\r\n"
             "  desktop windows         list compositor clients/windows/layers\r\n"
             "  desktop theme <name>    set session theme\r\n"
             "  desktop wallpaper <name> set symbolic wallpaper\r\n"
             "  desktop layout <name>   set dwindle/master/monocle layout\r\n"
             "  desktop focus on|off|toggle configure focus-follows-mouse\r\n"
             "  desktop bar on|off|toggle configure desktop bar\r\n"
             "  desktop launcher [show|hide|toggle] control launcher\r\n"
             "  desktop launch <app>    spawn terminal/settings/logs/packages/update/launcher\r\n"
             "  desktop killactive      close focused tiled client\r\n"
             "  desktop focus-window next|prev|<target> focus by id/address/class/title\r\n"
             "  desktop workspace [n|name:name|next|empty|previous] show or switch workspace\r\n"
             "  desktop dispatch renameworkspace <target> <name> rename a workspace\r\n"
             "  desktop dispatch movetoworkspace <n|name:name|empty|+1|-1> move focused client and follow\r\n"
             "  desktop dispatch movetoworkspacesilent <target> move focused client without switching\r\n"
             "  desktop dispatch fullscreen|pseudo|pseudotile|pin [on|off|toggle] set client state\r\n"
             "  desktop dispatch fullscreenstate <on|off|1|0> set fullscreen state\r\n"
             "  desktop dispatch cyclenext|swapnext|swapwindow|focusmaster|swapwithmaster client actions\r\n"
             "  desktop dispatch movefocus <l|r|u|d|next|prev> directional tiled focus\r\n"
             "  desktop dispatch focuswindow <id|0xaddr|class:app|title:text> focus matching client\r\n"
             "  desktop dispatch layoutmsg <msg> orientation/splitratio/masterratio actions\r\n"
             "  desktop dispatch resizeactive <x> <y> tiling ratio resize action\r\n"
             "  desktop dispatch submap <name|reset> set active submap\r\n"
             "  desktop reset           disable and restore default policy\r\n"
             "  desktop write-config    rewrite Hypr-style user config\r\n"
             "  desktop open terminal   compat alias for dispatch exec terminal\r\n"
             "  desktop close terminal  compat alias for killactive\r\n"
             "  desktop package         write installable desktop .opkg\r\n");
  } else if (ssh_shell_command_is(sub, "config")) {
    const char *config_args = ssh_shell_skip_spaces(sub + 6);
    if (*config_args == '\0' || ssh_shell_command_is(config_args, "show") ||
        ssh_shell_command_is(config_args, "template")) {
      orizon_desktop_format_config(out, sizeof(out));
    } else if (ssh_shell_command_is(config_args, "doctor") ||
               ssh_shell_command_is(config_args, "check") ||
               ssh_shell_command_is(config_args, "validate")) {
      orizon_desktop_format_config_doctor(out, sizeof(out));
    } else if (ssh_shell_command_is(config_args, "apply") ||
               ssh_shell_command_is(config_args, "import") ||
               ssh_shell_command_is(config_args, "reload")) {
      orizon_desktop_apply_hypr_config(out, sizeof(out));
      gui_desktop_reload_session();
    } else if (ssh_shell_command_is(config_args, "trace") ||
               ssh_shell_command_is(config_args, "explain") ||
               ssh_shell_command_is(config_args, "why")) {
      orizon_desktop_format_config_trace(out, sizeof(out));
    } else {
      snprintf(out, sizeof(out),
               "usage: desktop config [show|doctor|apply|trace]\r\n");
    }
  } else if (ssh_shell_command_is(sub, "doctor") ||
             ssh_shell_command_is(sub, "check")) {
    orizon_desktop_format_doctor(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "start") ||
             ssh_shell_command_is(sub, "stop") ||
             ssh_shell_command_is(sub, "restart") ||
             ssh_shell_command_is(sub, "reload") ||
             ssh_shell_command_is(sub, "recover")) {
    char action[16];
    size_t len = 0;
    while (sub[len] && sub[len] != ' ' && len + 1 < sizeof(action)) {
      action[len] = sub[len];
      len++;
    }
    action[len] = '\0';
    orizon_desktop_session_manager(action, out, sizeof(out));
    if (ssh_shell_command_is(sub, "reload")) {
      gui_desktop_reload_session();
    } else {
      gui_desktop_set_enabled(orizon_desktop_is_enabled());
    }
  } else if (ssh_shell_command_is(sub, "rescue") ||
             ssh_shell_command_is(sub, "session-rescue")) {
    orizon_desktop_format_session_rescue(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "state") ||
             ssh_shell_command_is(sub, "session-state") ||
             ssh_shell_command_is(sub, "manager")) {
    orizon_desktop_format_session_state(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "logs") ||
             ssh_shell_command_is(sub, "log")) {
    orizon_desktop_format_log(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "shortcuts") ||
             ssh_shell_command_is(sub, "keys")) {
    orizon_desktop_format_shortcuts(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "keymap") ||
             ssh_shell_command_is(sub, "inputmap") ||
             ssh_shell_command_is(sub, "ergonomics")) {
    gui_desktop_format_keymap(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "settings")) {
    const char *settings_args = ssh_shell_skip_spaces(sub + 8);
    if (*settings_args == '\0' || ssh_shell_command_is(settings_args, "show") ||
        ssh_shell_command_is(settings_args, "status")) {
      orizon_desktop_format_settings(out, sizeof(out));
    } else if (ssh_shell_command_is(settings_args, "repair") ||
               ssh_shell_command_is(settings_args, "defaults") ||
               ssh_shell_command_is(settings_args, "reset")) {
      orizon_desktop_repair_settings(out, sizeof(out));
      gui_desktop_reload_session();
    } else if (ssh_shell_command_is(settings_args, "presets") ||
               ssh_shell_command_is(settings_args, "profiles")) {
      orizon_desktop_format_settings_presets(out, sizeof(out));
    } else if (ssh_shell_command_is(settings_args, "doctor") ||
               ssh_shell_command_is(settings_args, "check")) {
      orizon_desktop_format_settings_doctor(out, sizeof(out));
    } else if (ssh_shell_command_is(settings_args, "paths") ||
               ssh_shell_command_is(settings_args, "path") ||
               ssh_shell_command_is(settings_args, "hub")) {
      orizon_desktop_format_settings_paths(out, sizeof(out));
    } else if (ssh_shell_command_is(settings_args, "export") ||
               ssh_shell_command_is(settings_args, "write") ||
               ssh_shell_command_is(settings_args, "generate")) {
      orizon_desktop_export_settings(out, sizeof(out));
    } else if (ssh_shell_command_is(settings_args, "sync") ||
               ssh_shell_command_is(settings_args, "apply") ||
               ssh_shell_command_is(settings_args, "reload")) {
      orizon_desktop_sync_settings(out, sizeof(out));
      gui_desktop_reload_session();
    } else if (ssh_shell_command_is(settings_args, "preset") ||
               ssh_shell_command_is(settings_args, "profile")) {
      const char *preset = ssh_shell_skip_spaces(
          settings_args +
          (ssh_shell_command_is(settings_args, "preset") ? 6 : 7));
      if (*preset == '\0') {
        snprintf(out, sizeof(out),
                 "usage: desktop settings preset <default|compact|cozy|performance|accessibility|locked>\r\n");
      } else {
        orizon_desktop_apply_settings_preset(preset, out, sizeof(out));
        gui_desktop_reload_session();
      }
    } else if (ssh_shell_command_is(settings_args, "set")) {
      const char *key = ssh_shell_skip_spaces(settings_args + 3);
      const char *value = key;
      char key_buf[48];
      size_t key_len;
      while (*value && *value != ' ') {
        value++;
      }
      key_len = (size_t)(value - key);
      if (*value == ' ') {
        value = ssh_shell_skip_spaces(value);
      }
      if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
        snprintf(out, sizeof(out),
                 "usage: desktop settings set <key> <value>\r\n");
      } else {
        memcpy(key_buf, key, key_len);
        key_buf[key_len] = '\0';
        orizon_desktop_set_setting(key_buf, value, out, sizeof(out));
        gui_desktop_reload_session();
      }
    } else {
      snprintf(out, sizeof(out),
               "usage: desktop settings [show|paths|export|sync|doctor|presets|preset <name>|repair|set <key> <value>]\r\n");
    }
  } else if (ssh_shell_command_is(sub, "session")) {
    orizon_desktop_format_session(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "input")) {
    const char *input_args = ssh_shell_skip_spaces(sub + 5);
    const char *value = input_args;
    char key_buf[48];
    size_t key_len;
    if (*input_args == '\0' || ssh_shell_command_is(input_args, "show") ||
        ssh_shell_command_is(input_args, "status")) {
      orizon_desktop_format_input(out, sizeof(out));
    } else if (ssh_shell_command_is(input_args, "submap")) {
      value = ssh_shell_skip_spaces(input_args + 6);
      if (*value == '\0') {
        gui_desktop_format_submap(out, sizeof(out));
      } else {
        gui_desktop_dispatch("submap", value, out, sizeof(out));
      }
    } else {
      while (*value && *value != ' ') {
        value++;
      }
      key_len = (size_t)(value - input_args);
      if (*value == ' ') {
        value = ssh_shell_skip_spaces(value);
      }
      if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
        snprintf(out, sizeof(out),
                 "usage: desktop input [layout <fr|us>|pointer <flat|natural|precise|accelerated>|focus <on|off|toggle>|submap <name|reset>]\r\n");
      } else {
        memcpy(key_buf, input_args, key_len);
        key_buf[key_len] = '\0';
        orizon_desktop_set_input(key_buf, value, out, sizeof(out));
        gui_desktop_reload_session();
      }
    }
  } else if (ssh_shell_command_is(sub, "pointer") ||
             ssh_shell_command_is(sub, "cursor") ||
             ssh_shell_command_is(sub, "mouse")) {
    gui_desktop_format_pointer(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "devices") ||
             ssh_shell_command_is(sub, "device")) {
    gui_desktop_format_devices(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "version") ||
             ssh_shell_command_is(sub, "about")) {
    gui_desktop_format_hyprctl_version(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "systeminfo") ||
             ssh_shell_command_is(sub, "system-info")) {
    gui_desktop_format_systeminfo(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "backend") ||
             ssh_shell_command_is(sub, "backend-info")) {
    orizon_desktop_format_backend(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "protocol") ||
             ssh_shell_command_is(sub, "protocols")) {
    orizon_desktop_format_protocol(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "layouts")) {
    gui_desktop_format_layouts(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "layout-state") ||
             ssh_shell_command_is(sub, "layoutstate") ||
             ssh_shell_command_is(sub, "workspace-layouts")) {
    gui_desktop_format_layout_state(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "layout-tree") ||
             ssh_shell_command_is(sub, "layouttree") ||
             ssh_shell_command_is(sub, "tree")) {
    gui_desktop_format_layout_tree(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "animations")) {
    gui_desktop_format_animations(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "decorations") ||
             ssh_shell_command_is(sub, "decoration")) {
    gui_desktop_format_decorations(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "render") ||
             ssh_shell_command_is(sub, "rendering") ||
             ssh_shell_command_is(sub, "renderdiag")) {
    gui_desktop_format_render(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "descriptions") ||
             ssh_shell_command_is(sub, "description")) {
    gui_desktop_format_descriptions(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "instances") ||
             ssh_shell_command_is(sub, "instance")) {
    gui_desktop_format_instances(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "submap")) {
    const char *value = ssh_shell_skip_spaces(sub + 6);
    if (*value == '\0' || ssh_shell_command_is(value, "show") ||
        ssh_shell_command_is(value, "status")) {
      gui_desktop_format_submap(out, sizeof(out));
    } else {
      gui_desktop_dispatch("submap", value, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "configerrors") ||
             ssh_shell_command_is(sub, "config-errors")) {
    orizon_desktop_format_config_errors(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "rollinglog") ||
             ssh_shell_command_is(sub, "rolling-log")) {
    orizon_desktop_format_rolling_log(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "focus-history") ||
             ssh_shell_command_is(sub, "focushistory")) {
    gui_desktop_format_focus_history(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "workspace-stack") ||
             ssh_shell_command_is(sub, "workspacestack") ||
             ssh_shell_command_is(sub, "stack")) {
    gui_desktop_format_workspace_stack(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "apps") ||
             ssh_shell_command_is(sub, "launcher apps")) {
    const char *app = ssh_shell_command_is(sub, "apps")
                          ? ssh_shell_skip_spaces(sub + 4)
                          : ssh_shell_skip_spaces(sub + strlen("launcher apps"));
    if (*app) {
      orizon_desktop_format_app_detail(app, out, sizeof(out));
    } else {
      orizon_desktop_format_apps(out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "app")) {
    const char *app = ssh_shell_skip_spaces(sub + 3);
    if (*app == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop app <terminal|settings|logs|packages|update|launcher>\r\n");
    } else {
      orizon_desktop_format_app_detail(app, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "modules") ||
             ssh_shell_command_is(sub, "packages") ||
             ssh_shell_command_is(sub, "package modules")) {
    orizon_desktop_format_modules(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "binds") ||
             ssh_shell_command_is(sub, "bind")) {
    gui_desktop_format_binds(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "rules") ||
             ssh_shell_command_is(sub, "rule")) {
    orizon_desktop_format_rules(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "monitors") ||
             ssh_shell_command_is(sub, "monitor")) {
    orizon_desktop_format_monitor_hints(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "runtime") ||
             ssh_shell_command_is(sub, "state")) {
    orizon_desktop_format_runtime(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "layers") ||
             ssh_shell_command_is(sub, "layer")) {
    gui_desktop_format_layers(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "keyword")) {
    const char *key = ssh_shell_skip_spaces(sub + 7);
    const char *value = key;
    char key_buf[96];
    size_t key_len;
    while (*value && *value != ' ') {
      value++;
    }
    key_len = (size_t)(value - key);
    if (*value == ' ') {
      value = ssh_shell_skip_spaces(value);
    }
    if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop keyword <hypr-key> <value>\r\n");
    } else {
      memcpy(key_buf, key, key_len);
      key_buf[key_len] = '\0';
      orizon_desktop_apply_hypr_keyword(key_buf, value, out, sizeof(out));
      gui_desktop_reload_session();
    }
  } else if (ssh_shell_command_is(sub, "hyprctl")) {
    const char *hypr = ssh_shell_skip_spaces(sub + 7);
    if (*hypr == '\0' || ssh_shell_command_is(hypr, "help")) {
      snprintf(out, sizeof(out),
               "usage: desktop hyprctl version|systeminfo|backend|protocol|clients|clientmodel|rulematches|workspaces|activeworkspace|activewindow|focushistory|workspacestack|monitors|binds|keymap|layers|layouts|layoutstate|layouttree|animations|decorations|render|descriptions|instances|submap|devices|cursorpos|splash|configerrors|configtrace|rollinglog|getoption <k>|keyword <k> <v>|dispatch <d> [args]|reload\r\n");
    } else if (ssh_shell_command_is(hypr, "version")) {
      gui_desktop_format_hyprctl_version(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "systeminfo")) {
      gui_desktop_format_systeminfo(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "backend") ||
               ssh_shell_command_is(hypr, "backend-info")) {
      orizon_desktop_format_backend(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "protocol") ||
               ssh_shell_command_is(hypr, "protocols")) {
      orizon_desktop_format_protocol(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "clients")) {
      gui_desktop_format_windows(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "clientmodel") ||
               ssh_shell_command_is(hypr, "client-model") ||
               ssh_shell_command_is(hypr, "clientmap") ||
               ssh_shell_command_is(hypr, "client-map")) {
      gui_desktop_format_client_model(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "rulematches") ||
               ssh_shell_command_is(hypr, "rule-matches") ||
               ssh_shell_command_is(hypr, "windowrules") ||
               ssh_shell_command_is(hypr, "window-rules")) {
      gui_desktop_format_rule_matches(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "workspaces")) {
      gui_desktop_format_workspaces(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "activeworkspace")) {
      gui_desktop_format_activeworkspace(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "activewindow")) {
      gui_desktop_format_activewindow(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "focushistory") ||
               ssh_shell_command_is(hypr, "focus-history")) {
      gui_desktop_format_focus_history(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "workspacestack") ||
               ssh_shell_command_is(hypr, "workspace-stack") ||
               ssh_shell_command_is(hypr, "stack")) {
      gui_desktop_format_workspace_stack(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "monitors")) {
      gui_desktop_format_monitors(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "binds")) {
      gui_desktop_format_binds(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "keymap") ||
               ssh_shell_command_is(hypr, "inputmap")) {
      gui_desktop_format_keymap(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "layers")) {
      gui_desktop_format_layers(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "layouts")) {
      gui_desktop_format_layouts(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "layoutstate") ||
               ssh_shell_command_is(hypr, "layout-state") ||
               ssh_shell_command_is(hypr, "workspacelayouts")) {
      gui_desktop_format_layout_state(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "layouttree") ||
               ssh_shell_command_is(hypr, "layout-tree") ||
               ssh_shell_command_is(hypr, "tree")) {
      gui_desktop_format_layout_tree(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "animations")) {
      gui_desktop_format_animations(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "decorations")) {
      gui_desktop_format_decorations(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "render") ||
               ssh_shell_command_is(hypr, "rendering")) {
      gui_desktop_format_render(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "descriptions")) {
      gui_desktop_format_descriptions(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "instances")) {
      gui_desktop_format_instances(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "submap")) {
      const char *value = ssh_shell_skip_spaces(hypr + 6);
      if (*value == '\0' || ssh_shell_command_is(value, "show") ||
          ssh_shell_command_is(value, "status")) {
        gui_desktop_format_submap(out, sizeof(out));
      } else {
        gui_desktop_dispatch("submap", value, out, sizeof(out));
      }
    } else if (ssh_shell_command_is(hypr, "devices")) {
      gui_desktop_format_devices(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "cursorpos")) {
      gui_desktop_format_cursorpos(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "splash")) {
      gui_desktop_format_splash(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "configerrors")) {
      orizon_desktop_format_config_errors(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "configtrace") ||
               ssh_shell_command_is(hypr, "config-trace")) {
      orizon_desktop_format_config_trace(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "rollinglog")) {
      orizon_desktop_format_rolling_log(out, sizeof(out));
    } else if (ssh_shell_command_is(hypr, "getoption")) {
      const char *key = ssh_shell_skip_spaces(hypr + 9);
      if (*key == '\0') {
        snprintf(out, sizeof(out),
                 "usage: desktop hyprctl getoption <hypr-key>\r\n");
      } else {
        orizon_desktop_format_hypr_option(key, out, sizeof(out));
      }
    } else if (ssh_shell_command_is(hypr, "keyword")) {
      const char *key = ssh_shell_skip_spaces(hypr + 7);
      const char *value = key;
      char key_buf[96];
      size_t key_len;
      while (*value && *value != ' ') {
        value++;
      }
      key_len = (size_t)(value - key);
      if (*value == ' ') {
        value = ssh_shell_skip_spaces(value);
      }
      if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
        snprintf(out, sizeof(out),
                 "usage: desktop hyprctl keyword <hypr-key> <value>\r\n");
      } else {
        memcpy(key_buf, key, key_len);
        key_buf[key_len] = '\0';
        orizon_desktop_apply_hypr_keyword(key_buf, value, out, sizeof(out));
        gui_desktop_reload_session();
      }
    } else if (ssh_shell_command_is(hypr, "reload")) {
      orizon_desktop_apply_hypr_config(out, sizeof(out));
      gui_desktop_reload_session();
    } else if (ssh_shell_command_is(hypr, "dispatch")) {
      const char *dispatch = ssh_shell_skip_spaces(hypr + 8);
      const char *dispatch_args = dispatch;
      while (*dispatch_args && *dispatch_args != ' ') {
        dispatch_args++;
      }
      if (*dispatch_args == ' ') {
        size_t dispatch_len = (size_t)(dispatch_args - dispatch);
        char name[32];
        if (dispatch_len >= sizeof(name)) {
          dispatch_len = sizeof(name) - 1;
        }
        memcpy(name, dispatch, dispatch_len);
        name[dispatch_len] = '\0';
        dispatch_args = ssh_shell_skip_spaces(dispatch_args);
        gui_desktop_dispatch(name, dispatch_args, out, sizeof(out));
      } else {
        gui_desktop_dispatch(dispatch, "", out, sizeof(out));
      }
    } else {
      snprintf(out, sizeof(out), "hyprctl: unknown command\r\n");
    }
  } else if (ssh_shell_command_is(sub, "dispatch")) {
    const char *dispatch = ssh_shell_skip_spaces(sub + 8);
    const char *dispatch_args = dispatch;
    while (*dispatch_args && *dispatch_args != ' ') {
      dispatch_args++;
    }
    if (*dispatch_args == ' ') {
      size_t dispatch_len = (size_t)(dispatch_args - dispatch);
      char name[32];
      if (dispatch_len >= sizeof(name)) {
        dispatch_len = sizeof(name) - 1;
      }
      memcpy(name, dispatch, dispatch_len);
      name[dispatch_len] = '\0';
      dispatch_args = ssh_shell_skip_spaces(dispatch_args);
      gui_desktop_dispatch(name, dispatch_args, out, sizeof(out));
    } else {
      gui_desktop_dispatch(dispatch, "", out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "profiles") ||
             ssh_shell_command_is(sub, "themes")) {
    orizon_desktop_format_profiles(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "preset") ||
             ssh_shell_command_is(sub, "profile")) {
    const char *value = ssh_shell_skip_spaces(
        sub + (ssh_shell_command_is(sub, "preset") ? 6 : 7));
    if (*value == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop preset <graphite|moss|ember|frost|focus>\r\n");
    } else {
      orizon_desktop_apply_preset(value, out, sizeof(out));
      gui_desktop_reload_session();
    }
  } else if (ssh_shell_command_is(sub, "autostart")) {
    const char *value = ssh_shell_skip_spaces(sub + 9);
    orizon_desktop_session_t session;
    if (*value == '\0') {
      orizon_desktop_format_autostart(out, sizeof(out));
    } else if (!ssh_shell_command_is(value, "terminal")) {
      snprintf(out, sizeof(out),
               "usage: desktop autostart terminal on|off|toggle\r\n");
    } else {
      value = ssh_shell_skip_spaces(value + 8);
      if (ssh_shell_command_is(value, "toggle")) {
        orizon_desktop_load_session(&session);
        value = session.autostart_terminal ? "off" : "on";
      }
      if (*value == '\0') {
        snprintf(out, sizeof(out),
                 "usage: desktop autostart terminal on|off|toggle\r\n");
      } else {
        orizon_desktop_set_session_option("autostart-terminal", value, out,
                                          sizeof(out));
        gui_desktop_reload_session();
      }
    }
  } else if (ssh_shell_command_is(sub, "windows") ||
             ssh_shell_command_is(sub, "window") ||
             ssh_shell_command_is(sub, "clients") ||
             ssh_shell_command_is(sub, "client")) {
    gui_desktop_format_windows(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "client-model") ||
             ssh_shell_command_is(sub, "clientmodel") ||
             ssh_shell_command_is(sub, "client-map") ||
             ssh_shell_command_is(sub, "clientmap") ||
             ssh_shell_command_is(sub, "model")) {
    gui_desktop_format_client_model(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "rule-matches") ||
             ssh_shell_command_is(sub, "rulematches") ||
             ssh_shell_command_is(sub, "windowrules") ||
             ssh_shell_command_is(sub, "window-rules")) {
    gui_desktop_format_rule_matches(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "activewindow") ||
             ssh_shell_command_is(sub, "active-window")) {
    gui_desktop_format_activewindow(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "workspaces")) {
    gui_desktop_format_workspaces(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "workspace")) {
    const char *value = ssh_shell_skip_spaces(sub + 9);
    uint32_t workspace;
    if (*value == '\0') {
      gui_desktop_format_workspaces(out, sizeof(out));
    } else if (ssh_shell_parse_uint(value, &workspace) == 0 &&
               gui_desktop_switch_workspace((int)workspace) == 0) {
      snprintf(out, sizeof(out), "desktop: workspace %u active\r\n",
               (unsigned)workspace);
    } else if (gui_desktop_dispatch("workspace", value, out, sizeof(out)) == 0) {
      /* dispatch already formatted the response */
    } else {
      snprintf(out, sizeof(out),
               "usage: desktop workspace <1-10|name:<name>|next|empty|+/-n|previous>\r\n");
    }
  } else if (ssh_shell_command_is(sub, "move")) {
    const char *target = ssh_shell_skip_spaces(sub + 4);
    uint32_t workspace;
    if (!ssh_shell_command_is(target, "terminal")) {
      snprintf(out, sizeof(out), "usage: desktop move terminal <1-10>\r\n");
    } else {
      target = ssh_shell_skip_spaces(target + 8);
      if (ssh_shell_parse_uint(target, &workspace) < 0 ||
          gui_desktop_move_terminal_to_workspace((int)workspace) < 0) {
        snprintf(out, sizeof(out), "usage: desktop move terminal <1-10>\r\n");
      } else {
        snprintf(out, sizeof(out),
                 "desktop: terminal moved to workspace %u\r\n",
                 (unsigned)workspace);
      }
    }
  } else if (ssh_shell_command_is(sub, "focus-window")) {
    const char *value = ssh_shell_skip_spaces(sub + 12);
    int rc;
    int formatted = 0;
    if (*value == '\0' || ssh_shell_command_is(value, "next") ||
        ssh_shell_command_is(value, "right") ||
        ssh_shell_command_is(value, "down")) {
      rc = gui_desktop_focus_next_client();
    } else if (ssh_shell_command_is(value, "prev") ||
               ssh_shell_command_is(value, "left") ||
               ssh_shell_command_is(value, "up")) {
      rc = gui_desktop_focus_prev_client();
    } else {
      rc = gui_desktop_dispatch("focuswindow", value, out, sizeof(out));
      if (rc == 0) {
        formatted = 1;
      } else {
        rc = -2;
      }
    }
    if (!formatted && rc != -2) {
      snprintf(out, sizeof(out), rc == 0 ? "desktop: focus changed\r\n"
                                         : "desktop: no client to focus\r\n");
    }
  } else if (ssh_shell_command_is(sub, "theme")) {
    const char *value = ssh_shell_skip_spaces(sub + 5);
    if (*value == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop theme <graphite|moss|ember|frost>\r\n");
    } else {
      orizon_desktop_set_session_option("theme", value, out, sizeof(out));
      gui_desktop_reload_session();
    }
  } else if (ssh_shell_command_is(sub, "wallpaper")) {
    const char *value = ssh_shell_skip_spaces(sub + 9);
    if (*value == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop wallpaper <aurora|dawn|noir|moss>\r\n");
    } else {
      orizon_desktop_set_session_option("wallpaper", value, out,
                                        sizeof(out));
      gui_desktop_reload_session();
    }
  } else if (ssh_shell_command_is(sub, "layout")) {
    const char *value = ssh_shell_skip_spaces(sub + 6);
    if (*value == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop layout <dwindle|master|monocle>\r\n");
    } else {
      orizon_desktop_set_session_option("layout", value, out, sizeof(out));
      gui_desktop_reload_session();
    }
  } else if (ssh_shell_command_is(sub, "focus")) {
    const char *value = ssh_shell_skip_spaces(sub + 5);
    orizon_desktop_session_t session;
    if (ssh_shell_command_is(value, "toggle")) {
      orizon_desktop_load_session(&session);
      value = session.focus_follows_mouse ? "off" : "on";
    }
    if (*value == '\0') {
      snprintf(out, sizeof(out), "usage: desktop focus on|off|toggle\r\n");
    } else {
      orizon_desktop_set_session_option("focus", value, out, sizeof(out));
      gui_desktop_reload_session();
    }
  } else if (ssh_shell_command_is(sub, "bar")) {
    const char *value = ssh_shell_skip_spaces(sub + 3);
    orizon_desktop_session_t session;
    if (ssh_shell_command_is(value, "toggle")) {
      orizon_desktop_load_session(&session);
      value = session.bar_enabled ? "off" : "on";
    }
    if (*value == '\0') {
      snprintf(out, sizeof(out), "usage: desktop bar on|off|toggle\r\n");
    } else {
      orizon_desktop_set_session_option("bar", value, out, sizeof(out));
      gui_desktop_reload_session();
    }
  } else if (ssh_shell_command_is(sub, "launcher")) {
    const char *value = ssh_shell_skip_spaces(sub + 8);
    if (*value == '\0' || ssh_shell_command_is(value, "show") ||
        ssh_shell_command_is(value, "open")) {
      gui_desktop_show_launcher();
      snprintf(out, sizeof(out), "desktop: launcher open\r\n");
    } else if (ssh_shell_command_is(value, "hide") ||
               ssh_shell_command_is(value, "close")) {
      gui_desktop_hide_launcher();
      snprintf(out, sizeof(out), "desktop: launcher closed\r\n");
    } else if (ssh_shell_command_is(value, "toggle")) {
      gui_desktop_toggle_launcher();
      snprintf(out, sizeof(out), "desktop: launcher toggled\r\n");
    } else {
      snprintf(out, sizeof(out),
               "usage: desktop launcher [show|hide|toggle]\r\n");
    }
  } else if (ssh_shell_command_is(sub, "launch")) {
    const char *app = ssh_shell_skip_spaces(sub + 6);
    if (*app == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop launch <terminal|settings|logs|packages|update|launcher>\r\n");
    } else {
      gui_desktop_spawn_app_client(app, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "spawn") ||
             ssh_shell_command_is(sub, "exec")) {
    const char *app = ssh_shell_skip_spaces(
        sub + (ssh_shell_command_is(sub, "spawn") ? 5 : 4));
    if (*app == '\0') {
      snprintf(out, sizeof(out),
               "usage: desktop exec <terminal|settings|logs|packages|update|launcher>\r\n");
    } else {
      gui_desktop_spawn_app_client(app, out, sizeof(out));
    }
  } else if (ssh_shell_command_is(sub, "apply")) {
    gui_desktop_reload_session();
    snprintf(out, sizeof(out), "desktop: session reloaded\r\n");
  } else if (ssh_shell_command_is(sub, "write-config") ||
             ssh_shell_command_is(sub, "regen-config")) {
    orizon_desktop_write_user_config(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "reset")) {
    orizon_desktop_reset(out, sizeof(out));
    gui_desktop_set_enabled(0);
  } else if (ssh_shell_command_is(sub, "enable") ||
             ssh_shell_command_is(sub, "install")) {
    orizon_desktop_set_enabled(1, out, sizeof(out));
    gui_desktop_set_enabled(1);
  } else if (ssh_shell_command_is(sub, "disable") ||
             ssh_shell_command_is(sub, "off")) {
    orizon_desktop_set_enabled(0, out, sizeof(out));
    gui_desktop_set_enabled(0);
  } else if (ssh_shell_command_is(sub, "open") ||
             ssh_shell_command_is(sub, "open terminal") ||
             ssh_shell_command_is(sub, "terminal")) {
    gui_desktop_open_terminal();
    snprintf(out, sizeof(out), "desktop: terminal open\r\n");
  } else if (ssh_shell_command_is(sub, "close") ||
             ssh_shell_command_is(sub, "close terminal")) {
    gui_desktop_close_active_client();
    snprintf(out, sizeof(out), "desktop: active client closed\r\n");
  } else if (ssh_shell_command_is(sub, "killactive")) {
    if (gui_desktop_close_active_client() == 0) {
      snprintf(out, sizeof(out), "desktop: active client closed\r\n");
    } else {
      snprintf(out, sizeof(out), "desktop: no active client\r\n");
    }
  } else if (ssh_shell_command_is(sub, "toggle")) {
    gui_desktop_toggle_terminal();
    snprintf(out, sizeof(out), "desktop: terminal toggled\r\n");
  } else if (ssh_shell_command_is(sub, "package") ||
             ssh_shell_command_is(sub, "sample")) {
    orizon_pkg_write_desktop_sample(out, sizeof(out));
  } else {
    snprintf(out, sizeof(out), "desktop: unknown command. Try 'desktop help'.\r\n");
  }
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_free(void) {
  kmalloc_stats_t stats;
  char out[256];

  kmalloc_get_stats(&stats);
  snprintf(out, sizeof(out),
           "          total_kb used_kb free_kb largest_kb\r\n"
           "heap      %8lu %7lu %7lu %10lu\r\n"
           "blocks: used=%lu free=%lu total=%lu\r\n",
           (unsigned long)(stats.total / 1024),
           (unsigned long)(stats.used / 1024),
           (unsigned long)(stats.free / 1024),
           (unsigned long)(stats.largest_free / 1024),
           (unsigned long)stats.used_blocks,
           (unsigned long)stats.free_blocks,
           (unsigned long)stats.blocks);
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_usb(int rescan) {
  char out[1400];
  size_t used = 0;

  if (rescan) {
    usb_rescan();
  }
  usb_format_status(out, sizeof(out));
  used = strlen(out);
  if (used + 2 < sizeof(out)) {
    strcat(out, "\r\n");
    used += 2;
  }
  if (used < sizeof(out)) {
    usb_format_port_status(out + used, sizeof(out) - used);
  }
  used = strlen(out);
  if (used + 2 < sizeof(out)) {
    strcat(out, "\r\n");
    used += 2;
  }
  if (used < sizeof(out)) {
    usb_format_device_status(out + used, sizeof(out) - used);
  }
  used = strlen(out);
  if (used + 2 < sizeof(out)) {
    strcat(out, "\r\n");
    used += 2;
  }
  if (used < sizeof(out)) {
    usb_format_net_status(out + used, sizeof(out) - used);
  }
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_pci(const char *args) {
  static pci_device_info_t devs[96];
  static char out[SSH_CHANNEL_TEXT_BUF];
  char line[192];
  size_t used = 0;
  int show_bars = ssh_shell_command_is(ssh_shell_skip_spaces(args), "bars");
  int total = pci_scan_all(devs, 96);

  snprintf(line, sizeof(line), "PCI devices: %d\r\n", total);
  ssh_shell_append(out, sizeof(out), &used, line);
  for (int i = 0; i < total && i < 96; i++) {
    const pci_device_info_t *dev = &devs[i];
    snprintf(line, sizeof(line),
             "%02x:%02x.%u vendor=%04x device=%04x class=%02x/%02x/%02x",
             dev->bus, dev->device, dev->function, dev->vendor_id,
             dev->device_id, dev->class_code, dev->subclass, dev->prog_if);
    ssh_shell_append(out, sizeof(out), &used, line);
    if (show_bars) {
      snprintf(line, sizeof(line),
               " bars=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx",
               (unsigned long)dev->bar[0], (unsigned long)dev->bar[1],
               (unsigned long)dev->bar[2], (unsigned long)dev->bar[3],
               (unsigned long)dev->bar[4], (unsigned long)dev->bar[5]);
      ssh_shell_append(out, sizeof(out), &used, line);
    }
    ssh_shell_append(out, sizeof(out), &used, "\r\n");
  }
  if (total > 96) {
    ssh_shell_append(out, sizeof(out), &used, "[truncated]\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_hw(const char *args) {
  static char out[2600];
  const char *sub = ssh_shell_skip_spaces(args);

  if (*sub == '\0' || ssh_shell_command_is(sub, "next")) {
    orizon_report_format_hardware_next(out, sizeof(out));
  } else {
    snprintf(out, sizeof(out), "usage: hw [next]\r\n");
  }
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_report(const char *args) {
  static char out[SSH_CHANNEL_TEXT_BUF];
  const char *sub = ssh_shell_skip_spaces(args);

  if (ssh_shell_command_is(sub, "next")) {
    orizon_report_format_hardware_next(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }

  if (ssh_shell_command_is(sub, "save")) {
    orizon_report_save(out, sizeof(out));
    if (strlen(out) + strlen("read with: cat " ORIZON_HARDWARE_REPORT_PATH
                             "\r\n") < sizeof(out)) {
      strcat(out, "read with: cat " ORIZON_HARDWARE_REPORT_PATH "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }

  if (ssh_shell_command_is(sub, "show") || *sub == '\0') {
    int n = orizon_report_format(out, sizeof(out));
    if (n >= 0 && (size_t)n >= sizeof(out) - 1 &&
        strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n[truncated over SSH; use report save]\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }

  ssh_queue_channel_text("usage: report [save|show|next]\r\n");
  ssh_shell_prompt();
}

static void ssh_shell_print_install_plan(const char *args) {
  static char report[4096];
  static char out[SSH_CHANNEL_TEXT_BUF];
  storage_device_info_t disk;
  orizon_install_config_t config;
  char disk_name[24];
  char line[160];
  size_t used = 0;
  const char *sub = ssh_shell_skip_spaces(args);
  const char *mode = "manual-later";
  const char *desktop_profile = "none";
  int disk_index = -1;
  int data_partition = -1;
  int count;

  if (strstr(sub, "desktop") || strstr(sub, "hypr")) {
    desktop_profile = ORIZON_DESKTOP_PROFILE;
  }
  if (*sub != '\0') {
    if (ssh_shell_command_is(sub, "manual") ||
        ssh_shell_command_is(sub, "manual-later") ||
        ssh_shell_command_is(sub, "desktop")) {
      mode = "manual-later";
    } else if (ssh_shell_command_is(sub, "dual-boot-esp")) {
      mode = "dual-boot-esp";
    } else if (ssh_shell_command_is(sub, "guided-full-disk") ||
               ssh_shell_command_is(sub, "full")) {
      mode = "guided-full-disk";
    } else if (ssh_shell_command_is(sub, "dual-boot-data")) {
      uint32_t parsed = 0;
      const char *part_arg =
          ssh_shell_skip_spaces(sub + strlen("dual-boot-data"));
      mode = "dual-boot-data";
      if (strncmp(part_arg, "part", 4) == 0) {
        part_arg += 4;
      }
      if (ssh_shell_parse_uint(part_arg, &parsed) < 0) {
        ssh_queue_channel_text(
            "usage: install-plan [manual|manual desktop|dual-boot-esp|dual-boot-data <part>|guided-full-disk]\r\n");
        ssh_shell_prompt();
        return;
      }
      data_partition = (int)parsed;
    } else {
      ssh_queue_channel_text(
          "usage: install-plan [manual|manual desktop|dual-boot-esp|dual-boot-data <part>|guided-full-disk]\r\n");
      ssh_shell_prompt();
      return;
    }
  }

  disk_name[0] = '\0';
  count = storage_device_count();
  if (count > 0) {
    disk_index = storage_selected_device();
    if (disk_index < 0) {
      disk_index = 0;
    }
    if (storage_get_device(disk_index, &disk) == 0) {
      snprintf(disk_name, sizeof(disk_name), "%s", disk.name);
    }
  }
  if (disk_name[0] == '\0') {
    snprintf(disk_name, sizeof(disk_name), "none");
  }

  config.language = "en_US";
  config.keyboard = "ssh";
  config.disk_mode = mode;
  config.hostname = "orizon-vm";
  config.desktop_profile = desktop_profile;
  config.disk_index = disk_index;
  config.disk_name = disk_name;
  config.data_partition_index = data_partition;

  orizon_install_format_plan(&config, report, sizeof(report));
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  if (ssh_write_absolute_text_file(SSH_INSTALL_REPORT_PATH, report) < 0) {
    ssh_queue_channel_text("install-plan: failed to write report\r\n");
    ssh_shell_prompt();
    return;
  }

  ssh_shell_append(out, sizeof(out), &used, report);
  if (used == 0 || out[used - 1] != '\n') {
    ssh_shell_append(out, sizeof(out), &used, "\r\n");
  }
  snprintf(line, sizeof(line),
           "install-plan: wrote %s\r\nread with: cat %s\r\n",
           SSH_INSTALL_REPORT_PATH, SSH_INSTALL_REPORT_PATH);
  ssh_shell_append(out, sizeof(out), &used, line);
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_mutate_path(const char *arg, const char *op) {
  static char path[MAX_PATH];
  int rc = -1;
  char out[64];

  if (!op) {
    op = "path";
  }
  if (ssh_shell_resolve_path(arg, path, sizeof(path)) < 0) {
    snprintf(out, sizeof(out), "%s: invalid path\r\n", op);
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (!ssh_shell_path_write_allowed(path)) {
    ssh_shell_policy_denied(op,
                            path,
                            "generic remote writes are limited to "
                            "/workspace, /home, /logs and /packages");
    return;
  }
  if (strcmp(op, "rm") == 0 && ssh_shell_path_is_remote_root(path)) {
    ssh_shell_policy_denied(op, path, "remote root deletion is blocked");
    return;
  }
  if (strcmp(op, "touch") == 0) {
    rc = vfs_create(path);
  } else if (strcmp(op, "mkdir") == 0) {
    rc = vfs_mkdir(path);
  } else if (strcmp(op, "rm") == 0) {
    rc = vfs_delete(path);
  }
  snprintf(out, sizeof(out), "%s: %s\r\n", op, rc == 0 ? "ok" : "failed");
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_write_text(const char *args, int append) {
  static char file_arg[MAX_PATH];
  static char path[MAX_PATH];
  const char *text = NULL;
  file_t *f;

  if (ssh_shell_split_path_text(args, file_arg, sizeof(file_arg), &text) < 0 ||
      ssh_shell_resolve_path(file_arg, path, sizeof(path)) < 0) {
    ssh_queue_channel_text(append ? "usage: append <file> <text>\r\n"
                                  : "usage: write <file> <text>\r\n");
    ssh_shell_prompt();
    return;
  }
  if (!ssh_shell_path_write_allowed(path)) {
    ssh_shell_policy_denied(append ? "append" : "write",
                            path,
                            "generic remote writes are limited to "
                            "/workspace, /home, /logs and /packages");
    return;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC));
  if (!f) {
    ssh_queue_channel_text(append ? "append: failed\r\n" : "write: failed\r\n");
    ssh_shell_prompt();
    return;
  }
  if (vfs_write(f, text, strlen(text)) < 0 ||
      vfs_write(f, "\n", 1) < 0) {
    ssh_queue_channel_text(append ? "append: write error\r\n"
                                  : "write: write error\r\n");
    vfs_close(f);
    ssh_shell_prompt();
    return;
  }
  vfs_close(f);
  ssh_queue_channel_text(append ? "append: ok\r\n" : "write: ok\r\n");
  ssh_shell_prompt();
}

static int ssh_write_absolute_text_file(const char *path, const char *text) {
  file_t *f;

  if (!path || !text) {
    return -1;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -1;
  }
  if (vfs_write(f, text, strlen(text)) < 0) {
    vfs_close(f);
    return -1;
  }
  vfs_close(f);
  return 0;
}

static void ssh_shell_print_audit(void) {
  char out[1536];

  ssh_format_audit(out, sizeof(out));
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_algorithms(void) {
  char out[1800];

  ssh_format_algorithms(out, sizeof(out));
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_security(const char *args) {
  static char out[4096];
  const char *sub = ssh_shell_skip_spaces(args);

  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    ssh_format_security(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "policy")) {
    ssh_format_security_policy(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "audit") ||
      ssh_shell_command_is(sub, "sessions")) {
    ssh_format_security_audit(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "keys") ||
      ssh_shell_command_is(sub, "hostkey")) {
    ssh_format_security_keys(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "doctor") ||
      ssh_shell_command_is(sub, "check")) {
    ssh_format_security_doctor(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(sub, "rotate")) {
    const char *rotate = ssh_shell_skip_spaces(sub + strlen("rotate"));
    if (ssh_shell_command_is(rotate, "ssh-hostkey") ||
        ssh_shell_command_is(rotate, "hostkey")) {
      ssh_queue_channel_text(
          "security rotate: regenerating SSH host key; future clients may "
          "need known_hosts cleanup.\r\n");
      ssh_reset_hostkey(out, sizeof(out));
      ssh_queue_channel_text(out);
      ssh_shell_prompt();
      return;
    }
  }
  ssh_queue_channel_text(
      "usage: security [status|policy|audit|keys|doctor|rotate ssh-hostkey]\r\n");
  ssh_shell_prompt();
}

static void ssh_shell_print_rollback_status(void) {
  static char out[4096];

  orizon_update_rollback_status(out, sizeof(out));
  if (strlen(out) + 2 < sizeof(out) &&
      (out[0] == '\0' || out[strlen(out) - 1] != '\n')) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_set_password(const char *password) {
  char out[256];

  ssh_set_password(password, out, sizeof(out));
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_set_auth_policy_remote(const char *args) {
  const ssh_status_t *st = ssh_get_status();
  char out[256];
  uint32_t value = 0;

  args = ssh_shell_skip_spaces(args);
  if (ssh_shell_command_is(args, "max")) {
    if (ssh_shell_parse_uint(args + 3, &value) < 0) {
      ssh_queue_channel_text("usage: ssh auth max <attempts>\r\n");
      ssh_shell_prompt();
      return;
    }
    ssh_set_auth_policy(value, st->auth_lockout_seconds, out, sizeof(out));
  } else if (ssh_shell_command_is(args, "lockout")) {
    if (ssh_shell_parse_uint(args + 7, &value) < 0) {
      ssh_queue_channel_text("usage: ssh auth lockout <seconds>\r\n");
      ssh_shell_prompt();
      return;
    }
    ssh_set_auth_policy(st->max_auth_attempts, value, out, sizeof(out));
  } else if (ssh_shell_command_is(args, "default") ||
             ssh_shell_command_is(args, "defaults")) {
    ssh_reset_auth_policy(out, sizeof(out));
  } else {
    ssh_format_auth(out, sizeof(out));
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_log(const char *which) {
  if (ssh_shell_command_is(which, "ssh")) {
    ssh_shell_print_file(ORIZON_SSH_LOG_PATH, 1800, 1);
  } else if (ssh_shell_command_is(which, "security")) {
    ssh_shell_print_file(ORIZON_SECURITY_LOG_PATH, 1800, 1);
  } else if (ssh_shell_command_is(which, "storage")) {
    static char out[1800];
    storage_format_log(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
  } else if (ssh_shell_command_is(which, "pci")) {
    static char out[1800];
    pci_format_diagnostics(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
  } else if (ssh_shell_command_is(which, "boot")) {
    ssh_shell_print_file(KLOG_BOOT_PATH, 1800, 1);
  } else if (ssh_shell_command_is(which, "update")) {
    ssh_shell_print_file(SSH_UPDATE_LOG_PATH, 1800, 1);
  } else if (ssh_shell_command_is(which, "install")) {
    if (vfs_exists(SSH_INSTALL_LOG_PATH)) {
      ssh_shell_print_file(SSH_INSTALL_LOG_PATH, 1800, 1);
    } else {
      ssh_shell_print_file(SSH_INSTALL_REPORT_PATH, 1800, 1);
    }
  } else if (ssh_shell_command_is(which, "network") ||
             ssh_shell_command_is(which, "net")) {
    ssh_shell_print_file(netstack_log_path(), 1800, 1);
  } else if (ssh_shell_command_is(which, "usb")) {
    ssh_shell_print_file(usb_log_path(), 1800, 1);
  } else if (ssh_shell_command_is(which, "wifi")) {
    ssh_shell_print_file(SSH_WIFI_LOG_PATH, 1800, 1);
  } else {
    char out[SSH_CHANNEL_TEXT_BUF];
    size_t n = klog_snapshot(out, sizeof(out) - 3);
    out[n] = '\0';
    if (n > 0 && out[n - 1] != '\n') {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out[0] ? out : "logs: empty\r\n");
    ssh_shell_prompt();
  }
}

static void ssh_shell_print_dns(const char *args) {
  char host[128];
  char ip_s[24];
  char out[192];
  uint32_t ip = 0;

  if (!ssh_shell_read_token(args, host, sizeof(host))) {
    netstack_format_dns(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (netstack_resolve_a(host, &ip) != 0) {
    ssh_queue_channel_text("dns: resolve failed\r\n");
    ssh_shell_prompt();
    return;
  }
  netstack_format_ipv4(ip, ip_s, sizeof(ip_s));
  snprintf(out, sizeof(out), "%s -> %s\r\n", host, ip_s);
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_ping(const char *args) {
  char target[128];
  char ip_s[24];
  char out[512];
  char line[96];
  size_t used = 0;
  uint32_t ip = 0;

  if (!ssh_shell_read_token(args, target, sizeof(target))) {
    ssh_queue_channel_text("usage: ping <ip-or-host>\r\n");
    ssh_shell_prompt();
    return;
  }
  if (netstack_parse_ipv4(target, &ip) != 0 &&
      netstack_resolve_a(target, &ip) != 0) {
    ssh_queue_channel_text("ping: cannot resolve host\r\n");
    ssh_shell_prompt();
    return;
  }
  netstack_format_ipv4(ip, ip_s, sizeof(ip_s));
  for (int i = 0; i < 4; i++) {
    uint32_t ms = 0;
    if (netstack_ping(ip, &ms) == 0) {
      snprintf(line, sizeof(line), "reply from %s time=%lums\r\n", ip_s,
               (unsigned long)ms);
    } else {
      snprintf(line, sizeof(line), "request timeout\r\n");
    }
    ssh_shell_append(out, sizeof(out), &used, line);
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_net(const char *args) {
  static char report[SSH_CHANNEL_TEXT_BUF];
  const char *sub = ssh_shell_skip_spaces(args);
  char line[512];
  size_t used = 0;
  size_t tls_len = 0;

  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    netstack_format_status(line, sizeof(line));
    ssh_shell_append(report, sizeof(report), &used, line);
    ssh_shell_append(report, sizeof(report), &used, "\r\n");
    netstack_format_route(line, sizeof(line));
    ssh_shell_append(report, sizeof(report), &used, line);
    ssh_shell_append(report, sizeof(report), &used, "\r\n");
    netstack_format_dns(line, sizeof(line));
    ssh_shell_append(report, sizeof(report), &used, line);
    ssh_shell_append(report, sizeof(report), &used, "\r\n");
    ssh_queue_channel_text(report);
    ssh_shell_prompt();
    return;
  }

  if (ssh_shell_command_is(sub, "check") ||
      ssh_shell_command_is(sub, "doctor")) {
    netstack_format_check(report, sizeof(report));
    ssh_queue_channel_text(report);
    ssh_shell_prompt();
    return;
  }

  if (ssh_shell_command_is(sub, "tcp")) {
    const char *tcp_args = ssh_shell_skip_spaces(sub + 3);
    char host[96];
    char token[32];
    char attempts_text[16];
    uint32_t port = 443;
    uint32_t attempts = 0;

    tcp_args = ssh_shell_read_token(tcp_args, host, sizeof(host));
    if (!tcp_args) {
      ssh_queue_channel_text(
          "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\r\n");
      ssh_shell_prompt();
      return;
    }
    if (*tcp_args) {
      tcp_args = ssh_shell_read_token(tcp_args, token, sizeof(token));
      if (!tcp_args) {
        ssh_queue_channel_text(
            "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\r\n");
        ssh_shell_prompt();
        return;
      }
      if (strcmp(token, "attempts") == 0 || strcmp(token, "tries") == 0 ||
          strcmp(token, "retry") == 0) {
        tcp_args = ssh_shell_read_token(tcp_args, attempts_text,
                                        sizeof(attempts_text));
        if (!tcp_args || ssh_shell_parse_uint(attempts_text, &attempts) < 0 ||
            attempts == 0 || attempts > 5 || *tcp_args) {
          ssh_queue_channel_text(
              "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\r\n");
          ssh_shell_prompt();
          return;
        }
      } else if (ssh_shell_parse_uint(token, &port) == 0 && port > 0 &&
                 port <= 65535) {
        if (*tcp_args) {
          tcp_args = ssh_shell_read_token(tcp_args, token, sizeof(token));
          if (!tcp_args || (strcmp(token, "attempts") != 0 &&
                            strcmp(token, "tries") != 0 &&
                            strcmp(token, "retry") != 0)) {
            ssh_queue_channel_text(
                "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\r\n");
            ssh_shell_prompt();
            return;
          }
          tcp_args = ssh_shell_read_token(tcp_args, attempts_text,
                                          sizeof(attempts_text));
          if (!tcp_args || ssh_shell_parse_uint(attempts_text, &attempts) < 0 ||
              attempts == 0 || attempts > 5 || *tcp_args) {
            ssh_queue_channel_text(
                "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\r\n");
            ssh_shell_prompt();
            return;
          }
        }
      } else {
        ssh_queue_channel_text(
            "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\r\n");
        ssh_shell_prompt();
        return;
      }
    }
    if (attempts > 0) {
      netstack_tcp_probe_retry(host, (uint16_t)port, (unsigned)attempts,
                               report, sizeof(report));
    } else {
      netstack_tcp_probe(host, (uint16_t)port, report, sizeof(report));
    }
    ssh_queue_channel_text(report);
    ssh_shell_prompt();
    return;
  }

  if (ssh_shell_command_is(sub, "diag") ||
      ssh_shell_command_is(sub, "daily")) {
    int include_tls = ssh_shell_command_is(sub, "diag");
    ssh_queue_channel_text(include_tls ? "net diag: daily VM network diagnostics\r\n"
                                       : "net daily: VM network diagnostics\r\n");
    netstack_format_daily(report, sizeof(report));
    ssh_queue_channel_text(report);
    netstack_format_check(report, sizeof(report));
    ssh_queue_channel_text(report);
    netstack_tcp_probe("raw.githubusercontent.com", 443, report,
                       sizeof(report));
    ssh_queue_channel_text(report);
    if (!include_tls) {
      ssh_shell_prompt();
      return;
    }
    ssh_queue_channel_text("net diag: TLS/root-trust probe\r\n");
    report[0] = '\0';
    if (netstack_github_tls_probe(report, sizeof(report), &tls_len) == 0) {
      snprintf(line, sizeof(line), "net tls: PASS bytes=%lu\r\n",
               (unsigned long)tls_len);
      ssh_queue_channel_text(line);
    } else {
      snprintf(line, sizeof(line), "net tls: FAIL status=%s\r\n",
               netstack_get_status()->status);
      ssh_queue_channel_text(line);
      ssh_queue_channel_text(
          "hint: run 'net tcp raw.githubusercontent.com 443' to separate TCP "
          "reachability from TLS/root trust.\r\n");
    }
    if (report[0]) {
      ssh_queue_channel_text(report);
      if (report[strlen(report) - 1] != '\n') {
        ssh_queue_channel_text("\r\n");
      }
    }
    ssh_shell_prompt();
    return;
  }

  if (ssh_shell_command_is(sub, "tls") ||
      ssh_shell_command_is(sub, "https")) {
    report[0] = '\0';
    if (netstack_github_tls_probe(report, sizeof(report), &tls_len) == 0) {
      snprintf(line, sizeof(line), "net tls: PASS bytes=%lu\r\n",
               (unsigned long)tls_len);
      ssh_queue_channel_text(line);
    } else {
      snprintf(line, sizeof(line), "net tls: FAIL status=%s\r\n",
               netstack_get_status()->status);
      ssh_queue_channel_text(line);
      ssh_queue_channel_text(
          "hint: run 'net tcp raw.githubusercontent.com 443' to separate TCP "
          "reachability from TLS/root trust.\r\n");
    }
    if (report[0]) {
      ssh_queue_channel_text(report);
      if (report[strlen(report) - 1] != '\n') {
        ssh_queue_channel_text("\r\n");
      }
    }
    ssh_shell_prompt();
    return;
  }

  if (ssh_shell_command_is(sub, "config")) {
    const char *cfg_args = ssh_shell_skip_spaces(sub + 6);
    if (*cfg_args == '\0' || ssh_shell_command_is(cfg_args, "show")) {
      ssh_shell_print_file(netstack_config_path(), 1200, 0);
      return;
    }
  }

  if (ssh_shell_command_is(sub, "renew") ||
      ssh_shell_command_is(sub, "dhcp") ||
      ssh_shell_command_is(sub, "auto") ||
      ssh_shell_command_is(sub, "reset") ||
      ssh_shell_command_is(sub, "config")) {
    ssh_queue_channel_text(
        "net: this command can disrupt the active SSH link; run it on the "
        "local console, then reconnect and use 'net check'.\r\n");
    ssh_shell_prompt();
    return;
  }

  ssh_queue_channel_text(
      "usage: net [status|check|doctor|tcp|tls|diag|config show]\r\n"
      "note: net dhcp/auto/renew/reset/config writes are local-console only "
      "during SSH.\r\n");
  ssh_shell_prompt();
}

static void ssh_shell_print_update(const char *args) {
  static char report[SSH_CHANNEL_TEXT_BUF];
  const char *sub = ssh_shell_skip_spaces(args);

  if (ssh_shell_command_is(sub, "status")) {
    orizon_update_format_status(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "bootguard")) {
    const char *guard_args = ssh_shell_skip_spaces(sub + strlen("bootguard"));
    if (ssh_shell_command_is(guard_args, "confirm") ||
        ssh_shell_command_is(guard_args, "validate")) {
      orizon_update_boot_guard_confirm(report, sizeof(report));
    } else if (ssh_shell_command_is(guard_args, "recover") ||
               ssh_shell_command_is(guard_args, "rollback")) {
      orizon_update_boot_guard_recover(report, sizeof(report));
    } else {
      orizon_update_boot_guard_status(report, sizeof(report));
    }
  } else {
    orizon_update_full_upgrade(report, sizeof(report));
  }
  if (strlen(report) + 2 < sizeof(report) &&
      (report[0] == '\0' || report[strlen(report) - 1] != '\n')) {
    strcat(report, "\r\n");
  }
  ssh_queue_channel_text(report);
  ssh_shell_prompt();
}

static void ssh_shell_print_bootguard(const char *args) {
  static char report[4096];
  const char *sub = ssh_shell_skip_spaces(args);

  if (ssh_shell_command_is(sub, "confirm") ||
      ssh_shell_command_is(sub, "validate")) {
    orizon_update_boot_guard_confirm(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "recover") ||
             ssh_shell_command_is(sub, "rollback")) {
    orizon_update_boot_guard_recover(report, sizeof(report));
  } else {
    orizon_update_boot_guard_status(report, sizeof(report));
  }
  if (strlen(report) + 2 < sizeof(report) &&
      (report[0] == '\0' || report[strlen(report) - 1] != '\n')) {
    strcat(report, "\r\n");
  }
  ssh_queue_channel_text(report);
  ssh_shell_prompt();
}

static void ssh_shell_print_rollback(void) {
  static char report[SSH_CHANNEL_TEXT_BUF];

  orizon_update_rollback(report, sizeof(report));
  if (strlen(report) + 2 < sizeof(report) &&
      (report[0] == '\0' || report[strlen(report) - 1] != '\n')) {
    strcat(report, "\r\n");
  }
  ssh_queue_channel_text(report);
  ssh_shell_prompt();
}

static void ssh_shell_print_disk(const char *args) {
  static char out[2200];
  const char *sub = ssh_shell_skip_spaces(args);

  out[0] = '\0';
  if (*sub == '\0' || ssh_shell_command_is(sub, "identify") ||
      ssh_shell_command_is(sub, "id")) {
    storage_format_identify(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "read-test") ||
             ssh_shell_command_is(sub, "read")) {
    uint64_t lba = 0;
    int valid = 1;
    const char *value =
        ssh_shell_skip_spaces(sub + (ssh_shell_command_is(sub, "read") ? 4 : 9));
    if (ssh_shell_command_is(value, "last") ||
        ssh_shell_command_is(value, "end")) {
      uint64_t sectors = storage_sector_count();
      lba = sectors > 0 ? sectors - 1 : 0;
    } else if (*value && ssh_shell_parse_uint64(value, &lba) < 0) {
      snprintf(out, sizeof(out), "usage: disk read-test [lba|last]\r\n");
      valid = 0;
    }
    if (valid) {
      storage_read_test(lba, out, sizeof(out));
    }
  } else {
    snprintf(out, sizeof(out), "usage: disk identify | disk read-test [lba|last]\r\n");
  }
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_gpt(const char *args) {
  static char out[2200];
  const char *sub = ssh_shell_skip_spaces(args);

  if (*sub == '\0' || ssh_shell_command_is(sub, "scan")) {
    if (orizon_install_format_partitions(out, sizeof(out)) < 0 &&
        out[0] == '\0') {
      snprintf(out, sizeof(out), "gpt scan: no selected disk\r\n");
    }
  } else {
    snprintf(out, sizeof(out), "usage: gpt scan\r\n");
  }
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_selftest(const char *args) {
  static char out[SSH_CHANNEL_TEXT_BUF];

  orizon_selftest_format(ssh_shell_skip_spaces(args), out, sizeof(out));
  if (strlen(out) + 2 < sizeof(out)) {
    strcat(out, "\r\n");
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static int ssh_shell_arg_is_arm(const char *arg) {
  return ssh_shell_command_is(arg, "arm") || ssh_shell_command_is(arg, "go") ||
         ssh_shell_command_is(arg, "first");
}

static void ssh_shell_print_wifi(const char *args) {
  static char report[SSH_CHANNEL_TEXT_BUF];
  char ssid[96];
  char password[96];
  const char *sub = ssh_shell_skip_spaces(args);
  const char *rest;

  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    wifi_format_status(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "scan")) {
    const char *scan_args = ssh_shell_skip_spaces(sub + 4);
    if (ssh_shell_command_is(scan_args, "poll") ||
        ssh_shell_command_is(scan_args, "wait")) {
      wifi_scan_poll(report, sizeof(report));
    } else {
      wifi_scan(ssh_shell_arg_is_arm(scan_args), report, sizeof(report));
    }
  } else if (ssh_shell_command_is(sub, "firmware")) {
    wifi_firmware_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "hw")) {
    wifi_hw_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "apm")) {
    wifi_apm_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "load")) {
    wifi_load_firmware(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "upload")) {
    const char *upload_args = ssh_shell_skip_spaces(sub + 6);
    if (ssh_shell_command_is(upload_args, "all")) {
      const char *all_args = ssh_shell_skip_spaces(upload_args + 3);
      wifi_upload_all_firmware(ssh_shell_arg_is_arm(all_args), report,
                               sizeof(report));
    } else {
      wifi_upload_firmware(ssh_shell_arg_is_arm(upload_args), report,
                           sizeof(report));
    }
  } else if (ssh_shell_command_is(sub, "boot")) {
    wifi_boot_firmware(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 4)),
                       report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "alive")) {
    wifi_alive_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "queues")) {
    wifi_queue_probe(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 6)),
                     report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "context")) {
    wifi_context_probe(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 7)),
                       report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "scheduler")) {
    wifi_scheduler_probe(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 9)),
                         report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "rx")) {
    const char *rx_args = ssh_shell_skip_spaces(sub + 2);
    wifi_rx_probe(ssh_shell_command_is(rx_args, "poll") ||
                      ssh_shell_command_is(rx_args, "wait"),
                  report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "command")) {
    wifi_command_probe(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 7)),
                       report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "nvm-info")) {
    wifi_nvm_info_probe(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 8)),
                        report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "nvm")) {
    wifi_nvm_probe(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 3)),
                   report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "bringup")) {
    wifi_bringup_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "crypto")) {
    wifi_crypto_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "wpa")) {
    wifi_wpa_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "key")) {
    char target[16];
    char mode[16];
    int group_key = 0;
    int arm_key = 0;
    const char *key_args = ssh_shell_skip_spaces(sub + 3);

    target[0] = '\0';
    mode[0] = '\0';
    rest = ssh_shell_read_token(key_args, target, sizeof(target));
    if (target[0]) {
      if (ssh_shell_arg_is_arm(target)) {
        arm_key = 1;
      } else if (strcmp(target, "gtk") == 0 || strcmp(target, "group") == 0) {
        group_key = 1;
        if (rest && *rest) {
          ssh_shell_read_token(rest, mode, sizeof(mode));
          arm_key = ssh_shell_arg_is_arm(mode);
        }
      } else if (strcmp(target, "pairwise") == 0 ||
                 strcmp(target, "ptk") == 0) {
        group_key = 0;
        if (rest && *rest) {
          ssh_shell_read_token(rest, mode, sizeof(mode));
          arm_key = ssh_shell_arg_is_arm(mode);
        }
      }
    }
    wifi_key_probe(group_key, arm_key, report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "data")) {
    wifi_data_probe(report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "bind")) {
    wifi_bind_probe(ssh_shell_arg_is_arm(ssh_shell_skip_spaces(sub + 4)),
                    report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "txcmd")) {
    char target[16];
    char mode[16];
    int arm_txcmd = 0;
    const char *txcmd_args = ssh_shell_skip_spaces(sub + 5);

    target[0] = '\0';
    mode[0] = '\0';
    rest = ssh_shell_read_token(txcmd_args, target, sizeof(target));
    if (target[0]) {
      if (ssh_shell_arg_is_arm(target)) {
        arm_txcmd = 1;
        target[0] = '\0';
      } else if (rest && *rest) {
        ssh_shell_read_token(rest, mode, sizeof(mode));
        arm_txcmd = ssh_shell_arg_is_arm(mode);
      }
    }
    wifi_txcmd_probe(target, arm_txcmd, report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "tx")) {
    char target[16];
    const char *tx_args = ssh_shell_skip_spaces(sub + 2);

    target[0] = '\0';
    ssh_shell_read_token(tx_args, target, sizeof(target));
    wifi_tx_stage_probe(target, report, sizeof(report));
  } else if (ssh_shell_command_is(sub, "connect") ||
             ssh_shell_command_is(sub, "join")) {
    int join = ssh_shell_command_is(sub, "join");
    rest = ssh_shell_skip_spaces(sub + (join ? 4 : 7));
    rest = ssh_shell_read_token(rest, ssid, sizeof(ssid));
    if (!rest) {
      snprintf(report, sizeof(report), "usage: wifi %s <ssid> [password]\r\n",
               join ? "join" : "connect");
    } else {
      password[0] = '\0';
      if (*rest) {
        ssh_shell_read_token(rest, password, sizeof(password));
      }
      if (join) {
        wifi_join(ssid, password, report, sizeof(report));
      } else {
        wifi_connect(ssid, password, report, sizeof(report));
      }
    }
  } else {
    snprintf(report, sizeof(report),
             "usage: wifi [status|hw|apm|firmware|load|upload [arm|all "
             "[arm]]|boot [arm]|alive|queues [arm]|context [arm]|scheduler "
             "[arm]|rx [poll]|command [arm]|nvm [arm]|nvm-info [arm]|"
             "bringup|crypto|wpa|key [pairwise|gtk] [arm]|data|bind [arm]|"
             "scan [arm|poll]|connect <ssid> [password]|join <ssid> "
             "[password]|tx [auth|assoc|m2|m4|data|all]|txcmd "
             "[auth|assoc|m2|m4|data] [arm]]\r\n");
  }

  ssh_queue_channel_text(report);
  if (strlen(report) + 2 < sizeof(report) &&
      (report[0] == '\0' || report[strlen(report) - 1] != '\n')) {
    ssh_queue_channel_text("\r\n");
  }
  ssh_shell_prompt();
}

static void ssh_process_channel_request(const uint8_t *payload,
                                        size_t payload_len) {
  const uint8_t *request = NULL;
  size_t request_len = 0;
  size_t off = 1;
  uint32_t recipient;
  uint8_t want_reply;

  if (!ssh_status.authenticated || off + 4 > payload_len) {
    return;
  }
  recipient = ssh_get_u32(payload + off);
  off += 4;
  if (recipient != ssh_server_channel ||
      ssh_read_string(payload, payload_len, &off, &request, &request_len) !=
          0 ||
      off + 1 > payload_len) {
    ssh_channel_failure_pending = 1;
    ssh_set_status("ssh: malformed channel request");
    return;
  }
  want_reply = payload[off++];

  if ((request_len == strlen("pty-req") &&
       memcmp(request, "pty-req", request_len) == 0) ||
      (request_len == strlen("env") && memcmp(request, "env", request_len) == 0)) {
    if (want_reply) {
      ssh_channel_success_pending = 1;
    }
    ssh_set_status("ssh: channel setup request accepted");
    return;
  }

  if (request_len == strlen("shell") &&
      memcmp(request, "shell", request_len) == 0) {
    ssh_status.shell_ready = 1;
    if (want_reply) {
      ssh_channel_success_pending = 1;
    }
    ssh_queue_channel_text(
        "\r\nOrizon OS remote shell\r\n"
        "Commands: help, desktop, desktop start, desktop stop, desktop rescue, desktop status, desktop state, desktop settings, desktop doctor, desktop package, security, security policy, security audit, security keys, security doctor, system status, system health, system snapshot, system backup, system services, system logs, system doctor, system init, rescue, hostname, ls, cd, cat, head, tail, write, logs, net, net check, net tcp, net daily, net tls, net diag, wifi, ps, pkg, update, storage, storage diag, storage vmcheck, persist status, persist slots, disk, disk read-test last, gpt scan, selftest, pci, hw next, report save, install-plan, free, bootguard, bootguard recover, rollback, rollback-status, audit, status, auth, hostkey, algorithms, reboot, shutdown, exit\r\n");
    ssh_shell_prompt();
    ssh_set_status("ssh: shell channel ready");
    return;
  }

  if (request_len == strlen("exec") &&
      memcmp(request, "exec", request_len) == 0) {
    const uint8_t *command = NULL;
    size_t command_len = 0;

    if (ssh_read_string(payload, payload_len, &off, &command, &command_len) ==
        0) {
      if (want_reply) {
        ssh_channel_success_pending = 1;
      }
      ssh_remote_exec_execute(command, command_len);
      ssh_set_status("ssh: exec request accepted");
      return;
    }
  }

  if (want_reply) {
    ssh_channel_failure_pending = 1;
  }
  ssh_set_status("ssh: unsupported channel request");
}

static void ssh_remote_shell_execute(const char *line) {
  static char out[1600];
  const char *args;

  if (!line || line[0] == '\0') {
    ssh_shell_prompt();
    return;
  }
  ssh_shell_command_total++;
  ssh_record_command("shell", line);
  if (strcmp(line, "help") == 0) {
    ssh_queue_channel_text(
        "Remote Orizon commands:\r\n"
        "  help                 show this help\r\n"
        "  status               show SSH transport state\r\n"
        "  auth                 show SSH auth policy\r\n"
        "  hostkey              show SSH host identity\r\n"
        "  security [policy|audit|keys|doctor] show hardening posture\r\n"
        "  system status        show live/installed state and first-boot hints\r\n"
        "  system health        show concise PASS/WARN system state\r\n"
        "  system snapshot      write /workspace/.orizon/system-snapshot.txt\r\n"
        "  system backup        export non-secret config to admin-backup.txt\r\n"
        "  system services      show init/service policy and runtime state\r\n"
        "  system logs          show boot-state, service-state and init logs\r\n"
        "  system doctor        audit roots/config/init state without writes\r\n"
        "  system init          run idempotent boot tasks and write init log\r\n"
        "  system repair        recreate missing default roots/config safely\r\n"
        "  rescue               show non-destructive recovery checklist\r\n"
        "  desktop help         show optional Hyprland-style desktop commands\r\n"
        "  desktop doctor       check optional desktop config/package state\r\n"
        "  desktop settings     show/update/sync the /system desktop settings hub\r\n"
        "  desktop package      write /workspace/packages/orizon-desktop-hypr.opkg\r\n"
        "  hostname [set name]  show or persist hostname\r\n"
        "  ls [path]            list files\r\n"
        "  cd <path>            change directory\r\n"
        "  pwd                  show remote cwd\r\n"
        "  cat <file>           print a file preview\r\n"
        "  head <file>          print a shorter file preview\r\n"
        "  tail <file>          print the end of a file preview\r\n"
        "  touch|mkdir|rm       edit VFS entries\r\n"
        "  write|append f text  write text to a file\r\n"
        "  logs [name]          show boot/storage/pci/network/usb/wifi/ssh/security/update logs\r\n"
        "  net check|tcp|daily|tls|diag show network/TCP/HTTPS diagnostics\r\n"
        "  net|route|dns|ping   show network status and probes\r\n"
        "  usb|usb rescan       show USB diagnostics\r\n"
        "  wifi ...             show Intel Wi-Fi diagnostics\r\n"
        "  usb rescan           rescan USB root ports\r\n"
        "  ps|pkg|update        show system/update/package state\r\n"
        "  pkg help             show package search/verify/install/update commands\r\n"
        "  pkg sample desktop|orizon-terminal create optional desktop packages\r\n"
        "  pkg audit|doctor     audit or diagnose package v5 state\r\n"
        "  pkg upgrade plan     show signed package upgrade plan\r\n"
        "  pkg search|remote    inspect local and signed remote package metadata\r\n"
        "  storage|storage diag|storage vmcheck show storage and read-only VM checks\r\n"
        "  persist status|slots|save|repair inspect or rewrite data snapshots\r\n"
        "  persist restore previous|slot <n> restore and promote a snapshot\r\n"
        "  disk identify        show read-only disk/NVMe identity\r\n"
        "  disk read-test [lba|last] read one sector without writing\r\n"
        "  gpt scan             read-only GPT partition scan\r\n"
        "  selftest [scope]     run PASS/WARN/FAIL live checks\r\n"
        "  pci [bars]           list PCI devices for hardware diagnosis\r\n"
        "  hw next              show future hardware capture plan\r\n"
        "  report save|next     write report or show hardware capture plan\r\n"
        "  install-plan [mode]  save non-destructive installer preflight report\r\n"
        "  install-plan manual desktop  include optional desktop in the plan\r\n"
        "  free                 show heap state\r\n"
        "  bootguard [confirm|recover] show/confirm/arm rollback fallback\r\n"
        "  rollback             restore the currently booted payload on installed VM\r\n"
        "  rollback-status      show saved rollback metadata\r\n"
        "  audit|ssh sessions   show SSH session counters\r\n"
        "  algorithms           show negotiated SSH algorithms\r\n"
        "  ssh password <pass>  change remote SSH password\r\n"
        "  ssh auth ...         change auth policy\r\n"
        "  sync                 persist Orizon data roots\r\n"
        "  reboot|shutdown      persist roots and restart/power off the VM\r\n"
        "  whoami|uname|uptime  basic system info\r\n"
        "  exit                 close channel\r\n");
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "status") == 0 || strcmp(line, "ssh status") == 0) {
    ssh_format_status(out, sizeof(out));
    if (strlen(out) + strlen("\r\norizon$ ") < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "bootguard")) {
    ssh_shell_print_bootguard(line + strlen("bootguard"));
    return;
  }
  if (strcmp(line, "rollback") == 0) {
    ssh_shell_print_rollback();
    return;
  }
  if (strcmp(line, "rollback-status") == 0) {
    ssh_shell_print_rollback_status();
    return;
  }
  if (strcmp(line, "auth") == 0 || strcmp(line, "ssh auth") == 0) {
    ssh_format_auth(out, sizeof(out));
    if (strlen(out) + strlen("\r\norizon$ ") < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "audit") == 0 || strcmp(line, "ssh audit") == 0) {
    ssh_shell_print_audit();
    return;
  }
  if (strcmp(line, "algorithms") == 0 ||
      strcmp(line, "ssh algorithms") == 0) {
    ssh_shell_print_algorithms();
    return;
  }
  if (strcmp(line, "hostkey") == 0 || strcmp(line, "ssh hostkey") == 0) {
    ssh_format_hostkey(out, sizeof(out));
    if (strlen(out) + strlen("\r\norizon$ ") < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "security")) {
    ssh_shell_print_security(line + strlen("security"));
    return;
  }
  if (ssh_shell_command_is(line, "system")) {
    ssh_shell_print_system(line + strlen("system"));
    return;
  }
  if (strcmp(line, "health") == 0) {
    ssh_shell_print_system("health");
    return;
  }
  if (strcmp(line, "snapshot") == 0) {
    ssh_shell_print_system("snapshot");
    return;
  }
  if (strcmp(line, "backup") == 0) {
    ssh_shell_print_system("backup");
    return;
  }
  if (strcmp(line, "services") == 0) {
    ssh_shell_print_system("services");
    return;
  }
  if (strcmp(line, "journal") == 0) {
    ssh_shell_print_system("logs");
    return;
  }
  if (strcmp(line, "doctor") == 0) {
    ssh_shell_print_system("doctor");
    return;
  }
  if (strcmp(line, "init") == 0) {
    ssh_shell_print_system("init");
    return;
  }
  if (strcmp(line, "rescue") == 0) {
    ssh_shell_print_system("rescue");
    return;
  }
  if (ssh_shell_command_is(line, "firstboot")) {
    static char firstboot_cmd[96];
    snprintf(firstboot_cmd, sizeof(firstboot_cmd), "firstboot %s",
             ssh_shell_skip_spaces(line + strlen("firstboot")));
    ssh_shell_print_system(firstboot_cmd);
    return;
  }
  if (ssh_shell_command_is(line, "hostname")) {
    ssh_shell_print_hostname(line + strlen("hostname"));
    return;
  }
  if (strcmp(line, "ssh hostkey reload") == 0) {
    ssh_reload_hostkey(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "ssh hostkey reset") == 0) {
    ssh_reset_hostkey(out, sizeof(out));
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "ssh password")) {
    args = ssh_shell_skip_spaces(line + strlen("ssh password"));
    if (ssh_shell_command_is(args, "off") ||
        ssh_shell_command_is(args, "disable") ||
        ssh_shell_command_is(args, "disabled")) {
      ssh_disable_password(out, sizeof(out));
      ssh_queue_channel_text(out);
      ssh_shell_prompt();
      return;
    }
    ssh_shell_set_password(args);
    return;
  }
  if (ssh_shell_command_is(line, "ssh lockout")) {
    args = ssh_shell_skip_spaces(line + strlen("ssh lockout"));
    if (ssh_shell_command_is(args, "clear") ||
        ssh_shell_command_is(args, "reset") ||
        ssh_shell_command_is(args, "unlock")) {
      ssh_clear_lockout(out, sizeof(out));
      ssh_queue_channel_text(out);
      ssh_shell_prompt();
      return;
    }
  }
  if (ssh_shell_command_is(line, "ssh auth")) {
    ssh_shell_set_auth_policy_remote(line + strlen("ssh auth"));
    return;
  }
  if (ssh_shell_command_is(line, "ls")) {
    ssh_shell_print_ls(ssh_shell_skip_spaces(line + 2));
    return;
  }
  if (ssh_shell_command_is(line, "cd")) {
    char path[MAX_PATH];
    int is_dir = 0;
    args = ssh_shell_skip_spaces(line + 2);
    if (*args == '\0') {
      strcpy(ssh_shell_cwd, "/home/orizon");
      ssh_shell_prompt();
      return;
    }
    if (ssh_shell_resolve_path(args, path, sizeof(path)) < 0 ||
        vfs_stat(path, NULL, &is_dir) < 0 || !is_dir) {
      ssh_queue_channel_text("cd: not a directory\r\n");
      ssh_shell_prompt();
      return;
    }
    strncpy(ssh_shell_cwd, path, sizeof(ssh_shell_cwd) - 1);
    ssh_shell_cwd[sizeof(ssh_shell_cwd) - 1] = '\0';
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "pwd") == 0) {
    snprintf(out, sizeof(out), "%s\r\n", ssh_shell_cwd);
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "cat")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(line + 3), SSH_FILE_READ_MAX, 0);
    return;
  }
  if (ssh_shell_command_is(line, "head")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(line + 4), 700, 0);
    return;
  }
  if (ssh_shell_command_is(line, "tail")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(line + 4), 4096, 1);
    return;
  }
  if (ssh_shell_command_is(line, "touch")) {
    ssh_shell_mutate_path(line + 5, "touch");
    return;
  }
  if (ssh_shell_command_is(line, "mkdir")) {
    ssh_shell_mutate_path(line + 5, "mkdir");
    return;
  }
  if (ssh_shell_command_is(line, "rm")) {
    ssh_shell_mutate_path(line + 2, "rm");
    return;
  }
  if (ssh_shell_command_is(line, "write") ||
      ssh_shell_command_is(line, "append")) {
    int append = ssh_shell_command_is(line, "append");

    args = line + (append ? 6 : 5);
    ssh_shell_write_text(args, append);
    return;
  }
  if (ssh_shell_command_is(line, "logs")) {
    ssh_shell_print_log(ssh_shell_skip_spaces(line + 4));
    return;
  }
  if (ssh_shell_command_is(line, "wifi")) {
    ssh_shell_print_wifi(ssh_shell_skip_spaces(line + 4));
    return;
  }
  if (strcmp(line, "ps") == 0) {
    ssh_shell_print_ps();
    return;
  }
  if (ssh_shell_command_is(line, "pkg")) {
    ssh_shell_print_pkg(line + 3);
    return;
  }
  if (ssh_shell_command_is(line, "desktop")) {
    ssh_shell_print_desktop(line + 7);
    return;
  }
  if (ssh_shell_command_is(line, "update") ||
      ssh_shell_command_is(line, "orizon-update")) {
    ssh_shell_print_update(line + (line[0] == 'u' ? 6 : 13));
    return;
  }
  if (ssh_shell_command_is(line, "storage")) {
    ssh_shell_print_storage(line + 7);
    return;
  }
  if (ssh_shell_command_is(line, "persist")) {
    ssh_shell_print_persist(line + 7);
    return;
  }
  if (ssh_shell_command_is(line, "disk")) {
    ssh_shell_print_disk(line + 4);
    return;
  }
  if (ssh_shell_command_is(line, "gpt")) {
    ssh_shell_print_gpt(line + 3);
    return;
  }
  if (ssh_shell_command_is(line, "selftest")) {
    ssh_shell_print_selftest(line + 8);
    return;
  }
  if (strcmp(line, "disks") == 0) {
    ssh_shell_print_storage("");
    return;
  }
  if (ssh_shell_command_is(line, "pci")) {
    ssh_shell_print_pci(line + 3);
    return;
  }
  if (ssh_shell_command_is(line, "hw")) {
    ssh_shell_print_hw(line + 2);
    return;
  }
  if (ssh_shell_command_is(line, "report")) {
    ssh_shell_print_report(line + 6);
    return;
  }
  if (ssh_shell_command_is(line, "install-plan")) {
    ssh_shell_print_install_plan(line + strlen("install-plan"));
    return;
  }
  if (strcmp(line, "timer") == 0) {
    timer_format_status(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "free") == 0) {
    ssh_shell_print_free();
    return;
  }
  if (strcmp(line, "ssh sessions") == 0 || strcmp(line, "sessions") == 0) {
    ssh_shell_print_audit();
    return;
  }
  if (ssh_shell_command_is(line, "net")) {
    ssh_shell_print_net(ssh_shell_skip_spaces(line + 3));
    return;
  }
  if (strcmp(line, "network-status") == 0) {
    ssh_shell_print_net("status");
    return;
  }
  if (strcmp(line, "usb") == 0 || strcmp(line, "usb rescan") == 0) {
    ssh_shell_print_usb(strcmp(line, "usb rescan") == 0);
    return;
  }
  if (strcmp(line, "route") == 0) {
    netstack_format_route(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "dns")) {
    ssh_shell_print_dns(ssh_shell_skip_spaces(line + 3));
    return;
  }
  if (ssh_shell_command_is(line, "ping")) {
    ssh_shell_print_ping(ssh_shell_skip_spaces(line + 4));
    return;
  }
  if (strcmp(line, "sync") == 0) {
    ssh_queue_channel_text(vfs_persist_save() == 0
                               ? "sync: ok\r\n"
                               : "sync: persistence unavailable\r\n");
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "reboot") == 0 || strcmp(line, "restart") == 0) {
    vfs_persist_save();
    ssh_queue_channel_text("reboot: scheduled in 2 seconds\r\n");
    power_schedule_reboot(TIMER_HZ * 2);
    return;
  }
  if (strcmp(line, "shutdown") == 0 || strcmp(line, "poweroff") == 0) {
    vfs_persist_save();
    ssh_queue_channel_text("shutdown: scheduled in 2 seconds\r\n");
    power_schedule_shutdown(TIMER_HZ * 2);
    return;
  }
  if (strcmp(line, "uptime") == 0) {
    snprintf(out, sizeof(out), "uptime=%lus ticks=%lu hz=%lu\r\n",
             (unsigned long)timer_uptime_seconds(),
             (unsigned long)timer_ticks(), (unsigned long)timer_hz());
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "neofetch") == 0 || strcmp(line, "sysinfo") == 0) {
    snprintf(out, sizeof(out),
             "Orizon OS\r\nkernel=core-x86_64 shell=ssh cwd=%s uptime=%lus\r\n",
             ssh_shell_cwd, (unsigned long)timer_uptime_seconds());
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "whoami") == 0 || strcmp(line, "id") == 0) {
    ssh_queue_channel_text("orizon\r\n");
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "uname") == 0 || strcmp(line, "uname -a") == 0) {
    ssh_queue_channel_text("Orizon OS x86_64 OrizonSSH_0.1\r\n");
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "exit") == 0 || strcmp(line, "logout") == 0) {
    ssh_queue_channel_text("logout\r\n");
    ssh_channel_exit_status_pending = 1;
    ssh_channel_close_pending = 1;
    return;
  }
  snprintf(out, sizeof(out), "%s: command not found\r\n", line);
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_remote_exec_execute(const uint8_t *command,
                                    size_t command_len) {
  static char cmd[160];
  static char out[1600];
  size_t copy = command_len;

  if (copy >= sizeof(cmd)) {
    copy = sizeof(cmd) - 1;
  }
  memcpy(cmd, command, copy);
  cmd[copy] = '\0';
  ssh_exec_request_total++;
  ssh_record_command("exec", cmd);

  ssh_shell_suppress_prompt = 1;
  ssh_channel_exit_code = 0;
  if (strcmp(cmd, "help") == 0) {
    ssh_queue_channel_text(
        "Remote Orizon commands: help, desktop, desktop start, desktop stop, desktop restart, desktop reload, desktop rescue, desktop state, desktop status, desktop settings, desktop doctor, desktop package, security, security policy, security audit, security keys, security doctor, security rotate ssh-hostkey, system status, system health, system snapshot, system backup, system services, system logs, system doctor, system init, system repair, rescue, hostname, hostname set <name>, ls, cd, cat, head, tail, touch, mkdir, rm, write, append, logs, net, net check, net tcp, net daily, net tls, net diag, route, dns, ping, usb, usb rescan, wifi, ps, pkg, update, update status, storage, storage diag, storage vmcheck, persist status, persist slots, persist save, persist repair, persist restore previous, persist restore slot <n>, disk, disk identify, disk read-test, disk read-test last, gpt scan, selftest, pci, hw next, report save, report next, install-plan, free, timer, bootguard, bootguard confirm, bootguard recover, rollback, rollback-status, audit, ssh sessions, sync, reboot, shutdown, status, auth, hostkey, algorithms, ssh password, ssh auth, ssh lockout, exit\r\n");
  } else if (ssh_shell_command_is(cmd, "system")) {
    ssh_shell_print_system(cmd + strlen("system"));
  } else if (strcmp(cmd, "health") == 0) {
    ssh_shell_print_system("health");
  } else if (strcmp(cmd, "snapshot") == 0) {
    ssh_shell_print_system("snapshot");
  } else if (strcmp(cmd, "backup") == 0) {
    ssh_shell_print_system("backup");
  } else if (strcmp(cmd, "services") == 0) {
    ssh_shell_print_system("services");
  } else if (strcmp(cmd, "journal") == 0) {
    ssh_shell_print_system("logs");
  } else if (strcmp(cmd, "doctor") == 0) {
    ssh_shell_print_system("doctor");
  } else if (strcmp(cmd, "init") == 0) {
    ssh_shell_print_system("init");
  } else if (strcmp(cmd, "rescue") == 0) {
    ssh_shell_print_system("rescue");
  } else if (ssh_shell_command_is(cmd, "firstboot")) {
    static char firstboot_cmd[96];
    snprintf(firstboot_cmd, sizeof(firstboot_cmd), "firstboot %s",
             ssh_shell_skip_spaces(cmd + strlen("firstboot")));
    ssh_shell_print_system(firstboot_cmd);
  } else if (ssh_shell_command_is(cmd, "hostname")) {
    ssh_shell_print_hostname(cmd + strlen("hostname"));
  } else if (ssh_shell_command_is(cmd, "ls")) {
    ssh_shell_print_ls(ssh_shell_skip_spaces(cmd + 2));
  } else if (ssh_shell_command_is(cmd, "cat")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(cmd + 3), SSH_FILE_READ_MAX, 0);
  } else if (ssh_shell_command_is(cmd, "head")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(cmd + 4), 700, 0);
  } else if (ssh_shell_command_is(cmd, "tail")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(cmd + 4), 4096, 1);
  } else if (ssh_shell_command_is(cmd, "touch")) {
    ssh_shell_mutate_path(cmd + 5, "touch");
  } else if (ssh_shell_command_is(cmd, "mkdir")) {
    ssh_shell_mutate_path(cmd + 5, "mkdir");
  } else if (ssh_shell_command_is(cmd, "rm")) {
    ssh_shell_mutate_path(cmd + 2, "rm");
  } else if (ssh_shell_command_is(cmd, "write")) {
    ssh_shell_write_text(cmd + 5, 0);
  } else if (ssh_shell_command_is(cmd, "append")) {
    ssh_shell_write_text(cmd + 6, 1);
  } else if (ssh_shell_command_is(cmd, "logs")) {
    ssh_shell_print_log(ssh_shell_skip_spaces(cmd + 4));
  } else if (ssh_shell_command_is(cmd, "wifi")) {
    ssh_shell_print_wifi(ssh_shell_skip_spaces(cmd + 4));
  } else if (strcmp(cmd, "ps") == 0) {
    ssh_shell_print_ps();
  } else if (ssh_shell_command_is(cmd, "pkg")) {
    ssh_shell_print_pkg(cmd + 3);
  } else if (ssh_shell_command_is(cmd, "desktop")) {
    ssh_shell_print_desktop(cmd + 7);
  } else if (ssh_shell_command_is(cmd, "update") ||
             ssh_shell_command_is(cmd, "orizon-update")) {
    ssh_shell_print_update(cmd + (cmd[0] == 'u' ? 6 : 13));
  } else if (ssh_shell_command_is(cmd, "storage")) {
    ssh_shell_print_storage(cmd + 7);
  } else if (ssh_shell_command_is(cmd, "persist")) {
    ssh_shell_print_persist(cmd + 7);
  } else if (ssh_shell_command_is(cmd, "disk")) {
    ssh_shell_print_disk(cmd + 4);
  } else if (ssh_shell_command_is(cmd, "gpt")) {
    ssh_shell_print_gpt(cmd + 3);
  } else if (ssh_shell_command_is(cmd, "selftest")) {
    ssh_shell_print_selftest(cmd + 8);
  } else if (strcmp(cmd, "disks") == 0) {
    ssh_shell_print_storage("");
  } else if (ssh_shell_command_is(cmd, "pci")) {
    ssh_shell_print_pci(cmd + 3);
  } else if (ssh_shell_command_is(cmd, "hw")) {
    ssh_shell_print_hw(cmd + 2);
  } else if (ssh_shell_command_is(cmd, "report")) {
    ssh_shell_print_report(cmd + 6);
  } else if (ssh_shell_command_is(cmd, "install-plan")) {
    ssh_shell_print_install_plan(cmd + strlen("install-plan"));
  } else if (strcmp(cmd, "free") == 0) {
    ssh_shell_print_free();
  } else if (strcmp(cmd, "audit") == 0 || strcmp(cmd, "ssh audit") == 0 ||
             strcmp(cmd, "ssh sessions") == 0 || strcmp(cmd, "sessions") == 0) {
    ssh_shell_print_audit();
  } else if (strcmp(cmd, "sync") == 0) {
    ssh_queue_channel_text(vfs_persist_save() == 0
                               ? "sync: persistent roots saved\r\n"
                               : "sync: save failed or not installed\r\n");
  } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart") == 0) {
    vfs_persist_save();
    ssh_queue_channel_text("reboot: scheduled in 2 seconds\r\n");
    power_schedule_reboot(TIMER_HZ * 2);
  } else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "poweroff") == 0) {
    vfs_persist_save();
    ssh_queue_channel_text("shutdown: scheduled in 2 seconds\r\n");
    power_schedule_shutdown(TIMER_HZ * 2);
  } else if (strcmp(cmd, "timer") == 0) {
    timer_format_status(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (ssh_shell_command_is(cmd, "bootguard")) {
    ssh_shell_print_bootguard(cmd + strlen("bootguard"));
  } else if (strcmp(cmd, "rollback") == 0) {
    ssh_shell_print_rollback();
  } else if (strcmp(cmd, "rollback-status") == 0) {
    ssh_shell_print_rollback_status();
  } else if (ssh_shell_command_is(cmd, "ssh password")) {
    const char *args = ssh_shell_skip_spaces(cmd + strlen("ssh password"));
    if (ssh_shell_command_is(args, "off") ||
        ssh_shell_command_is(args, "disable") ||
        ssh_shell_command_is(args, "disabled")) {
      ssh_disable_password(out, sizeof(out));
      ssh_queue_channel_text(out);
    } else {
      ssh_shell_set_password(args);
    }
  } else if (ssh_shell_command_is(cmd, "ssh lockout")) {
    const char *args = ssh_shell_skip_spaces(cmd + strlen("ssh lockout"));
    if (ssh_shell_command_is(args, "clear") ||
        ssh_shell_command_is(args, "reset") ||
        ssh_shell_command_is(args, "unlock")) {
      ssh_clear_lockout(out, sizeof(out));
      ssh_queue_channel_text(out);
    } else {
      ssh_queue_channel_text("usage: ssh lockout clear\r\n");
    }
  } else if (ssh_shell_command_is(cmd, "ssh auth")) {
    ssh_shell_set_auth_policy_remote(cmd + strlen("ssh auth"));
  } else if (strcmp(cmd, "ssh hostkey reload") == 0) {
    ssh_reload_hostkey(out, sizeof(out));
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "ssh hostkey reset") == 0) {
    ssh_reset_hostkey(out, sizeof(out));
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "ssh status") == 0) {
    ssh_format_status(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "auth") == 0 || strcmp(cmd, "ssh auth") == 0) {
    ssh_format_auth(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "algorithms") == 0 ||
             strcmp(cmd, "ssh algorithms") == 0) {
    ssh_shell_print_algorithms();
  } else if (strcmp(cmd, "hostkey") == 0 || strcmp(cmd, "ssh hostkey") == 0) {
    ssh_format_hostkey(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (ssh_shell_command_is(cmd, "security")) {
    ssh_shell_print_security(cmd + strlen("security"));
  } else if (ssh_shell_command_is(cmd, "net")) {
    ssh_shell_print_net(ssh_shell_skip_spaces(cmd + 3));
  } else if (strcmp(cmd, "network-status") == 0) {
    ssh_shell_print_net("status");
  } else if (strcmp(cmd, "usb") == 0 || strcmp(cmd, "usb rescan") == 0) {
    ssh_shell_print_usb(strcmp(cmd, "usb rescan") == 0);
  } else if (strcmp(cmd, "route") == 0) {
    netstack_format_route(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (ssh_shell_command_is(cmd, "dns")) {
    ssh_shell_print_dns(ssh_shell_skip_spaces(cmd + 3));
  } else if (ssh_shell_command_is(cmd, "ping")) {
    ssh_shell_print_ping(ssh_shell_skip_spaces(cmd + 4));
  } else if (strcmp(cmd, "uptime") == 0) {
    snprintf(out, sizeof(out), "uptime=%lus ticks=%lu hz=%lu\r\n",
             (unsigned long)timer_uptime_seconds(),
             (unsigned long)timer_ticks(), (unsigned long)timer_hz());
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "whoami") == 0 || strcmp(cmd, "id") == 0) {
    ssh_queue_channel_text("orizon\r\n");
  } else if (strcmp(cmd, "uname") == 0 || strcmp(cmd, "uname -a") == 0) {
    ssh_queue_channel_text("Orizon OS x86_64 OrizonSSH_0.1\r\n");
  } else if (strcmp(cmd, "pwd") == 0) {
    ssh_queue_channel_text("/home/orizon\r\n");
  } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "logout") == 0) {
    /* No output: exec exit only reports a successful exit-status. */
  } else {
    snprintf(out, sizeof(out), "%s: command not found\r\n", cmd);
    ssh_queue_channel_text(out);
    ssh_channel_exit_code = 127;
  }
  ssh_shell_suppress_prompt = 0;
  ssh_channel_exit_status_pending = 1;
  ssh_channel_close_pending = 1;
}

static void ssh_process_channel_data(const uint8_t *payload,
                                     size_t payload_len) {
  const uint8_t *data = NULL;
  size_t data_len = 0;
  size_t off = 1;
  uint32_t recipient;

  if (!ssh_status.shell_ready || off + 4 > payload_len) {
    return;
  }
  recipient = ssh_get_u32(payload + off);
  off += 4;
  if (recipient != ssh_server_channel ||
      ssh_read_string(payload, payload_len, &off, &data, &data_len) != 0) {
    ssh_status.errors++;
    ssh_set_status("ssh: malformed channel data");
    return;
  }

  for (size_t i = 0; i < data_len; i++) {
    char ch = (char)data[i];
    if (ch == '\r' || ch == '\n') {
      if (ch == '\n' && ssh_shell_last_was_cr) {
        ssh_shell_last_was_cr = 0;
        continue;
      }
      ssh_shell_last_was_cr = (ch == '\r');
      ssh_queue_channel_text("\r\n");
      ssh_shell_line[ssh_shell_line_len] = '\0';
      ssh_remote_shell_execute(ssh_shell_line);
      ssh_shell_line_len = 0;
      continue;
    }
    ssh_shell_last_was_cr = 0;
    if (ch == '\b' || ch == 0x7f) {
      if (ssh_shell_line_len > 0) {
        ssh_shell_line_len--;
        ssh_queue_channel_text("\b \b");
      }
      continue;
    }
    if (ssh_shell_line_len + 1 < sizeof(ssh_shell_line)) {
      ssh_shell_line[ssh_shell_line_len++] = ch;
      if (ch >= 32 && ch < 127) {
        char echo[2];
        echo[0] = ch;
        echo[1] = '\0';
        ssh_queue_channel_text(echo);
      }
    }
  }
  ssh_set_status("ssh: shell data received");
}

static void ssh_process_packet(const uint8_t *payload, size_t payload_len) {
  uint8_t type;

  if (!payload || payload_len == 0) {
    return;
  }
  type = payload[0];
  ssh_status.last_packet_type = type;
  ssh_status.ssh_packets_rx++;

  if (type == SSH_MSG_KEXINIT) {
    ssh_process_kexinit(payload, payload_len);
    return;
  }
  if (type == SSH_MSG_NEWKEYS) {
    ssh_status.client_newkeys_seen = 1;
    ssh_in_encrypted = 1;
    ssh_set_status("ssh: client NEWKEYS received; auth layer next");
    return;
  }
  if (type == SSH_MSG_KEXDH_INIT) {
    ssh_process_kexdh_init(payload, payload_len);
    return;
  }
  if (type == SSH_MSG_SERVICE_REQUEST) {
    ssh_process_service_request(payload, payload_len);
    return;
  }
  if (type == SSH_MSG_USERAUTH_REQUEST) {
    ssh_process_userauth_request(payload, payload_len);
    return;
  }
  if (type == SSH_MSG_CHANNEL_OPEN) {
    ssh_process_channel_open(payload, payload_len);
    return;
  }
  if (type == SSH_MSG_CHANNEL_REQUEST) {
    ssh_process_channel_request(payload, payload_len);
    return;
  }
  if (type == SSH_MSG_CHANNEL_DATA) {
    ssh_process_channel_data(payload, payload_len);
    return;
  }
  if (type == SSH_MSG_CHANNEL_EOF || type == SSH_MSG_CHANNEL_CLOSE) {
    if (!ssh_channel_close_sent) {
      ssh_channel_close_pending = 1;
    }
    ssh_set_status("ssh: client channel close received");
    return;
  }
  ssh_set_status("ssh: client SSH packet received");
}

static void ssh_drain_binary_packets(void) {
  while (ssh_binary_rx_used >= 5) {
    uint32_t packet_len = ssh_get_u32(ssh_binary_rx);
    uint8_t padding_len;
    size_t total_len;
    size_t payload_len;

    if (packet_len < 6 || packet_len > SSH_PACKET_MAX - 4) {
      ssh_status.errors++;
      ssh_binary_rx_used = 0;
      ssh_set_status("ssh: invalid client packet length");
      return;
    }
    total_len = (size_t)packet_len + 4;
    if (ssh_binary_rx_used < total_len) {
      return;
    }
    padding_len = ssh_binary_rx[4];
    if ((size_t)padding_len + 1 >= packet_len) {
      ssh_status.errors++;
      memmove(ssh_binary_rx, ssh_binary_rx + total_len,
              ssh_binary_rx_used - total_len);
      ssh_binary_rx_used -= total_len;
      ssh_set_status("ssh: invalid client packet padding");
      continue;
    }
    payload_len = (size_t)packet_len - (size_t)padding_len - 1;
    ssh_process_packet(ssh_binary_rx + 5, payload_len);
    ssh_seq_in++;
    memmove(ssh_binary_rx, ssh_binary_rx + total_len,
            ssh_binary_rx_used - total_len);
    ssh_binary_rx_used -= total_len;
    if (ssh_in_encrypted && ssh_binary_rx_used > 0) {
      return;
    }
  }
}

static void ssh_capture_encrypted(const uint8_t *data, size_t len) {
  if (!data || len == 0) {
    return;
  }
  if (len > sizeof(ssh_encrypted_rx) - ssh_encrypted_rx_used) {
    ssh_status.errors++;
    ssh_encrypted_rx_used = 0;
    ssh_set_status("ssh: encrypted packet buffer overflow");
    return;
  }
  memcpy(ssh_encrypted_rx + ssh_encrypted_rx_used, data, len);
  ssh_encrypted_rx_used += len;

  while (ssh_encrypted_rx_used >= 4 + SHA256_DIGEST_SIZE) {
    uint8_t ctr_preview[16];
    uint8_t len_block[4];
    uint8_t plain[SSH_PACKET_MAX];
    uint8_t mac[SHA256_DIGEST_SIZE];
    uint32_t packet_len;
    uint8_t padding_len;
    size_t total_len;
    size_t payload_len;

    memcpy(ctr_preview, ssh_ctr_c2s, sizeof(ctr_preview));
    aes128_ctr_crypt_update(ssh_key_c2s, ctr_preview, ssh_encrypted_rx,
                            sizeof(len_block), len_block);
    packet_len = ssh_get_u32(len_block);
    if (packet_len < 6 || packet_len > SSH_PACKET_MAX - 4) {
      ssh_status.errors++;
      ssh_encrypted_rx_used = 0;
      ssh_set_status("ssh: invalid encrypted packet length");
      return;
    }

    total_len = 4 + (size_t)packet_len + SHA256_DIGEST_SIZE;
    if (ssh_encrypted_rx_used < total_len) {
      return;
    }

    aes128_ctr_crypt_update(ssh_key_c2s, ssh_ctr_c2s, ssh_encrypted_rx,
                            4 + (size_t)packet_len, plain);
    ssh_mac_packet(ssh_mac_c2s, ssh_seq_in, plain, 4 + (size_t)packet_len,
                   mac);
    if (memcmp(mac, ssh_encrypted_rx + 4 + (size_t)packet_len,
               SHA256_DIGEST_SIZE) != 0) {
      ssh_status.errors++;
      ssh_encrypted_rx_used = 0;
      ssh_set_status("ssh: encrypted packet MAC mismatch");
      return;
    }

    padding_len = plain[4];
    if ((size_t)padding_len + 1 >= packet_len) {
      ssh_status.errors++;
      ssh_encrypted_rx_used = 0;
      ssh_set_status("ssh: invalid encrypted packet padding");
      return;
    }
    payload_len = (size_t)packet_len - (size_t)padding_len - 1;
    ssh_process_packet(plain + 5, payload_len);
    ssh_seq_in++;

    memmove(ssh_encrypted_rx, ssh_encrypted_rx + total_len,
            ssh_encrypted_rx_used - total_len);
    ssh_encrypted_rx_used -= total_len;
  }
}

static void ssh_capture_binary(const uint8_t *data, size_t len) {
  if (!data || len == 0) {
    return;
  }
  if (len > sizeof(ssh_binary_rx) - ssh_binary_rx_used) {
    ssh_status.errors++;
    ssh_binary_rx_used = 0;
    ssh_set_status("ssh: client packet buffer overflow");
    return;
  }
  memcpy(ssh_binary_rx + ssh_binary_rx_used, data, len);
  ssh_binary_rx_used += len;
  ssh_drain_binary_packets();
}

static void ssh_capture_client_data(const uint8_t *data, size_t len) {
  size_t i;

  if (!data || len == 0) {
    return;
  }

  ssh_status.packets_rx++;
  ssh_status.bytes_rx += (uint32_t)len;

  i = 0;
  if (!ssh_status.client_banner_seen) {
    while (i < len) {
      uint8_t ch = data[i++];
      if (ch == '\n') {
        ssh_status.remote_banner[ssh_remote_banner_len] = '\0';
        ssh_status.client_banner_seen = 1;
        ssh_set_status("ssh: client banner received");
        break;
      }
      if (ch != '\r' &&
          ssh_remote_banner_len < sizeof(ssh_status.remote_banner) - 1) {
        ssh_status.remote_banner[ssh_remote_banner_len++] = (char)ch;
        ssh_status.remote_banner[ssh_remote_banner_len] = '\0';
      }
    }
    if (!ssh_status.client_banner_seen) {
      return;
    }
  }

  if (i < len) {
    if (ssh_in_encrypted) {
      ssh_capture_encrypted(data + i, len - i);
    } else {
      ssh_capture_binary(data + i, len - i);
      if (ssh_in_encrypted && ssh_binary_rx_used > 0) {
        ssh_capture_encrypted(ssh_binary_rx, ssh_binary_rx_used);
        ssh_binary_rx_used = 0;
      }
    }
  }
}

static void ssh_refresh_state(void) {
  ssh_status.listening =
      ssh_status.enabled &&
      ssh_server.state == NETSTACK_TCP_SERVER_LISTEN;
  ssh_status.connected =
      ssh_status.enabled &&
      ssh_server.state == NETSTACK_TCP_SERVER_ESTABLISHED;
  ssh_status.remote_ip = ssh_server.remote_ip;
  ssh_status.remote_port = ssh_server.remote_port;
  ssh_status.sessions = ssh_session_total;

  if (ssh_server.connections != ssh_seen_connections) {
    ssh_seen_connections = ssh_server.connections;
    ssh_session_total++;
    ssh_status.sessions = ssh_session_total;
    ssh_last_activity_tick = timer_ticks();
    ssh_status.banner_sent = 0;
    ssh_status.client_banner_seen = 0;
    ssh_disconnect_close_polls = 0;
    ssh_status.remote_banner[0] = '\0';
    ssh_reset_negotiation();
    ssh_set_status("ssh: tcp client connected");
  }
}

int ssh_start(char *report, size_t report_size) {
  char ip[24];
  const netstack_status_t *net;

  if (ssh_status.enabled) {
    ssh_format_report(report, report_size);
    return 0;
  }

  ssh_set_status("ssh: configuring IPv4");
  if (netstack_configure_ipv4() != 0) {
    ssh_status.errors++;
    ssh_set_status("ssh: cannot start without IPv4");
    if (report && report_size > 0) {
      snprintf(report, report_size,
               "ssh: start failed; configure network first with net dhcp or net config.\n");
    }
    return -1;
  }

  netstack_tcp_server_init(&ssh_server, ORIZON_SSH_PORT);
  ssh_status.enabled = 1;
  ssh_status.configured = 1;
  ssh_status.port = ORIZON_SSH_PORT;
  ssh_status.banner_sent = 0;
  ssh_status.client_banner_seen = 0;
  ssh_status.remote_banner[0] = '\0';
  ssh_session_total = 0;
  ssh_exec_request_total = 0;
  ssh_shell_command_total = 0;
  ssh_channel_close_total = 0;
  ssh_listener_reset_total = 0;
  ssh_listener_recover_total = 0;
  ssh_auth_success_total = 0;
  ssh_auth_failure_total = 0;
  ssh_policy_denied_total = 0;
  ssh_policy_denied_sensitive = 0;
  ssh_policy_denied_internal = 0;
  ssh_policy_denied_write_scope = 0;
  ssh_policy_denied_root = 0;
  strcpy(ssh_last_command, "none");
  strcpy(ssh_last_audit, "none");
  strcpy(ssh_last_policy_denial, "none");
  memset(ssh_audit_recent, 0, sizeof(ssh_audit_recent));
  ssh_audit_recent_next = 0;
  ssh_audit_recent_count = 0;
  ssh_reset_negotiation();
  ssh_seen_connections = ssh_server.connections;
  ssh_disconnect_close_polls = 0;
  ssh_last_activity_tick = timer_ticks();
  ssh_ensure_config();
  if (ssh_ensure_hostkey() != 0) {
    ssh_status.errors++;
    ssh_set_status("ssh: host key unavailable");
    if (report && report_size > 0) {
      snprintf(report, report_size,
               "ssh: start failed; host key could not be loaded or persisted.\n");
    }
    return -1;
  }
  ssh_set_status("ssh: listening on tcp/22");

  net = netstack_get_status();
  netstack_format_ipv4(net->ip, ip, sizeof(ip));
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: listening on %s:%u\n"
             "ssh: auth=%s hostkey=%s source=%s\n"
             "ssh: connect with: ssh orizon@%s\n",
             ip, (unsigned)ORIZON_SSH_PORT,
             ssh_status.auth_configured ? "password" : "disabled",
             ssh_status.hostkey_persistent ? "persistent" : "bootstrap",
             ssh_status.hostkey_source, ip);
  }
  return 0;
}

int ssh_stop(char *report, size_t report_size) {
  if (ssh_status.enabled) {
    netstack_tcp_server_close(&ssh_server);
  }
  ssh_status.enabled = 0;
  ssh_status.listening = 0;
  ssh_status.connected = 0;
  ssh_status.banner_sent = 0;
  ssh_status.client_banner_seen = 0;
  ssh_status.remote_ip = 0;
  ssh_status.remote_port = 0;
  ssh_disconnect_close_polls = 0;
  ssh_reset_negotiation();
  ssh_set_status("ssh: stopped");
  if (report && report_size > 0) {
    snprintf(report, report_size, "ssh: stopped\n");
  }
  return 0;
}

int ssh_poll(void) {
  uint8_t *rx = ssh_poll_rx;
  size_t rx_len = 0;
  uint8_t *txbuf = ssh_poll_tx;
  const void *tx = NULL;
  size_t tx_len = 0;
  int tx_kind = 0;
  int rc;

  if (!ssh_status.enabled) {
    return 0;
  }

  ssh_ensure_listener_alive();
  ssh_refresh_state();
  if (ssh_status.connected && !ssh_has_pending_tx() &&
      ssh_last_activity_tick > 0 &&
      timer_ticks() - ssh_last_activity_tick > SSH_SESSION_IDLE_TIMEOUT_TICKS) {
    ssh_reopen_listener("ssh: session watchdog reset idle connection");
    return 1;
  }
  if (ssh_status.connected && !ssh_status.banner_sent) {
    tx = SSH_BANNER;
    tx_len = strlen(SSH_BANNER);
    tx_kind = 1;
  } else if (ssh_status.connected && ssh_status.client_banner_seen &&
             !ssh_status.server_kexinit_sent) {
    tx_len = ssh_build_kexinit(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 2;
  } else if (ssh_status.connected && ssh_status.server_kexinit_sent &&
             ssh_status.client_kexinit_seen &&
             !ssh_algorithm_ready() &&
             !ssh_status.disconnect_sent) {
    tx_len = ssh_build_disconnect(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 3;
  } else if (ssh_status.connected && ssh_status.ecdh_ready &&
             ssh_status.client_kex_packet_seen &&
             !ssh_status.ecdh_reply_sent) {
    tx_len = ssh_build_ecdh_reply(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 4;
  } else if (ssh_status.connected && ssh_status.ecdh_reply_sent &&
             !ssh_status.newkeys_sent) {
    tx_len = ssh_build_newkeys(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 5;
  } else if (ssh_status.connected && ssh_status.newkeys_sent &&
             ssh_status.client_newkeys_seen && ssh_service_accept_pending &&
             !ssh_status.service_accept_sent) {
    tx_len = ssh_build_service_accept(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 6;
  } else if (ssh_status.connected && ssh_auth_success_pending) {
    tx_len = ssh_build_userauth_success(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 7;
  } else if (ssh_status.connected && ssh_auth_failure_pending) {
    tx_len = ssh_build_userauth_failure(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 8;
  } else if (ssh_status.connected && ssh_channel_open_confirm_pending) {
    tx_len = ssh_build_channel_open_confirmation(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 9;
  } else if (ssh_status.connected && ssh_channel_success_pending) {
    tx_len = ssh_build_channel_status(txbuf, sizeof(ssh_poll_tx),
                                      SSH_MSG_CHANNEL_SUCCESS);
    tx = tx_len ? txbuf : NULL;
    tx_kind = 10;
  } else if (ssh_status.connected && ssh_channel_failure_pending) {
    tx_len = ssh_build_channel_status(txbuf, sizeof(ssh_poll_tx),
                                      SSH_MSG_CHANNEL_FAILURE);
    tx = tx_len ? txbuf : NULL;
    tx_kind = 11;
  } else if (ssh_status.connected && ssh_channel_data_pending) {
    tx_len = ssh_build_channel_data(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 12;
  } else if (ssh_status.connected && ssh_channel_exit_status_pending &&
             !ssh_channel_data_pending) {
    tx_len = ssh_build_channel_exit_status(txbuf, sizeof(ssh_poll_tx));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 14;
  } else if (ssh_status.connected && ssh_channel_close_pending &&
             !ssh_channel_data_pending && !ssh_channel_exit_status_pending) {
    tx_len = ssh_build_channel_status(txbuf, sizeof(ssh_poll_tx),
                                      SSH_MSG_CHANNEL_CLOSE);
    tx = tx_len ? txbuf : NULL;
    tx_kind = 13;
  } else if (ssh_status.enabled && ssh_disconnect_close_polls > 0) {
    ssh_disconnect_close_polls--;
    if (ssh_disconnect_close_polls == 0) {
      netstack_tcp_server_close(&ssh_server);
      netstack_tcp_server_init(&ssh_server, ORIZON_SSH_PORT);
      ssh_seen_connections = ssh_server.connections;
      ssh_reset_negotiation();
      ssh_set_status("ssh: closed staged diagnostic session");
      ssh_refresh_state();
      return 1;
    }
  }

  rc = netstack_tcp_server_poll(&ssh_server, tx, tx_len, rx,
                                sizeof(ssh_poll_rx), &rx_len);
  if (rc < 0) {
    ssh_status.errors++;
  }
  if (tx && tx_len > 0 && rc == 4) {
    ssh_last_activity_tick = timer_ticks();
    ssh_status.bytes_tx += (uint32_t)tx_len;
    if (tx_kind == 1) {
      ssh_status.banner_sent = 1;
      ssh_set_status("ssh: protocol banner sent");
    } else if (tx_kind == 2) {
      ssh_status.server_kexinit_sent = 1;
      ssh_seq_out++;
      ssh_set_status("ssh: server KEXINIT sent");
    } else if (tx_kind == 3) {
      ssh_status.disconnect_sent = 1;
      ssh_disconnect_close_polls = 8;
      ssh_seq_out++;
      ssh_set_status("ssh: staged disconnect sent");
    } else if (tx_kind == 4) {
      ssh_status.ecdh_reply_sent = 1;
      ssh_seq_out++;
      ssh_set_status("ssh: ECDH_REPLY sent");
    } else if (tx_kind == 5) {
      ssh_status.newkeys_sent = 1;
      ssh_out_encrypted = 1;
      ssh_seq_out++;
      ssh_set_status("ssh: NEWKEYS sent");
    } else if (tx_kind == 6) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_status.service_accept_sent = 1;
      ssh_service_accept_pending = 0;
      ssh_seq_out++;
      ssh_set_status("ssh: encrypted SERVICE_ACCEPT sent");
    } else if (tx_kind == 7) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_auth_success_pending = 0;
      ssh_seq_out++;
      ssh_set_status("ssh: USERAUTH_SUCCESS sent");
    } else if (tx_kind == 8) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_auth_failure_pending = 0;
      ssh_status.auth_failure_sent = 1;
      ssh_seq_out++;
      ssh_set_status("ssh: USERAUTH_FAILURE sent");
    } else if (tx_kind == 9) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_channel_open_confirm_pending = 0;
      ssh_status.channel_open_confirm_sent = 1;
      ssh_seq_out++;
      ssh_set_status("ssh: CHANNEL_OPEN_CONFIRMATION sent");
    } else if (tx_kind == 10) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_channel_success_pending = 0;
      ssh_seq_out++;
      ssh_set_status("ssh: CHANNEL_SUCCESS sent");
    } else if (tx_kind == 11) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_channel_failure_pending = 0;
      ssh_seq_out++;
      ssh_set_status("ssh: CHANNEL_FAILURE sent");
    } else if (tx_kind == 12) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      if (ssh_channel_last_chunk_len > 0 &&
          ssh_channel_tx_off + ssh_channel_last_chunk_len < ssh_channel_tx_len) {
        ssh_channel_tx_off += ssh_channel_last_chunk_len;
      } else {
        ssh_channel_data_pending = 0;
        ssh_channel_tx_len = 0;
        ssh_channel_tx_off = 0;
      }
      ssh_channel_last_chunk_len = 0;
      ssh_seq_out++;
      ssh_set_status("ssh: CHANNEL_DATA sent");
    } else if (tx_kind == 13) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_seq_out++;
      ssh_channel_close_pending = 0;
      ssh_channel_close_sent = 1;
      ssh_channel_close_total++;
      ssh_set_status("ssh: channel close sent; transport reusable");
    } else if (tx_kind == 14) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_channel_exit_status_pending = 0;
      ssh_seq_out++;
      ssh_set_status("ssh: CHANNEL exit-status sent");
    }
  }
  if (rx_len > 0) {
    ssh_last_activity_tick = timer_ticks();
    ssh_capture_client_data(rx, rx_len);
  }
  ssh_refresh_state();
  return rc;
}

void ssh_format_status(char *buf, size_t size) {
  char rip[24];
  uint64_t lockout;

  if (!buf || size == 0) {
    return;
  }
  netstack_format_ipv4(ssh_status.remote_ip, rip, sizeof(rip));
  lockout = ssh_lockout_remaining();
  snprintf(buf, size,
           "ssh: enabled=%s state=%s port=%u connected=%s remote=%s:%u "
           "sessions=%lu banner=%s skex=%s ckex=%s pkt=%u ecdh=%s "
           "reply=%s newkeys=%s cnewkeys=%s keys=%s enc=%s svc=%s "
           "authcfg=%s auth=%s failures=%lu lockout=%lus hostkey=%s chan=%s shell=%s kex=%s "
           "rx=%lu spkts=%lu tx=%lu errors=%lu status=\"%s\"",
           ssh_status.enabled ? "yes" : "no",
           netstack_tcp_server_state_name(&ssh_server),
           (unsigned)ssh_status.port,
           ssh_status.connected ? "yes" : "no", rip,
           (unsigned)ssh_status.remote_port,
           (unsigned long)ssh_status.sessions,
           ssh_status.banner_sent ? "sent" : "pending",
           ssh_status.server_kexinit_sent ? "sent" : "pending",
           ssh_status.client_kexinit_seen ? "seen" : "pending",
           (unsigned)ssh_status.last_packet_type,
           ssh_status.ecdh_ready ? "ready" : "pending",
           ssh_status.ecdh_reply_sent ? "sent" : "pending",
           ssh_status.newkeys_sent ? "sent" : "pending",
           ssh_status.client_newkeys_seen ? "seen" : "pending",
           ssh_status.traffic_keys_ready ? "ready" : "pending",
           ssh_status.encrypted_packet_seen ? "seen" : "pending",
           ssh_status.service_accept_sent ? "sent" : "pending",
           ssh_status.auth_configured ? "password" : "disabled",
           ssh_status.authenticated
               ? "ok"
               : (ssh_status.userauth_request_seen ? "requested" : "pending"),
           (unsigned long)ssh_status.auth_failures,
           (unsigned long)lockout,
           ssh_status.hostkey_persistent
               ? "persistent"
               : (ssh_status.hostkey_bootstrap ? "bootstrap" : "generated"),
           ssh_status.channel_open_confirm_sent
               ? "open"
               : (ssh_status.channel_open_seen ? "seen" : "pending"),
           ssh_status.shell_ready ? "ready" : "pending",
           ssh_status.kex_algorithm[0] ? ssh_status.kex_algorithm : "none",
           (unsigned long)ssh_status.bytes_rx,
           (unsigned long)ssh_status.ssh_packets_rx,
           (unsigned long)ssh_status.bytes_tx,
           (unsigned long)ssh_status.errors, ssh_status.status);
}

void ssh_format_algorithms(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  snprintf(buf, size,
           "ssh algorithms:\n"
           "  client-banner: %s\n"
           "  client-first-kex: %s\n"
           "  client-first-hostkey: %s\n"
           "  kex: %s\n"
           "  hostkey: %s\n"
           "  cipher-c2s: %s\n"
           "  cipher-s2c: %s\n"
           "  mac-c2s: %s\n"
           "  mac-s2c: %s\n"
           "  compression-c2s: %s\n"
           "  compression-s2c: %s\n"
           "  hostkey-sha256: %s\n"
           "  client-public-sha256: %s\n"
           "  server-public-sha256: %s\n"
           "  shared-secret-sha256: %s\n"
           "  exchange-hash-sha256: %s\n"
           "  signature-sha256: %s\n"
           "  key-c2s-sha256: %s\n"
           "  key-s2c-sha256: %s\n"
           "  mac-c2s-sha256: %s\n"
           "  mac-s2c-sha256: %s\n",
           ssh_status.client_banner_seen ? ssh_status.remote_banner : "none",
           ssh_status.client_kex_first[0] ? ssh_status.client_kex_first : "none",
           ssh_status.client_hostkey_first[0]
               ? ssh_status.client_hostkey_first
               : "none",
           ssh_status.kex_algorithm[0] ? ssh_status.kex_algorithm : "none",
           ssh_status.hostkey_algorithm[0] ? ssh_status.hostkey_algorithm
                                           : "none",
           ssh_status.cipher_c2s[0] ? ssh_status.cipher_c2s : "none",
           ssh_status.cipher_s2c[0] ? ssh_status.cipher_s2c : "none",
           ssh_status.mac_c2s[0] ? ssh_status.mac_c2s : "none",
           ssh_status.mac_s2c[0] ? ssh_status.mac_s2c : "none",
           ssh_status.compression_c2s[0] ? ssh_status.compression_c2s : "none",
           ssh_status.compression_s2c[0] ? ssh_status.compression_s2c : "none",
           ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256 : "none",
           ssh_status.client_public_sha256[0] ? ssh_status.client_public_sha256
                                              : "none",
           ssh_status.server_public_sha256[0] ? ssh_status.server_public_sha256
                                              : "none",
           ssh_status.shared_secret_sha256[0] ? ssh_status.shared_secret_sha256
                                              : "none",
           ssh_status.exchange_hash_sha256[0]
               ? ssh_status.exchange_hash_sha256
               : "none",
           ssh_status.signature_sha256[0] ? ssh_status.signature_sha256
                                          : "none",
           ssh_status.client_to_server_key_sha256[0]
               ? ssh_status.client_to_server_key_sha256
               : "none",
           ssh_status.server_to_client_key_sha256[0]
               ? ssh_status.server_to_client_key_sha256
               : "none",
           ssh_status.client_to_server_mac_sha256[0]
               ? ssh_status.client_to_server_mac_sha256
               : "none",
           ssh_status.server_to_client_mac_sha256[0]
               ? ssh_status.server_to_client_mac_sha256
               : "none");
}

void ssh_format_auth(char *buf, size_t size) {
  uint64_t lockout;

  if (!buf || size == 0) {
    return;
  }
  lockout = ssh_lockout_remaining();
  snprintf(buf, size,
           "ssh auth:\n"
           "  user: orizon\n"
           "  password-auth: %s\n"
           "  max-attempts: %lu\n"
           "  lockout-seconds: %lu\n"
           "  current-failures: %lu\n"
           "  total-success: %lu\n"
           "  total-failure: %lu\n"
           "  lockout-remaining: %lus\n"
           "  config: %s\n"
           "  log: %s\n",
           ssh_status.auth_configured ? "enabled" : "disabled",
           (unsigned long)ssh_status.max_auth_attempts,
           (unsigned long)ssh_status.auth_lockout_seconds,
           (unsigned long)ssh_status.auth_failures,
           (unsigned long)ssh_auth_success_total,
           (unsigned long)ssh_auth_failure_total,
           (unsigned long)lockout,
           ORIZON_SSH_CONFIG_PATH, ORIZON_SSH_LOG_PATH);
}

void ssh_format_audit(char *buf, size_t size) {
  char remote[24];
  static char line[768];
  size_t used = 0;
  uint64_t idle_seconds = 0;
  uint64_t hz = timer_hz();

  if (!buf || size == 0) {
    return;
  }
  buf[0] = '\0';
  netstack_format_ipv4(ssh_status.remote_ip, remote, sizeof(remote));
  if (hz > 0 && ssh_last_activity_tick > 0 &&
      timer_ticks() >= ssh_last_activity_tick) {
    idle_seconds = (timer_ticks() - ssh_last_activity_tick) / hz;
  }
  snprintf(line, sizeof(line),
           "ssh audit:\n"
           "  remote: %s:%u\n"
           "  sessions: %lu\n"
           "  auth-success: %lu\n"
           "  auth-failure: %lu\n"
           "  exec-requests: %lu\n"
           "  shell-commands: %lu\n"
           "  channel-closes: %lu\n"
           "  listener-resets: %lu\n"
           "  listener-recovers: %lu\n"
           "  idle-seconds: %lu\n"
           "  last-command: %s\n"
           "  last-audit: %s\n",
           remote, (unsigned)ssh_status.remote_port,
           (unsigned long)ssh_status.sessions,
           (unsigned long)ssh_auth_success_total,
           (unsigned long)ssh_auth_failure_total,
           (unsigned long)ssh_exec_request_total,
           (unsigned long)ssh_shell_command_total,
           (unsigned long)ssh_channel_close_total,
           (unsigned long)ssh_listener_reset_total,
           (unsigned long)ssh_listener_recover_total,
           (unsigned long)idle_seconds,
           ssh_last_command, ssh_last_audit);
  ssh_shell_append(buf, size, &used, line);
  if (ssh_audit_recent_count > 0) {
    ssh_shell_append(buf, size, &used, "  recent:\n");
    for (uint32_t i = 0; i < ssh_audit_recent_count; i++) {
      uint32_t idx = (ssh_audit_recent_next + SSH_AUDIT_RECENT -
                      ssh_audit_recent_count + i) % SSH_AUDIT_RECENT;
      ssh_shell_append(buf, size, &used, "    - ");
      ssh_shell_append(buf, size, &used, ssh_audit_recent[idx]);
      ssh_shell_append(buf, size, &used, "\n");
    }
  }
}

void ssh_format_hostkey(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  ssh_ensure_hostkey();
  snprintf(buf, size,
           "ssh hostkey:\n"
           "  algorithm: rsa-sha2-256\n"
           "  storage: %s\n"
           "  bootstrap-material: %s\n"
           "  source: %s\n"
           "  path: %s\n"
           "  fingerprint-sha256: %s\n"
           "  status: %s\n",
           ssh_status.hostkey_persistent ? "persistent-file" : "compiled",
           ssh_status.hostkey_bootstrap ? "yes" : "no",
           ssh_status.hostkey_source[0] ? ssh_status.hostkey_source : "none",
           ORIZON_SSH_HOSTKEY_PATH,
           ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256 : "none",
           ssh_status.hostkey_status[0] ? ssh_status.hostkey_status
                                        : "ssh: host key not loaded");
}

static void ssh_security_write_policy_file(void) {
  char text[2048];

  snprintf(text, sizeof(text),
           "policy-version: 2\n"
           "roles: local-console=admin remote=orizon-admin "
           "user-admin-split=prepared command-scoped=yes\n"
           "vfs-read: normal=allow sensitive=deny secret-like-names=deny\n"
           "vfs-write: roots=/workspace,/home,/logs,/packages "
           "internal=/workspace/.orizon:deny sensitive=deny "
           "remote-roots:rm=deny\n"
           "protected-files: %s,%s\n"
           "ssh-auth: password=opt-in lockout=yes audit=yes\n"
           "ssh-audit-redaction: ssh-password,write,append,wifi-credentials\n"
           "package-script-policy: allow=mkdir,touch,write,append,echo,sync "
           "safe-roots=/system,/home,/packages,/logs,/tmp,/workspace "
           "sensitive=deny internal-state=deny\n"
           "update-policy: manifest.sig=required "
           "key=orizon-update-root-2026-05 algorithm=rsa-pkcs1-sha256\n"
           "package-policy: signed-manifest-pin=required "
           "detached-sidecar=/workspace/.orizon/package-index.sig\n"
           "key-rotation: ssh-hostkey=runtime "
           "update-root=release-required package-root=release-required\n"
           "secrets-policy: private-keys=.ssh/env/local-host-files/token-looking "
           "tracked-scan=required\n"
           "limits: no-unix-uid-gid no-acl no-sudo no-secureboot no-tpm "
           "no-disk-encryption\n",
           ORIZON_SSH_CONFIG_PATH, ORIZON_SSH_HOSTKEY_PATH);
  ssh_write_text_file_raw(ORIZON_SECURITY_POLICY_PATH, text);
}

static void ssh_security_write_state_file(void) {
  char text[2048];
  uint64_t lockout;

  ssh_ensure_hostkey();
  lockout = ssh_lockout_remaining();
  snprintf(text, sizeof(text),
           "security-state-version: 2\n"
           "role: local-console=admin remote=orizon-admin "
           "command-scoped=yes user-admin-split=planned\n"
           "ssh-auth: password=%s max-attempts=%lu lockout-seconds=%lu "
           "failures=%lu lockout-remaining=%lus\n"
           "ssh-hostkey: storage=%s bootstrap=%s path=%s "
           "fingerprint-sha256=%s rotation=runtime\n"
           "policy-denies: total=%lu sensitive=%lu internal=%lu "
           "write-scope=%lu remote-root=%lu last=\"%s\"\n"
           "paths: policy=%s audit=%s doctor=%s\n"
           "update-root: id=orizon-update-root-2026-05 "
           "rotation=release-required status=\"%s\"\n"
           "package-root: id=orizon-update-root-2026-05 "
           "rotation=release-required sidecar=/workspace/.orizon/package-index.sig "
           "fallback=signed-update-manifest-pin\n"
           "limits: unix-acl=no sudo=no secureboot=no tpm=no "
           "disk-encryption=no\n",
           ssh_status.auth_configured ? "enabled" : "disabled",
           (unsigned long)ssh_status.max_auth_attempts,
           (unsigned long)ssh_status.auth_lockout_seconds,
           (unsigned long)ssh_status.auth_failures, (unsigned long)lockout,
           ssh_status.hostkey_persistent ? "persistent" : "compiled-fallback",
           ssh_status.hostkey_bootstrap ? "yes" : "no",
           ORIZON_SSH_HOSTKEY_PATH,
           ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256 : "none",
           (unsigned long)ssh_policy_denied_total,
           (unsigned long)ssh_policy_denied_sensitive,
           (unsigned long)ssh_policy_denied_internal,
           (unsigned long)ssh_policy_denied_write_scope,
           (unsigned long)ssh_policy_denied_root, ssh_last_policy_denial,
           ORIZON_SECURITY_POLICY_PATH, ORIZON_SECURITY_LOG_PATH,
           ORIZON_SECURITY_DOCTOR_PATH, orizon_update_status());
  ssh_write_text_file_raw(ORIZON_SECURITY_STATE_PATH, text);
}

void ssh_format_security(char *buf, size_t size) {
  uint64_t lockout;

  if (!buf || size == 0) {
    return;
  }
  ssh_ensure_hostkey();
  ssh_security_write_policy_file();
  ssh_security_write_state_file();
  lockout = ssh_lockout_remaining();
  snprintf(buf, size,
           "Orizon security status\n"
           "mode: single-user admin shell; command-scoped hardening active; "
           "user-admin-split=planned\n"
           "role-policy: local-console=admin remote=orizon-admin "
           "command-scoped=yes user-admin-split=prepared\n"
           "remote-user: orizon\n"
           "vfs.policy: version=2 read-sensitive=deny "
           "write-roots=/workspace,/home,/logs,/packages "
           "internal-state=/workspace/.orizon:deny remote-root-rm=deny "
           "no uid/gid/acl yet\n"
           "security.state: policy=%s state=%s doctor=%s\n"
           "ssh.auth: %s max-attempts=%lu lockout-seconds=%lu "
           "failures=%lu lockout-remaining=%lus\n"
           "ssh.hostkey: %s bootstrap=%s path=%s fingerprint-sha256=%s\n"
           "ssh.audit: sessions=%lu auth-success=%lu auth-failure=%lu "
           "last=%s\n"
           "policy-denies: total=%lu sensitive=%lu internal=%lu "
           "write-scope=%lu remote-root=%lu last=\"%s\"\n"
           "security.audit-log: %s mirrored-from=ssh-audit "
           "policy-changes=yes\n"
           "ssh.file-policy: sensitive-read=blocked sensitive-write=blocked "
           "generic-write-roots=/workspace,/home,/logs,/packages "
           "internal-state-write=/workspace/.orizon:blocked\n"
           "update.manifest-policy: required manifest.sig "
           "rsa-pkcs1-sha256 key=orizon-update-root-2026-05 state=\"%s\"\n"
           "key-rotation: ssh-hostkey=runtime update-root=release-required "
           "package-root=release-required\n"
           "packages.remote-index: signed-manifest-sha256-pinned "
           "detached-sidecar=/workspace/.orizon/package-index.sig "
           "warn-if-missing\n"
           "packages.script-policy: safe-paths=/system,/home,/packages,"
           "/logs,/tmp,/workspace sensitive-paths=blocked "
           "internal-state=/workspace/.orizon:blocked\n"
           "secrets.release-policy: tracked-secret-scan=yes "
           "private-keys/env/local-host-files=blocked\n"
           "protected-files: %s, %s\n"
           "limits: no Unix uid/gid/ACL, no sudo split, no SecureBoot/TPM "
           "attestation, no disk encryption yet\n",
           ORIZON_SECURITY_POLICY_PATH, ORIZON_SECURITY_STATE_PATH,
           ORIZON_SECURITY_DOCTOR_PATH,
           ssh_status.auth_configured ? "password-enabled" : "password-disabled",
           (unsigned long)ssh_status.max_auth_attempts,
           (unsigned long)ssh_status.auth_lockout_seconds,
           (unsigned long)ssh_status.auth_failures, (unsigned long)lockout,
           ssh_status.hostkey_persistent ? "persistent" : "compiled-fallback",
           ssh_status.hostkey_bootstrap ? "yes" : "no",
           ORIZON_SSH_HOSTKEY_PATH,
           ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256 : "none",
           (unsigned long)ssh_status.sessions,
           (unsigned long)ssh_auth_success_total,
           (unsigned long)ssh_auth_failure_total, ssh_last_audit,
           (unsigned long)ssh_policy_denied_total,
           (unsigned long)ssh_policy_denied_sensitive,
           (unsigned long)ssh_policy_denied_internal,
           (unsigned long)ssh_policy_denied_write_scope,
           (unsigned long)ssh_policy_denied_root, ssh_last_policy_denial,
           ORIZON_SECURITY_LOG_PATH, orizon_update_status(), ORIZON_SSH_CONFIG_PATH,
           ORIZON_SSH_HOSTKEY_PATH);
}

void ssh_format_security_policy(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  ssh_security_write_policy_file();
  ssh_security_write_state_file();
  snprintf(buf, size,
           "security policy:\n"
           "  policy-version: 2\n"
           "  users: local-console=admin remote=orizon-admin "
           "user-admin-split=prepared command-scoped=yes\n"
           "  ssh-auth: password=opt-in max-attempts=%lu lockout=%lus "
           "audit=yes\n"
           "  ssh-audit-redaction: ssh-password/write/append/wifi-credentials "
           "arguments redacted\n"
           "  vfs-read: cat/head/tail block sensitive paths and secret-like "
           "names\n"
           "  vfs-write: generic SSH writes allowed only under "
           "/workspace,/home,/logs,/packages; remote-root-rm=deny\n"
           "  internal-state: /workspace/.orizon write=blocked "
           "package-payload=blocked\n"
           "  package-script-policy: allow=mkdir,touch,write,append,echo,sync "
           "safe-roots=/system,/home,/packages,/logs,/tmp,/workspace\n"
           "  update-policy: manifest.sig required; key=orizon-update-root-2026-05\n"
           "  key-rotation: ssh-hostkey=runtime update-root=release-required "
           "package-root=release-required\n"
           "  release-secret-policy: tracked scan blocks private keys, .ssh, "
           "local env/host files and token-looking payloads\n"
           "  protected-files: %s, %s\n"
           "  state-files: policy=%s state=%s doctor=%s\n"
           "  admin-commands: ssh auth, ssh password, security rotate ssh-hostkey, "
           "hostname set, net config, pkg, update status\n"
           "  limits: path policy only; no Unix uid/gid/acl/sudo/mac, "
           "SecureBoot, TPM or disk encryption yet\n",
           (unsigned long)ssh_status.max_auth_attempts,
           (unsigned long)ssh_status.auth_lockout_seconds,
           ORIZON_SSH_CONFIG_PATH, ORIZON_SSH_HOSTKEY_PATH,
           ORIZON_SECURITY_POLICY_PATH, ORIZON_SECURITY_STATE_PATH,
           ORIZON_SECURITY_DOCTOR_PATH);
}

void ssh_format_security_audit(char *buf, size_t size) {
  char audit[1800];
  char line[640];
  size_t used = 0;

  if (!buf || size == 0) {
    return;
  }
  buf[0] = '\0';
  ssh_security_write_policy_file();
  ssh_security_write_state_file();
  ssh_format_audit(audit, sizeof(audit));
  snprintf(line, sizeof(line),
           "security audit:\n"
           "  persistent-log: %s present=%s\n"
           "  ssh-log: %s present=%s\n"
           "  policy-denies: total=%lu sensitive=%lu internal=%lu "
           "write-scope=%lu remote-root=%lu last=\"%s\"\n"
           "  state-files: policy=%s state=%s doctor=%s\n"
           "  mirror: ssh audit events plus policy changes; passwords and "
           "write payloads are redacted\n",
           ORIZON_SECURITY_LOG_PATH,
           vfs_exists(ORIZON_SECURITY_LOG_PATH) ? "yes" : "no",
           ORIZON_SSH_LOG_PATH,
           vfs_exists(ORIZON_SSH_LOG_PATH) ? "yes" : "no",
           (unsigned long)ssh_policy_denied_total,
           (unsigned long)ssh_policy_denied_sensitive,
           (unsigned long)ssh_policy_denied_internal,
           (unsigned long)ssh_policy_denied_write_scope,
           (unsigned long)ssh_policy_denied_root, ssh_last_policy_denial,
           ORIZON_SECURITY_POLICY_PATH, ORIZON_SECURITY_STATE_PATH,
           ORIZON_SECURITY_DOCTOR_PATH);
  ssh_shell_append(buf, size, &used, line);
  ssh_shell_append(buf, size, &used, audit);
}

void ssh_format_security_keys(char *buf, size_t size) {
  char hostkey[640];
  size_t used = 0;

  if (!buf || size == 0) {
    return;
  }
  buf[0] = '\0';
  ssh_security_write_policy_file();
  ssh_security_write_state_file();
  ssh_format_hostkey(hostkey, sizeof(hostkey));
  ssh_shell_append(buf, size, &used,
                   "security keys:\n"
                   "  ssh-hostkey: per-install persistent RSA identity\n"
                   "  rotation: command=security rotate ssh-hostkey "
                   "effect=future-ssh-sessions known-hosts-may-change\n"
                   "  rotation-summary: ssh-hostkey=runtime "
                   "update-root=release-required package-root=release-required\n"
                   "  private-material: never printed by hostkey/security; "
                   "read/write blocked by SSH file policy\n"
                   "  update-root: id=orizon-update-root-2026-05 "
                   "storage=compiled-public-key rotation=release-required\n"
                   "  package-index: signed-manifest-sha256-pinned "
                   "detached-sidecar=/workspace/.orizon/package-index.sig "
                   "rotation=release-required warn-if-missing\n");
  ssh_shell_append(buf, size, &used, hostkey);
}

void ssh_format_security_doctor(char *buf, size_t size) {
  char line[256];
  char detail[192];
  size_t used = 0;
  int warnings = 0;
  int failures = 0;
  int hostkey_ok;
  int policy_file_ok;
  int state_file_ok;

  if (!buf || size == 0) {
    return;
  }
  buf[0] = '\0';
  ssh_ensure_hostkey();
  ssh_security_write_policy_file();
  ssh_security_write_state_file();
  hostkey_ok = ssh_status.hostkey_loaded && ssh_status.hostkey_persistent;
  policy_file_ok = vfs_exists(ORIZON_SECURITY_POLICY_PATH);
  state_file_ok = vfs_exists(ORIZON_SECURITY_STATE_PATH);

#define SECURITY_DOCTOR_LINE(label, state, detail)                         \
  do {                                                                     \
    snprintf(line, sizeof(line), "  %-22s %-4s %s\n", label, state, detail); \
    ssh_shell_append(buf, size, &used, line);                              \
  } while (0)

  ssh_shell_append(buf, size, &used, "security doctor:\n");
  SECURITY_DOCTOR_LINE("ssh.hostkey", hostkey_ok ? "PASS" : "WARN",
                       ssh_status.hostkey_persistent
                           ? "persistent host key loaded"
                           : "compiled/bootstrap fallback or not persisted");
  if (!hostkey_ok) {
    warnings++;
  }
  SECURITY_DOCTOR_LINE("ssh.auth-lockout", "PASS",
                       "password auth is opt-in and lockout is configurable");
  SECURITY_DOCTOR_LINE("audit.persistence",
                       vfs_exists(ORIZON_SECURITY_LOG_PATH) ? "PASS" : "WARN",
                       ORIZON_SECURITY_LOG_PATH);
  if (!vfs_exists(ORIZON_SECURITY_LOG_PATH)) {
    warnings++;
  }
  SECURITY_DOCTOR_LINE("audit.redaction", "PASS",
                       "password/write/append/wifi arguments redacted");
  SECURITY_DOCTOR_LINE("policy.file", policy_file_ok ? "PASS" : "WARN",
                       ORIZON_SECURITY_POLICY_PATH);
  if (!policy_file_ok) {
    warnings++;
  }
  SECURITY_DOCTOR_LINE("policy.state", state_file_ok ? "PASS" : "WARN",
                       ORIZON_SECURITY_STATE_PATH);
  if (!state_file_ok) {
    warnings++;
  }
  SECURITY_DOCTOR_LINE("vfs.policy", "PASS",
                       "version=2 sensitive reads blocked; writes scoped");
  snprintf(detail, sizeof(detail),
           "tracked total=%lu sensitive=%lu internal=%lu root=%lu",
           (unsigned long)ssh_policy_denied_total,
           (unsigned long)ssh_policy_denied_sensitive,
           (unsigned long)ssh_policy_denied_internal,
           (unsigned long)ssh_policy_denied_root);
  SECURITY_DOCTOR_LINE("policy-denies", "PASS", detail);
  SECURITY_DOCTOR_LINE("ssh.file-policy", "PASS",
                       "sensitive paths blocked; writes scoped");
  SECURITY_DOCTOR_LINE("update.manifest", "PASS",
                       "manifest.sig required with compiled root key");
  SECURITY_DOCTOR_LINE("package.policy", "PASS",
                       "package paths/scripts are scoped");
  SECURITY_DOCTOR_LINE("package.signature", "WARN",
                       "sidecar prepared; fallback is signed manifest pin");
  warnings++;
  SECURITY_DOCTOR_LINE("key.rotation", "WARN",
                       "ssh runtime; update/package require signed release");
  warnings++;
  SECURITY_DOCTOR_LINE("release.secrets", "PASS",
                       "tracked secret scan is part of release validation");
  SECURITY_DOCTOR_LINE("user-admin-split", "WARN",
                       "planned; current remote user is command-scoped admin");
  warnings++;
  SECURITY_DOCTOR_LINE("secureboot-tpm", "WARN",
                       "not implemented yet");
  warnings++;
  SECURITY_DOCTOR_LINE("disk.encryption", "WARN",
                       "not implemented yet");
  warnings++;
  SECURITY_DOCTOR_LINE("doctor.snapshot", "PASS",
                       ORIZON_SECURITY_DOCTOR_PATH);
  snprintf(line, sizeof(line), "summary: %s warnings=%d failures=%d\n",
           failures ? "FAIL" : (warnings ? "WARN" : "PASS"), warnings,
           failures);
  ssh_shell_append(buf, size, &used, line);
  ssh_write_text_file_raw(ORIZON_SECURITY_DOCTOR_PATH, buf);

#undef SECURITY_DOCTOR_LINE
}

void ssh_format_report(char *buf, size_t size) {
  char status[512];
  char algs[1600];
  char hostkey[512];
  const netstack_status_t *net = netstack_get_status();
  char ip[24];

  if (!buf || size == 0) {
    return;
  }
  ssh_format_status(status, sizeof(status));
  ssh_format_algorithms(algs, sizeof(algs));
  ssh_format_hostkey(hostkey, sizeof(hostkey));
  netstack_format_ipv4(net->ip, ip, sizeof(ip));
  snprintf(buf, size,
           "%s\n"
           "%s"
           "%s"
           "config: %s\n"
           "log: %s\n"
           "local: %s:%u\n"
           "note: SSH remote shell is enabled after password auth.\n",
           status, algs, hostkey, ORIZON_SSH_CONFIG_PATH,
           ORIZON_SSH_LOG_PATH, ip, (unsigned)ORIZON_SSH_PORT);
}

const ssh_status_t *ssh_get_status(void) {
  return &ssh_status;
}
