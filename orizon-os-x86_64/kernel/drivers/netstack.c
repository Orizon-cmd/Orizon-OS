/*
 * Orizon OS x86_64 - Minimal IPv4/DHCP/DNS/TCP transport
 *
 * This stack is intentionally small and blocking. It exists so the in-kernel
 * updater can start using the network without depending on a userspace tool.
 */

#include "../include/netstack.h"
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
    tls_put(out, out_cap, &off, (uint8_t)(seed >> 24));
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
  *out_len = off;
  return 0;
}

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
  uint16_t server_version;
  uint16_t cipher;
  uint16_t key_exchange_group;
  uint16_t key_signature_alg;
  size_t key_public_len;
  size_t certificate_count;
  size_t certificate_chain_len;
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

static void tls_copy_asn1_time(const uint8_t *value, size_t len, char *out,
                               size_t out_cap) {
  copy_ascii_limited(out, out_cap, value, len);
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
    p += 3;
    if (p + cert_len > end) {
      break;
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
  if (sig_pos + 2 <= len) {
    info->key_signature_alg = get_be16(body + sig_pos);
  }
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
        uint8_t hs_type = rx[p];
        size_t hs_len = get_be24(rx + p + 1);
        p += 4;
        if (p + hs_len > record_end) {
          info->partial_handshake = 1;
          break;
        }
        tls_parse_handshake_message(hs_type, rx + p, hs_len, info);
        p += hs_len;
      }
    }

    off = record_end;
  }

  if (off < rx_len) {
    info->partial_record = 1;
  }
}

static void summarize_tls_response(const char *host, const uint8_t *rx,
                                   size_t rx_len, char *out, size_t out_cap) {
  tls_parse_info_t info;
  char line[192];

  out[0] = '\0';
  tls_parse_records(rx, rx_len, &info, host);

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
  append_text(out, out_cap, "tls next trust-chain-validation-and-key-schedule\n");
  append_text(out, out_cap, "tls first-bytes ");
  append_hex_bytes(out, out_cap, rx, rx_len < 96 ? rx_len : 96);
  append_text(out, out_cap, "\n");
}

int netstack_tls_probe(const char *host, char *out, size_t out_cap,
                       size_t *out_len) {
  uint32_t ip = 0;
  tcp_conn_t conn;
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

  summarize_tls_response(host, tls_rx_buf, rx_len, out, out_cap);
  if (out_len) {
    *out_len = strlen(out);
  }
  set_status("tls: server response received");
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
