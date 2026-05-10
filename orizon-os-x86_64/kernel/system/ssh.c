/*
 * Orizon OS x86_64 - SSH service
 *
 * This is the first safe remote-management brick: an inbound TCP/22 service
 * with an SSH protocol banner and diagnostics. Full encrypted KEX/auth/shell
 * will build on top of this listener instead of hiding an unsafe backdoor.
 */

#include "../include/ssh.h"
#include "../include/aes_gcm.h"
#include "../include/klog.h"
#include "../include/netstack.h"
#include "../include/packages.h"
#include "../include/rsa.h"
#include "../include/sched.h"
#include "../include/sha256.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"
#include "../include/x25519.h"

#define SSH_BANNER "SSH-2.0-OrizonSSH_0.1\r\n"
#define SSH_RX_BUF 2048
#define SSH_PACKET_MAX 4096
#define SSH_TX_BUF 1400

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

/*
 * Development host key for the current staged SSH server.
 * TODO: replace with per-install persistent key generation before SSH shell is
 * enabled by default on real machines.
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
static uint8_t ssh_hostkey_p[64];
static uint8_t ssh_hostkey_q[64];
static uint8_t ssh_hostkey_dmp1[64];
static uint8_t ssh_hostkey_dmq1[64];
static uint8_t ssh_hostkey_iqmp[64];
static rsa_crt_private_key_t ssh_hostkey = {
    .n = ORIZON_SSH_RSA_N,
    .n_len = sizeof(ORIZON_SSH_RSA_N),
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
static int ssh_channel_close_pending = 0;
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
static char ssh_channel_tx[900];
static size_t ssh_channel_tx_len = 0;
static char ssh_shell_line[256];
static size_t ssh_shell_line_len = 0;
static char ssh_shell_cwd[MAX_PATH] = "/home/orizon";
static int ssh_shell_suppress_prompt = 0;

static int ssh_ensure_hostkey(void);
static int ssh_load_hostkey_file(void);
static int ssh_write_hostkey_file(void);
static int ssh_rebuild_host_key_blob(void);
static void ssh_install_bootstrap_hostkey(void);
static void ssh_reset_negotiation(void);
static void ssh_refresh_state(void);

static void ssh_log_line(const char *line) {
  file_t *f;

  if (!line) {
    return;
  }
  vfs_mkdir("/logs");
  f = vfs_open(ORIZON_SSH_LOG_PATH, O_CREAT | O_WRONLY | O_APPEND);
  if (!f) {
    return;
  }
  vfs_write(f, line, strlen(line));
  vfs_write(f, "\n", 1);
  vfs_close(f);
}

static void ssh_set_status(const char *status) {
  strncpy(ssh_status.status, status, sizeof(ssh_status.status) - 1);
  ssh_status.status[sizeof(ssh_status.status) - 1] = '\0';
  klog_info("ssh", status);
  ssh_log_line(status);
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
  if (ssh_status.max_auth_attempts > 0 &&
      ssh_status.auth_failures >= ssh_status.max_auth_attempts) {
    ssh_status.auth_lockout_until =
        timer_uptime_seconds() + ssh_status.auth_lockout_seconds;
    ssh_status.auth_failures = 0;
    ssh_set_status("ssh: auth locked after repeated failures");
    return;
  }
  ssh_set_status(reason ? reason : "ssh: authentication failed");
}

static void ssh_note_auth_success(void) {
  ssh_status.authenticated = 1;
  ssh_status.auth_failures = 0;
  ssh_status.auth_lockout_until = 0;
  ssh_auth_success_pending = 1;
  ssh_set_status("ssh: password auth accepted");
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
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: password auth disabled; existing sessions remain until closed.\n");
  }
  return 0;
}

int ssh_reload_config(char *report, size_t report_size) {
  ssh_load_config();
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
  if (report && report_size > 0) {
    snprintf(report, report_size, "ssh: auth lockout cleared.\n");
  }
  return 0;
}

int ssh_set_auth_policy(uint32_t max_attempts, uint32_t lockout_seconds,
                        char *report, size_t report_size) {
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
  return 0;
}

int ssh_reset_hostkey(char *report, size_t report_size) {
  vfs_delete(ORIZON_SSH_HOSTKEY_PATH);
  ssh_install_bootstrap_hostkey();
  if (ssh_write_hostkey_file() != 0) {
    if (report && report_size > 0) {
      snprintf(report, report_size,
               "ssh: host key reset failed; could not write %s.\n",
               ORIZON_SSH_HOSTKEY_PATH);
    }
    return -1;
  }
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: host key reset and persisted to %s\n"
             "ssh: hostkey-sha256=%s\n",
             ORIZON_SSH_HOSTKEY_PATH,
             ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256
                                          : "none");
  }
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
                                         const uint8_t *iqmp) {
  memcpy(ssh_hostkey_n, n, sizeof(ssh_hostkey_n));
  memcpy(ssh_hostkey_p, p, sizeof(ssh_hostkey_p));
  memcpy(ssh_hostkey_q, q, sizeof(ssh_hostkey_q));
  memcpy(ssh_hostkey_dmp1, dmp1, sizeof(ssh_hostkey_dmp1));
  memcpy(ssh_hostkey_dmq1, dmq1, sizeof(ssh_hostkey_dmq1));
  memcpy(ssh_hostkey_iqmp, iqmp, sizeof(ssh_hostkey_iqmp));
  ssh_hostkey.n = ssh_hostkey_n;
  ssh_hostkey.n_len = sizeof(ssh_hostkey_n);
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
                               ORIZON_SSH_RSA_DMQ1, ORIZON_SSH_RSA_IQMP);
  ssh_set_hostkey_report("compiled-bootstrap",
                         "ssh: using compiled bootstrap host key", 0, 1);
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
  uint8_t p_buf[64];
  uint8_t q_buf[64];
  uint8_t dmp1_buf[64];
  uint8_t dmq1_buf[64];
  uint8_t iqmp_buf[64];
  ssize_t n;

  f = vfs_open(ORIZON_SSH_HOSTKEY_PATH, O_RDONLY);
  if (!f) {
    return -1;
  }
  memset(text, 0, sizeof(text));
  n = vfs_read(f, text, sizeof(text) - 1);
  vfs_close(f);
  if (n <= 0 || !strstr(text, "format orizon-ssh-rsa-crt-v1")) {
    return -1;
  }
  if (ssh_parse_hex_field(text, "n", n_buf, sizeof(n_buf)) != 0 ||
      ssh_parse_hex_field(text, "p", p_buf, sizeof(p_buf)) != 0 ||
      ssh_parse_hex_field(text, "q", q_buf, sizeof(q_buf)) != 0 ||
      ssh_parse_hex_field(text, "dmp1", dmp1_buf, sizeof(dmp1_buf)) != 0 ||
      ssh_parse_hex_field(text, "dmq1", dmq1_buf, sizeof(dmq1_buf)) != 0 ||
      ssh_parse_hex_field(text, "iqmp", iqmp_buf, sizeof(iqmp_buf)) != 0) {
    return -1;
  }
  ssh_install_hostkey_material(n_buf, p_buf, q_buf, dmp1_buf, dmq1_buf,
                               iqmp_buf);
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
                      "format orizon-ssh-rsa-crt-v1\n"
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
                      "generator compiled-bootstrap\n"
                      "note rsa-generation-todo\n") != 0 ||
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
                         ssh_status.hostkey_bootstrap);
  return 0;
}

static int ssh_ensure_hostkey(void) {
  if (ssh_status.hostkey_loaded && ssh_hostkey.n &&
      ssh_status.hostkey_sha256[0]) {
    return 0;
  }
  if (ssh_load_hostkey_file() == 0) {
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

  if (!ssh_channel_data_pending || ssh_channel_tx_len == 0) {
    return 0;
  }
  payload[off++] = SSH_MSG_CHANNEL_DATA;
  ssh_put_u32(payload + off, ssh_client_channel);
  off += 4;
  if (ssh_put_string(payload, sizeof(ssh_channel_payload), &off,
                     (const uint8_t *)ssh_channel_tx,
                     ssh_channel_tx_len) != 0) {
    return 0;
  }
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
  ssh_put_u32(payload + off, 0);
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
  ssh_channel_close_pending = 0;
  ssh_encrypted_rx_used = 0;
  memset(ssh_pending_ctr_s2c, 0, sizeof(ssh_pending_ctr_s2c));
  ssh_pending_ctr_s2c_ready = 0;
  ssh_client_channel = 0;
  ssh_server_channel = 0;
  ssh_client_window = 0;
  ssh_client_max_packet = 0;
  ssh_channel_tx_len = 0;
  ssh_shell_line_len = 0;
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
    ssh_status.channel_open_seen = 1;
    ssh_channel_open_confirm_pending = 1;
    ssh_set_status("ssh: session channel open received");
    return;
  }
  ssh_channel_failure_pending = 1;
  ssh_set_status("ssh: unsupported channel type");
}

static void ssh_remote_exec_execute(const uint8_t *command,
                                    size_t command_len);

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

static void ssh_shell_print_file(const char *arg, size_t max_bytes) {
  static char path[MAX_PATH];
  static char out[880];
  static char buf[640];
  size_t used = 0;
  ssize_t n;
  file_t *f;

  if (ssh_shell_resolve_path(arg, path, sizeof(path)) < 0) {
    ssh_queue_channel_text("cat: invalid path\r\n");
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
  n = vfs_read(f, buf, max_bytes);
  vfs_close(f);
  if (n < 0) {
    ssh_queue_channel_text("cat: read failed\r\n");
    ssh_shell_prompt();
    return;
  }
  ssh_shell_append_file_text(out, sizeof(out), &used, buf, (size_t)n);
  if ((size_t)n == max_bytes || (size_t)n == sizeof(buf)) {
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

static void ssh_shell_print_storage(void) {
  static char out[880];
  static char line[160];
  static char cap[64];
  size_t used = 0;
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
  }
  ssh_queue_channel_text(out);
  ssh_shell_prompt();
}

static void ssh_shell_print_pkg(const char *args) {
  static char out[880];
  const char *sub = ssh_shell_skip_spaces(args);

  if (*sub == '\0' || ssh_shell_command_is(sub, "status")) {
    orizon_pkg_status(out, sizeof(out));
  } else if (ssh_shell_command_is(sub, "list")) {
    orizon_pkg_list(out, sizeof(out));
  } else {
    snprintf(out, sizeof(out), "pkg: remote supports 'pkg status' and 'pkg list'\r\n");
  }
  if (strlen(out) + 2 < sizeof(out)) {
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
    ssh_shell_print_file(ORIZON_SSH_LOG_PATH, 560);
  } else if (ssh_shell_command_is(which, "boot")) {
    ssh_shell_print_file(KLOG_BOOT_PATH, 560);
  } else {
    char out[880];
    size_t n = klog_snapshot(out, sizeof(out) - 3);
    out[n] = '\0';
    if (n > 0 && out[n - 1] != '\n') {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out[0] ? out : "logs: empty\r\n");
    ssh_shell_prompt();
  }
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
        "\r\nOrizon OS remote shell preview\r\n"
        "Commands: help, ls, cd, cat, write, logs, net, ps, pkg, storage, status, auth, hostkey, exit\r\n");
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
  static char out[880];
  const char *args;

  if (!line || line[0] == '\0') {
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "help") == 0) {
    ssh_queue_channel_text(
        "Remote Orizon commands:\r\n"
        "  help                 show this help\r\n"
        "  status               show SSH transport state\r\n"
        "  auth                 show SSH auth policy\r\n"
        "  hostkey              show SSH host identity\r\n"
        "  ls [path]            list files\r\n"
        "  cd <path>            change directory\r\n"
        "  pwd                  show remote cwd\r\n"
        "  cat <file>           print a file preview\r\n"
        "  head <file>          print a shorter file preview\r\n"
        "  touch|mkdir|rm       edit VFS entries\r\n"
        "  write|append f text  write text to a file\r\n"
        "  logs [ssh|boot]      show logs\r\n"
        "  net|route|dns        show network diagnostics\r\n"
        "  ps|pkg|storage       show system state\r\n"
        "  ssh password <pass>  change remote SSH password\r\n"
        "  ssh auth ...         change auth policy\r\n"
        "  sync                 persist Orizon data roots\r\n"
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
  if (strcmp(line, "auth") == 0 || strcmp(line, "ssh auth") == 0) {
    ssh_format_auth(out, sizeof(out));
    if (strlen(out) + strlen("\r\norizon$ ") < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
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
    ssh_shell_print_file(ssh_shell_skip_spaces(line + 3), 1400);
    return;
  }
  if (ssh_shell_command_is(line, "head")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(line + 4), 700);
    return;
  }
  if (ssh_shell_command_is(line, "touch")) {
    char path[MAX_PATH];
    if (ssh_shell_resolve_path(line + 5, path, sizeof(path)) < 0 ||
        vfs_create(path) < 0) {
      ssh_queue_channel_text("touch: failed\r\n");
    } else {
      ssh_queue_channel_text("touch: ok\r\n");
    }
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "mkdir")) {
    char path[MAX_PATH];
    if (ssh_shell_resolve_path(line + 5, path, sizeof(path)) < 0 ||
        vfs_mkdir(path) < 0) {
      ssh_queue_channel_text("mkdir: failed\r\n");
    } else {
      ssh_queue_channel_text("mkdir: ok\r\n");
    }
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "rm")) {
    char path[MAX_PATH];
    if (ssh_shell_resolve_path(line + 2, path, sizeof(path)) < 0 ||
        vfs_delete(path) < 0) {
      ssh_queue_channel_text("rm: failed\r\n");
    } else {
      ssh_queue_channel_text("rm: ok\r\n");
    }
    ssh_shell_prompt();
    return;
  }
  if (ssh_shell_command_is(line, "write") ||
      ssh_shell_command_is(line, "append")) {
    char file_arg[MAX_PATH];
    char path[MAX_PATH];
    const char *text = NULL;
    int append = ssh_shell_command_is(line, "append");
    file_t *f;

    args = line + (append ? 6 : 5);
    if (ssh_shell_split_path_text(args, file_arg, sizeof(file_arg), &text) < 0 ||
        ssh_shell_resolve_path(file_arg, path, sizeof(path)) < 0) {
      ssh_queue_channel_text(append ? "usage: append <file> <text>\r\n"
                                    : "usage: write <file> <text>\r\n");
      ssh_shell_prompt();
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
    return;
  }
  if (ssh_shell_command_is(line, "logs")) {
    ssh_shell_print_log(ssh_shell_skip_spaces(line + 4));
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
  if (strcmp(line, "storage") == 0 || strcmp(line, "disks") == 0) {
    ssh_shell_print_storage();
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
    ssh_queue_channel_text("memory: 64 MB heap profile; detailed allocator stats pending\r\n");
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "net") == 0 || strcmp(line, "network-status") == 0) {
    netstack_format_status(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
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
  if (strcmp(line, "dns") == 0) {
    netstack_format_dns(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
    ssh_shell_prompt();
    return;
  }
  if (strcmp(line, "sync") == 0) {
    ssh_queue_channel_text(vfs_persist_save() == 0
                               ? "sync: ok\r\n"
                               : "sync: persistence unavailable\r\n");
    ssh_shell_prompt();
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
  static char out[768];
  size_t copy = command_len;

  if (copy >= sizeof(cmd)) {
    copy = sizeof(cmd) - 1;
  }
  memcpy(cmd, command, copy);
  cmd[copy] = '\0';

  ssh_shell_suppress_prompt = 1;
  if (strstr(cmd, "help")) {
    ssh_queue_channel_text(
        "Remote Orizon commands: help, ls, cd, cat, write, logs, net, ps, pkg, storage, timer, status, auth, hostkey, ssh password, ssh auth, ssh lockout, exit\r\n");
  } else if (ssh_shell_command_is(cmd, "ls")) {
    ssh_shell_print_ls(ssh_shell_skip_spaces(cmd + 2));
  } else if (ssh_shell_command_is(cmd, "cat")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(cmd + 3), 1400);
  } else if (ssh_shell_command_is(cmd, "head")) {
    ssh_shell_print_file(ssh_shell_skip_spaces(cmd + 4), 700);
  } else if (ssh_shell_command_is(cmd, "logs")) {
    ssh_shell_print_log(ssh_shell_skip_spaces(cmd + 4));
  } else if (strcmp(cmd, "ps") == 0) {
    ssh_shell_print_ps();
  } else if (ssh_shell_command_is(cmd, "pkg")) {
    ssh_shell_print_pkg(cmd + 3);
  } else if (strcmp(cmd, "storage") == 0 || strcmp(cmd, "disks") == 0) {
    ssh_shell_print_storage();
  } else if (strcmp(cmd, "timer") == 0) {
    timer_format_status(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
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
  } else if (strstr(cmd, "status")) {
    ssh_format_status(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strstr(cmd, "auth")) {
    ssh_format_auth(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strstr(cmd, "hostkey")) {
    ssh_format_hostkey(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "net") == 0 || strcmp(cmd, "network-status") == 0) {
    netstack_format_status(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "route") == 0) {
    netstack_format_route(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "dns") == 0) {
    netstack_format_dns(out, sizeof(out));
    if (strlen(out) + 2 < sizeof(out)) {
      strcat(out, "\r\n");
    }
    ssh_queue_channel_text(out);
  } else if (strcmp(cmd, "uptime") == 0) {
    snprintf(out, sizeof(out), "uptime=%lus ticks=%lu hz=%lu\r\n",
             (unsigned long)timer_uptime_seconds(),
             (unsigned long)timer_ticks(), (unsigned long)timer_hz());
    ssh_queue_channel_text(out);
  } else if (strstr(cmd, "whoami")) {
    ssh_queue_channel_text("orizon\r\n");
  } else if (strstr(cmd, "uname")) {
    ssh_queue_channel_text("Orizon OS x86_64 OrizonSSH_0.1\r\n");
  } else if (strstr(cmd, "pwd")) {
    ssh_queue_channel_text("/home/orizon\r\n");
  } else {
    snprintf(out, sizeof(out), "%s: command not found\r\n", cmd);
    ssh_queue_channel_text(out);
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
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      ssh_shell_line[ssh_shell_line_len] = '\0';
      ssh_remote_shell_execute(ssh_shell_line);
      ssh_shell_line_len = 0;
      continue;
    }
    if (ch == '\b' || ch == 0x7f) {
      if (ssh_shell_line_len > 0) {
        ssh_shell_line_len--;
      }
      continue;
    }
    if (ssh_shell_line_len + 1 < sizeof(ssh_shell_line)) {
      ssh_shell_line[ssh_shell_line_len++] = ch;
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
    ssh_channel_close_pending = 1;
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
  ssh_status.sessions = ssh_server.connections;

  if (ssh_server.connections != ssh_seen_connections) {
    ssh_seen_connections = ssh_server.connections;
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
      ssh_channel_data_pending = 0;
      ssh_channel_tx_len = 0;
      ssh_seq_out++;
      ssh_set_status("ssh: CHANNEL_DATA sent");
    } else if (tx_kind == 13) {
      if (ssh_pending_ctr_s2c_ready) {
        memcpy(ssh_ctr_s2c, ssh_pending_ctr_s2c, sizeof(ssh_ctr_s2c));
        ssh_pending_ctr_s2c_ready = 0;
      }
      ssh_seq_out++;
      ssh_channel_close_pending = 0;
      ssh_disconnect_close_polls = 6;
      ssh_set_status("ssh: channel close sent; graceful relisten pending");
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
           ssh_status.hostkey_persistent ? "persistent" : "bootstrap",
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
           "  mac-s2c-sha256: %s\n"
           "  next: generated per-install RSA material and full PTY\n",
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
           "  lockout-remaining: %lus\n"
           "  config: %s\n"
           "  log: %s\n",
           ssh_status.auth_configured ? "enabled" : "disabled",
           (unsigned long)ssh_status.max_auth_attempts,
           (unsigned long)ssh_status.auth_lockout_seconds,
           (unsigned long)ssh_status.auth_failures,
           (unsigned long)lockout,
           ORIZON_SSH_CONFIG_PATH, ORIZON_SSH_LOG_PATH);
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
           "  status: %s\n"
           "  next: replace bootstrap material with generated per-install RSA\n",
           ssh_status.hostkey_persistent ? "persistent-file" : "compiled",
           ssh_status.hostkey_bootstrap ? "yes" : "no",
           ssh_status.hostkey_source[0] ? ssh_status.hostkey_source : "none",
           ORIZON_SSH_HOSTKEY_PATH,
           ssh_status.hostkey_sha256[0] ? ssh_status.hostkey_sha256 : "none",
           ssh_status.hostkey_status[0] ? ssh_status.hostkey_status
                                        : "ssh: host key not loaded");
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
           "note: SSH remote shell preview is enabled after password auth.\n",
           status, algs, hostkey, ORIZON_SSH_CONFIG_PATH,
           ORIZON_SSH_LOG_PATH, ip, (unsigned)ORIZON_SSH_PORT);
}

const ssh_status_t *ssh_get_status(void) {
  return &ssh_status;
}
