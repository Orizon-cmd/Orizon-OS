/*
 * Orizon OS x86_64 - Minimal IPv4 stack for the updater
 */

#ifndef _NETSTACK_H
#define _NETSTACK_H

#include "types.h"

typedef struct {
  int ipv4_ready;
  uint32_t ip;
  uint32_t subnet;
  uint32_t gateway;
  uint32_t dns;
  uint32_t last_resolved_ip;
  char last_host[96];
  char status[160];
} netstack_status_t;

int netstack_configure_ipv4(void);
int netstack_resolve_a(const char *host, uint32_t *out_ip);
int netstack_http_get(const char *host, const char *path, char *out,
                      size_t out_cap, size_t *out_len);
int netstack_github_probe(char *out, size_t out_cap, size_t *out_len);
const netstack_status_t *netstack_get_status(void);
void netstack_format_status(char *buf, size_t size);
void netstack_format_ipv4(uint32_t ip, char *buf, size_t size);

#endif /* _NETSTACK_H */
