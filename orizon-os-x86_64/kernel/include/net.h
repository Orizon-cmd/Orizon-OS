/*
 * Orizon OS x86_64 - Minimal Ethernet Network Layer
 */

#ifndef _NET_H
#define _NET_H

#include "types.h"

typedef struct {
  int present;
  int initialized;
  int link_up;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t mac[6];
  const char *driver;
  const char *status;
} net_device_status_t;

int net_init(void);
void net_poll(void);
int net_link_up(void);
const net_device_status_t *net_get_status(void);
const char *net_status(void);
void net_format_status(char *buf, size_t size);

int net_send_ethernet(const uint8_t dst_mac[6], uint16_t ether_type,
                      const void *payload, size_t payload_len);
int net_recv_ethernet(uint8_t src_mac[6], uint16_t *ether_type,
                      void *payload, size_t payload_cap);

#endif /* _NET_H */
