/*
 * Orizon OS x86_64 - SSH service control
 */

#ifndef _SSH_H
#define _SSH_H

#include "types.h"

#define ORIZON_SSH_PORT 22
#define ORIZON_SSH_CONFIG_PATH "/system/ssh.conf"
#define ORIZON_SSH_LOG_PATH "/logs/ssh.log"

typedef struct {
  int enabled;
  int configured;
  int listening;
  int connected;
  int banner_sent;
  int client_banner_seen;
  int kex_seen;
  int disconnect_sent;
  uint16_t port;
  uint32_t remote_ip;
  uint16_t remote_port;
  uint32_t sessions;
  uint32_t packets_rx;
  uint32_t bytes_rx;
  uint32_t bytes_tx;
  uint32_t errors;
  char remote_banner[128];
  char status[160];
} ssh_status_t;

int ssh_start(char *report, size_t report_size);
int ssh_stop(char *report, size_t report_size);
int ssh_poll(void);
void ssh_format_status(char *buf, size_t size);
void ssh_format_report(char *buf, size_t size);
const ssh_status_t *ssh_get_status(void);

#endif /* _SSH_H */
