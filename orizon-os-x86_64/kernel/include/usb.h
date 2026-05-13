/*
 * Minimal USB core interface (xHCI/EHCI + HID keyboard)
 */

#ifndef _USB_H
#define _USB_H

#include "types.h"

typedef void (*usb_keyboard_callback_t)(int key);

typedef struct {
  int present;
  char controller[8];
  uint8_t port;
  uint16_t vendor_id;
  uint16_t product_id;
  uint8_t device_class;
  uint8_t device_subclass;
  uint8_t device_protocol;
  uint8_t interface_class;
  uint8_t interface_subclass;
  uint8_t interface_protocol;
  uint8_t config_value;
  uint8_t control_interface;
  uint8_t data_interface;
  uint8_t bulk_in_ep;
  uint8_t bulk_out_ep;
  uint16_t bulk_in_mps;
  uint16_t bulk_out_mps;
  char driver_hint[32];
  char status[128];
} usb_net_info_t;

typedef struct {
  int present;
  char controller[8];
  uint8_t port;
  uint16_t vendor_id;
  uint16_t product_id;
  uint8_t device_class;
  uint8_t device_subclass;
  uint8_t device_protocol;
  uint8_t interface_class;
  uint8_t interface_subclass;
  uint8_t interface_protocol;
  uint8_t config_value;
  char hint[48];
  char status[96];
} usb_device_info_t;

void usb_init(void);
void usb_poll(void);
void usb_rescan(void);

void usb_set_keyboard_callback(usb_keyboard_callback_t cb);
void usb_hid_kbd_handle_report(const uint8_t *rep, int len);
void usb_submit_hid_report(const uint8_t *rep, int len);
void usb_note_device(const char *controller, uint8_t port,
                     const uint8_t *dev_desc, const uint8_t *cfg,
                     uint16_t cfg_len);
void usb_format_status(char *buf, size_t size);
void usb_format_device_status(char *buf, size_t size);
void usb_format_net_status(char *buf, size_t size);
void usb_format_port_status(char *buf, size_t size);
unsigned long usb_device_count(void);
int usb_net_present(void);
int usb_get_net_info(usb_net_info_t *out);

/* Expose controller init for staged debugging */
void usb_xhci_init(void);
void usb_ehci_init(void);
int usb_xhci_ready(void);
int usb_ehci_ready(void);
void usb_xhci_rescan(void);
void usb_ehci_rescan(void);
void usb_xhci_format_ports(char *buf, size_t size);
void usb_ehci_format_ports(char *buf, size_t size);

#endif /* _USB_H */
