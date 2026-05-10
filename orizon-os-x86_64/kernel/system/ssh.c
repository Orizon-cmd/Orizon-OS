/*
 * Orizon OS x86_64 - SSH service
 *
 * This is the first safe remote-management brick: an inbound TCP/22 service
 * with an SSH protocol banner and diagnostics. Full encrypted KEX/auth/shell
 * will build on top of this listener instead of hiding an unsafe backdoor.
 */

#include "../include/ssh.h"
#include "../include/klog.h"
#include "../include/netstack.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"

#define SSH_BANNER "SSH-2.0-OrizonSSH_0.1\r\n"
#define SSH_RX_BUF 2048
#define SSH_PACKET_MAX 4096
#define SSH_TX_BUF 1024

#define SSH_MSG_DISCONNECT 1
#define SSH_MSG_KEXINIT 20
#define SSH_MSG_KEXDH_INIT 30

#define SSH_KEX_ALGORITHMS "curve25519-sha256,curve25519-sha256@libssh.org"
#define SSH_HOSTKEY_ALGORITHMS "rsa-sha2-256,rsa-sha2-512"
#define SSH_CIPHER_ALGORITHMS "aes128-ctr,aes128-gcm@openssh.com"
#define SSH_MAC_ALGORITHMS "hmac-sha2-256,hmac-sha1"
#define SSH_COMPRESSION_ALGORITHMS "none"

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
    .status = "ssh: stopped",
};

static netstack_tcp_server_t ssh_server;
static uint32_t ssh_seen_connections = 0;
static int ssh_disconnect_close_polls = 0;
static uint8_t ssh_binary_rx[SSH_PACKET_MAX];
static size_t ssh_binary_rx_used = 0;
static size_t ssh_remote_banner_len = 0;

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

  if (ssh_wrap_packet(out, cap, payload, off, &wrapped) != 0) {
    return 0;
  }
  return wrapped;
}

static size_t ssh_build_disconnect(uint8_t *out, size_t cap) {
  const char *message =
      "Orizon SSH KEXINIT parsed; secure KEX/auth/shell are next.";
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

static void ssh_reset_negotiation(void) {
  ssh_status.server_kexinit_sent = 0;
  ssh_status.client_kexinit_seen = 0;
  ssh_status.client_kex_packet_seen = 0;
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
  ssh_binary_rx_used = 0;
  ssh_remote_banner_len = 0;
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
  const char *start = server_list;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  while (start && *start) {
    const char *end = start;
    while (*end && *end != ',') {
      end++;
    }
    if (ssh_namelist_has(client_list, client_list_len, start,
                         (size_t)(end - start))) {
      ssh_copy_name((const uint8_t *)start, (size_t)(end - start), out,
                    out_size);
      return;
    }
    if (*end == '\0') {
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
  if (type == SSH_MSG_KEXDH_INIT) {
    ssh_status.client_kex_packet_seen = 1;
    ssh_status.kex_seen = 1;
    ssh_set_status("ssh: client ECDH init received");
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
    memmove(ssh_binary_rx, ssh_binary_rx + total_len,
            ssh_binary_rx_used - total_len);
    ssh_binary_rx_used -= total_len;
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
    ssh_capture_binary(data + i, len - i);
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
             (ssh_status.client_kex_packet_seen || !ssh_algorithm_ready()) &&
             !ssh_status.disconnect_sent) {
    tx_len = ssh_build_disconnect(txbuf, sizeof(txbuf));
    tx = tx_len ? txbuf : NULL;
    tx_kind = 3;
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
      ssh_set_status("ssh: server KEXINIT sent");
    } else if (tx_kind == 3) {
      ssh_status.disconnect_sent = 1;
      ssh_disconnect_close_polls = 8;
      ssh_set_status("ssh: staged disconnect sent");
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
           "sessions=%lu banner=%s skex=%s ckex=%s pkt=%u kex=%s rx=%lu "
           "spkts=%lu tx=%lu errors=%lu status=\"%s\"",
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
           "  next: implement host key + ECDH reply + NEWKEYS\n",
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
           ssh_status.compression_s2c[0] ? ssh_status.compression_s2c : "none");
}

void ssh_format_report(char *buf, size_t size) {
  char status[512];
  char algs[768];
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
