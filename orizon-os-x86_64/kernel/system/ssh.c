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
#include "../include/rsa.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"
#include "../include/x25519.h"

#define SSH_BANNER "SSH-2.0-OrizonSSH_0.1\r\n"
#define SSH_RX_BUF 2048
#define SSH_PACKET_MAX 4096
#define SSH_TX_BUF 1024

#define SSH_MSG_DISCONNECT 1
#define SSH_MSG_SERVICE_REQUEST 5
#define SSH_MSG_SERVICE_ACCEPT 6
#define SSH_MSG_KEXINIT 20
#define SSH_MSG_NEWKEYS 21
#define SSH_MSG_KEXDH_INIT 30
#define SSH_MSG_KEXDH_REPLY 31

#define SSH_KEX_ALGORITHMS "curve25519-sha256,curve25519-sha256@libssh.org"
#define SSH_HOSTKEY_ALGORITHMS "rsa-sha2-256"
#define SSH_CIPHER_ALGORITHMS "aes128-ctr,aes128-gcm@openssh.com"
#define SSH_MAC_ALGORITHMS "hmac-sha2-256,hmac-sha1"
#define SSH_COMPRESSION_ALGORITHMS "none"
#define SSH_RSA_SIGNATURE_SIZE 128U

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
    .status = "ssh: stopped",
};

static netstack_tcp_server_t ssh_server;
static uint32_t ssh_seen_connections = 0;
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
static uint8_t ssh_encrypted_rx[SSH_PACKET_MAX + SHA256_DIGEST_SIZE];
static size_t ssh_encrypted_rx_used = 0;
static uint8_t ssh_pending_ctr_s2c[16];
static int ssh_pending_ctr_s2c_ready = 0;
static uint8_t ssh_mac_input[SSH_PACKET_MAX + 4];

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

static void ssh_write_default_config(void) {
  file_t *f;
  const char *text =
      "enabled yes\n"
      "port 22\n"
      "mode staged-ssh-kexinit\n"
      "auth disabled-until-kex-auth-shell\n";

  vfs_mkdir("/system");
  f = vfs_open(ORIZON_SSH_CONFIG_PATH, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return;
  }
  vfs_write(f, text, strlen(text));
  vfs_close(f);
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

static int ssh_wrap_packet(uint8_t *out, size_t cap, const uint8_t *payload,
                           size_t payload_len, size_t *out_len) {
  size_t padding_len = 8 - ((payload_len + 5) % 8);
  size_t packet_len;
  size_t off = 0;
  uint32_t seed = 0x4f5a5353U ^ (uint32_t)timer_ticks() ^
                  (uint32_t)ssh_status.sessions;

  if (padding_len < 4) {
    padding_len += 8;
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
  static const uint8_t exponent[3] = {0x01, 0x00, 0x01};
  size_t off = 0;

  if (ssh_host_key_blob_len > 0) {
    if (!ssh_status.hostkey_sha256[0]) {
      sha256_buffer_hex(ssh_host_key_blob, ssh_host_key_blob_len,
                        ssh_status.hostkey_sha256);
    }
    return 0;
  }
  if (ssh_put_cstring(ssh_host_key_blob, sizeof(ssh_host_key_blob), &off,
                      "ssh-rsa") != 0 ||
      ssh_put_mpint(ssh_host_key_blob, sizeof(ssh_host_key_blob), &off,
                    exponent, sizeof(exponent)) != 0 ||
      ssh_put_mpint(ssh_host_key_blob, sizeof(ssh_host_key_blob), &off,
                    ORIZON_SSH_RSA_N, sizeof(ORIZON_SSH_RSA_N)) != 0) {
    return -1;
  }
  ssh_host_key_blob_len = off;
  sha256_buffer_hex(ssh_host_key_blob, ssh_host_key_blob_len,
                    ssh_status.hostkey_sha256);
  return 0;
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
  uint8_t plain[SSH_PACKET_MAX];
  uint8_t mac[SHA256_DIGEST_SIZE];
  uint8_t ctr_tmp[16];
  size_t plain_len = 0;

  if (!out || !payload || !ssh_out_encrypted || !ssh_status.traffic_keys_ready ||
      ssh_wrap_packet(plain, sizeof(plain), payload, payload_len, &plain_len) !=
          0 ||
      cap < plain_len + SHA256_DIGEST_SIZE) {
    return 0;
  }

  ssh_mac_packet(ssh_mac_s2c, ssh_seq_out, plain, plain_len, mac);
  memcpy(ctr_tmp, ssh_ctr_s2c, sizeof(ctr_tmp));
  aes128_ctr_crypt_update(ssh_key_s2c, ctr_tmp, plain, plain_len, out);
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
  ssh_status.hostkey_sha256[0] = '\0';
  ssh_status.server_public_sha256[0] = '\0';
  ssh_status.shared_secret_sha256[0] = '\0';
  ssh_status.exchange_hash_sha256[0] = '\0';
  ssh_status.signature_sha256[0] = '\0';
  ssh_status.client_to_server_key_sha256[0] = '\0';
  ssh_status.server_to_client_key_sha256[0] = '\0';
  ssh_status.client_to_server_mac_sha256[0] = '\0';
  ssh_status.server_to_client_mac_sha256[0] = '\0';
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
  ssh_encrypted_rx_used = 0;
  memset(ssh_pending_ctr_s2c, 0, sizeof(ssh_pending_ctr_s2c));
  ssh_pending_ctr_s2c_ready = 0;
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
  rsa_crt_private_key_t key = {
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

  sha256_buffer(ssh_exchange_hash, sizeof(ssh_exchange_hash), signature_digest);
  if (rsa_pkcs1v15_sha256_sign_crt(ssh_host_signature,
                                   sizeof(ssh_host_signature),
                                   signature_digest, &key) != 0) {
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
  UNUSED(payload);
  UNUSED(payload_len);
  ssh_status.encrypted_packet_seen = 1;
  ssh_status.userauth_request_seen = 1;
  ssh_set_status("ssh: encrypted USERAUTH_REQUEST received; auth next");
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
  if (type == 50) {
    ssh_process_userauth_request(payload, payload_len);
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
  ssh_write_default_config();
  ssh_set_status("ssh: listening on tcp/22");

  net = netstack_get_status();
  netstack_format_ipv4(net->ip, ip, sizeof(ip));
  if (report && report_size > 0) {
    snprintf(report, report_size,
             "ssh: listening on %s:%u\n"
             "ssh: from another machine, test with: ssh root@%s\n"
             "ssh: current stage accepts TCP and SSH banner diagnostics; encrypted shell is next.\n",
             ip, (unsigned)ORIZON_SSH_PORT, ip);
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
  uint8_t rx[SSH_RX_BUF];
  size_t rx_len = 0;
  uint8_t txbuf[SSH_TX_BUF];
  const void *tx = NULL;
  size_t tx_len = 0;
  int tx_kind = 0;
  int rc;

  if (!ssh_status.enabled) {
    return 0;
  }

  ssh_refresh_state();
  if (ssh_status.connected && !ssh_status.banner_sent) {
    tx = SSH_BANNER;
    tx_len = strlen(SSH_BANNER);
    tx_kind = 1;
  } else if (ssh_status.connected && ssh_status.client_banner_seen &&
             !ssh_status.server_kexinit_sent) {
    tx_len = ssh_build_kexinit(txbuf, sizeof(txbuf));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 2;
  } else if (ssh_status.connected && ssh_status.server_kexinit_sent &&
             ssh_status.client_kexinit_seen &&
             !ssh_algorithm_ready() &&
             !ssh_status.disconnect_sent) {
    tx_len = ssh_build_disconnect(txbuf, sizeof(txbuf));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 3;
  } else if (ssh_status.connected && ssh_status.ecdh_ready &&
             ssh_status.client_kex_packet_seen &&
             !ssh_status.ecdh_reply_sent) {
    tx_len = ssh_build_ecdh_reply(txbuf, sizeof(txbuf));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 4;
  } else if (ssh_status.connected && ssh_status.ecdh_reply_sent &&
             !ssh_status.newkeys_sent) {
    tx_len = ssh_build_newkeys(txbuf, sizeof(txbuf));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 5;
  } else if (ssh_status.connected && ssh_status.newkeys_sent &&
             ssh_status.client_newkeys_seen && ssh_service_accept_pending &&
             !ssh_status.service_accept_sent) {
    tx_len = ssh_build_service_accept(txbuf, sizeof(txbuf));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 6;
  } else if (ssh_status.connected && ssh_status.userauth_request_seen) {
    netstack_tcp_server_close(&ssh_server);
    netstack_tcp_server_init(&ssh_server, ORIZON_SSH_PORT);
    ssh_seen_connections = ssh_server.connections;
    ssh_reset_negotiation();
    ssh_set_status("ssh: closed before encrypted auth layer");
    ssh_refresh_state();
    return 1;
  } else if (ssh_status.connected && ssh_status.disconnect_sent &&
             ssh_disconnect_close_polls > 0) {
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

  rc = netstack_tcp_server_poll(&ssh_server, tx, tx_len, rx, sizeof(rx),
                                &rx_len);
  if (rc < 0) {
    ssh_status.errors++;
  }
  if (tx && tx_len > 0 && rc == 4) {
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
    }
  }
  if (rx_len > 0) {
    ssh_capture_client_data(rx, rx_len);
  }
  ssh_refresh_state();
  return rc;
}

void ssh_format_status(char *buf, size_t size) {
  char rip[24];

  if (!buf || size == 0) {
    return;
  }
  netstack_format_ipv4(ssh_status.remote_ip, rip, sizeof(rip));
  snprintf(buf, size,
           "ssh: enabled=%s state=%s port=%u connected=%s remote=%s:%u "
           "sessions=%lu banner=%s skex=%s ckex=%s pkt=%u ecdh=%s "
           "reply=%s newkeys=%s cnewkeys=%s keys=%s enc=%s svc=%s auth=%s kex=%s "
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
           ssh_status.userauth_request_seen ? "seen" : "pending",
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
           "  next: implement encrypted packets, auth and PTY\n",
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

void ssh_format_report(char *buf, size_t size) {
  char status[512];
  char algs[1600];
  const netstack_status_t *net = netstack_get_status();
  char ip[24];

  if (!buf || size == 0) {
    return;
  }
  ssh_format_status(status, sizeof(status));
  ssh_format_algorithms(algs, sizeof(algs));
  netstack_format_ipv4(net->ip, ip, sizeof(ip));
  snprintf(buf, size,
           "%s\n"
           "%s"
           "config: %s\n"
           "log: %s\n"
           "local: %s:%u\n"
           "note: SSH remote shell is not enabled until KEX/auth/PTY are implemented.\n",
           status, algs, ORIZON_SSH_CONFIG_PATH, ORIZON_SSH_LOG_PATH, ip,
           (unsigned)ORIZON_SSH_PORT);
}

const ssh_status_t *ssh_get_status(void) {
  return &ssh_status;
}
