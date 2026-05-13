/*
 * USB core glue (xHCI/EHCI + HID keyboard)
 */

#include "../include/usb.h"
#include "../include/gui.h"
#include "../include/string.h"

/* Forward declarations */
void usb_xhci_init(void);
void usb_xhci_poll(void);
int usb_xhci_ready(void);

void usb_ehci_init(void);
void usb_ehci_poll(void);
int usb_ehci_ready(void);

static usb_keyboard_callback_t keyboard_cb = 0;
static const int usb_report_size = 8;

#define USB_REPORT_QUEUE_SIZE 32
typedef struct {
  uint8_t data[8];
  int len;
} usb_report_t;

static usb_report_t report_queue[USB_REPORT_QUEUE_SIZE];
static int report_head = 0;
static int report_tail = 0;
static unsigned long report_seen = 0;
static unsigned long key_seen = 0;
static unsigned long report_dropped = 0;
static unsigned long usb_seen_devices = 0;
static usb_device_info_t usb_last_device;
static usb_net_info_t usb_net;

static uint16_t usb_le16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static int usb_id_is_realtek_lan(uint16_t vid, uint16_t pid) {
  if (vid != 0x0bda) {
    return 0;
  }
  return pid == 0x8152 || pid == 0x8153 || pid == 0x8155 ||
         pid == 0x8156 || pid == 0x8053;
}

static int usb_id_is_asix_lan(uint16_t vid, uint16_t pid) {
  if (vid != 0x0b95) {
    return 0;
  }
  return pid == 0x1720 || pid == 0x1780 || pid == 0x178a ||
         pid == 0x1790 || pid == 0x7720 || pid == 0x772a ||
         pid == 0x772b || pid == 0x7e2b;
}

static int usb_id_is_smsc_lan(uint16_t vid) {
  return vid == 0x0424;
}

static int usb_class_is_net(uint8_t cls, uint8_t sub, uint8_t proto) {
  if (cls == 0x02) {
    return 1; /* CDC control: ECM/NCM and related USB networking classes. */
  }
  if (cls == 0x0a) {
    return 1; /* CDC data interface. */
  }
  if (cls == 0xe0 && sub == 0x01 && proto == 0x03) {
    return 1; /* RNDIS-style network adapters. */
  }
  return 0;
}

static const char *usb_net_hint_for(uint16_t vid, uint16_t pid,
                                    uint8_t cls, uint8_t sub,
                                    uint8_t proto) {
  if (usb_id_is_realtek_lan(vid, pid)) {
    return "realtek-rtl815x";
  }
  if (usb_id_is_asix_lan(vid, pid)) {
    return "asix-ax88xxx";
  }
  if (usb_id_is_smsc_lan(vid)) {
    return "smsc-lan95xx";
  }
  if (cls == 0x02 && sub == 0x06) {
    return "cdc-ecm";
  }
  if (cls == 0x02 && sub == 0x0d) {
    return "cdc-ncm";
  }
  if (cls == 0xe0 && sub == 0x01 && proto == 0x03) {
    return "rndis";
  }
  if (cls == 0x0a) {
    return "cdc-data";
  }
  return "usb-net";
}

static const char *usb_device_hint_for(uint8_t dev_cls, uint8_t if_cls,
                                       uint16_t vid, uint16_t pid) {
  if (usb_id_is_realtek_lan(vid, pid) || usb_id_is_asix_lan(vid, pid) ||
      usb_id_is_smsc_lan(vid) || usb_class_is_net(dev_cls, 0, 0) ||
      usb_class_is_net(if_cls, 0, 0)) {
    return "usb-ethernet-candidate";
  }
  if (dev_cls == 0x09 || if_cls == 0x09) {
    return "usb-hub";
  }
  if (dev_cls == 0x03 || if_cls == 0x03) {
    return "usb-hid";
  }
  if (dev_cls == 0xef) {
    return "usb-composite";
  }
  if (dev_cls == 0xff || if_cls == 0xff) {
    return "vendor-specific";
  }
  return "usb-device";
}

void usb_set_keyboard_callback(usb_keyboard_callback_t cb) {
  keyboard_cb = cb;
}

void usb_hid_handle_key(int key) {
  key_seen++;
  if (keyboard_cb) {
    keyboard_cb(key);
  }
}

void usb_submit_hid_report(const uint8_t *rep, int len) {
  if (!rep || len <= 0) {
    return;
  }
  report_seen++;
  int next = (report_head + 1) % USB_REPORT_QUEUE_SIZE;
  if (next == report_tail) {
    report_tail = (report_tail + 1) % USB_REPORT_QUEUE_SIZE;
    report_dropped++;
    if ((report_dropped & 0x3F) == 1) {
      serial_puts("[USB] HID queue overflow; oldest report replaced\n");
    }
  }
  int copy_len = len;
  if (copy_len > usb_report_size) {
    copy_len = usb_report_size;
  }
  memset(report_queue[report_head].data, 0, sizeof(report_queue[report_head].data));
  for (int i = 0; i < copy_len; i++) {
    report_queue[report_head].data[i] = rep[i];
  }
  report_queue[report_head].len = copy_len;
  report_head = next;
}

void usb_note_device(const char *controller, uint8_t port,
                     const uint8_t *dev_desc, const uint8_t *cfg,
                     uint16_t cfg_len) {
  usb_net_info_t candidate;
  usb_device_info_t dev;
  uint16_t off = 0;
  int matched = 0;
  int vendor_match;
  int current_net_iface = 0;
  uint8_t cur_iface = 0xff;
  uint8_t cur_cls = 0;
  uint8_t cur_sub = 0;
  uint8_t cur_proto = 0;

  if (!dev_desc || !cfg || cfg_len < 9) {
    return;
  }

  memset(&dev, 0, sizeof(dev));
  dev.present = 1;
  dev.port = port;
  dev.vendor_id = usb_le16(dev_desc + 8);
  dev.product_id = usb_le16(dev_desc + 10);
  dev.device_class = dev_desc[4];
  dev.device_subclass = dev_desc[5];
  dev.device_protocol = dev_desc[6];
  snprintf(dev.controller, sizeof(dev.controller), "%s",
           controller ? controller : "usb");

  memset(&candidate, 0, sizeof(candidate));
  candidate.present = 1;
  candidate.port = port;
  candidate.vendor_id = dev.vendor_id;
  candidate.product_id = dev.product_id;
  candidate.device_class = dev.device_class;
  candidate.device_subclass = dev.device_subclass;
  candidate.device_protocol = dev.device_protocol;
  candidate.control_interface = 0xff;
  candidate.data_interface = 0xff;
  snprintf(candidate.controller, sizeof(candidate.controller), "%s",
           controller ? controller : "usb");

  vendor_match =
      usb_id_is_realtek_lan(candidate.vendor_id, candidate.product_id) ||
      usb_id_is_asix_lan(candidate.vendor_id, candidate.product_id) ||
      usb_id_is_smsc_lan(candidate.vendor_id);
  if (usb_class_is_net(candidate.device_class, candidate.device_subclass,
                       candidate.device_protocol)) {
    matched = 1;
    candidate.interface_class = candidate.device_class;
    candidate.interface_subclass = candidate.device_subclass;
    candidate.interface_protocol = candidate.device_protocol;
  }

  while (off + 2 <= cfg_len) {
    uint8_t len = cfg[off];
    uint8_t type = cfg[off + 1];
    if (len == 0 || off + len > cfg_len) {
      break;
    }

    if (type == 2 && len >= 9) {
      candidate.config_value = cfg[off + 5];
      dev.config_value = cfg[off + 5];
    } else if (type == 4 && len >= 9) {
      cur_iface = cfg[off + 2];
      cur_cls = cfg[off + 5];
      cur_sub = cfg[off + 6];
      cur_proto = cfg[off + 7];
      if (dev.interface_class == 0) {
        dev.interface_class = cur_cls;
        dev.interface_subclass = cur_sub;
        dev.interface_protocol = cur_proto;
      }
      current_net_iface = vendor_match ||
                          usb_class_is_net(cur_cls, cur_sub, cur_proto);
      if (usb_class_is_net(cur_cls, cur_sub, cur_proto)) {
        matched = 1;
        if (candidate.interface_class == 0 || cur_cls != 0x0a) {
          candidate.interface_class = cur_cls;
          candidate.interface_subclass = cur_sub;
          candidate.interface_protocol = cur_proto;
          candidate.control_interface = cur_iface;
        }
        if (cur_cls == 0x0a) {
          candidate.data_interface = cur_iface;
        }
      }
    } else if (type == 5 && len >= 7 && current_net_iface) {
      uint8_t addr = cfg[off + 2];
      uint8_t attr = cfg[off + 3] & 0x03;
      uint16_t mps = usb_le16(cfg + off + 4);
      if (attr == 2) {
        if ((addr & 0x80) && candidate.bulk_in_ep == 0) {
          candidate.bulk_in_ep = addr;
          candidate.bulk_in_mps = mps;
        } else if (!(addr & 0x80) && candidate.bulk_out_ep == 0) {
          candidate.bulk_out_ep = addr;
          candidate.bulk_out_mps = mps;
        }
      }
    }

    off = (uint16_t)(off + len);
  }

  snprintf(dev.hint, sizeof(dev.hint), "%s",
           usb_device_hint_for(dev.device_class, dev.interface_class,
                               dev.vendor_id, dev.product_id));
  snprintf(dev.status, sizeof(dev.status), "%s",
           (dev.device_class == 0x09 || dev.interface_class == 0x09)
               ? "USB hub detected; downstream hub driver pending"
               : "USB device descriptor captured");
  usb_last_device = dev;
  usb_seen_devices++;

  if (!matched && !vendor_match) {
    return;
  }

  if (candidate.interface_class == 0 && vendor_match) {
    candidate.interface_class = cur_cls;
    candidate.interface_subclass = cur_sub;
    candidate.interface_protocol = cur_proto;
    candidate.control_interface = cur_iface;
  }

  snprintf(candidate.driver_hint, sizeof(candidate.driver_hint), "%s",
           usb_net_hint_for(candidate.vendor_id, candidate.product_id,
                            candidate.interface_class,
                            candidate.interface_subclass,
                            candidate.interface_protocol));
  snprintf(candidate.status, sizeof(candidate.status),
           "USB Ethernet detected; class driver pending, DHCP unavailable");

  if (!usb_net.present ||
      (!usb_net.bulk_in_ep && candidate.bulk_in_ep) ||
      (!usb_net.bulk_out_ep && candidate.bulk_out_ep)) {
    usb_net = candidate;
    serial_puts("[USB] network adapter detected: ");
    serial_puts(candidate.driver_hint);
    serial_puts("\n");
  }
}

void usb_format_status(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  snprintf(buf, size,
           "xhci=%s ehci=%s hid_reports=%lu hid_keys=%lu queue_drops=%lu "
           "devices=%lu usb_net=%s",
           usb_xhci_ready() ? "ready" : "no",
           usb_ehci_ready() ? "ready" : "no", report_seen, key_seen,
           report_dropped, usb_seen_devices,
           usb_net.present ? usb_net.driver_hint : "none");
}

void usb_format_device_status(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  if (!usb_last_device.present) {
    snprintf(buf, size, "usb-device count=0 status=no descriptor captured");
    return;
  }
  snprintf(buf, size,
           "usb-device count=%lu last=%s port=%u vid=%04x pid=%04x "
           "dev-class=%02x/%02x/%02x iface=%02x/%02x/%02x cfg=%u "
           "hint=%s status=%s",
           usb_seen_devices, usb_last_device.controller, usb_last_device.port,
           usb_last_device.vendor_id, usb_last_device.product_id,
           usb_last_device.device_class, usb_last_device.device_subclass,
           usb_last_device.device_protocol, usb_last_device.interface_class,
           usb_last_device.interface_subclass, usb_last_device.interface_protocol,
           usb_last_device.config_value, usb_last_device.hint,
           usb_last_device.status);
}

void usb_format_net_status(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  if (!usb_net.present) {
    snprintf(buf, size, "usb-net present=no status=no USB Ethernet adapter detected");
    return;
  }
  snprintf(buf, size,
           "usb-net present=yes controller=%s port=%u vid=%04x pid=%04x "
           "dev-class=%02x/%02x/%02x iface=%02x/%02x/%02x cfg=%u "
           "ctrl-if=%u data-if=%u bulk-in=%02x/%u bulk-out=%02x/%u "
           "driver=%s status=%s",
           usb_net.controller, usb_net.port, usb_net.vendor_id,
           usb_net.product_id, usb_net.device_class, usb_net.device_subclass,
           usb_net.device_protocol, usb_net.interface_class,
           usb_net.interface_subclass, usb_net.interface_protocol,
           usb_net.config_value, usb_net.control_interface,
           usb_net.data_interface, usb_net.bulk_in_ep, usb_net.bulk_in_mps,
           usb_net.bulk_out_ep, usb_net.bulk_out_mps, usb_net.driver_hint,
           usb_net.status);
}

int usb_net_present(void) {
  return usb_net.present;
}

unsigned long usb_device_count(void) {
  return usb_seen_devices;
}

int usb_get_net_info(usb_net_info_t *out) {
  if (!out || !usb_net.present) {
    return -1;
  }
  *out = usb_net;
  return 0;
}

void usb_format_port_status(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  if (usb_xhci_ready()) {
    usb_xhci_format_ports(buf, size);
    return;
  }
  if (usb_ehci_ready()) {
    usb_ehci_format_ports(buf, size);
    return;
  }
  snprintf(buf, size, "usb-ports xhci=no ehci=no");
}

void usb_rescan(void) {
  if (usb_xhci_ready()) {
    usb_xhci_rescan();
  }
  if (usb_ehci_ready()) {
    usb_ehci_rescan();
  }
}

void usb_init(void) {
  usb_xhci_init();
  usb_ehci_init();
}

void usb_poll(void) {
  if (usb_xhci_ready()) {
    usb_xhci_poll();
  }
  if (usb_ehci_ready()) {
    usb_ehci_poll();
  }

  while (report_tail != report_head) {
    usb_report_t *rep = &report_queue[report_tail];
    usb_hid_kbd_handle_report(rep->data, rep->len);
    report_tail = (report_tail + 1) % USB_REPORT_QUEUE_SIZE;
  }
}
