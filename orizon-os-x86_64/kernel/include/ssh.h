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
  int server_kexinit_sent;
  int client_kexinit_seen;
  int client_kex_packet_seen;
  int kex_seen;
  int disconnect_sent;
  uint8_t last_packet_type;
  uint16_t port;
  uint32_t remote_ip;
  uint16_t remote_port;
  uint32_t sessions;
  uint32_t packets_rx;
  uint32_t ssh_packets_rx;
  uint32_t bytes_rx;
  uint32_t bytes_tx;
  uint32_t errors;
  char remote_banner[128];
  char kex_algorithm[64];
  char hostkey_algorithm[64];
  char cipher_c2s[64];
  char cipher_s2c[64];
  char mac_c2s[64];
  char mac_s2c[64];
  char compression_c2s[32];
  char compression_s2c[32];
  char client_kex_first[96];
  char client_hostkey_first[96];
  char status[160];
} ssh_status_t;

int ssh_start(char *report, size_t report_size);
int ssh_stop(char *report, size_t report_size);
int ssh_poll(void);
void ssh_format_status(char *buf, size_t size);
void ssh_format_report(char *buf, size_t size);
void ssh_format_algorithms(char *buf, size_t size);
const ssh_status_t *ssh_get_status(void);

#endif /* _SSH_H */
