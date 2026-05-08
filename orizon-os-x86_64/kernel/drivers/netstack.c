/*
 * Orizon OS x86_64 - Minimal IPv4/DHCP/DNS/TCP transport
 *
 * This stack is intentionally small and blocking. It exists so the in-kernel
 * updater can start using the network without depending on a userspace tool.
 */

#include "../include/netstack.h"
#include "../include/aes_gcm.h"
#include "../include/net.h"
#include "../include/sha256.h"
#include "../include/string.h"
#include "../include/timer.h"

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IPV4 0x0800

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DNS_PORT 53
#define HTTP_PORT 80
#define HTTPS_PORT 443

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define NETSTACK_MTU 1500
#define TCP_WINDOW 8192

static netstack_status_t stack_status = {
    .ipv4_ready = 0,
    .ip = 0,
    .subnet = 0,
    .gateway = 0,
    .dns = 0,
    .last_resolved_ip = 0,
    .last_host = {0},
    .status = "ipv4: not configured",
};

static uint16_t ip_ident = 1;
static uint16_t dns_ident = 0x4F5A;
static uint16_t tcp_next_port = 40000;
static uint32_t arp_cached_ip = 0;
static uint8_t arp_cached_mac[6] = {0};

static const uint8_t mac_broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static uint8_t frame_buf[NETSTACK_MTU];
static uint8_t packet_buf[NETSTACK_MTU];
static uint8_t transport_buf[NETSTACK_MTU];
static uint8_t checksum_buf[NETSTACK_MTU + 16];
static uint8_t tls_tx_buf[512];
static uint8_t tls_rx_buf[16384];
static uint8_t tls_secure_rx_buf[2048];
static uint8_t tls_server_plain_buf[2048];
static uint8_t tls_app_rx_buf[32768];
static uint8_t tls_app_plain_buf[32768];
static uint8_t tls_client_random[32];
static uint8_t tls_client_x25519_scalar[32];
static size_t tls_client_hello_handshake_len = 0;
static int tls_client_x25519_scalar_ready = 0;
static int tls_last_client_key_exchange_sent = 0;
static int tls_last_client_finished_sent = 0;
static size_t tls_last_secure_reply_len = 0;
static char tls_last_secure_reply_sha256[SHA256_HEX_SIZE];
static uint8_t tls_last_client_finished_plain[16];
static size_t tls_last_client_finished_plain_len = 0;
static int tls_last_http_get_sent = 0;
static int tls_last_http_decrypted = 0;
static size_t tls_last_http_reply_len = 0;
static size_t tls_last_http_plain_len = 0;
static char tls_last_http_reply_sha256[SHA256_HEX_SIZE];
static char tls_last_http_plain_sha256[SHA256_HEX_SIZE];
static char tls_last_http_status[80];
static uint8_t tls_last_http_first_record_type = 0;
static size_t tls_last_http_first_record_len = 0;
static size_t tls_last_http_decrypt_failures = 0;

static uint16_t get_be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t get_be24(const uint8_t *p) {
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static void put_be24(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)((v >> 16) & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
  p[2] = (uint8_t)(v & 0xff);
}

static void put_be16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xff);
}

static void put_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)((v >> 16) & 0xff);
  p[2] = (uint8_t)((v >> 8) & 0xff);
  p[3] = (uint8_t)(v & 0xff);
}

static void put_be64(uint8_t *p, uint64_t v) {
  for (int i = 7; i >= 0; i--) {
    p[7 - i] = (uint8_t)(v >> (i * 8));
  }
}

static uint64_t deadline_from_ms(uint64_t ms) {
  uint64_t ticks = (ms * TIMER_HZ + 999ULL) / 1000ULL;
  if (ticks == 0) {
    ticks = 1;
  }
  return timer_ticks() + ticks;
}

static int before_deadline(uint64_t deadline) {
  return timer_ticks() <= deadline;
}

static void short_wait(void) {
  __asm__ volatile("hlt");
}

static void set_status(const char *status) {
  strncpy(stack_status.status, status, sizeof(stack_status.status) - 1);
  stack_status.status[sizeof(stack_status.status) - 1] = '\0';
}

void netstack_format_ipv4(uint32_t ip, char *buf, size_t size) {
  snprintf(buf, size, "%lu.%lu.%lu.%lu", (unsigned long)((ip >> 24) & 0xff),
           (unsigned long)((ip >> 16) & 0xff),
           (unsigned long)((ip >> 8) & 0xff), (unsigned long)(ip & 0xff));
}

static uint16_t checksum16(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t sum = 0;

  while (len > 1) {
    sum += get_be16(p);
    p += 2;
    len -= 2;
  }
  if (len) {
    sum += (uint16_t)p[0] << 8;
  }
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }
  return (uint16_t)~sum;
}

static int same_subnet(uint32_t a, uint32_t b) {
  if (stack_status.subnet == 0) {
    return 0;
  }
  return (a & stack_status.subnet) == (b & stack_status.subnet);
}

static int recv_frame(uint8_t src_mac[6], uint16_t *eth_type, uint8_t *payload,
                      size_t cap) {
  int n = net_recv_ethernet(src_mac, eth_type, payload, cap);
  if (n <= 0) {
    return n;
  }
  return n;
}

static int send_ipv4_raw(const uint8_t dst_mac[6], uint32_t src_ip,
                         uint32_t dst_ip, uint8_t proto, const void *payload,
                         size_t payload_len) {
  if (payload_len + 20 > NETSTACK_MTU) {
    return -1;
  }

  memset(packet_buf, 0, sizeof(packet_buf));
  packet_buf[0] = 0x45;
  packet_buf[1] = 0;
  put_be16(packet_buf + 2, (uint16_t)(20 + payload_len));
  put_be16(packet_buf + 4, ip_ident++);
  put_be16(packet_buf + 6, 0x4000);
  packet_buf[8] = 64;
  packet_buf[9] = proto;
  put_be32(packet_buf + 12, src_ip);
  put_be32(packet_buf + 16, dst_ip);
  if (payload_len > 0) {
    memcpy(packet_buf + 20, payload, payload_len);
  }
  put_be16(packet_buf + 10, checksum16(packet_buf, 20));
  return net_send_ethernet(dst_mac, ETH_TYPE_IPV4, packet_buf, 20 + payload_len);
}

static int send_udp_raw(const uint8_t dst_mac[6], uint32_t src_ip,
                        uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                        const void *payload, size_t payload_len) {
  if (payload_len + 8 > NETSTACK_MTU - 20) {
    return -1;
  }

  memset(transport_buf, 0, sizeof(transport_buf));
  put_be16(transport_buf + 0, src_port);
  put_be16(transport_buf + 2, dst_port);
  put_be16(transport_buf + 4, (uint16_t)(payload_len + 8));
  put_be16(transport_buf + 6, 0); /* UDP checksum is optional for IPv4. */
  if (payload_len > 0) {
    memcpy(transport_buf + 8, payload, payload_len);
  }
  return send_ipv4_raw(dst_mac, src_ip, dst_ip, IP_PROTO_UDP, transport_buf,
                       payload_len + 8);
}

static void send_arp_reply(const uint8_t dst_mac[6], uint32_t dst_ip) {
  uint8_t arp[28];
  const net_device_status_t *dev = net_get_status();

  memset(arp, 0, sizeof(arp));
  put_be16(arp + 0, 1);
  put_be16(arp + 2, ETH_TYPE_IPV4);
  arp[4] = 6;
  arp[5] = 4;
  put_be16(arp + 6, 2);
  memcpy(arp + 8, dev->mac, 6);
  put_be32(arp + 14, stack_status.ip);
  memcpy(arp + 18, dst_mac, 6);
  put_be32(arp + 24, dst_ip);
  net_send_ethernet(dst_mac, ETH_TYPE_ARP, arp, sizeof(arp));
}

static void handle_arp(const uint8_t src_mac[6], const uint8_t *arp,
                       size_t len) {
  if (len < 28 || get_be16(arp + 0) != 1 || get_be16(arp + 2) != ETH_TYPE_IPV4 ||
      arp[4] != 6 || arp[5] != 4) {
    return;
  }

  uint16_t op = get_be16(arp + 6);
  uint32_t sender_ip = get_be32(arp + 14);
  uint32_t target_ip = get_be32(arp + 24);

  if (op == 2) {
    arp_cached_ip = sender_ip;
    memcpy(arp_cached_mac, arp + 8, 6);
  } else if (op == 1 && stack_status.ip != 0 && target_ip == stack_status.ip) {
    send_arp_reply(src_mac, sender_ip);
  }
}

static int send_arp_request(uint32_t target_ip) {
  uint8_t arp[28];
  const net_device_status_t *dev = net_get_status();

  memset(arp, 0, sizeof(arp));
  put_be16(arp + 0, 1);
  put_be16(arp + 2, ETH_TYPE_IPV4);
  arp[4] = 6;
  arp[5] = 4;
  put_be16(arp + 6, 1);
  memcpy(arp + 8, dev->mac, 6);
  put_be32(arp + 14, stack_status.ip);
  memset(arp + 18, 0, 6);
  put_be32(arp + 24, target_ip);
  return net_send_ethernet(mac_broadcast, ETH_TYPE_ARP, arp, sizeof(arp));
}

static int arp_resolve(uint32_t ip, uint8_t out_mac[6]) {
  uint64_t deadline;
  uint64_t resend_at = 0;

  if (arp_cached_ip == ip) {
    memcpy(out_mac, arp_cached_mac, 6);
    return 0;
  }

  deadline = deadline_from_ms(2500);
  while (before_deadline(deadline)) {
    if (timer_ticks() >= resend_at) {
      send_arp_request(ip);
      resend_at = timer_ticks() + (TIMER_HZ / 4);
    }

    uint8_t src_mac[6];
    uint16_t eth_type = 0;
    int n = recv_frame(src_mac, &eth_type, frame_buf, sizeof(frame_buf));
    if (n > 0 && eth_type == ETH_TYPE_ARP) {
      handle_arp(src_mac, frame_buf, (size_t)n);
      if (arp_cached_ip == ip) {
        memcpy(out_mac, arp_cached_mac, 6);
        return 0;
      }
    } else {
      short_wait();
    }
  }
  return -1;
}

static int parse_ipv4_udp(const uint8_t *packet, size_t len, uint32_t *src_ip,
                          uint32_t *dst_ip, uint16_t *src_port,
                          uint16_t *dst_port, const uint8_t **payload,
                          size_t *payload_len) {
  if (len < 28 || (packet[0] >> 4) != 4) {
    return -1;
  }
  size_t ihl = (size_t)(packet[0] & 0x0f) * 4;
  if (ihl < 20 || len < ihl + 8 || packet[9] != IP_PROTO_UDP) {
    return -1;
  }
  size_t total_len = get_be16(packet + 2);
  if (total_len > len || total_len < ihl + 8) {
    return -1;
  }
  const uint8_t *udp = packet + ihl;
  size_t udp_len = get_be16(udp + 4);
  if (udp_len < 8 || ihl + udp_len > total_len) {
    return -1;
  }
  *src_ip = get_be32(packet + 12);
  *dst_ip = get_be32(packet + 16);
  *src_port = get_be16(udp + 0);
  *dst_port = get_be16(udp + 2);
  *payload = udp + 8;
  *payload_len = udp_len - 8;
  return 0;
}

static int send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                    const void *payload, size_t payload_len) {
  uint32_t next_hop = same_subnet(stack_status.ip, dst_ip) ? dst_ip : stack_status.gateway;
  uint8_t dst_mac[6];

  if (!stack_status.ipv4_ready || next_hop == 0) {
    return -1;
  }
  if (arp_resolve(next_hop, dst_mac) != 0) {
    return -1;
  }
  return send_udp_raw(dst_mac, stack_status.ip, dst_ip, src_port, dst_port,
                      payload, payload_len);
}

static int dhcp_add_option(uint8_t *opts, size_t *off, uint8_t code,
                           const void *data, uint8_t len) {
  if (*off + 2 + len >= 312) {
    return -1;
  }
  opts[(*off)++] = code;
  opts[(*off)++] = len;
  if (len > 0) {
    memcpy(opts + *off, data, len);
    *off += len;
  }
  return 0;
}

static size_t build_dhcp_packet(uint8_t *out, uint8_t msg_type, uint32_t xid,
                                uint32_t requested_ip, uint32_t server_id) {
  const net_device_status_t *dev = net_get_status();
  uint8_t param_req[] = {1, 3, 6, 15, 51, 54};
  size_t off = 240;

  memset(out, 0, 548);
  out[0] = 1;
  out[1] = 1;
  out[2] = 6;
  put_be32(out + 4, xid);
  put_be16(out + 10, 0x8000);
  memcpy(out + 28, dev->mac, 6);
  out[236] = 99;
  out[237] = 130;
  out[238] = 83;
  out[239] = 99;

  dhcp_add_option(out, &off, 53, &msg_type, 1);
  uint8_t client_id[7];
  client_id[0] = 1;
  memcpy(client_id + 1, dev->mac, 6);
  dhcp_add_option(out, &off, 61, client_id, sizeof(client_id));
  dhcp_add_option(out, &off, 55, param_req, sizeof(param_req));
  if (requested_ip != 0) {
    uint8_t ip_bytes[4];
    put_be32(ip_bytes, requested_ip);
    dhcp_add_option(out, &off, 50, ip_bytes, 4);
  }
  if (server_id != 0) {
    uint8_t server_bytes[4];
    put_be32(server_bytes, server_id);
    dhcp_add_option(out, &off, 54, server_bytes, 4);
  }
  out[off++] = 255;
  if (off < 300) {
    off = 300;
  }
  return off;
}

static void parse_dhcp_options(const uint8_t *opts, size_t len, uint8_t *msg_type,
                               uint32_t *server_id, uint32_t *subnet,
                               uint32_t *router, uint32_t *dns) {
  size_t off = 0;
  while (off < len) {
    uint8_t code = opts[off++];
    if (code == 255) {
      break;
    }
    if (code == 0) {
      continue;
    }
    if (off >= len) {
      break;
    }
    uint8_t opt_len = opts[off++];
    if (off + opt_len > len) {
      break;
    }

    if (code == 53 && opt_len >= 1) {
      *msg_type = opts[off];
    } else if (code == 54 && opt_len >= 4) {
      *server_id = get_be32(opts + off);
    } else if (code == 1 && opt_len >= 4) {
      *subnet = get_be32(opts + off);
    } else if (code == 3 && opt_len >= 4) {
      *router = get_be32(opts + off);
    } else if (code == 6 && opt_len >= 4) {
      *dns = get_be32(opts + off);
    }
    off += opt_len;
  }
}

static int wait_dhcp(uint32_t xid, uint8_t wanted_type, uint32_t *yiaddr,
                     uint32_t *server_id, uint32_t *subnet, uint32_t *router,
                     uint32_t *dns) {
  uint64_t deadline = deadline_from_ms(4000);
  while (before_deadline(deadline)) {
    uint8_t src_mac[6];
    uint16_t eth_type = 0;
    int n = recv_frame(src_mac, &eth_type, frame_buf, sizeof(frame_buf));
    if (n <= 0) {
      short_wait();
      continue;
    }
    if (eth_type == ETH_TYPE_ARP) {
      handle_arp(src_mac, frame_buf, (size_t)n);
      continue;
    }
    if (eth_type != ETH_TYPE_IPV4) {
      continue;
    }

    uint32_t src_ip = 0;
    uint32_t dst_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    const uint8_t *udp_payload = NULL;
    size_t udp_len = 0;
    if (parse_ipv4_udp(frame_buf, (size_t)n, &src_ip, &dst_ip, &src_port,
                       &dst_port, &udp_payload, &udp_len) != 0) {
      continue;
    }
    UNUSED(src_ip);
    UNUSED(dst_ip);
    if (src_port != DHCP_SERVER_PORT || dst_port != DHCP_CLIENT_PORT ||
        udp_len < 240 || udp_payload[0] != 2 || get_be32(udp_payload + 4) != xid) {
      continue;
    }
    if (udp_payload[236] != 99 || udp_payload[237] != 130 ||
        udp_payload[238] != 83 || udp_payload[239] != 99) {
      continue;
    }

    uint8_t msg_type = 0;
    parse_dhcp_options(udp_payload + 240, udp_len - 240, &msg_type, server_id,
                       subnet, router, dns);
    if (msg_type == wanted_type) {
      *yiaddr = get_be32(udp_payload + 16);
      return 0;
    }
  }
  return -1;
}

int netstack_configure_ipv4(void) {
  uint8_t dhcp[548];
  uint32_t xid = 0x4f5a0000U ^ (uint32_t)timer_ticks();
  uint32_t offered_ip = 0;
  uint32_t server_id = 0;
  uint32_t subnet = 0;
  uint32_t router = 0;
  uint32_t dns = 0;
  size_t len;

  if (stack_status.ipv4_ready) {
    return 0;
  }
  if (net_init() != 0 || !net_link_up()) {
    set_status("ipv4: ethernet link unavailable");
    return -1;
  }

  set_status("ipv4: dhcp discover");
  len = build_dhcp_packet(dhcp, 1, xid, 0, 0);
  send_udp_raw(mac_broadcast, 0, 0xffffffffU, DHCP_CLIENT_PORT,
               DHCP_SERVER_PORT, dhcp, len);
  if (wait_dhcp(xid, 2, &offered_ip, &server_id, &subnet, &router, &dns) != 0 ||
      offered_ip == 0) {
    set_status("ipv4: dhcp offer timeout");
    return -1;
  }

  set_status("ipv4: dhcp request");
  len = build_dhcp_packet(dhcp, 3, xid, offered_ip, server_id);
  send_udp_raw(mac_broadcast, 0, 0xffffffffU, DHCP_CLIENT_PORT,
               DHCP_SERVER_PORT, dhcp, len);
  if (wait_dhcp(xid, 5, &offered_ip, &server_id, &subnet, &router, &dns) != 0) {
    set_status("ipv4: dhcp ack timeout");
    return -1;
  }

  stack_status.ip = offered_ip;
  stack_status.subnet = subnet ? subnet : 0xffffff00U;
  stack_status.gateway = router;
  stack_status.dns = dns ? dns : router;
  stack_status.ipv4_ready = 1;
  arp_cached_ip = 0;
  set_status("ipv4: configured by dhcp");
  return 0;
}

static int dns_write_name(uint8_t *buf, size_t cap, size_t *off,
                          const char *host) {
  const char *label = host;
  const char *p = host;
  while (1) {
    if (*p == '.' || *p == '\0') {
      size_t len = (size_t)(p - label);
      if (len == 0 || len > 63 || *off + len + 1 >= cap) {
        return -1;
      }
      buf[(*off)++] = (uint8_t)len;
      memcpy(buf + *off, label, len);
      *off += len;
      if (*p == '\0') {
        break;
      }
      label = p + 1;
    }
    p++;
  }
  if (*off + 1 >= cap) {
    return -1;
  }
  buf[(*off)++] = 0;
  return 0;
}

static int dns_skip_name(const uint8_t *buf, size_t len, size_t *off) {
  size_t p = *off;
  while (p < len) {
    uint8_t c = buf[p++];
    if (c == 0) {
      *off = p;
      return 0;
    }
    if ((c & 0xc0) == 0xc0) {
      if (p >= len) {
        return -1;
      }
      *off = p + 1;
      return 0;
    }
    if ((c & 0xc0) != 0 || p + c > len) {
      return -1;
    }
    p += c;
  }
  return -1;
}

int netstack_resolve_a(const char *host, uint32_t *out_ip) {
  uint8_t query[512];
  size_t off = 12;
  uint16_t id = ++dns_ident;
  uint16_t local_port = (uint16_t)(41000 + (id & 0x0fff));
  uint64_t deadline;

  if (!host || !out_ip) {
    return -1;
  }
  if (netstack_configure_ipv4() != 0) {
    return -1;
  }

  memset(query, 0, sizeof(query));
  put_be16(query + 0, id);
  put_be16(query + 2, 0x0100);
  put_be16(query + 4, 1);
  if (dns_write_name(query, sizeof(query), &off, host) != 0) {
    return -1;
  }
  put_be16(query + off, 1);
  off += 2;
  put_be16(query + off, 1);
  off += 2;

  set_status("dns: resolving host");
  if (send_udp(stack_status.dns, local_port, DNS_PORT, query, off) < 0) {
    set_status("dns: send failed");
    return -1;
  }

  deadline = deadline_from_ms(5000);
  while (before_deadline(deadline)) {
    uint8_t src_mac[6];
    uint16_t eth_type = 0;
    int n = recv_frame(src_mac, &eth_type, frame_buf, sizeof(frame_buf));
    if (n <= 0) {
      short_wait();
      continue;
    }
    if (eth_type == ETH_TYPE_ARP) {
      handle_arp(src_mac, frame_buf, (size_t)n);
      continue;
    }
    if (eth_type != ETH_TYPE_IPV4) {
      continue;
    }

    uint32_t src_ip = 0;
    uint32_t dst_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    const uint8_t *dns_payload = NULL;
    size_t dns_len = 0;
    if (parse_ipv4_udp(frame_buf, (size_t)n, &src_ip, &dst_ip, &src_port,
                       &dst_port, &dns_payload, &dns_len) != 0) {
      continue;
    }
    UNUSED(dst_ip);
    if (src_ip != stack_status.dns || src_port != DNS_PORT ||
        dst_port != local_port || dns_len < 12 || get_be16(dns_payload) != id) {
      continue;
    }

    uint16_t qd = get_be16(dns_payload + 4);
    uint16_t an = get_be16(dns_payload + 6);
    size_t roff = 12;
    for (uint16_t i = 0; i < qd; i++) {
      if (dns_skip_name(dns_payload, dns_len, &roff) != 0 || roff + 4 > dns_len) {
        return -1;
      }
      roff += 4;
    }
    for (uint16_t i = 0; i < an; i++) {
      if (dns_skip_name(dns_payload, dns_len, &roff) != 0 || roff + 10 > dns_len) {
        return -1;
      }
      uint16_t type = get_be16(dns_payload + roff);
      uint16_t klass = get_be16(dns_payload + roff + 2);
      uint16_t rdlen = get_be16(dns_payload + roff + 8);
      roff += 10;
      if (roff + rdlen > dns_len) {
        return -1;
      }
      if (type == 1 && klass == 1 && rdlen == 4) {
        *out_ip = get_be32(dns_payload + roff);
        stack_status.last_resolved_ip = *out_ip;
        strncpy(stack_status.last_host, host, sizeof(stack_status.last_host) - 1);
        stack_status.last_host[sizeof(stack_status.last_host) - 1] = '\0';
        set_status("dns: host resolved");
        return 0;
      }
      roff += rdlen;
    }
  }

  set_status("dns: timeout");
  return -1;
}

typedef struct {
  uint32_t remote_ip;
  uint16_t remote_port;
  uint16_t local_port;
  uint32_t seq;
  uint32_t ack;
  uint8_t remote_mac[6];
} tcp_conn_t;

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             const uint8_t *tcp, size_t tcp_len) {
  memset(checksum_buf, 0, sizeof(checksum_buf));
  put_be32(checksum_buf + 0, src_ip);
  put_be32(checksum_buf + 4, dst_ip);
  checksum_buf[8] = 0;
  checksum_buf[9] = IP_PROTO_TCP;
  put_be16(checksum_buf + 10, (uint16_t)tcp_len);
  memcpy(checksum_buf + 12, tcp, tcp_len);
  return checksum16(checksum_buf, tcp_len + 12);
}

static int send_tcp(tcp_conn_t *conn, uint8_t flags, const void *data,
                    size_t data_len) {
  size_t header_len = (flags & TCP_SYN) ? 24 : 20;
  size_t tcp_len = header_len + data_len;

  if (tcp_len > NETSTACK_MTU - 20) {
    return -1;
  }

  memset(transport_buf, 0, sizeof(transport_buf));
  put_be16(transport_buf + 0, conn->local_port);
  put_be16(transport_buf + 2, conn->remote_port);
  put_be32(transport_buf + 4, conn->seq);
  put_be32(transport_buf + 8, conn->ack);
  transport_buf[12] = (uint8_t)((header_len / 4) << 4);
  transport_buf[13] = flags;
  put_be16(transport_buf + 14, TCP_WINDOW);
  if (flags & TCP_SYN) {
    transport_buf[20] = 2;
    transport_buf[21] = 4;
    put_be16(transport_buf + 22, 1460);
  }
  if (data_len > 0) {
    memcpy(transport_buf + header_len, data, data_len);
  }
  put_be16(transport_buf + 16, tcp_checksum(stack_status.ip, conn->remote_ip,
                                           transport_buf, tcp_len));
  return send_ipv4_raw(conn->remote_mac, stack_status.ip, conn->remote_ip,
                       IP_PROTO_TCP, transport_buf, tcp_len);
}

static int parse_ipv4_tcp(const uint8_t *packet, size_t len, uint32_t *src_ip,
                          uint32_t *dst_ip, uint16_t *src_port,
                          uint16_t *dst_port, uint32_t *seq, uint32_t *ack,
                          uint8_t *flags, const uint8_t **payload,
                          size_t *payload_len) {
  if (len < 40 || (packet[0] >> 4) != 4) {
    return -1;
  }
  size_t ihl = (size_t)(packet[0] & 0x0f) * 4;
  size_t total_len = get_be16(packet + 2);
  if (ihl < 20 || total_len > len || total_len < ihl + 20 ||
      packet[9] != IP_PROTO_TCP) {
    return -1;
  }
  const uint8_t *tcp = packet + ihl;
  size_t tcp_header_len = (size_t)(tcp[12] >> 4) * 4;
  if (tcp_header_len < 20 || ihl + tcp_header_len > total_len) {
    return -1;
  }

  *src_ip = get_be32(packet + 12);
  *dst_ip = get_be32(packet + 16);
  *src_port = get_be16(tcp + 0);
  *dst_port = get_be16(tcp + 2);
  *seq = get_be32(tcp + 4);
  *ack = get_be32(tcp + 8);
  *flags = tcp[13];
  *payload = tcp + tcp_header_len;
  *payload_len = total_len - ihl - tcp_header_len;
  return 0;
}

static int tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t port) {
  uint32_t next_hop = same_subnet(stack_status.ip, remote_ip) ? remote_ip : stack_status.gateway;
  uint64_t deadline;

  memset(conn, 0, sizeof(*conn));
  conn->remote_ip = remote_ip;
  conn->remote_port = port;
  conn->local_port = tcp_next_port++;
  if (tcp_next_port < 40000 || tcp_next_port > 60000) {
    tcp_next_port = 40000;
  }
  conn->seq = 0x4f5a1000U ^ (uint32_t)timer_ticks();

  if (arp_resolve(next_hop, conn->remote_mac) != 0) {
    set_status("tcp: gateway arp failed");
    return -1;
  }

  set_status("tcp: syn");
  send_tcp(conn, TCP_SYN, NULL, 0);
  deadline = deadline_from_ms(5000);
  while (before_deadline(deadline)) {
    uint8_t src_mac[6];
    uint16_t eth_type = 0;
    int n = recv_frame(src_mac, &eth_type, frame_buf, sizeof(frame_buf));
    if (n <= 0) {
      short_wait();
      continue;
    }
    if (eth_type == ETH_TYPE_ARP) {
      handle_arp(src_mac, frame_buf, (size_t)n);
      continue;
    }
    if (eth_type != ETH_TYPE_IPV4) {
      continue;
    }

    uint32_t src_ip = 0;
    uint32_t dst_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    uint32_t rseq = 0;
    uint32_t rack = 0;
    uint8_t flags = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (parse_ipv4_tcp(frame_buf, (size_t)n, &src_ip, &dst_ip, &src_port,
                       &dst_port, &rseq, &rack, &flags, &payload,
                       &payload_len) != 0) {
      continue;
    }
    UNUSED(dst_ip);
    UNUSED(payload);
    UNUSED(payload_len);
    if (src_ip != remote_ip || src_port != port || dst_port != conn->local_port) {
      continue;
    }
    if (flags & TCP_RST) {
      set_status("tcp: reset");
      return -1;
    }
    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
        rack == conn->seq + 1) {
      conn->seq++;
      conn->ack = rseq + 1;
      send_tcp(conn, TCP_ACK, NULL, 0);
      set_status("tcp: connected");
      return 0;
    }
  }

  set_status("tcp: connect timeout");
  return -1;
}

static int tcp_send_data(tcp_conn_t *conn, const void *data, size_t len) {
  int rc = send_tcp(conn, TCP_PSH | TCP_ACK, data, len);
  if (rc >= 0) {
    conn->seq += (uint32_t)len;
  }
  return rc;
}

static int tcp_recv_all(tcp_conn_t *conn, char *out, size_t out_cap,
                        size_t *out_len) {
  size_t used = 0;
  uint64_t deadline = deadline_from_ms(9000);

  if (out_cap > 0) {
    out[0] = '\0';
  }

  while (before_deadline(deadline)) {
    uint8_t src_mac[6];
    uint16_t eth_type = 0;
    int n = recv_frame(src_mac, &eth_type, frame_buf, sizeof(frame_buf));
    if (n <= 0) {
      short_wait();
      continue;
    }
    if (eth_type == ETH_TYPE_ARP) {
      handle_arp(src_mac, frame_buf, (size_t)n);
      continue;
    }
    if (eth_type != ETH_TYPE_IPV4) {
      continue;
    }

    uint32_t src_ip = 0;
    uint32_t dst_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    uint32_t rseq = 0;
    uint32_t rack = 0;
    uint8_t flags = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (parse_ipv4_tcp(frame_buf, (size_t)n, &src_ip, &dst_ip, &src_port,
                       &dst_port, &rseq, &rack, &flags, &payload,
                       &payload_len) != 0) {
      continue;
    }
    UNUSED(dst_ip);
    UNUSED(rack);
    if (src_ip != conn->remote_ip || src_port != conn->remote_port ||
        dst_port != conn->local_port) {
      continue;
    }
    if (flags & TCP_RST) {
      set_status("tcp: reset while receiving");
      return -1;
    }
    if (payload_len > 0) {
      if (rseq == conn->ack) {
        size_t copy = payload_len;
        if (out_cap > 0 && used + copy >= out_cap) {
          copy = out_cap - used - 1;
        }
        if (copy > 0) {
          memcpy(out + used, payload, copy);
          used += copy;
          out[used] = '\0';
        }
        conn->ack += (uint32_t)payload_len;
        send_tcp(conn, TCP_ACK, NULL, 0);
        deadline = deadline_from_ms(1500);
      } else {
        send_tcp(conn, TCP_ACK, NULL, 0);
      }
    }
    if (flags & TCP_FIN) {
      conn->ack++;
      send_tcp(conn, TCP_ACK, NULL, 0);
      if (out_len) {
        *out_len = used;
      }
      set_status("tcp: response received");
      return 0;
    }
  }

  if (out_len) {
    *out_len = used;
  }
  return used > 0 ? 0 : -1;
}

static int tls_records_have_message(const uint8_t *rx, size_t rx_len,
                                    uint8_t wanted_type) {
  size_t off = 0;

  while (off + 5 <= rx_len) {
    uint8_t record_type = rx[off];
    uint16_t record_len = get_be16(rx + off + 3);
    size_t record_end = off + 5 + record_len;

    if (record_end > rx_len) {
      return 0;
    }
    if (record_type == 21) {
      return 1;
    }
    if (record_type == 22) {
      size_t p = off + 5;
      while (p + 4 <= record_end) {
        uint8_t hs_type = rx[p];
        uint32_t hs_len = ((uint32_t)rx[p + 1] << 16) |
                          ((uint32_t)rx[p + 2] << 8) | rx[p + 3];
        if (p + 4 + hs_len > record_end) {
          break;
        }
        if (hs_type == wanted_type) {
          return 1;
        }
        p += 4 + hs_len;
      }
    }
    off = record_end;
  }

  return 0;
}

static int tcp_recv_bytes(tcp_conn_t *conn, uint8_t *out, size_t out_cap,
                          size_t *out_len, uint64_t wait_ms) {
  size_t used = 0;
  uint64_t deadline = deadline_from_ms(wait_ms);

  while (before_deadline(deadline)) {
    uint8_t src_mac[6];
    uint16_t eth_type = 0;
    int n = recv_frame(src_mac, &eth_type, frame_buf, sizeof(frame_buf));
    if (n <= 0) {
      short_wait();
      continue;
    }
    if (eth_type == ETH_TYPE_ARP) {
      handle_arp(src_mac, frame_buf, (size_t)n);
      continue;
    }
    if (eth_type != ETH_TYPE_IPV4) {
      continue;
    }

    uint32_t src_ip = 0;
    uint32_t dst_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    uint32_t rseq = 0;
    uint32_t rack = 0;
    uint8_t flags = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (parse_ipv4_tcp(frame_buf, (size_t)n, &src_ip, &dst_ip, &src_port,
                       &dst_port, &rseq, &rack, &flags, &payload,
                       &payload_len) != 0) {
      continue;
    }
    UNUSED(dst_ip);
    UNUSED(rack);
    if (src_ip != conn->remote_ip || src_port != conn->remote_port ||
        dst_port != conn->local_port) {
      continue;
    }
    if (flags & TCP_RST) {
      set_status("tcp: reset while receiving bytes");
      return -1;
    }
    if (payload_len > 0) {
      if (rseq == conn->ack) {
        size_t copy = payload_len;
        if (copy > out_cap - used) {
          copy = out_cap - used;
        }
        if (copy > 0) {
          memcpy(out + used, payload, copy);
          used += copy;
        }
        conn->ack += (uint32_t)payload_len;
        send_tcp(conn, TCP_ACK, NULL, 0);
        if (used == out_cap || tls_records_have_message(out, used, 14)) {
          break;
        }
        deadline = deadline_from_ms(1000);
      } else {
        send_tcp(conn, TCP_ACK, NULL, 0);
      }
    }
    if (flags & TCP_FIN) {
      conn->ack++;
      send_tcp(conn, TCP_ACK, NULL, 0);
      break;
    }
  }

  if (out_len) {
    *out_len = used;
  }
  return used > 0 ? 0 : -1;
}

static size_t append_text(char *out, size_t cap, const char *text) {
  size_t used = strlen(out);
  if (used + 1 >= cap) {
    return used;
  }
  snprintf(out + used, cap - used, "%s", text);
  return strlen(out);
}

static size_t append_hex_bytes(char *out, size_t cap, const uint8_t *data,
                               size_t len) {
  static const char hex[] = "0123456789abcdef";
  size_t used = strlen(out);
  for (size_t i = 0; i < len && used + 3 < cap; i++) {
    out[used++] = hex[data[i] >> 4];
    out[used++] = hex[data[i] & 0x0f];
  }
  out[used] = '\0';
  return used;
}

static int tls_put(uint8_t *buf, size_t cap, size_t *off, uint8_t v) {
  if (*off + 1 > cap) {
    return -1;
  }
  buf[(*off)++] = v;
  return 0;
}

static int tls_put16(uint8_t *buf, size_t cap, size_t *off, uint16_t v) {
  if (*off + 2 > cap) {
    return -1;
  }
  put_be16(buf + *off, v);
  *off += 2;
  return 0;
}

static int tls_put_bytes(uint8_t *buf, size_t cap, size_t *off,
                         const void *data, size_t len) {
  if (*off + len > cap) {
    return -1;
  }
  memcpy(buf + *off, data, len);
  *off += len;
  return 0;
}

static int tls_put_extension_header(uint8_t *buf, size_t cap, size_t *off,
                                    uint16_t type, uint16_t len) {
  return tls_put16(buf, cap, off, type) == 0 &&
                 tls_put16(buf, cap, off, len) == 0
             ? 0
             : -1;
}

static int build_tls_client_hello(const char *host, uint8_t *out,
                                  size_t out_cap, size_t *out_len) {
  static const uint16_t suites[] = {
      0xc02f, /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */
      0xc02b, /* TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 */
      0xc030, /* TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 */
      0xc02c, /* TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 */
      0xcca8, /* TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 */
      0xcca9, /* TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 */
      0x009e, /* TLS_DHE_RSA_WITH_AES_128_GCM_SHA256 */
      0x009c, /* TLS_RSA_WITH_AES_128_GCM_SHA256 */
  };
  static const uint16_t groups[] = {0x001d, 0x0017, 0x0018};
  static const uint16_t sigs[] = {0x0403, 0x0804, 0x0401,
                                  0x0503, 0x0805, 0x0501};
  const char alpn[] = "http/1.1";
  size_t host_len = strlen(host);
  size_t off = 0;

  if (!host || host_len == 0 || host_len > 253 || out_cap < 128) {
    return -1;
  }

  memset(out, 0, out_cap);
  memset(tls_client_x25519_scalar, 0, sizeof(tls_client_x25519_scalar));
  tls_client_x25519_scalar_ready = 0;
  tls_last_client_key_exchange_sent = 0;
  tls_last_client_finished_sent = 0;
  tls_last_secure_reply_len = 0;
  tls_last_secure_reply_sha256[0] = '\0';
  tls_last_client_finished_plain_len = 0;
  tls_last_http_get_sent = 0;
  tls_last_http_decrypted = 0;
  tls_last_http_reply_len = 0;
  tls_last_http_plain_len = 0;
  tls_last_http_reply_sha256[0] = '\0';
  tls_last_http_plain_sha256[0] = '\0';
  tls_last_http_status[0] = '\0';
  tls_last_http_first_record_type = 0;
  tls_last_http_first_record_len = 0;
  tls_last_http_decrypt_failures = 0;
  tls_client_hello_handshake_len = 0;
  tls_put(out, out_cap, &off, 22);      /* handshake record */
  tls_put16(out, out_cap, &off, 0x0301); /* record legacy version */
  size_t record_len_pos = off;
  tls_put16(out, out_cap, &off, 0);

  tls_put(out, out_cap, &off, 1); /* ClientHello */
  size_t hs_len_pos = off;
  tls_put(out, out_cap, &off, 0);
  tls_put(out, out_cap, &off, 0);
  tls_put(out, out_cap, &off, 0);
  size_t body_start = off;

  tls_put16(out, out_cap, &off, 0x0303); /* TLS 1.2 legacy_version */
  uint32_t seed = 0x4f5a544cU ^ (uint32_t)timer_ticks();
  for (int i = 0; i < 32; i++) {
    seed = seed * 1664525U + 1013904223U;
    tls_client_random[i] = (uint8_t)(seed >> 24);
    tls_put(out, out_cap, &off, tls_client_random[i]);
  }
  tls_put(out, out_cap, &off, 0); /* no session id */

  tls_put16(out, out_cap, &off, (uint16_t)(sizeof(suites)));
  for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
    tls_put16(out, out_cap, &off, suites[i]);
  }
  tls_put(out, out_cap, &off, 1);
  tls_put(out, out_cap, &off, 0);

  size_t ext_len_pos = off;
  tls_put16(out, out_cap, &off, 0);
  size_t ext_start = off;

  tls_put_extension_header(out, out_cap, &off, 0x0000,
                           (uint16_t)(5 + host_len));
  tls_put16(out, out_cap, &off, (uint16_t)(3 + host_len));
  tls_put(out, out_cap, &off, 0);
  tls_put16(out, out_cap, &off, (uint16_t)host_len);
  tls_put_bytes(out, out_cap, &off, host, host_len);

  tls_put_extension_header(out, out_cap, &off, 0x000a,
                           (uint16_t)(2 + sizeof(groups)));
  tls_put16(out, out_cap, &off, sizeof(groups));
  for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
    tls_put16(out, out_cap, &off, groups[i]);
  }

  tls_put_extension_header(out, out_cap, &off, 0x000b, 2);
  tls_put(out, out_cap, &off, 1);
  tls_put(out, out_cap, &off, 0);

  tls_put_extension_header(out, out_cap, &off, 0x000d,
                           (uint16_t)(2 + sizeof(sigs)));
  tls_put16(out, out_cap, &off, sizeof(sigs));
  for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
    tls_put16(out, out_cap, &off, sigs[i]);
  }

  tls_put_extension_header(out, out_cap, &off, 0x0017, 0); /* EMS */
  tls_put_extension_header(out, out_cap, &off, 0x0023, 0); /* session ticket */
  tls_put_extension_header(out, out_cap, &off, 0xff01, 1);
  tls_put(out, out_cap, &off, 0);

  tls_put_extension_header(out, out_cap, &off, 0x0010,
                           (uint16_t)(2 + 1 + strlen(alpn)));
  tls_put16(out, out_cap, &off, (uint16_t)(1 + strlen(alpn)));
  tls_put(out, out_cap, &off, (uint8_t)strlen(alpn));
  tls_put_bytes(out, out_cap, &off, alpn, strlen(alpn));

  size_t ext_len = off - ext_start;
  size_t body_len = off - body_start;
  out[ext_len_pos] = (uint8_t)(ext_len >> 8);
  out[ext_len_pos + 1] = (uint8_t)ext_len;
  out[hs_len_pos] = (uint8_t)(body_len >> 16);
  out[hs_len_pos + 1] = (uint8_t)(body_len >> 8);
  out[hs_len_pos + 2] = (uint8_t)body_len;
  put_be16(out + record_len_pos, (uint16_t)(body_len + 4));
  tls_client_hello_handshake_len = body_len + 4;
  *out_len = off;
  return 0;
}

#define TLS_MAX_CERT_SUMMARIES 4
#define TLS_RSA_MAX_BYTES 256

typedef struct {
  int parsed;
  size_t cert_len;
  char subject_sha256[SHA256_HEX_SIZE];
  char issuer_sha256[SHA256_HEX_SIZE];
  char tbs_sha256[SHA256_HEX_SIZE];
  char signature_sha256[SHA256_HEX_SIZE];
  const char *signature_alg;
  const char *tbs_signature_alg;
  const char *public_key_alg;
  const char *public_key_curve;
  int signature_alg_consistent;
  int signature_sha256_rsa;
  int public_key_rsa;
  int rsa_modulus_ready;
  int signature_ready;
  size_t signature_len;
  size_t public_key_bits;
  size_t rsa_modulus_len;
  uint32_t rsa_exponent;
  uint8_t tbs_digest[SHA256_DIGEST_SIZE];
  uint8_t signature_bytes[TLS_RSA_MAX_BYTES];
  uint8_t rsa_modulus[TLS_RSA_MAX_BYTES];
} tls_cert_summary_t;

typedef struct {
  size_t records;
  size_t handshake_records;
  size_t alerts;
  size_t handshake_messages;
  int partial_record;
  int partial_handshake;
  int server_hello_seen;
  int certificate_seen;
  int server_key_exchange_seen;
  int server_hello_done_seen;
  int extended_master_secret;
  int key_agreement_ready;
  int key_agreement_x25519;
  int client_key_bootstrap;
  int client_key_exchange_ready;
  int master_secret_ready;
  int traffic_keys_ready;
  int key_schedule_supported;
  int handshake_ctx_ready;
  int client_finished_ready;
  int server_secure_decrypted;
  int server_change_cipher_spec_seen;
  int server_ticket_seen;
  int server_finished_seen;
  int server_finished_verified;
  int server_finished_verified_without_client_finished;
  int server_alert_seen;
  uint64_t server_read_seq_next;
  uint16_t server_version;
  uint16_t cipher;
  uint16_t key_exchange_group;
  uint16_t key_signature_alg;
  size_t key_public_len;
  size_t client_key_exchange_len;
  size_t client_key_exchange_record_len;
  size_t client_finished_record_len;
  size_t client_secure_flight_len;
  size_t server_secure_plain_len;
  uint8_t server_alert_level;
  uint8_t server_alert_description;
  char client_public_sha256[SHA256_HEX_SIZE];
  char shared_secret_sha256[SHA256_HEX_SIZE];
  char client_key_exchange_sha256[SHA256_HEX_SIZE];
  char session_hash_sha256[SHA256_HEX_SIZE];
  char master_secret_sha256[SHA256_HEX_SIZE];
  char key_block_sha256[SHA256_HEX_SIZE];
  char client_write_key_sha256[SHA256_HEX_SIZE];
  char server_write_key_sha256[SHA256_HEX_SIZE];
  char client_finished_sha256[SHA256_HEX_SIZE];
  char client_finished_record_sha256[SHA256_HEX_SIZE];
  char server_secure_plain_sha256[SHA256_HEX_SIZE];
  char server_finished_sha256[SHA256_HEX_SIZE];
  uint8_t client_public[32];
  uint8_t shared_secret[32];
  uint8_t server_random[32];
  uint8_t server_key_public[32];
  uint8_t client_key_exchange[64];
  uint8_t client_key_exchange_record[80];
  uint8_t client_finished_record[80];
  uint8_t client_finished_plain[16];
  uint8_t client_secure_flight[96];
  uint8_t master_secret[48];
  uint8_t key_block[72];
  uint8_t client_write_key[16];
  uint8_t server_write_key[16];
  uint8_t client_write_iv[4];
  uint8_t server_write_iv[4];
  sha256_ctx_t handshake_ctx;
  size_t certificate_count;
  size_t certificate_chain_len;
  size_t cert_summaries;
  size_t chain_links_checked;
  size_t chain_links_ok;
  int leaf_signature_verify_ready;
  int leaf_signature_verified;
  const char *leaf_signature_verify_status;
  tls_cert_summary_t certs[TLS_MAX_CERT_SUMMARIES];
  size_t leaf_certificate_len;
  char leaf_certificate_sha256[SHA256_HEX_SIZE];
  int leaf_identity_parsed;
  int leaf_identity_match;
  size_t leaf_dns_names;
  char leaf_matched_dns[96];
  char leaf_first_dns[96];
  char leaf_not_before[32];
  char leaf_not_after[32];
  char alpn[16];
  uint8_t alert_level;
  uint8_t alert_description;
  const char *expected_host;
} tls_parse_info_t;

static const char *tls_cipher_name(uint16_t cipher) {
  switch (cipher) {
  case 0xc02f:
    return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
  case 0xc02b:
    return "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256";
  case 0xc030:
    return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
  case 0xc02c:
    return "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384";
  case 0xcca8:
    return "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256";
  case 0xcca9:
    return "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256";
  case 0x009e:
    return "TLS_DHE_RSA_WITH_AES_128_GCM_SHA256";
  case 0x009c:
    return "TLS_RSA_WITH_AES_128_GCM_SHA256";
  default:
    return "unknown";
  }
}

static const char *tls_group_name(uint16_t group) {
  switch (group) {
  case 0x001d:
    return "x25519";
  case 0x0017:
    return "secp256r1";
  case 0x0018:
    return "secp384r1";
  default:
    return "unknown";
  }
}

static const char *tls_signature_name(uint16_t sig) {
  switch (sig) {
  case 0x0401:
    return "rsa_pkcs1_sha256";
  case 0x0501:
    return "rsa_pkcs1_sha384";
  case 0x0403:
    return "ecdsa_secp256r1_sha256";
  case 0x0503:
    return "ecdsa_secp384r1_sha384";
  case 0x0804:
    return "rsa_pss_rsae_sha256";
  case 0x0805:
    return "rsa_pss_rsae_sha384";
  default:
    return "unknown";
  }
}

static size_t tls_key_block_len(uint16_t cipher) {
  switch (cipher) {
  case 0xc02f: /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */
  case 0xc02b: /* TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 */
    return 40; /* client key + server key + fixed IVs, no MAC keys. */
  default:
    return 0;
  }
}

static char lower_ascii(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static int ascii_ieq_char(char a, char b) {
  return lower_ascii(a) == lower_ascii(b);
}

static int ascii_ieq_span(const char *a, const uint8_t *b, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (!a[i] || !ascii_ieq_char(a[i], (char)b[i])) {
      return 0;
    }
  }
  return a[len] == '\0';
}

static int ascii_iends_with_span(const char *host, const uint8_t *suffix,
                                 size_t suffix_len, size_t *prefix_len) {
  size_t host_len = strlen(host);
  if (host_len <= suffix_len) {
    return 0;
  }

  size_t start = host_len - suffix_len;
  for (size_t i = 0; i < suffix_len; i++) {
    if (!ascii_ieq_char(host[start + i], (char)suffix[i])) {
      return 0;
    }
  }
  if (prefix_len) {
    *prefix_len = start;
  }
  return 1;
}

static int tls_dns_name_matches(const char *host, const uint8_t *dns,
                                size_t dns_len) {
  if (!host || !dns || dns_len == 0) {
    return 0;
  }

  if (dns_len > 2 && dns[0] == '*' && dns[1] == '.') {
    size_t prefix_len = 0;
    if (!ascii_iends_with_span(host, dns + 1, dns_len - 1, &prefix_len) ||
        prefix_len == 0) {
      return 0;
    }
    for (size_t i = 0; i < prefix_len; i++) {
      if (host[i] == '.') {
        return 0;
      }
    }
    return 1;
  }

  return ascii_ieq_span(host, dns, dns_len);
}

static void copy_ascii_limited(char *out, size_t out_cap, const uint8_t *src,
                               size_t src_len) {
  size_t n = src_len;
  if (!out || out_cap == 0) {
    return;
  }
  if (n >= out_cap) {
    n = out_cap - 1;
  }
  for (size_t i = 0; i < n; i++) {
    uint8_t c = src[i];
    out[i] = (c >= 32 && c <= 126) ? (char)c : '?';
  }
  out[n] = '\0';
}

static int asn1_read_tlv(const uint8_t *der, size_t der_len, size_t *off,
                         uint8_t *tag, const uint8_t **value,
                         size_t *value_len) {
  if (!der || !off || !tag || !value || !value_len || *off + 2 > der_len) {
    return -1;
  }

  *tag = der[(*off)++];
  uint8_t first_len = der[(*off)++];
  size_t len = 0;
  if ((first_len & 0x80) == 0) {
    len = first_len;
  } else {
    size_t len_bytes = first_len & 0x7f;
    if (len_bytes == 0 || len_bytes > sizeof(size_t) || *off + len_bytes > der_len) {
      return -1;
    }
    for (size_t i = 0; i < len_bytes; i++) {
      len = (len << 8) | der[(*off)++];
    }
  }

  if (*off + len > der_len) {
    return -1;
  }
  *value = der + *off;
  *value_len = len;
  *off += len;
  return 0;
}

static void tls_sha256_bytes_hex(const void *data, size_t len,
                                 uint8_t digest[SHA256_DIGEST_SIZE],
                                 char hex[SHA256_HEX_SIZE]) {
  sha256_ctx_t ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, digest);
  sha256_hex(digest, hex);
}

static void tls_sha256_ctx_hex(const sha256_ctx_t *ctx,
                               uint8_t digest[SHA256_DIGEST_SIZE],
                               char hex[SHA256_HEX_SIZE]) {
  sha256_ctx_t copy = *ctx;
  sha256_final(&copy, digest);
  sha256_hex(digest, hex);
}

static void tls_hmac_sha256_vector(const uint8_t *key, size_t key_len,
                                   const uint8_t **parts,
                                   const size_t *part_lens,
                                   size_t part_count,
                                   uint8_t out[SHA256_DIGEST_SIZE]) {
  uint8_t key_block[64];
  uint8_t inner_pad[64];
  uint8_t outer_pad[64];
  uint8_t key_hash[SHA256_DIGEST_SIZE];
  uint8_t inner_hash[SHA256_DIGEST_SIZE];
  sha256_ctx_t ctx;

  memset(key_block, 0, sizeof(key_block));
  if (key_len > sizeof(key_block)) {
    sha256_init(&ctx);
    sha256_update(&ctx, key, key_len);
    sha256_final(&ctx, key_hash);
    memcpy(key_block, key_hash, sizeof(key_hash));
  } else if (key && key_len > 0) {
    memcpy(key_block, key, key_len);
  }

  for (size_t i = 0; i < sizeof(key_block); i++) {
    inner_pad[i] = key_block[i] ^ 0x36;
    outer_pad[i] = key_block[i] ^ 0x5c;
  }

  sha256_init(&ctx);
  sha256_update(&ctx, inner_pad, sizeof(inner_pad));
  for (size_t i = 0; i < part_count; i++) {
    if (parts[i] && part_lens[i] > 0) {
      sha256_update(&ctx, parts[i], part_lens[i]);
    }
  }
  sha256_final(&ctx, inner_hash);

  sha256_init(&ctx);
  sha256_update(&ctx, outer_pad, sizeof(outer_pad));
  sha256_update(&ctx, inner_hash, sizeof(inner_hash));
  sha256_final(&ctx, out);
}

static void tls_hmac_sha256(const uint8_t *key, size_t key_len,
                            const uint8_t *data, size_t data_len,
                            uint8_t out[SHA256_DIGEST_SIZE]) {
  const uint8_t *parts[1] = {data};
  size_t part_lens[1] = {data_len};
  tls_hmac_sha256_vector(key, key_len, parts, part_lens, 1, out);
}

static int tls_prf_sha256(const uint8_t *secret, size_t secret_len,
                          const char *label, const uint8_t *seed_a,
                          size_t seed_a_len, const uint8_t *seed_b,
                          size_t seed_b_len, uint8_t *out,
                          size_t out_len) {
  uint8_t seed[128];
  uint8_t a[SHA256_DIGEST_SIZE];
  uint8_t chunk[SHA256_DIGEST_SIZE];
  size_t label_len = strlen(label);
  size_t seed_len = label_len + seed_a_len + seed_b_len;
  size_t produced = 0;

  if (!secret || !label || !out || seed_len > sizeof(seed)) {
    return -1;
  }

  memcpy(seed, label, label_len);
  if (seed_a && seed_a_len > 0) {
    memcpy(seed + label_len, seed_a, seed_a_len);
  }
  if (seed_b && seed_b_len > 0) {
    memcpy(seed + label_len + seed_a_len, seed_b, seed_b_len);
  }

  tls_hmac_sha256(secret, secret_len, seed, seed_len, a);
  while (produced < out_len) {
    const uint8_t *parts[2] = {a, seed};
    size_t part_lens[2] = {sizeof(a), seed_len};
    size_t copy;

    tls_hmac_sha256_vector(secret, secret_len, parts, part_lens, 2, chunk);
    copy = out_len - produced;
    if (copy > sizeof(chunk)) {
      copy = sizeof(chunk);
    }
    memcpy(out + produced, chunk, copy);
    produced += copy;
    tls_hmac_sha256(secret, secret_len, a, sizeof(a), a);
  }

  return 0;
}

static int tls_oid_equal(const uint8_t *oid, size_t oid_len,
                         const uint8_t *expected, size_t expected_len) {
  return oid_len == expected_len && memcmp(oid, expected, expected_len) == 0;
}

static const char *tls_signature_oid_name(const uint8_t *oid, size_t oid_len) {
  static const uint8_t sha256_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                       0x0d, 0x01, 0x01, 0x0b};
  static const uint8_t sha384_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                       0x0d, 0x01, 0x01, 0x0c};
  static const uint8_t ecdsa_sha256[] = {0x2a, 0x86, 0x48, 0xce,
                                         0x3d, 0x04, 0x03, 0x02};
  static const uint8_t ecdsa_sha384[] = {0x2a, 0x86, 0x48, 0xce,
                                         0x3d, 0x04, 0x03, 0x03};

  if (tls_oid_equal(oid, oid_len, sha256_rsa, sizeof(sha256_rsa))) {
    return "sha256WithRSAEncryption";
  }
  if (tls_oid_equal(oid, oid_len, sha384_rsa, sizeof(sha384_rsa))) {
    return "sha384WithRSAEncryption";
  }
  if (tls_oid_equal(oid, oid_len, ecdsa_sha256, sizeof(ecdsa_sha256))) {
    return "ecdsaWithSHA256";
  }
  if (tls_oid_equal(oid, oid_len, ecdsa_sha384, sizeof(ecdsa_sha384))) {
    return "ecdsaWithSHA384";
  }
  return "unknown";
}

static int tls_signature_oid_is_sha256_rsa(const uint8_t *oid,
                                           size_t oid_len) {
  static const uint8_t sha256_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                       0x0d, 0x01, 0x01, 0x0b};
  return tls_oid_equal(oid, oid_len, sha256_rsa, sizeof(sha256_rsa));
}

static const char *tls_public_key_oid_name(const uint8_t *oid,
                                           size_t oid_len) {
  static const uint8_t rsa_encryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                           0x0d, 0x01, 0x01, 0x01};
  static const uint8_t ec_public_key[] = {0x2a, 0x86, 0x48, 0xce,
                                         0x3d, 0x02, 0x01};

  if (tls_oid_equal(oid, oid_len, rsa_encryption, sizeof(rsa_encryption))) {
    return "rsaEncryption";
  }
  if (tls_oid_equal(oid, oid_len, ec_public_key, sizeof(ec_public_key))) {
    return "id-ecPublicKey";
  }
  return "unknown";
}

static int tls_public_key_oid_is_rsa(const uint8_t *oid, size_t oid_len) {
  static const uint8_t rsa_encryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                           0x0d, 0x01, 0x01, 0x01};
  return tls_oid_equal(oid, oid_len, rsa_encryption, sizeof(rsa_encryption));
}

static const char *tls_curve_oid_name(const uint8_t *oid, size_t oid_len) {
  static const uint8_t secp256r1[] = {0x2a, 0x86, 0x48, 0xce,
                                      0x3d, 0x03, 0x01, 0x07};
  static const uint8_t secp384r1[] = {0x2b, 0x81, 0x04, 0x00, 0x22};

  if (tls_oid_equal(oid, oid_len, secp256r1, sizeof(secp256r1))) {
    return "secp256r1";
  }
  if (tls_oid_equal(oid, oid_len, secp384r1, sizeof(secp384r1))) {
    return "secp384r1";
  }
  return "";
}

static int tls_parse_algorithm_identifier(const uint8_t *der, size_t der_len,
                                          const uint8_t **oid,
                                          size_t *oid_len,
                                          const uint8_t **params,
                                          size_t *params_len,
                                          uint8_t *params_tag) {
  size_t off = 0;
  uint8_t tag = 0;

  if (asn1_read_tlv(der, der_len, &off, &tag, oid, oid_len) != 0 ||
      tag != 0x06) {
    return -1;
  }

  if (params) {
    *params = NULL;
  }
  if (params_len) {
    *params_len = 0;
  }
  if (params_tag) {
    *params_tag = 0;
  }
  if (off < der_len && params && params_len && params_tag) {
    if (asn1_read_tlv(der, der_len, &off, params_tag, params, params_len) != 0) {
      return -1;
    }
  }
  return 0;
}

static size_t tls_bit_length(const uint8_t *data, size_t len) {
  while (len > 0 && *data == 0) {
    data++;
    len--;
  }
  if (len == 0) {
    return 0;
  }

  uint8_t first = data[0];
  size_t bits = (len - 1) * 8;
  while (first) {
    bits++;
    first >>= 1;
  }
  return bits;
}

static uint32_t tls_parse_small_uint(const uint8_t *data, size_t len) {
  uint32_t value = 0;
  while (len > 0 && *data == 0) {
    data++;
    len--;
  }
  if (len > 4) {
    return 0;
  }
  for (size_t i = 0; i < len; i++) {
    value = (value << 8) | data[i];
  }
  return value;
}

static void tls_parse_rsa_public_key(const uint8_t *bit_string,
                                     size_t bit_string_len,
                                     tls_cert_summary_t *summary) {
  if (!bit_string || bit_string_len < 2 || bit_string[0] != 0 || !summary) {
    return;
  }

  size_t off = 1;
  uint8_t tag = 0;
  const uint8_t *seq = NULL;
  size_t seq_len = 0;
  if (asn1_read_tlv(bit_string, bit_string_len, &off, &tag, &seq, &seq_len) !=
          0 ||
      tag != 0x30) {
    return;
  }

  size_t p = 0;
  const uint8_t *modulus = NULL;
  size_t modulus_len = 0;
  const uint8_t *exponent = NULL;
  size_t exponent_len = 0;
  if (asn1_read_tlv(seq, seq_len, &p, &tag, &modulus, &modulus_len) != 0 ||
      tag != 0x02) {
    return;
  }
  if (asn1_read_tlv(seq, seq_len, &p, &tag, &exponent, &exponent_len) != 0 ||
      tag != 0x02) {
    return;
  }

  summary->public_key_bits = tls_bit_length(modulus, modulus_len);
  summary->rsa_exponent = tls_parse_small_uint(exponent, exponent_len);
  while (modulus_len > 0 && *modulus == 0) {
    modulus++;
    modulus_len--;
  }
  if (modulus_len > 0 && modulus_len <= TLS_RSA_MAX_BYTES) {
    memcpy(summary->rsa_modulus, modulus, modulus_len);
    summary->rsa_modulus_len = modulus_len;
    summary->rsa_modulus_ready = 1;
  }
}

static void tls_parse_spki(const uint8_t *der, size_t der_len,
                           tls_cert_summary_t *summary) {
  size_t off = 0;
  uint8_t tag = 0;
  const uint8_t *alg_seq = NULL;
  size_t alg_seq_len = 0;
  const uint8_t *spki = NULL;
  size_t spki_len = 0;
  const uint8_t *oid = NULL;
  size_t oid_len = 0;
  const uint8_t *params = NULL;
  size_t params_len = 0;
  uint8_t params_tag = 0;

  if (!summary ||
      asn1_read_tlv(der, der_len, &off, &tag, &alg_seq, &alg_seq_len) != 0 ||
      tag != 0x30) {
    return;
  }
  if (tls_parse_algorithm_identifier(alg_seq, alg_seq_len, &oid, &oid_len,
                                     &params, &params_len, &params_tag) != 0) {
    return;
  }

  summary->public_key_alg = tls_public_key_oid_name(oid, oid_len);
  summary->public_key_rsa = tls_public_key_oid_is_rsa(oid, oid_len);
  if (params_tag == 0x06) {
    summary->public_key_curve = tls_curve_oid_name(params, params_len);
  }

  if (asn1_read_tlv(der, der_len, &off, &tag, &spki, &spki_len) != 0 ||
      tag != 0x03) {
    return;
  }
  if (summary->public_key_rsa) {
    tls_parse_rsa_public_key(spki, spki_len, summary);
  } else if (spki_len > 1) {
    summary->public_key_bits = (spki_len - 1) * 8;
  }
}

static int rsa_ge_bytes(const uint8_t *a, const uint8_t *b, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (a[i] > b[i]) {
      return 1;
    }
    if (a[i] < b[i]) {
      return 0;
    }
  }
  return 1;
}

static void rsa_sub_inplace(uint8_t *a, const uint8_t *b, size_t len) {
  uint16_t borrow = 0;
  for (size_t i = len; i > 0; i--) {
    uint16_t av = a[i - 1];
    uint16_t bv = (uint16_t)b[i - 1] + borrow;
    if (av < bv) {
      a[i - 1] = (uint8_t)(av + 256 - bv);
      borrow = 1;
    } else {
      a[i - 1] = (uint8_t)(av - bv);
      borrow = 0;
    }
  }
}

static int rsa_ge_ext(const uint8_t *tmp, const uint8_t *mod, size_t len) {
  if (tmp[0] != 0) {
    return 1;
  }
  return rsa_ge_bytes(tmp + 1, mod, len);
}

static void rsa_sub_ext(uint8_t *tmp, const uint8_t *mod, size_t len) {
  uint16_t borrow = 0;
  for (size_t i = len + 1; i > 1; i--) {
    uint16_t av = tmp[i - 1];
    uint16_t bv = (uint16_t)mod[i - 2] + borrow;
    if (av < bv) {
      tmp[i - 1] = (uint8_t)(av + 256 - bv);
      borrow = 1;
    } else {
      tmp[i - 1] = (uint8_t)(av - bv);
      borrow = 0;
    }
  }
  if (borrow) {
    tmp[0]--;
  }
}

static void rsa_add_mod(uint8_t *out, const uint8_t *a, const uint8_t *b,
                        const uint8_t *mod, size_t len) {
  uint8_t tmp[TLS_RSA_MAX_BYTES + 1];
  uint16_t carry = 0;

  memset(tmp, 0, sizeof(tmp));
  for (size_t i = len; i > 0; i--) {
    uint16_t sum = (uint16_t)a[i - 1] + b[i - 1] + carry;
    tmp[i] = (uint8_t)sum;
    carry = sum >> 8;
  }
  tmp[0] = (uint8_t)carry;

  if (rsa_ge_ext(tmp, mod, len)) {
    rsa_sub_ext(tmp, mod, len);
  }
  memcpy(out, tmp + 1, len);
}

static void rsa_double_mod(uint8_t *out, const uint8_t *a,
                           const uint8_t *mod, size_t len) {
  rsa_add_mod(out, a, a, mod, len);
}

static void rsa_mul_mod(uint8_t *out, const uint8_t *a, const uint8_t *b,
                        const uint8_t *mod, size_t len) {
  uint8_t result[TLS_RSA_MAX_BYTES];
  uint8_t temp[TLS_RSA_MAX_BYTES];

  memset(result, 0, len);
  memcpy(temp, a, len);
  if (rsa_ge_bytes(temp, mod, len)) {
    rsa_sub_inplace(temp, mod, len);
  }

  for (size_t i = len; i > 0; i--) {
    uint8_t byte = b[i - 1];
    for (int bit = 0; bit < 8; bit++) {
      if (byte & (uint8_t)(1U << bit)) {
        rsa_add_mod(result, result, temp, mod, len);
      }
      rsa_double_mod(temp, temp, mod, len);
    }
  }
  memcpy(out, result, len);
}

static int rsa_left_pad(uint8_t *out, size_t out_len, const uint8_t *in,
                        size_t in_len) {
  if (in_len > out_len || out_len > TLS_RSA_MAX_BYTES) {
    return -1;
  }
  memset(out, 0, out_len);
  memcpy(out + out_len - in_len, in, in_len);
  return 0;
}

static int rsa_modexp65537(uint8_t *out, const uint8_t *sig, size_t sig_len,
                           const uint8_t *mod, size_t mod_len) {
  uint8_t base[TLS_RSA_MAX_BYTES];
  uint8_t result[TLS_RSA_MAX_BYTES];
  uint8_t scratch[TLS_RSA_MAX_BYTES];

  if (!out || !sig || !mod || mod_len == 0 || mod_len > TLS_RSA_MAX_BYTES ||
      rsa_left_pad(base, mod_len, sig, sig_len) != 0) {
    return -1;
  }
  if (rsa_ge_bytes(base, mod, mod_len)) {
    return -1;
  }

  memcpy(result, base, mod_len);
  for (int i = 0; i < 16; i++) {
    rsa_mul_mod(scratch, result, result, mod, mod_len);
    memcpy(result, scratch, mod_len);
  }
  rsa_mul_mod(out, result, base, mod, mod_len);
  return 0;
}

static int tls_check_pkcs1_sha256(const uint8_t *em, size_t em_len,
                                  const uint8_t digest[SHA256_DIGEST_SIZE]) {
  static const uint8_t sha256_digest_info_prefix[] = {
      0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
      0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
  size_t tail_len = sizeof(sha256_digest_info_prefix) + SHA256_DIGEST_SIZE;

  if (!em || em_len < 3 + 8 + tail_len || em[0] != 0 || em[1] != 1) {
    return 0;
  }

  size_t ps_len = em_len - 3 - tail_len;
  for (size_t i = 0; i < ps_len; i++) {
    if (em[2 + i] != 0xff) {
      return 0;
    }
  }
  if (em[2 + ps_len] != 0) {
    return 0;
  }
  if (memcmp(em + 3 + ps_len, sha256_digest_info_prefix,
             sizeof(sha256_digest_info_prefix)) != 0) {
    return 0;
  }
  return memcmp(em + 3 + ps_len + sizeof(sha256_digest_info_prefix), digest,
                SHA256_DIGEST_SIZE) == 0;
}

static int tls_verify_leaf_signature(tls_parse_info_t *info) {
  uint8_t encoded[TLS_RSA_MAX_BYTES];
  tls_cert_summary_t *leaf;
  tls_cert_summary_t *issuer;

  if (!info || info->cert_summaries < 2) {
    return 0;
  }

  leaf = &info->certs[0];
  issuer = &info->certs[1];
  if (!leaf->signature_sha256_rsa || !leaf->signature_alg_consistent ||
      !leaf->signature_ready || !issuer->public_key_rsa ||
      !issuer->rsa_modulus_ready || issuer->rsa_exponent != 65537 ||
      issuer->rsa_modulus_len == 0 ||
      leaf->signature_len > issuer->rsa_modulus_len) {
    info->leaf_signature_verify_status = "unsupported";
    return 0;
  }

  info->leaf_signature_verify_ready = 1;
  if (rsa_modexp65537(encoded, leaf->signature_bytes, leaf->signature_len,
                      issuer->rsa_modulus, issuer->rsa_modulus_len) != 0) {
    info->leaf_signature_verify_status = "rsa-error";
    return 0;
  }

  if (tls_check_pkcs1_sha256(encoded, issuer->rsa_modulus_len,
                             leaf->tbs_digest)) {
    info->leaf_signature_verified = 1;
    info->leaf_signature_verify_status = "ok";
    return 1;
  }

  info->leaf_signature_verify_status = "bad-signature";
  return 0;
}

#define FE51_MASK ((uint64_t)((1ULL << 51) - 1ULL))

typedef uint64_t fe51[5];

static uint64_t load_le64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) {
    v = (v << 8) | p[i];
  }
  return v;
}

static void fe51_reduce(fe51 h) {
  for (int round = 0; round < 2; round++) {
    uint64_t c;
    c = h[0] >> 51;
    h[0] &= FE51_MASK;
    h[1] += c;
    c = h[1] >> 51;
    h[1] &= FE51_MASK;
    h[2] += c;
    c = h[2] >> 51;
    h[2] &= FE51_MASK;
    h[3] += c;
    c = h[3] >> 51;
    h[3] &= FE51_MASK;
    h[4] += c;
    c = h[4] >> 51;
    h[4] &= FE51_MASK;
    h[0] += c * 19;
  }
}

static void fe51_copy(fe51 out, const fe51 in) {
  for (int i = 0; i < 5; i++) {
    out[i] = in[i];
  }
}

static void fe51_1(fe51 out) {
  out[0] = 1;
  out[1] = 0;
  out[2] = 0;
  out[3] = 0;
  out[4] = 0;
}

static void fe51_0(fe51 out) {
  memset(out, 0, sizeof(fe51));
}

static void fe51_frombytes(fe51 out, const uint8_t in[32]) {
  out[0] = load_le64(in) & FE51_MASK;
  out[1] = (load_le64(in + 6) >> 3) & FE51_MASK;
  out[2] = (load_le64(in + 12) >> 6) & FE51_MASK;
  out[3] = (load_le64(in + 19) >> 1) & FE51_MASK;
  out[4] = (load_le64(in + 24) >> 12) & FE51_MASK;
}

static int fe51_ge_p(const fe51 h) {
  static const uint64_t p[5] = {FE51_MASK - 18, FE51_MASK, FE51_MASK,
                                FE51_MASK, FE51_MASK};
  for (int i = 4; i >= 0; i--) {
    if (h[i] > p[i]) {
      return 1;
    }
    if (h[i] < p[i]) {
      return 0;
    }
  }
  return 1;
}

static void fe51_sub_p(fe51 h) {
  static const uint64_t p[5] = {FE51_MASK - 18, FE51_MASK, FE51_MASK,
                                FE51_MASK, FE51_MASK};
  uint64_t borrow = 0;
  for (int i = 0; i < 5; i++) {
    uint64_t sub = p[i] + borrow;
    uint64_t old = h[i];
    h[i] = old - sub;
    borrow = old < sub;
  }
}

static void store_le64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    p[i] = (uint8_t)(v >> (8 * i));
  }
}

static void fe51_tobytes(uint8_t out[32], const fe51 in) {
  fe51 h;
  fe51_copy(h, in);
  fe51_reduce(h);
  if (fe51_ge_p(h)) {
    fe51_sub_p(h);
  }

  uint64_t q0 = h[0] | (h[1] << 51);
  uint64_t q1 = (h[1] >> 13) | (h[2] << 38);
  uint64_t q2 = (h[2] >> 26) | (h[3] << 25);
  uint64_t q3 = (h[3] >> 39) | (h[4] << 12);
  store_le64(out, q0);
  store_le64(out + 8, q1);
  store_le64(out + 16, q2);
  store_le64(out + 24, q3);
  out[31] &= 0x7f;
}

static void fe51_add(fe51 out, const fe51 a, const fe51 b) {
  for (int i = 0; i < 5; i++) {
    out[i] = a[i] + b[i];
  }
  fe51_reduce(out);
}

static void fe51_sub(fe51 out, const fe51 a, const fe51 b) {
  static const uint64_t p4[5] = {(FE51_MASK - 18) * 4, FE51_MASK * 4,
                                 FE51_MASK * 4, FE51_MASK * 4,
                                 FE51_MASK * 4};
  for (int i = 0; i < 5; i++) {
    out[i] = a[i] + p4[i] - b[i];
  }
  fe51_reduce(out);
}

static void fe51_mul(fe51 out, const fe51 f, const fe51 g) {
  unsigned __int128 h0 =
      (unsigned __int128)f[0] * g[0] +
      (unsigned __int128)19 * (f[1] * (unsigned __int128)g[4] +
                               f[2] * (unsigned __int128)g[3] +
                               f[3] * (unsigned __int128)g[2] +
                               f[4] * (unsigned __int128)g[1]);
  unsigned __int128 h1 =
      (unsigned __int128)f[0] * g[1] + (unsigned __int128)f[1] * g[0] +
      (unsigned __int128)19 * (f[2] * (unsigned __int128)g[4] +
                               f[3] * (unsigned __int128)g[3] +
                               f[4] * (unsigned __int128)g[2]);
  unsigned __int128 h2 =
      (unsigned __int128)f[0] * g[2] + (unsigned __int128)f[1] * g[1] +
      (unsigned __int128)f[2] * g[0] +
      (unsigned __int128)19 * (f[3] * (unsigned __int128)g[4] +
                               f[4] * (unsigned __int128)g[3]);
  unsigned __int128 h3 =
      (unsigned __int128)f[0] * g[3] + (unsigned __int128)f[1] * g[2] +
      (unsigned __int128)f[2] * g[1] + (unsigned __int128)f[3] * g[0] +
      (unsigned __int128)19 * f[4] * g[4];
  unsigned __int128 h4 =
      (unsigned __int128)f[0] * g[4] + (unsigned __int128)f[1] * g[3] +
      (unsigned __int128)f[2] * g[2] + (unsigned __int128)f[3] * g[1] +
      (unsigned __int128)f[4] * g[0];

  out[0] = (uint64_t)h0 & FE51_MASK;
  h1 += h0 >> 51;
  out[1] = (uint64_t)h1 & FE51_MASK;
  h2 += h1 >> 51;
  out[2] = (uint64_t)h2 & FE51_MASK;
  h3 += h2 >> 51;
  out[3] = (uint64_t)h3 & FE51_MASK;
  h4 += h3 >> 51;
  out[4] = (uint64_t)h4 & FE51_MASK;
  out[0] += (uint64_t)(h4 >> 51) * 19;
  fe51_reduce(out);
}

static void fe51_sq(fe51 out, const fe51 a) {
  fe51_mul(out, a, a);
}

static int exponent_pminus2_bit(int bit) {
  if (bit == 2 || bit == 4 || bit >= 255) {
    return 0;
  }
  return 1;
}

static void fe51_inv(fe51 out, const fe51 z) {
  fe51 result;
  fe51 base;
  fe51 scratch;
  fe51_1(result);
  fe51_copy(base, z);
  for (int bit = 254; bit >= 0; bit--) {
    fe51_sq(scratch, result);
    fe51_copy(result, scratch);
    if (exponent_pminus2_bit(bit)) {
      fe51_mul(scratch, result, base);
      fe51_copy(result, scratch);
    }
  }
  fe51_copy(out, result);
}

static void fe51_cswap(fe51 a, fe51 b, uint8_t swap) {
  uint64_t mask = 0 - (uint64_t)swap;
  for (int i = 0; i < 5; i++) {
    uint64_t t = mask & (a[i] ^ b[i]);
    a[i] ^= t;
    b[i] ^= t;
  }
}

static void x25519_clamp(uint8_t scalar[32]) {
  scalar[0] &= 248;
  scalar[31] &= 127;
  scalar[31] |= 64;
}

static void x25519_scalar_mult(uint8_t out[32], const uint8_t scalar_in[32],
                               const uint8_t point_in[32]) {
  static const fe51 a24 = {121665, 0, 0, 0, 0};
  uint8_t scalar[32];
  uint8_t point[32];
  fe51 x1, x2, z2, x3, z3;
  fe51 a, aa, b, bb, e, c, d, da, cb, tmp0, tmp1;
  uint8_t swap = 0;

  memcpy(scalar, scalar_in, sizeof(scalar));
  memcpy(point, point_in, sizeof(point));
  x25519_clamp(scalar);
  point[31] &= 0x7f;

  fe51_frombytes(x1, point);
  fe51_1(x2);
  fe51_0(z2);
  fe51_copy(x3, x1);
  fe51_1(z3);

  for (int t = 254; t >= 0; t--) {
    uint8_t kt = (uint8_t)((scalar[t >> 3] >> (t & 7)) & 1);
    swap ^= kt;
    fe51_cswap(x2, x3, swap);
    fe51_cswap(z2, z3, swap);
    swap = kt;

    fe51_add(a, x2, z2);
    fe51_sq(aa, a);
    fe51_sub(b, x2, z2);
    fe51_sq(bb, b);
    fe51_sub(e, aa, bb);
    fe51_add(c, x3, z3);
    fe51_sub(d, x3, z3);
    fe51_mul(da, d, a);
    fe51_mul(cb, c, b);
    fe51_add(tmp0, da, cb);
    fe51_sq(x3, tmp0);
    fe51_sub(tmp0, da, cb);
    fe51_sq(tmp1, tmp0);
    fe51_mul(z3, tmp1, x1);
    fe51_mul(x2, aa, bb);
    fe51_mul(tmp0, e, a24);
    fe51_add(tmp0, aa, tmp0);
    fe51_mul(z2, e, tmp0);
  }

  fe51_cswap(x2, x3, swap);
  fe51_cswap(z2, z3, swap);
  fe51_inv(z2, z2);
  fe51_mul(x2, x2, z2);
  fe51_tobytes(out, x2);
}

static void tls_make_x25519_scalar(const tls_parse_info_t *info,
                                   uint8_t scalar[32]) {
  sha256_ctx_t ctx;
  uint64_t ticks = timer_ticks();
  const char domain[] = "orizon-tls-x25519-bootstrap";

  if (tls_client_x25519_scalar_ready) {
    memcpy(scalar, tls_client_x25519_scalar, sizeof(tls_client_x25519_scalar));
    return;
  }

  sha256_init(&ctx);
  sha256_update(&ctx, domain, sizeof(domain) - 1);
  sha256_update(&ctx, tls_client_random, sizeof(tls_client_random));
  sha256_update(&ctx, info->server_random, sizeof(info->server_random));
  sha256_update(&ctx, info->server_key_public, info->key_public_len);
  sha256_update(&ctx, &ticks, sizeof(ticks));
  sha256_final(&ctx, tls_client_x25519_scalar);
  x25519_clamp(tls_client_x25519_scalar);
  tls_client_x25519_scalar_ready = 1;
  memcpy(scalar, tls_client_x25519_scalar, sizeof(tls_client_x25519_scalar));
}

static void tls_prepare_x25519_key_agreement(tls_parse_info_t *info) {
  static const uint8_t basepoint[32] = {9};
  uint8_t scalar[32];
  uint8_t client_public[32];
  uint8_t shared_secret[32];

  if (!info || info->key_exchange_group != 0x001d ||
      info->key_public_len != 32) {
    return;
  }

  tls_make_x25519_scalar(info, scalar);
  x25519_scalar_mult(client_public, scalar, basepoint);
  x25519_scalar_mult(shared_secret, scalar, info->server_key_public);
  memcpy(info->client_public, client_public, sizeof(client_public));
  memcpy(info->shared_secret, shared_secret, sizeof(shared_secret));
  sha256_buffer_hex(client_public, sizeof(client_public),
                    info->client_public_sha256);
  sha256_buffer_hex(shared_secret, sizeof(shared_secret),
                    info->shared_secret_sha256);
  info->key_agreement_x25519 = 1;
  info->key_agreement_ready = 1;
  info->client_key_bootstrap = 1;
}

static int tls_build_client_key_exchange(tls_parse_info_t *info) {
  uint8_t hs[64];
  size_t hs_off = 0;
  size_t rec_off = 0;
  size_t body_start;
  size_t body_len;

  if (!info || !info->key_agreement_x25519) {
    return -1;
  }

  tls_put(hs, sizeof(hs), &hs_off, 16); /* ClientKeyExchange */
  hs_off += 3;
  body_start = hs_off;
  tls_put(hs, sizeof(hs), &hs_off, 32);
  tls_put_bytes(hs, sizeof(hs), &hs_off, info->client_public,
                sizeof(info->client_public));
  body_len = hs_off - body_start;
  put_be24(hs + 1, (uint32_t)body_len);

  if (hs_off > sizeof(info->client_key_exchange)) {
    return -1;
  }
  memcpy(info->client_key_exchange, hs, hs_off);
  info->client_key_exchange_len = hs_off;
  sha256_buffer_hex(info->client_key_exchange, info->client_key_exchange_len,
                    info->client_key_exchange_sha256);

  tls_put(info->client_key_exchange_record,
          sizeof(info->client_key_exchange_record), &rec_off, 22);
  tls_put16(info->client_key_exchange_record,
            sizeof(info->client_key_exchange_record), &rec_off, 0x0303);
  tls_put16(info->client_key_exchange_record,
            sizeof(info->client_key_exchange_record), &rec_off,
            (uint16_t)info->client_key_exchange_len);
  tls_put_bytes(info->client_key_exchange_record,
                sizeof(info->client_key_exchange_record), &rec_off,
                info->client_key_exchange, info->client_key_exchange_len);
  info->client_key_exchange_record_len = rec_off;
  info->client_key_exchange_ready = 1;
  return 0;
}

static int tls_build_client_finished(tls_parse_info_t *info,
                                     const uint8_t session_hash[32]) {
  uint8_t verify_data[12];
  uint8_t plaintext[16];
  uint8_t ciphertext[16];
  uint8_t tag[16];
  uint8_t nonce[12];
  uint8_t explicit_nonce[8] = {0};
  uint8_t aad[13];
  size_t off = 0;

  if (!info || !session_hash || !info->master_secret_ready ||
      !info->traffic_keys_ready) {
    return -1;
  }

  if (tls_prf_sha256(info->master_secret, sizeof(info->master_secret),
                     "client finished", session_hash, 32, NULL, 0,
                     verify_data, sizeof(verify_data)) != 0) {
    return -1;
  }

  plaintext[0] = 20; /* Finished */
  plaintext[1] = 0;
  plaintext[2] = 0;
  plaintext[3] = sizeof(verify_data);
  memcpy(plaintext + 4, verify_data, sizeof(verify_data));
  memcpy(info->client_finished_plain, plaintext, sizeof(plaintext));
  sha256_buffer_hex(plaintext, sizeof(plaintext), info->client_finished_sha256);

  memcpy(nonce, info->client_write_iv, sizeof(info->client_write_iv));
  memcpy(nonce + sizeof(info->client_write_iv), explicit_nonce,
         sizeof(explicit_nonce));
  put_be64(aad, 0); /* first record after ChangeCipherSpec starts at seq 0 */
  aad[8] = 22;      /* handshake */
  put_be16(aad + 9, 0x0303);
  put_be16(aad + 11, sizeof(plaintext));

  if (aes128_gcm_encrypt(info->client_write_key, nonce, aad, sizeof(aad),
                         plaintext, sizeof(plaintext), ciphertext, tag) != 0) {
    return -1;
  }

  tls_put(info->client_finished_record, sizeof(info->client_finished_record),
          &off, 22);
  tls_put16(info->client_finished_record, sizeof(info->client_finished_record),
            &off, 0x0303);
  tls_put16(info->client_finished_record, sizeof(info->client_finished_record),
            &off, (uint16_t)(sizeof(explicit_nonce) + sizeof(ciphertext) +
                             sizeof(tag)));
  tls_put_bytes(info->client_finished_record,
                sizeof(info->client_finished_record), &off, explicit_nonce,
                sizeof(explicit_nonce));
  tls_put_bytes(info->client_finished_record,
                sizeof(info->client_finished_record), &off, ciphertext,
                sizeof(ciphertext));
  tls_put_bytes(info->client_finished_record,
                sizeof(info->client_finished_record), &off, tag, sizeof(tag));
  info->client_finished_record_len = off;
  sha256_buffer_hex(info->client_finished_record,
                    info->client_finished_record_len,
                    info->client_finished_record_sha256);

  off = 0;
  tls_put(info->client_secure_flight, sizeof(info->client_secure_flight), &off,
          20); /* ChangeCipherSpec */
  tls_put16(info->client_secure_flight, sizeof(info->client_secure_flight),
            &off, 0x0303);
  tls_put16(info->client_secure_flight, sizeof(info->client_secure_flight),
            &off, 1);
  tls_put(info->client_secure_flight, sizeof(info->client_secure_flight), &off,
          1);
  tls_put_bytes(info->client_secure_flight, sizeof(info->client_secure_flight),
                &off, info->client_finished_record,
                info->client_finished_record_len);
  info->client_secure_flight_len = off;
  info->client_finished_ready = 1;
  return 0;
}

static int tls_build_encrypted_client_record(const tls_parse_info_t *info,
                                             uint64_t seq, uint8_t record_type,
                                             const uint8_t *plain,
                                             size_t plain_len, uint8_t *out,
                                             size_t out_cap,
                                             size_t *out_len) {
  uint8_t explicit_nonce[8];
  uint8_t nonce[12];
  uint8_t aad[13];
  uint8_t tag[16];
  size_t off = 0;

  if (!info || !plain || !out || !out_len ||
      out_cap < 5 + sizeof(explicit_nonce) + plain_len + sizeof(tag)) {
    return -1;
  }

  put_be64(explicit_nonce, seq);
  memcpy(nonce, info->client_write_iv, sizeof(info->client_write_iv));
  memcpy(nonce + sizeof(info->client_write_iv), explicit_nonce,
         sizeof(explicit_nonce));
  put_be64(aad, seq);
  aad[8] = record_type;
  put_be16(aad + 9, 0x0303);
  put_be16(aad + 11, (uint16_t)plain_len);

  tls_put(out, out_cap, &off, record_type);
  tls_put16(out, out_cap, &off, 0x0303);
  tls_put16(out, out_cap, &off,
            (uint16_t)(sizeof(explicit_nonce) + plain_len + sizeof(tag)));
  tls_put_bytes(out, out_cap, &off, explicit_nonce, sizeof(explicit_nonce));
  if (aes128_gcm_encrypt(info->client_write_key, nonce, aad, sizeof(aad),
                         plain, plain_len, out + off, tag) != 0) {
    return -1;
  }
  off += plain_len;
  tls_put_bytes(out, out_cap, &off, tag, sizeof(tag));
  *out_len = off;
  return 0;
}

static void tls_prepare_key_schedule(tls_parse_info_t *info) {
  uint8_t session_hash[SHA256_DIGEST_SIZE];
  uint8_t random_seed[64];
  size_t key_block_len;

  if (!info || !info->key_agreement_ready || !info->server_hello_done_seen ||
      !info->handshake_ctx_ready ||
      tls_build_client_key_exchange(info) != 0) {
    return;
  }

  sha256_ctx_t session_ctx = info->handshake_ctx;
  sha256_update(&session_ctx, info->client_key_exchange,
                info->client_key_exchange_len);
  tls_sha256_ctx_hex(&session_ctx, session_hash, info->session_hash_sha256);

  if (info->extended_master_secret) {
    if (tls_prf_sha256(info->shared_secret, sizeof(info->shared_secret),
                       "extended master secret", session_hash,
                       sizeof(session_hash), NULL, 0, info->master_secret,
                       sizeof(info->master_secret)) != 0) {
      return;
    }
  } else {
    memcpy(random_seed, tls_client_random, sizeof(tls_client_random));
    memcpy(random_seed + sizeof(tls_client_random), info->server_random,
           sizeof(info->server_random));
    if (tls_prf_sha256(info->shared_secret, sizeof(info->shared_secret),
                       "master secret", random_seed, 64, NULL, 0,
                       info->master_secret, sizeof(info->master_secret)) != 0) {
      return;
    }
  }

  info->master_secret_ready = 1;
  sha256_buffer_hex(info->master_secret, sizeof(info->master_secret),
                    info->master_secret_sha256);

  key_block_len = tls_key_block_len(info->cipher);
  if (key_block_len == 0 || key_block_len > sizeof(info->key_block)) {
    return;
  }
  info->key_schedule_supported = 1;
  memcpy(random_seed, info->server_random, sizeof(info->server_random));
  memcpy(random_seed + sizeof(info->server_random), tls_client_random,
         sizeof(tls_client_random));
  if (tls_prf_sha256(info->master_secret, sizeof(info->master_secret),
                     "key expansion", random_seed, 64, NULL, 0,
                     info->key_block, key_block_len) != 0) {
    return;
  }

  info->traffic_keys_ready = 1;
  memcpy(info->client_write_key, info->key_block, 16);
  memcpy(info->server_write_key, info->key_block + 16, 16);
  memcpy(info->client_write_iv, info->key_block + 32, 4);
  memcpy(info->server_write_iv, info->key_block + 36, 4);
  sha256_buffer_hex(info->key_block, key_block_len, info->key_block_sha256);
  sha256_buffer_hex(info->key_block, 16, info->client_write_key_sha256);
  sha256_buffer_hex(info->key_block + 16, 16, info->server_write_key_sha256);
  tls_build_client_finished(info, session_hash);
}

static int tls_decrypt_aes_gcm_record(const tls_parse_info_t *info,
                                      uint64_t seq, uint8_t record_type,
                                      uint16_t version, const uint8_t *record,
                                      size_t record_len, uint8_t *plain,
                                      size_t plain_cap, size_t *plain_len) {
  uint8_t nonce[12];
  uint8_t aad[13];
  size_t ciphertext_len;

  if (!info || !record || !plain || !plain_len || record_len < 24) {
    return -1;
  }
  ciphertext_len = record_len - 8 - 16;
  if (ciphertext_len > plain_cap) {
    return -1;
  }

  memcpy(nonce, info->server_write_iv, sizeof(info->server_write_iv));
  memcpy(nonce + sizeof(info->server_write_iv), record, 8);
  put_be64(aad, seq);
  aad[8] = record_type;
  put_be16(aad + 9, version);
  put_be16(aad + 11, (uint16_t)ciphertext_len);
  if (aes128_gcm_decrypt(info->server_write_key, nonce, aad, sizeof(aad),
                         record + 8, ciphertext_len,
                         record + 8 + ciphertext_len, plain) != 0) {
    return -1;
  }
  *plain_len = ciphertext_len;
  return 0;
}

static void tls_parse_decrypted_server_handshake(tls_parse_info_t *info,
                                                 const uint8_t *plain,
                                                 size_t plain_len,
                                                 sha256_ctx_t *server_ctx,
                                                 sha256_ctx_t *server_ctx_no_client) {
  size_t p = 0;
  while (p + 4 <= plain_len) {
    uint8_t hs_type = plain[p];
    size_t hs_len = get_be24(plain + p + 1);
    if (p + 4 + hs_len > plain_len) {
      break;
    }

    if (hs_type == 20 && hs_len >= 12) {
      uint8_t digest[SHA256_DIGEST_SIZE];
      uint8_t expected[12];
      char digest_hex[SHA256_HEX_SIZE];
      tls_sha256_ctx_hex(server_ctx, digest, digest_hex);
      if (tls_prf_sha256(info->master_secret, sizeof(info->master_secret),
                         "server finished", digest, sizeof(digest), NULL, 0,
                         expected, sizeof(expected)) == 0) {
        info->server_finished_seen = 1;
        if (memcmp(plain + p + 4, expected, sizeof(expected)) == 0) {
          info->server_finished_verified = 1;
        }
      }
      if (!info->server_finished_verified && server_ctx_no_client) {
        tls_sha256_ctx_hex(server_ctx_no_client, digest, digest_hex);
        if (tls_prf_sha256(info->master_secret, sizeof(info->master_secret),
                           "server finished", digest, sizeof(digest), NULL, 0,
                           expected, sizeof(expected)) == 0 &&
            memcmp(plain + p + 4, expected, sizeof(expected)) == 0) {
          info->server_finished_verified_without_client_finished = 1;
        }
      }
      sha256_buffer_hex(plain + p, 4 + hs_len, info->server_finished_sha256);
    } else {
      if (hs_type == 4) {
        info->server_ticket_seen = 1;
      }
    }

    sha256_update(server_ctx, plain + p, 4 + hs_len);
    if (server_ctx_no_client) {
      sha256_update(server_ctx_no_client, plain + p, 4 + hs_len);
    }
    p += 4 + hs_len;
  }
}

static void tls_decrypt_server_secure_reply(tls_parse_info_t *info) {
  size_t off = 0;
  size_t plain_total = 0;
  uint64_t server_seq = 0;
  sha256_ctx_t server_ctx;
  sha256_ctx_t server_ctx_no_client;

  if (!info || !info->traffic_keys_ready || !info->client_finished_ready ||
      tls_last_secure_reply_len == 0) {
    return;
  }

  server_ctx_no_client = info->handshake_ctx;
  sha256_update(&server_ctx_no_client, info->client_key_exchange,
                info->client_key_exchange_len);
  server_ctx = server_ctx_no_client;
  if (tls_last_client_finished_plain_len == sizeof(tls_last_client_finished_plain)) {
    sha256_update(&server_ctx, tls_last_client_finished_plain,
                  tls_last_client_finished_plain_len);
  } else {
    sha256_update(&server_ctx, info->client_finished_plain,
                  sizeof(info->client_finished_plain));
  }

  while (off + 5 <= tls_last_secure_reply_len) {
    uint8_t record_type = tls_secure_rx_buf[off];
    uint16_t version = get_be16(tls_secure_rx_buf + off + 1);
    size_t record_len = get_be16(tls_secure_rx_buf + off + 3);
    const uint8_t *record = tls_secure_rx_buf + off + 5;
    size_t plain_len = 0;

    off += 5;
    if (off + record_len > tls_last_secure_reply_len) {
      break;
    }

    if (record_type == 22 && !info->server_change_cipher_spec_seen) {
      tls_parse_decrypted_server_handshake(info, record, record_len,
                                           &server_ctx,
                                           &server_ctx_no_client);
    } else if (record_type == 20 && record_len == 1 && record[0] == 1) {
      info->server_change_cipher_spec_seen = 1;
    } else if ((record_type == 22 || record_type == 21) &&
               plain_total < sizeof(tls_server_plain_buf) &&
               tls_decrypt_aes_gcm_record(
                   info, server_seq, record_type, version, record, record_len,
                   tls_server_plain_buf + plain_total,
                   sizeof(tls_server_plain_buf) - plain_total, &plain_len) ==
                   0) {
      uint8_t *plain = tls_server_plain_buf + plain_total;
      info->server_secure_decrypted = 1;
      server_seq++;
      if (record_type == 22) {
        tls_parse_decrypted_server_handshake(info, plain, plain_len,
                                             &server_ctx,
                                             &server_ctx_no_client);
      } else if (record_type == 21 && plain_len >= 2) {
        info->server_alert_seen = 1;
        info->server_alert_level = plain[0];
        info->server_alert_description = plain[1];
      }
      plain_total += plain_len;
    }

    off += record_len;
  }

  if (plain_total > 0) {
    info->server_secure_plain_len = plain_total;
    sha256_buffer_hex(tls_server_plain_buf, plain_total,
                      info->server_secure_plain_sha256);
  }
  info->server_read_seq_next = server_seq;
}

static void tls_copy_http_status(const uint8_t *plain, size_t plain_len) {
  size_t n = 0;

  tls_last_http_status[0] = '\0';
  while (n + 1 < sizeof(tls_last_http_status) && n < plain_len &&
         plain[n] != '\r' && plain[n] != '\n') {
    uint8_t c = plain[n];
    tls_last_http_status[n] = (c >= 32 && c <= 126) ? (char)c : '?';
    n++;
  }
  tls_last_http_status[n] = '\0';
}

static void tls_decrypt_server_http_reply(tls_parse_info_t *info) {
  size_t off = 0;
  size_t plain_total = 0;
  uint64_t seq;

  tls_last_http_decrypted = 0;
  tls_last_http_plain_len = 0;
  tls_last_http_plain_sha256[0] = '\0';
  tls_last_http_status[0] = '\0';
  if (!info || !info->traffic_keys_ready || !info->server_finished_verified ||
      tls_last_http_reply_len == 0) {
    return;
  }

  seq = info->server_read_seq_next;
  while (off + 5 <= tls_last_http_reply_len) {
    uint8_t record_type = tls_app_rx_buf[off];
    uint16_t version = get_be16(tls_app_rx_buf + off + 1);
    size_t record_len = get_be16(tls_app_rx_buf + off + 3);
    const uint8_t *record = tls_app_rx_buf + off + 5;
    size_t plain_len = 0;

    if (tls_last_http_first_record_type == 0) {
      tls_last_http_first_record_type = record_type;
      tls_last_http_first_record_len = record_len;
    }
    off += 5;
    if (off + record_len > tls_last_http_reply_len) {
      tls_last_http_decrypt_failures++;
      break;
    }

    if (record_type == 23 && plain_total < sizeof(tls_app_plain_buf)) {
      if (tls_decrypt_aes_gcm_record(
              info, seq, record_type, version, record, record_len,
              tls_app_plain_buf + plain_total,
              sizeof(tls_app_plain_buf) - plain_total, &plain_len) == 0) {
        plain_total += plain_len;
        tls_last_http_decrypted = 1;
        seq++;
      } else {
        tls_last_http_decrypt_failures++;
      }
    } else if (record_type == 22) {
      if (tls_decrypt_aes_gcm_record(info, seq, record_type, version, record,
                                     record_len, tls_server_plain_buf,
                                     sizeof(tls_server_plain_buf),
                                     &plain_len) == 0) {
        if (plain_len >= 4 && tls_server_plain_buf[0] == 4) {
          info->server_ticket_seen = 1;
        }
        seq++;
      } else {
        tls_last_http_decrypt_failures++;
      }
    } else if (record_type == 21) {
      uint8_t alert[2];
      if (tls_decrypt_aes_gcm_record(info, seq, record_type, version, record,
                                     record_len, alert, sizeof(alert),
                                     &plain_len) == 0) {
        seq++;
      } else {
        tls_last_http_decrypt_failures++;
      }
    }

    off += record_len;
  }

  if (plain_total > 0) {
    tls_last_http_plain_len = plain_total;
    sha256_buffer_hex(tls_app_plain_buf, plain_total,
                      tls_last_http_plain_sha256);
    tls_copy_http_status(tls_app_plain_buf, plain_total);
  }
}

static void tls_copy_asn1_time(const uint8_t *value, size_t len, char *out,
                               size_t out_cap) {
  copy_ascii_limited(out, out_cap, value, len);
}

static int tls_parse_certificate_names(const uint8_t *cert, size_t cert_len,
                                       tls_cert_summary_t *summary) {
  size_t off = 0;
  uint8_t tag = 0;
  const uint8_t *cert_seq = NULL;
  size_t cert_seq_len = 0;
  const uint8_t *tbs = NULL;
  size_t tbs_len = 0;
  const uint8_t *value = NULL;
  size_t value_len = 0;
  const uint8_t *oid = NULL;
  size_t oid_len = 0;

  if (!summary) {
    return -1;
  }
  memset(summary, 0, sizeof(*summary));
  summary->cert_len = cert_len;

  if (asn1_read_tlv(cert, cert_len, &off, &tag, &cert_seq, &cert_seq_len) != 0 ||
      tag != 0x30) {
    return -1;
  }

  size_t cert_off = 0;
  size_t tbs_tlv_start = cert_off;
  if (asn1_read_tlv(cert_seq, cert_seq_len, &cert_off, &tag, &tbs, &tbs_len) !=
          0 ||
      tag != 0x30) {
    return -1;
  }
  tls_sha256_bytes_hex(cert_seq + tbs_tlv_start, cert_off - tbs_tlv_start,
                       summary->tbs_digest, summary->tbs_sha256);

  size_t p = 0;
  if (p < tbs_len && tbs[p] == 0xa0) {
    if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0) {
      return -1;
    }
  }

  if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0) {
    return -1;
  }

  if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0 ||
      tag != 0x30) {
    return -1;
  }
  if (tls_parse_algorithm_identifier(value, value_len, &oid, &oid_len, NULL,
                                     NULL, NULL) == 0) {
    summary->tbs_signature_alg = tls_signature_oid_name(oid, oid_len);
  }

  if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0 ||
      tag != 0x30) {
    return -1;
  }
  sha256_buffer_hex(value, value_len, summary->issuer_sha256);

  if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0 ||
      tag != 0x30) {
    return -1;
  }

  if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0 ||
      tag != 0x30) {
    return -1;
  }
  sha256_buffer_hex(value, value_len, summary->subject_sha256);

  if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0 ||
      tag != 0x30) {
    return -1;
  }
  tls_parse_spki(value, value_len, summary);

  if (asn1_read_tlv(cert_seq, cert_seq_len, &cert_off, &tag, &value,
                    &value_len) != 0 ||
      tag != 0x30) {
    return -1;
  }
  if (tls_parse_algorithm_identifier(value, value_len, &oid, &oid_len, NULL,
                                     NULL, NULL) == 0) {
    summary->signature_alg = tls_signature_oid_name(oid, oid_len);
    summary->signature_sha256_rsa = tls_signature_oid_is_sha256_rsa(oid, oid_len);
  }

  if (summary->signature_alg && summary->tbs_signature_alg &&
      strcmp(summary->signature_alg, "unknown") != 0 &&
      strcmp(summary->signature_alg, summary->tbs_signature_alg) == 0) {
    summary->signature_alg_consistent = 1;
  }

  if (asn1_read_tlv(cert_seq, cert_seq_len, &cert_off, &tag, &value,
                    &value_len) == 0 &&
      tag == 0x03 && value_len > 1) {
    summary->signature_len = value_len - 1;
    sha256_buffer_hex(value + 1, value_len - 1, summary->signature_sha256);
    if (summary->signature_len <= TLS_RSA_MAX_BYTES) {
      memcpy(summary->signature_bytes, value + 1, summary->signature_len);
      summary->signature_ready = 1;
    }
  }

  summary->parsed = 1;
  return 0;
}

static void tls_parse_subject_alt_names(const uint8_t *der, size_t der_len,
                                        tls_parse_info_t *info) {
  size_t off = 0;
  uint8_t tag = 0;
  const uint8_t *seq = NULL;
  size_t seq_len = 0;

  if (asn1_read_tlv(der, der_len, &off, &tag, &seq, &seq_len) != 0 ||
      tag != 0x30) {
    return;
  }

  size_t p = 0;
  while (p < seq_len) {
    const uint8_t *name = NULL;
    size_t name_len = 0;
    if (asn1_read_tlv(seq, seq_len, &p, &tag, &name, &name_len) != 0) {
      break;
    }
    if (tag != 0x82) {
      continue;
    }

    info->leaf_dns_names++;
    if (!info->leaf_first_dns[0]) {
      copy_ascii_limited(info->leaf_first_dns, sizeof(info->leaf_first_dns),
                         name, name_len);
    }
    if (!info->leaf_identity_match &&
        tls_dns_name_matches(info->expected_host, name, name_len)) {
      info->leaf_identity_match = 1;
      copy_ascii_limited(info->leaf_matched_dns,
                         sizeof(info->leaf_matched_dns), name, name_len);
    }
  }
}

static void tls_parse_certificate_extensions(const uint8_t *der, size_t der_len,
                                             tls_parse_info_t *info) {
  size_t off = 0;
  uint8_t tag = 0;
  const uint8_t *seq = NULL;
  size_t seq_len = 0;
  static const uint8_t san_oid[] = {0x55, 0x1d, 0x11};

  if (asn1_read_tlv(der, der_len, &off, &tag, &seq, &seq_len) != 0 ||
      tag != 0x30) {
    return;
  }

  size_t p = 0;
  while (p < seq_len) {
    const uint8_t *ext = NULL;
    size_t ext_len = 0;
    if (asn1_read_tlv(seq, seq_len, &p, &tag, &ext, &ext_len) != 0 ||
        tag != 0x30) {
      break;
    }

    size_t e = 0;
    const uint8_t *oid = NULL;
    size_t oid_len = 0;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    if (asn1_read_tlv(ext, ext_len, &e, &tag, &oid, &oid_len) != 0 ||
        tag != 0x06) {
      continue;
    }

    if (e < ext_len && ext[e] == 0x01) {
      const uint8_t *critical = NULL;
      size_t critical_len = 0;
      asn1_read_tlv(ext, ext_len, &e, &tag, &critical, &critical_len);
      UNUSED(critical);
      UNUSED(critical_len);
    }

    if (asn1_read_tlv(ext, ext_len, &e, &tag, &value, &value_len) != 0 ||
        tag != 0x04) {
      continue;
    }

    if (oid_len == sizeof(san_oid) &&
        memcmp(oid, san_oid, sizeof(san_oid)) == 0) {
      tls_parse_subject_alt_names(value, value_len, info);
    }
  }
}

static void tls_parse_leaf_identity(const uint8_t *cert, size_t cert_len,
                                    tls_parse_info_t *info) {
  size_t off = 0;
  uint8_t tag = 0;
  const uint8_t *cert_seq = NULL;
  size_t cert_seq_len = 0;
  const uint8_t *tbs = NULL;
  size_t tbs_len = 0;

  if (asn1_read_tlv(cert, cert_len, &off, &tag, &cert_seq, &cert_seq_len) != 0 ||
      tag != 0x30) {
    return;
  }

  size_t cert_off = 0;
  if (asn1_read_tlv(cert_seq, cert_seq_len, &cert_off, &tag, &tbs, &tbs_len) !=
          0 ||
      tag != 0x30) {
    return;
  }

  size_t p = 0;
  const uint8_t *value = NULL;
  size_t value_len = 0;
  if (p < tbs_len && tbs[p] == 0xa0) {
    if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0) {
      return;
    }
  }

  for (int i = 0; i < 3; i++) {
    if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0) {
      return;
    }
  }

  if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0 ||
      tag != 0x30) {
    return;
  }
  size_t v = 0;
  const uint8_t *time_value = NULL;
  size_t time_len = 0;
  if (asn1_read_tlv(value, value_len, &v, &tag, &time_value, &time_len) == 0 &&
      (tag == 0x17 || tag == 0x18)) {
    tls_copy_asn1_time(time_value, time_len, info->leaf_not_before,
                       sizeof(info->leaf_not_before));
  }
  if (asn1_read_tlv(value, value_len, &v, &tag, &time_value, &time_len) == 0 &&
      (tag == 0x17 || tag == 0x18)) {
    tls_copy_asn1_time(time_value, time_len, info->leaf_not_after,
                       sizeof(info->leaf_not_after));
  }

  for (int i = 0; i < 2; i++) {
    if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0) {
      return;
    }
  }

  while (p < tbs_len) {
    if (asn1_read_tlv(tbs, tbs_len, &p, &tag, &value, &value_len) != 0) {
      return;
    }
    if (tag == 0xa3) {
      tls_parse_certificate_extensions(value, value_len, info);
      info->leaf_identity_parsed = 1;
      return;
    }
  }

  info->leaf_identity_parsed = 1;
}

static void tls_parse_server_hello(const uint8_t *body, size_t len,
                                   tls_parse_info_t *info) {
  if (len < 38) {
    return;
  }

  size_t sid_len = body[34];
  size_t cipher_pos = 35 + sid_len;
  if (cipher_pos + 3 > len) {
    return;
  }

  info->server_hello_seen = 1;
  info->server_version = get_be16(body);
  memcpy(info->server_random, body + 2, sizeof(info->server_random));
  info->cipher = get_be16(body + cipher_pos);

  size_t ext_pos = cipher_pos + 3;
  if (ext_pos + 2 > len) {
    return;
  }

  size_t ext_total = get_be16(body + ext_pos);
  ext_pos += 2;
  if (ext_pos + ext_total > len) {
    return;
  }

  size_t ext_end = ext_pos + ext_total;
  while (ext_pos + 4 <= ext_end) {
    uint16_t type = get_be16(body + ext_pos);
    uint16_t ext_len = get_be16(body + ext_pos + 2);
    const uint8_t *ext = body + ext_pos + 4;
    ext_pos += 4;
    if (ext_pos + ext_len > ext_end) {
      break;
    }
    if (type == 0x0017 && ext_len == 0) {
      info->extended_master_secret = 1;
    } else if (type == 0x0010 && ext_len >= 3) {
      size_t list_len = get_be16(ext);
      size_t alpn_len = ext[2];
      if (list_len + 2 <= ext_len && alpn_len + 3 <= ext_len &&
          alpn_len < sizeof(info->alpn)) {
        memcpy(info->alpn, ext + 3, alpn_len);
        info->alpn[alpn_len] = '\0';
      }
    }
    ext_pos += ext_len;
  }
}

static void tls_parse_certificate(const uint8_t *body, size_t len,
                                  tls_parse_info_t *info) {
  if (len < 3) {
    return;
  }

  size_t chain_len = get_be24(body);
  size_t p = 3;
  size_t end = p + chain_len;
  if (end > len) {
    return;
  }

  info->certificate_chain_len = chain_len;
  while (p + 3 <= end) {
    size_t cert_len = get_be24(body + p);
    tls_cert_summary_t cert_summary;
    p += 3;
    if (p + cert_len > end) {
      break;
    }
    if (tls_parse_certificate_names(body + p, cert_len, &cert_summary) == 0 &&
        info->cert_summaries < TLS_MAX_CERT_SUMMARIES) {
      info->certs[info->cert_summaries++] = cert_summary;
      if (info->cert_summaries >= 2) {
        tls_cert_summary_t *child = &info->certs[info->cert_summaries - 2];
        tls_cert_summary_t *issuer = &info->certs[info->cert_summaries - 1];
        info->chain_links_checked++;
        if (strcmp(child->issuer_sha256, issuer->subject_sha256) == 0) {
          info->chain_links_ok++;
        }
      }
    }
    if (info->certificate_count == 0) {
      info->leaf_certificate_len = cert_len;
      sha256_buffer_hex(body + p, cert_len, info->leaf_certificate_sha256);
      tls_parse_leaf_identity(body + p, cert_len, info);
    }
    info->certificate_count++;
    p += cert_len;
  }

  if (info->certificate_count > 0) {
    info->certificate_seen = 1;
    tls_verify_leaf_signature(info);
  }
}

static void tls_parse_server_key_exchange(const uint8_t *body, size_t len,
                                          tls_parse_info_t *info) {
  if (len < 4 || body[0] != 3) {
    return;
  }

  size_t pub_len = body[3];
  size_t sig_pos = 4 + pub_len;
  if (sig_pos > len) {
    return;
  }

  info->server_key_exchange_seen = 1;
  info->key_exchange_group = get_be16(body + 1);
  info->key_public_len = pub_len;
  if (info->key_exchange_group == 0x001d && pub_len == 32) {
    memcpy(info->server_key_public, body + 4, 32);
  }
  if (sig_pos + 2 <= len) {
    info->key_signature_alg = get_be16(body + sig_pos);
  }
  tls_prepare_x25519_key_agreement(info);
}

static void tls_parse_handshake_message(uint8_t hs_type, const uint8_t *body,
                                        size_t len, tls_parse_info_t *info) {
  info->handshake_messages++;
  switch (hs_type) {
  case 2:
    tls_parse_server_hello(body, len, info);
    break;
  case 11:
    tls_parse_certificate(body, len, info);
    break;
  case 12:
    tls_parse_server_key_exchange(body, len, info);
    break;
  case 14:
    info->server_hello_done_seen = 1;
    break;
  default:
    break;
  }
}

static void tls_parse_records(const uint8_t *rx, size_t rx_len,
                              tls_parse_info_t *info, const char *host) {
  size_t off = 0;

  memset(info, 0, sizeof(*info));
  info->expected_host = host;
  sha256_init(&info->handshake_ctx);
  if (tls_client_hello_handshake_len > 0 &&
      tls_client_hello_handshake_len <= sizeof(tls_tx_buf) - 5) {
    sha256_update(&info->handshake_ctx, tls_tx_buf + 5,
                  tls_client_hello_handshake_len);
    info->handshake_ctx_ready = 1;
  }
  while (off + 5 <= rx_len) {
    uint8_t record_type = rx[off];
    size_t record_len = get_be16(rx + off + 3);
    size_t record_end = off + 5 + record_len;

    if (record_end > rx_len) {
      info->partial_record = 1;
      return;
    }

    info->records++;
    if (record_type == 21) {
      info->alerts++;
      if (record_len >= 2) {
        info->alert_level = rx[off + 5];
        info->alert_description = rx[off + 6];
      }
    } else if (record_type == 22) {
      size_t p = off + 5;
      info->handshake_records++;
      while (p + 4 <= record_end) {
        const uint8_t *hs_start = rx + p;
        uint8_t hs_type = rx[p];
        size_t hs_len = get_be24(rx + p + 1);
        if (p + 4 + hs_len > record_end) {
          info->partial_handshake = 1;
          break;
        }
        if (info->handshake_ctx_ready) {
          sha256_update(&info->handshake_ctx, hs_start, 4 + hs_len);
        }
        tls_parse_handshake_message(hs_type, hs_start + 4, hs_len, info);
        p += 4 + hs_len;
      }
    }

    off = record_end;
  }

  if (off < rx_len) {
    info->partial_record = 1;
  }
  if (!info->partial_record && !info->partial_handshake) {
    tls_prepare_key_schedule(info);
  }
}

static void summarize_tls_response(const char *host, const uint8_t *rx,
                                   size_t rx_len, char *out, size_t out_cap) {
  tls_parse_info_t info;
  char line[192];

  out[0] = '\0';
  tls_parse_records(rx, rx_len, &info, host);
  tls_decrypt_server_secure_reply(&info);
  if (!tls_last_http_decrypted) {
    tls_decrypt_server_http_reply(&info);
  }

  snprintf(line, sizeof(line), "tls host=%s port=443 bytes=%lu\n", host,
           (unsigned long)rx_len);
  append_text(out, out_cap, line);
  snprintf(line, sizeof(line),
           "tls records total=%lu handshake=%lu alerts=%lu messages=%lu\n",
           (unsigned long)info.records, (unsigned long)info.handshake_records,
           (unsigned long)info.alerts, (unsigned long)info.handshake_messages);
  append_text(out, out_cap, line);

  if (info.server_hello_seen) {
    snprintf(line, sizeof(line),
             "tls server-hello version=%04x cipher=%04x %s\n",
             (unsigned int)info.server_version, (unsigned int)info.cipher,
             tls_cipher_name(info.cipher));
    append_text(out, out_cap, line);
    if (info.alpn[0]) {
      snprintf(line, sizeof(line), "tls alpn %s\n", info.alpn);
      append_text(out, out_cap, line);
    }
    snprintf(line, sizeof(line), "tls extended-master-secret %s\n",
             info.extended_master_secret ? "yes" : "no");
    append_text(out, out_cap, line);
  }

  if (info.certificate_seen) {
    snprintf(line, sizeof(line),
             "tls certificate certs=%lu chain-bytes=%lu leaf-bytes=%lu\n",
             (unsigned long)info.certificate_count,
             (unsigned long)info.certificate_chain_len,
             (unsigned long)info.leaf_certificate_len);
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line), "tls leaf-sha256 %s\n",
             info.leaf_certificate_sha256);
    append_text(out, out_cap, line);
    if (info.leaf_not_before[0] || info.leaf_not_after[0]) {
      snprintf(line, sizeof(line), "tls leaf-validity not-before=%s not-after=%s\n",
               info.leaf_not_before[0] ? info.leaf_not_before : "unknown",
               info.leaf_not_after[0] ? info.leaf_not_after : "unknown");
      append_text(out, out_cap, line);
    }
    if (info.leaf_identity_parsed) {
      snprintf(line, sizeof(line),
               "tls leaf-identity host=%s san-match=%s dns-names=%lu matched=%s first=%s\n",
               host, info.leaf_identity_match ? "yes" : "no",
               (unsigned long)info.leaf_dns_names,
               info.leaf_matched_dns[0] ? info.leaf_matched_dns : "none",
               info.leaf_first_dns[0] ? info.leaf_first_dns : "none");
      append_text(out, out_cap, line);
    }
    if (info.cert_summaries > 0) {
      tls_cert_summary_t *leaf = &info.certs[0];
      snprintf(line, sizeof(line),
               "tls chain-link links-ok=%lu/%lu names-parsed=%lu\n",
               (unsigned long)info.chain_links_ok,
               (unsigned long)info.chain_links_checked,
               (unsigned long)info.cert_summaries);
      append_text(out, out_cap, line);
      snprintf(line, sizeof(line),
               "tls cert-signature leaf-alg=%s tbs-match=%s sig-bytes=%lu\n",
               leaf->signature_alg ? leaf->signature_alg : "unknown",
               leaf->signature_alg_consistent ? "yes" : "no",
               (unsigned long)leaf->signature_len);
      append_text(out, out_cap, line);
      snprintf(line, sizeof(line), "tls leaf-tbs-sha256 %s\n",
               leaf->tbs_sha256);
      append_text(out, out_cap, line);
      if (leaf->signature_sha256[0]) {
        snprintf(line, sizeof(line), "tls leaf-signature-sha256 %s\n",
                 leaf->signature_sha256);
        append_text(out, out_cap, line);
      }
      snprintf(line, sizeof(line), "tls leaf-issuer-sha256 %s\n",
               info.certs[0].issuer_sha256);
      append_text(out, out_cap, line);
      if (info.cert_summaries > 1) {
        tls_cert_summary_t *issuer = &info.certs[1];
        snprintf(line, sizeof(line), "tls next-subject-sha256 %s\n",
                 issuer->subject_sha256);
        append_text(out, out_cap, line);
        snprintf(line, sizeof(line),
                 "tls issuer-public-key alg=%s bits=%lu exponent=%lu\n",
                 issuer->public_key_alg ? issuer->public_key_alg : "unknown",
                 (unsigned long)issuer->public_key_bits,
                 (unsigned long)issuer->rsa_exponent);
        append_text(out, out_cap, line);
        snprintf(line, sizeof(line),
                 "tls signature-material ready=%s method=rsa-pkcs1-sha256\n",
                 (leaf->signature_sha256_rsa && issuer->public_key_rsa &&
                  issuer->public_key_bits > 0 && issuer->rsa_exponent > 0)
                     ? "yes"
                     : "no");
        append_text(out, out_cap, line);
        snprintf(line, sizeof(line),
                 "tls leaf-signature-verify ready=%s verified=%s status=%s\n",
                 info.leaf_signature_verify_ready ? "yes" : "no",
                 info.leaf_signature_verified ? "yes" : "no",
                 info.leaf_signature_verify_status
                     ? info.leaf_signature_verify_status
                     : "not-run");
        append_text(out, out_cap, line);
      }
    }
  }

  if (info.server_key_exchange_seen) {
    snprintf(line, sizeof(line),
             "tls server-key-exchange group=%04x %s public-key-bytes=%lu signature=%04x %s\n",
             (unsigned int)info.key_exchange_group,
             tls_group_name(info.key_exchange_group),
             (unsigned long)info.key_public_len,
             (unsigned int)info.key_signature_alg,
             tls_signature_name(info.key_signature_alg));
    append_text(out, out_cap, line);
  }

  if (info.key_agreement_ready) {
    snprintf(line, sizeof(line),
             "tls key-agreement group=x25519 ready=yes rng=bootstrap-not-secure client-key=%s\n",
             info.client_key_bootstrap ? "generated" : "unknown");
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line), "tls client-public-sha256 %s\n",
             info.client_public_sha256);
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line), "tls shared-secret-sha256 %s\n",
             info.shared_secret_sha256);
    append_text(out, out_cap, line);
  }

  if (info.client_key_exchange_ready) {
    snprintf(line, sizeof(line),
             "tls client-keyexchange ready=yes sent=%s handshake-bytes=%lu record-bytes=%lu sha256=%s\n",
             tls_last_client_key_exchange_sent ? "yes" : "no",
             (unsigned long)info.client_key_exchange_len,
             (unsigned long)info.client_key_exchange_record_len,
             info.client_key_exchange_sha256);
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line), "tls session-hash-sha256 %s\n",
             info.session_hash_sha256);
    append_text(out, out_cap, line);
  }

  if (info.master_secret_ready) {
    snprintf(line, sizeof(line),
             "tls master-secret ready=yes mode=%s sha256=%s\n",
             info.extended_master_secret ? "extended" : "legacy",
             info.master_secret_sha256);
    append_text(out, out_cap, line);
  }

  if (info.traffic_keys_ready) {
    snprintf(line, sizeof(line),
             "tls traffic-keys ready=yes suite=aes-128-gcm-sha256 key-block-sha256=%s\n",
             info.key_block_sha256);
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line), "tls client-write-key-sha256 %s\n",
             info.client_write_key_sha256);
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line), "tls server-write-key-sha256 %s\n",
             info.server_write_key_sha256);
    append_text(out, out_cap, line);
  } else if (info.master_secret_ready && !info.key_schedule_supported) {
    snprintf(line, sizeof(line),
             "tls traffic-keys ready=no unsupported-cipher=%04x\n",
             (unsigned int)info.cipher);
    append_text(out, out_cap, line);
  }

  if (info.client_finished_ready) {
    snprintf(line, sizeof(line),
             "tls client-finished ready=yes sent=%s flight-bytes=%lu record-sha256=%s\n",
             tls_last_client_finished_sent ? "yes" : "no",
             (unsigned long)info.client_secure_flight_len,
             info.client_finished_record_sha256);
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line), "tls client-finished-plain-sha256 %s\n",
             info.client_finished_sha256);
    append_text(out, out_cap, line);
  }

  if (tls_last_secure_reply_len > 0) {
    snprintf(line, sizeof(line),
             "tls server-secure-reply bytes=%lu sha256=%s first-bytes=",
             (unsigned long)tls_last_secure_reply_len,
             tls_last_secure_reply_sha256);
    append_text(out, out_cap, line);
    append_hex_bytes(out, out_cap, tls_secure_rx_buf,
                     tls_last_secure_reply_len < 64
                         ? tls_last_secure_reply_len
                         : 64);
    append_text(out, out_cap, "\n");
  }

  if (info.server_secure_decrypted) {
    snprintf(line, sizeof(line),
             "tls server-secure-decrypt ready=yes plain-bytes=%lu sha256=%s ccs=%s ticket=%s\n",
             (unsigned long)info.server_secure_plain_len,
             info.server_secure_plain_sha256,
             info.server_change_cipher_spec_seen ? "yes" : "not-seen",
             info.server_ticket_seen ? "yes" : "no");
    append_text(out, out_cap, line);
    snprintf(line, sizeof(line),
             "tls server-finished seen=%s verified=%s alt-no-client-finished=%s sha256=%s\n",
             info.server_finished_seen ? "yes" : "no",
             info.server_finished_verified ? "yes" : "no",
             info.server_finished_verified_without_client_finished ? "yes"
                                                                    : "no",
             info.server_finished_sha256[0] ? info.server_finished_sha256
                                            : "none");
    append_text(out, out_cap, line);
  }

  if (info.server_alert_seen) {
    snprintf(line, sizeof(line),
             "tls server-secure-alert level=%lu description=%lu\n",
             (unsigned long)info.server_alert_level,
             (unsigned long)info.server_alert_description);
    append_text(out, out_cap, line);
  }

  if (tls_last_http_get_sent) {
    snprintf(line, sizeof(line),
             "tls encrypted-http-get sent=yes encrypted-bytes=%lu first-record=%02x/%lu failures=%lu sha256=%s\n",
             (unsigned long)tls_last_http_reply_len,
             (unsigned int)tls_last_http_first_record_type,
             (unsigned long)tls_last_http_first_record_len,
             (unsigned long)tls_last_http_decrypt_failures,
             tls_last_http_reply_sha256[0] ? tls_last_http_reply_sha256
                                           : "pending");
    append_text(out, out_cap, line);
    append_text(out, out_cap, "tls encrypted-http-reply-first-bytes ");
    append_hex_bytes(out, out_cap, tls_app_rx_buf,
                     tls_last_http_reply_len < 48 ? tls_last_http_reply_len
                                                  : 48);
    append_text(out, out_cap, "\n");
  }

  if (tls_last_http_decrypted) {
    snprintf(line, sizeof(line),
             "tls encrypted-http-response decrypted=yes plain-bytes=%lu sha256=%s status=%s\n",
             (unsigned long)tls_last_http_plain_len,
             tls_last_http_plain_sha256,
             tls_last_http_status[0] ? tls_last_http_status : "unknown");
    append_text(out, out_cap, line);
    append_text(out, out_cap, "tls encrypted-http-first-bytes ");
    append_hex_bytes(out, out_cap, tls_app_plain_buf,
                     tls_last_http_plain_len < 64 ? tls_last_http_plain_len
                                                  : 64);
    append_text(out, out_cap, "\n");
  }

  if (info.alerts > 0) {
    snprintf(line, sizeof(line), "tls alert level=%lu description=%lu\n",
             (unsigned long)info.alert_level,
             (unsigned long)info.alert_description);
    append_text(out, out_cap, line);
  }

  snprintf(line, sizeof(line), "tls server-hello-done %s\n",
           info.server_hello_done_seen ? "yes" : "no");
  append_text(out, out_cap, line);
  if (info.partial_record || info.partial_handshake) {
    snprintf(line, sizeof(line), "tls parse partial-record=%s partial-handshake=%s\n",
             info.partial_record ? "yes" : "no",
             info.partial_handshake ? "yes" : "no");
    append_text(out, out_cap, line);
  }
  if (tls_last_http_decrypted) {
    append_text(out, out_cap,
                "tls next tls-package-body-streaming-and-boot-writer\n");
  } else if (info.server_finished_seen && !info.server_finished_verified) {
    append_text(out, out_cap,
                "tls next tls-server-finished-transcript-verification\n");
  } else {
    append_text(out, out_cap,
                "tls next tls-encrypted-http-get-and-package-body\n");
  }
  append_text(out, out_cap, "tls first-bytes ");
  append_hex_bytes(out, out_cap, rx, rx_len < 96 ? rx_len : 96);
  append_text(out, out_cap, "\n");
}

int netstack_tls_probe(const char *host, char *out, size_t out_cap,
                       size_t *out_len) {
  uint32_t ip = 0;
  tcp_conn_t conn;
  tls_parse_info_t send_info;
  size_t hello_len = 0;
  size_t rx_len = 0;

  if (!host || !out || out_cap == 0) {
    return -1;
  }
  out[0] = '\0';
  if (netstack_resolve_a(host, &ip) != 0) {
    return -1;
  }
  if (tcp_connect(&conn, ip, HTTPS_PORT) != 0) {
    return -1;
  }
  if (build_tls_client_hello(host, tls_tx_buf, sizeof(tls_tx_buf), &hello_len) != 0) {
    set_status("tls: clienthello build failed");
    return -1;
  }

  set_status("tls: clienthello");
  if (tcp_send_data(&conn, tls_tx_buf, hello_len) < 0) {
    set_status("tls: send failed");
    return -1;
  }
  if (tcp_recv_bytes(&conn, tls_rx_buf, sizeof(tls_rx_buf), &rx_len, 6000) != 0) {
    set_status("tls: response timeout");
    return -1;
  }

  tls_parse_records(tls_rx_buf, rx_len, &send_info, host);
  if (send_info.client_key_exchange_ready &&
      send_info.client_key_exchange_record_len > 0) {
    set_status("tls: clientkeyexchange");
    if (tcp_send_data(&conn, send_info.client_key_exchange_record,
                      send_info.client_key_exchange_record_len) >= 0) {
      tls_last_client_key_exchange_sent = 1;
      set_status("tls: clientkeyexchange sent");
    } else {
      set_status("tls: clientkeyexchange send failed");
    }
  }
  if (tls_last_client_key_exchange_sent && send_info.client_finished_ready &&
      send_info.client_secure_flight_len > 0) {
    size_t secure_reply_len = 0;
    set_status("tls: client finished");
    if (tcp_send_data(&conn, send_info.client_secure_flight,
                      send_info.client_secure_flight_len) >= 0) {
      tls_last_client_finished_sent = 1;
      memcpy(tls_last_client_finished_plain, send_info.client_finished_plain,
             sizeof(tls_last_client_finished_plain));
      tls_last_client_finished_plain_len = sizeof(tls_last_client_finished_plain);
      set_status("tls: finished sent");
      if (tcp_recv_bytes(&conn, tls_secure_rx_buf, sizeof(tls_secure_rx_buf),
                         &secure_reply_len, 3000) == 0) {
        tls_last_secure_reply_len = secure_reply_len;
        sha256_buffer_hex(tls_secure_rx_buf, secure_reply_len,
                          tls_last_secure_reply_sha256);
        set_status("tls: server secure reply");
      }
    } else {
      set_status("tls: finished send failed");
    }
  }
  tls_decrypt_server_secure_reply(&send_info);
  if (send_info.server_finished_verified) {
    static const char http_get[] =
        "GET /Orizon-cmd/Orizon-OS/main/Orizon-OS.iso HTTP/1.1\r\n"
        "Host: raw.githubusercontent.com\r\n"
        "User-Agent: OrizonOS-update/0.1\r\n"
        "Accept: */*\r\n"
        "Range: bytes=0-2047\r\n"
        "Connection: close\r\n"
        "\r\n";
    size_t app_record_len = 0;
    size_t app_reply_len = 0;
    if (tls_build_encrypted_client_record(
            &send_info, 1, 23, (const uint8_t *)http_get,
            sizeof(http_get) - 1, tls_tx_buf, sizeof(tls_tx_buf),
            &app_record_len) == 0) {
      set_status("tls: encrypted http get");
      if (tcp_send_data(&conn, tls_tx_buf, app_record_len) >= 0) {
        tls_last_http_get_sent = 1;
        set_status("tls: encrypted get sent");
        if (tcp_recv_bytes(&conn, tls_app_rx_buf, sizeof(tls_app_rx_buf),
                           &app_reply_len, 5000) == 0) {
          tls_last_http_reply_len = app_reply_len;
          sha256_buffer_hex(tls_app_rx_buf, app_reply_len,
                            tls_last_http_reply_sha256);
          tls_decrypt_server_http_reply(&send_info);
          set_status("tls: encrypted http reply");
        }
      }
    }
  }

  summarize_tls_response(host, tls_rx_buf, rx_len, out, out_cap);
  if (out_len) {
    *out_len = strlen(out);
  }
  set_status("tls: server response received");
  return 0;
}

static const uint8_t *http_body_start(const uint8_t *plain, size_t plain_len,
                                      size_t *body_len) {
  if (body_len) {
    *body_len = 0;
  }
  if (!plain || plain_len < 4) {
    return NULL;
  }
  for (size_t i = 0; i + 3 < plain_len; i++) {
    if (plain[i] == '\r' && plain[i + 1] == '\n' &&
        plain[i + 2] == '\r' && plain[i + 3] == '\n') {
      if (body_len) {
        *body_len = plain_len - (i + 4);
      }
      return plain + i + 4;
    }
  }
  return NULL;
}

int netstack_https_range_get(const char *host, const char *path,
                             uint64_t start, uint64_t end, void *out,
                             size_t out_cap, size_t *out_len,
                             char *diag, size_t diag_cap) {
  uint32_t ip = 0;
  tcp_conn_t conn;
  tls_parse_info_t send_info;
  size_t hello_len = 0;
  size_t rx_len = 0;
  char http_get[512];
  size_t app_record_len = 0;
  size_t app_reply_len = 0;
  const uint8_t *body;
  size_t body_len = 0;

  if (out_len) {
    *out_len = 0;
  }
  if (diag && diag_cap > 0) {
    diag[0] = '\0';
  }
  if (!host || !path || !out || out_cap == 0 || end < start) {
    return -1;
  }
  if (netstack_resolve_a(host, &ip) != 0) {
    return -2;
  }
  if (tcp_connect(&conn, ip, HTTPS_PORT) != 0) {
    return -3;
  }
  if (build_tls_client_hello(host, tls_tx_buf, sizeof(tls_tx_buf),
                             &hello_len) != 0) {
    set_status("tls: clienthello build failed");
    return -4;
  }

  set_status("tls: clienthello");
  if (tcp_send_data(&conn, tls_tx_buf, hello_len) < 0) {
    set_status("tls: send failed");
    return -5;
  }
  if (tcp_recv_bytes(&conn, tls_rx_buf, sizeof(tls_rx_buf), &rx_len, 6000) !=
      0) {
    set_status("tls: response timeout");
    return -6;
  }

  tls_parse_records(tls_rx_buf, rx_len, &send_info, host);
  if (!send_info.client_key_exchange_ready ||
      send_info.client_key_exchange_record_len == 0) {
    return -7;
  }
  if (tcp_send_data(&conn, send_info.client_key_exchange_record,
                    send_info.client_key_exchange_record_len) < 0) {
    return -8;
  }
  tls_last_client_key_exchange_sent = 1;

  if (!send_info.client_finished_ready || send_info.client_secure_flight_len == 0) {
    return -9;
  }
  if (tcp_send_data(&conn, send_info.client_secure_flight,
                    send_info.client_secure_flight_len) < 0) {
    return -10;
  }
  tls_last_client_finished_sent = 1;
  memcpy(tls_last_client_finished_plain, send_info.client_finished_plain,
         sizeof(tls_last_client_finished_plain));
  tls_last_client_finished_plain_len = sizeof(tls_last_client_finished_plain);

  if (tcp_recv_bytes(&conn, tls_secure_rx_buf, sizeof(tls_secure_rx_buf),
                     &tls_last_secure_reply_len, 3000) != 0) {
    return -11;
  }
  sha256_buffer_hex(tls_secure_rx_buf, tls_last_secure_reply_len,
                    tls_last_secure_reply_sha256);
  tls_decrypt_server_secure_reply(&send_info);
  if (!send_info.server_finished_verified) {
    return -12;
  }

  snprintf(http_get, sizeof(http_get),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: OrizonOS-update/1.0\r\n"
           "Accept: */*\r\n"
           "Range: bytes=%lu-%lu\r\n"
           "Connection: close\r\n"
           "\r\n",
           path, host, (unsigned long)start, (unsigned long)end);

  if (tls_build_encrypted_client_record(
          &send_info, 1, 23, (const uint8_t *)http_get, strlen(http_get),
          tls_tx_buf, sizeof(tls_tx_buf), &app_record_len) != 0) {
    return -13;
  }
  if (tcp_send_data(&conn, tls_tx_buf, app_record_len) < 0) {
    return -14;
  }
  tls_last_http_get_sent = 1;
  if (tcp_recv_bytes(&conn, tls_app_rx_buf, sizeof(tls_app_rx_buf),
                     &app_reply_len, 6000) != 0) {
    return -15;
  }
  tls_last_http_reply_len = app_reply_len;
  sha256_buffer_hex(tls_app_rx_buf, app_reply_len, tls_last_http_reply_sha256);
  tls_decrypt_server_http_reply(&send_info);
  if (!tls_last_http_decrypted) {
    return -16;
  }

  body = http_body_start(tls_app_plain_buf, tls_last_http_plain_len, &body_len);
  if (!body || body_len == 0 || body_len > out_cap) {
    return -17;
  }
  memcpy(out, body, body_len);
  if (out_len) {
    *out_len = body_len;
  }
  if (diag && diag_cap > 0) {
    snprintf(diag, diag_cap,
             "https range status=%s bytes=%lu plain=%lu sha256=%s",
             tls_last_http_status[0] ? tls_last_http_status : "unknown",
             (unsigned long)body_len, (unsigned long)tls_last_http_plain_len,
             tls_last_http_plain_sha256);
  }
  set_status("tls: https range downloaded");
  return 0;
}

int netstack_http_get(const char *host, const char *path, char *out,
                      size_t out_cap, size_t *out_len) {
  uint32_t ip = 0;
  tcp_conn_t conn;
  char request[384];

  if (!host || !path || !out || out_cap == 0) {
    return -1;
  }
  if (netstack_resolve_a(host, &ip) != 0) {
    return -1;
  }
  if (tcp_connect(&conn, ip, HTTP_PORT) != 0) {
    return -1;
  }

  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: Orizon-OS-Updater/0.1\r\n"
           "Accept: */*\r\n"
           "Connection: close\r\n\r\n",
           path, host);
  set_status("http: requesting github");
  if (tcp_send_data(&conn, request, strlen(request)) < 0) {
    set_status("http: send failed");
    return -1;
  }
  return tcp_recv_all(&conn, out, out_cap, out_len);
}

int netstack_github_probe(char *out, size_t out_cap, size_t *out_len) {
  return netstack_http_get("raw.githubusercontent.com",
                           "/Orizon-cmd/Orizon-OS/main/README.md", out,
                           out_cap, out_len);
}

int netstack_github_tls_probe(char *out, size_t out_cap, size_t *out_len) {
  return netstack_tls_probe("raw.githubusercontent.com", out, out_cap, out_len);
}

const netstack_status_t *netstack_get_status(void) {
  return &stack_status;
}

void netstack_format_status(char *buf, size_t size) {
  char ip[24];
  char gw[24];
  char dns[24];
  char resolved[24];
  netstack_format_ipv4(stack_status.ip, ip, sizeof(ip));
  netstack_format_ipv4(stack_status.gateway, gw, sizeof(gw));
  netstack_format_ipv4(stack_status.dns, dns, sizeof(dns));
  netstack_format_ipv4(stack_status.last_resolved_ip, resolved, sizeof(resolved));
  snprintf(buf, size, "ipv4=%s ip=%s gateway=%s dns=%s host=%s resolved=%s",
           stack_status.ipv4_ready ? "yes" : "no", ip, gw, dns,
           stack_status.last_host[0] ? stack_status.last_host : "none",
           resolved);
}
