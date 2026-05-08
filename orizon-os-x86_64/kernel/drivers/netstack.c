/*
 * Orizon OS x86_64 - Minimal IPv4/DHCP/DNS/TCP transport
 *
 * This stack is intentionally small and blocking. It exists so the in-kernel
 * updater can start using the network without depending on a userspace tool.
 */

#include "../include/netstack.h"
#include "../include/net.h"
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

static uint16_t get_be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
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
