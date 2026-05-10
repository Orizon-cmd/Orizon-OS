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
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"

#define SSH_BANNER "SSH-2.0-OrizonSSH_0.1\r\n"
#define SSH_RX_BUF 768
#define SSH_DISCONNECT_BUF 192

static ssh_status_t ssh_status = {
    .enabled = 0,
    .configured = 0,
    .listening = 0,
    .connected = 0,
    .banner_sent = 0,
    .client_banner_seen = 0,
    .kex_seen = 0,
    .disconnect_sent = 0,
    .port = ORIZON_SSH_PORT,
    .remote_ip = 0,
    .remote_port = 0,
    .sessions = 0,
    .packets_rx = 0,
    .bytes_rx = 0,
    .bytes_tx = 0,
    .errors = 0,
    .remote_banner = {0},
    .status = "ssh: stopped",
};

static netstack_tcp_server_t ssh_server;
static uint32_t ssh_seen_connections = 0;
static int ssh_disconnect_close_polls = 0;

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
      "mode staged-ssh\n"
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

static size_t ssh_build_disconnect(uint8_t *out, size_t cap) {
  const char *message =
      "Orizon SSH listener is online; secure KEX/auth/shell are next.";
  size_t message_len = strlen(message);
  size_t payload_len = 1 + 4 + 4 + message_len + 4;
  size_t padding_len = 8 - ((payload_len + 5) % 8);
  size_t packet_len;
  size_t off = 0;

  if (padding_len < 4) {
    padding_len += 8;
  }
  packet_len = 1 + payload_len + padding_len;
  if (!out || cap < packet_len + 4) {
    return 0;
  }

  ssh_put_u32(out + off, (uint32_t)packet_len);
  off += 4;
  out[off++] = (uint8_t)padding_len;
  out[off++] = 1; /* SSH_MSG_DISCONNECT */
  ssh_put_u32(out + off, 3); /* SSH_DISCONNECT_KEY_EXCHANGE_FAILED */
  off += 4;
  ssh_put_u32(out + off, (uint32_t)message_len);
  off += 4;
  memcpy(out + off, message, message_len);
  off += message_len;
  ssh_put_u32(out + off, 0);
  off += 4;
  memset(out + off, 0, padding_len);
  off += padding_len;
  return off;
}

static void ssh_capture_client_data(const uint8_t *data, size_t len) {
  size_t i;
  size_t copy = 0;

  if (!data || len == 0) {
    return;
  }

  ssh_status.packets_rx++;
  ssh_status.bytes_rx += (uint32_t)len;

  if (!ssh_status.client_banner_seen && len >= 4 &&
      data[0] == 'S' && data[1] == 'S' && data[2] == 'H' && data[3] == '-') {
    for (i = 0; i < len && i < sizeof(ssh_status.remote_banner) - 1; i++) {
      if (data[i] == '\r' || data[i] == '\n') {
        break;
      }
      ssh_status.remote_banner[i] = (char)data[i];
      copy = i + 1;
    }
    ssh_status.remote_banner[copy] = '\0';
    ssh_status.client_banner_seen = 1;
    ssh_set_status("ssh: client banner received");
    while (i < len && (data[i] == '\r' || data[i] == '\n')) {
      i++;
    }
    if (i < len) {
      ssh_status.kex_seen = 1;
      ssh_set_status("ssh: client key exchange packet received");
    }
    return;
  }

  if (ssh_status.client_banner_seen) {
    ssh_status.kex_seen = 1;
    ssh_set_status("ssh: client key exchange packet received");
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
    ssh_status.kex_seen = 0;
    ssh_status.disconnect_sent = 0;
    ssh_disconnect_close_polls = 0;
    ssh_status.remote_banner[0] = '\0';
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
  ssh_status.kex_seen = 0;
  ssh_status.disconnect_sent = 0;
  ssh_status.remote_banner[0] = '\0';
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
  ssh_status.kex_seen = 0;
  ssh_status.disconnect_sent = 0;
  ssh_status.remote_ip = 0;
  ssh_status.remote_port = 0;
  ssh_disconnect_close_polls = 0;
  ssh_set_status("ssh: stopped");
  if (report && report_size > 0) {
    snprintf(report, report_size, "ssh: stopped\n");
  }
  return 0;
}

int ssh_poll(void) {
  uint8_t rx[SSH_RX_BUF];
  size_t rx_len = 0;
  uint8_t disconnect[SSH_DISCONNECT_BUF];
  const void *tx = NULL;
  size_t tx_len = 0;
  int rc;

  if (!ssh_status.enabled) {
    return 0;
  }

  ssh_refresh_state();
  if (ssh_status.connected && !ssh_status.banner_sent) {
    tx = SSH_BANNER;
    tx_len = strlen(SSH_BANNER);
  } else if (ssh_status.connected && ssh_status.kex_seen &&
             !ssh_status.disconnect_sent) {
    tx_len = ssh_build_disconnect(disconnect, sizeof(disconnect));
    tx = tx_len ? disconnect : NULL;
  } else if (ssh_status.connected && ssh_status.disconnect_sent &&
             ssh_disconnect_close_polls > 0) {
    ssh_disconnect_close_polls--;
    if (ssh_disconnect_close_polls == 0) {
      netstack_tcp_server_close(&ssh_server);
      netstack_tcp_server_init(&ssh_server, ORIZON_SSH_PORT);
      ssh_seen_connections = ssh_server.connections;
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
    if (!ssh_status.banner_sent) {
      ssh_status.banner_sent = 1;
      ssh_set_status("ssh: protocol banner sent");
    } else if (ssh_status.kex_seen && !ssh_status.disconnect_sent) {
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
           "sessions=%lu banner=%s client_banner=%s kex=%s rx=%lu tx=%lu "
           "errors=%lu status=\"%s\"",
           ssh_status.enabled ? "yes" : "no",
           netstack_tcp_server_state_name(&ssh_server),
           (unsigned)ssh_status.port,
           ssh_status.connected ? "yes" : "no", rip,
           (unsigned)ssh_status.remote_port,
           (unsigned long)ssh_status.sessions,
           ssh_status.banner_sent ? "sent" : "pending",
           ssh_status.client_banner_seen ? ssh_status.remote_banner : "none",
           ssh_status.kex_seen ? "seen" : "no",
           (unsigned long)ssh_status.bytes_rx,
           (unsigned long)ssh_status.bytes_tx,
           (unsigned long)ssh_status.errors, ssh_status.status);
}

void ssh_format_report(char *buf, size_t size) {
  char status[512];
  const netstack_status_t *net = netstack_get_status();
  char ip[24];

  if (!buf || size == 0) {
    return;
  }
  ssh_format_status(status, sizeof(status));
  netstack_format_ipv4(net->ip, ip, sizeof(ip));
  snprintf(buf, size,
           "%s\n"
           "config: %s\n"
           "log: %s\n"
           "local: %s:%u\n"
           "note: SSH remote shell is not enabled until KEX/auth/PTY are implemented.\n",
           status, ORIZON_SSH_CONFIG_PATH, ORIZON_SSH_LOG_PATH, ip,
           (unsigned)ORIZON_SSH_PORT);
}

const ssh_status_t *ssh_get_status(void) {
  return &ssh_status;
}
