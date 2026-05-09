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

#endif /* _NETSTACK_H */
