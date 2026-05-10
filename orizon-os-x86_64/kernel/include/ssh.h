/*
 * Orizon OS x86_64 - SSH service control
 */

#ifndef _SSH_H
#define _SSH_H

#include "types.h"

#define ORIZON_SSH_PORT 22
#define ORIZON_SSH_CONFIG_PATH "/system/ssh.conf"
#define ORIZON_SSH_HOSTKEY_PATH "/system/ssh_host_rsa.key"
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
  int ecdh_ready;
  int ecdh_reply_sent;
  int newkeys_sent;
  int client_newkeys_seen;
  int traffic_keys_ready;
  int encrypted_packet_seen;
  int service_accept_sent;
  int userauth_request_seen;
  int auth_configured;
  int authenticated;
  int auth_failure_sent;
  uint32_t auth_failures;
  uint64_t auth_lockout_until;
  uint32_t max_auth_attempts;
  uint32_t auth_lockout_seconds;
  int hostkey_loaded;
  int hostkey_persistent;
  int hostkey_bootstrap;
  int channel_open_seen;
  int channel_open_confirm_sent;
  int shell_ready;
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
  char client_public_sha256[65];
  char hostkey_sha256[65];
  char server_public_sha256[65];
  char shared_secret_sha256[65];
  char exchange_hash_sha256[65];
  char signature_sha256[65];
  char client_to_server_key_sha256[65];
  char server_to_client_key_sha256[65];
  char client_to_server_mac_sha256[65];
  char server_to_client_mac_sha256[65];
  char auth_user[32];
  char auth_method[32];
  char hostkey_source[48];
  char hostkey_status[128];
  char status[160];
} ssh_status_t;

int ssh_set_password(const char *password, char *report, size_t report_size);
int ssh_disable_password(char *report, size_t report_size);
int ssh_reload_config(char *report, size_t report_size);
int ssh_clear_lockout(char *report, size_t report_size);
int ssh_set_auth_policy(uint32_t max_attempts, uint32_t lockout_seconds,
                        char *report, size_t report_size);
int ssh_reset_auth_policy(char *report, size_t report_size);
int ssh_reload_hostkey(char *report, size_t report_size);
int ssh_reset_hostkey(char *report, size_t report_size);
int ssh_start(char *report, size_t report_size);
int ssh_stop(char *report, size_t report_size);
int ssh_poll(void);
void ssh_format_status(char *buf, size_t size);
void ssh_format_report(char *buf, size_t size);
void ssh_format_algorithms(char *buf, size_t size);
void ssh_format_auth(char *buf, size_t size);
void ssh_format_hostkey(char *buf, size_t size);
const ssh_status_t *ssh_get_status(void);

#endif /* _SSH_H */
