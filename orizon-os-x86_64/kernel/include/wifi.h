/*
 * Orizon OS x86_64 - Wi-Fi hardware detection and driver staging
 */

#ifndef _WIFI_H
#define _WIFI_H

#include "types.h"

typedef struct {
  int present;
  int driver_ready;
  int associated;
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t vendor_id;
  uint16_t device_id;
  const char *chipset;
  const char *driver;
  const char *status;
  int firmware_present;
  int firmware_valid;
  size_t firmware_size;
  uint32_t firmware_version;
  uint32_t firmware_build;
  unsigned long firmware_tlv_count;
  unsigned long firmware_inst_bytes;
  unsigned long firmware_data_bytes;
  unsigned long firmware_section_count;
  unsigned long firmware_api_count;
  unsigned long firmware_capa_count;
  unsigned long firmware_parse_errors;
  char firmware_human[65];
  const char *firmware_name;
  const char *firmware_source;
} wifi_status_t;

int wifi_init(void);
void wifi_poll(void);
const wifi_status_t *wifi_get_status(void);
void wifi_format_status(char *buf, size_t size);
int wifi_firmware_probe(char *report, size_t report_size);
int wifi_scan(char *report, size_t report_size);
int wifi_connect(const char *ssid, const char *password, char *report,
                 size_t report_size);

#endif /* _WIFI_H */
