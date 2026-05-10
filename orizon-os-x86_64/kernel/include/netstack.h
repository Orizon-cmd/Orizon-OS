/*
 * Orizon OS x86_64 - Minimal IPv4 stack for the updater
 */

#ifndef _NETSTACK_H
#define _NETSTACK_H

#include "types.h"

typedef struct {
  int ipv4_ready;
  int static_config_loaded;
  uint32_t ip;
  uint32_t subnet;
  uint32_t gateway;
  uint32_t dns;
  uint32_t last_resolved_ip;
  char last_host[96];
  char status[160];
} netstack_status_t;

typedef enum {
  NETSTACK_TCP_SERVER_CLOSED = 0,
  NETSTACK_TCP_SERVER_LISTEN = 1,
  NETSTACK_TCP_SERVER_SYN_RCVD = 2,
  NETSTACK_TCP_SERVER_ESTABLISHED = 3,
} netstack_tcp_server_state_t;

typedef struct {
  int enabled;
  netstack_tcp_server_state_t state;
  uint16_t listen_port;
  uint16_t remote_port;
  uint32_t remote_ip;
  uint32_t seq;
  uint32_t ack;
  uint8_t remote_mac[6];
  uint32_t connections;
  uint32_t rx_packets;
  uint32_t tx_packets;
  uint32_t rx_bytes;
  uint32_t tx_bytes;
  uint32_t resets;
  uint32_t last_flags;
} netstack_tcp_server_t;

const char *netstack_config_path(void);
const char *netstack_log_path(void);
int netstack_parse_ipv4(const char *text, uint32_t *out_ip);
int netstack_configure_ipv4_dhcp(void);
int netstack_configure_ipv4(void);
int netstack_configure_ipv4_static(uint32_t ip, uint32_t subnet,
                                   uint32_t gateway, uint32_t dns);
int netstack_configure_static_from_vfs(void);
int netstack_save_static_config(uint32_t ip, uint32_t subnet,
                                uint32_t gateway, uint32_t dns);
int netstack_save_dhcp_config(void);
void netstack_reset(void);
int netstack_ping(uint32_t target_ip, uint32_t *reply_ms);
int netstack_resolve_a(const char *host, uint32_t *out_ip);
int netstack_http_get(const char *host, const char *path, char *out,
                      size_t out_cap, size_t *out_len);
int netstack_github_probe(char *out, size_t out_cap, size_t *out_len);
int netstack_tls_probe(const char *host, char *out, size_t out_cap,
                       size_t *out_len);
int netstack_https_range_get(const char *host, const char *path,
                             uint64_t start, uint64_t end, void *out,
                             size_t out_cap, size_t *out_len,
                             char *diag, size_t diag_cap);
int netstack_github_tls_probe(char *out, size_t out_cap, size_t *out_len);
const netstack_status_t *netstack_get_status(void);
void netstack_format_status(char *buf, size_t size);
void netstack_format_route(char *buf, size_t size);
void netstack_format_dns(char *buf, size_t size);
void netstack_format_ipv4(uint32_t ip, char *buf, size_t size);
void netstack_tcp_server_init(netstack_tcp_server_t *srv, uint16_t port);
int netstack_tcp_server_poll(netstack_tcp_server_t *srv, const void *tx,
                             size_t tx_len, uint8_t *rx, size_t rx_cap,
                             size_t *rx_len);
int netstack_tcp_server_close(netstack_tcp_server_t *srv);
const char *netstack_tcp_server_state_name(const netstack_tcp_server_t *srv);

#endif /* _NETSTACK_H */
