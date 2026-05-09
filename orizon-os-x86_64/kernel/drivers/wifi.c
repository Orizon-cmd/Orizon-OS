/*
 * Orizon OS x86_64 - Wi-Fi driver staging
 *
 * This file intentionally stops before touching Intel Wi-Fi MMIO. Modern Intel
 * CNVi devices require firmware loading, DMA command queues, MAC/radio setup,
 * 802.11 management frames, and WPA handshakes. For real hardware safety, the
 * first milestone is reliable detection and user-visible diagnostics.
 */

#include "../include/wifi.h"
#include "../include/gui.h"
#include "../include/pci.h"
#include "../include/string.h"

static wifi_status_t wifi_status_state = {
    .present = 0,
    .driver_ready = 0,
    .associated = 0,
    .bus = 0,
    .device = 0,
    .function = 0,
    .vendor_id = 0,
    .device_id = 0,
    .chipset = "none",
    .driver = "none",
    .status = "wifi: not initialized",
};

static const char *intel_wifi_name(uint16_t device_id) {
  switch (device_id) {
    case 0x54F0:
      return "Intel Alder Lake-N CNVi Wi-Fi";
    case 0x4DF0:
      return "Intel Wi-Fi 6 AX201/AX211 family";
    case 0xA0F0:
      return "Intel Wi-Fi 6 AX201 family";
    case 0x51F0:
    case 0x7AF0:
      return "Intel Wi-Fi 6E AX211 family";
    case 0x2723:
      return "Intel Wi-Fi 6 AX200/AX201 family";
    case 0x2725:
      return "Intel Wi-Fi 6E AX210 family";
    default:
      return "Intel wireless controller";
  }
}

static void wifi_set_status(const char *status) {
  wifi_status_state.status = status;
  serial_puts("[wifi] ");
  serial_puts(status);
  serial_puts("\n");
}

int wifi_init(void) {
  pci_device_info_t devs[8];
  int count;

  if (wifi_status_state.present || wifi_status_state.driver_ready) {
    return wifi_status_state.driver_ready ? 0 : -1;
  }

  count = pci_scan_class(0x02, 0x80, 0xFF, devs, 8);
  if (count <= 0) {
    wifi_set_status("wifi: no PCI wireless controller detected");
    return -1;
  }

  for (int i = 0; i < count; i++) {
    if (devs[i].vendor_id != 0x8086) {
      continue;
    }

    wifi_status_state.present = 1;
    wifi_status_state.bus = devs[i].bus;
    wifi_status_state.device = devs[i].device;
    wifi_status_state.function = devs[i].function;
    wifi_status_state.vendor_id = devs[i].vendor_id;
    wifi_status_state.device_id = devs[i].device_id;
    wifi_status_state.chipset = intel_wifi_name(devs[i].device_id);
    wifi_status_state.driver = "intel-iwlwifi-stage0";
    wifi_status_state.driver_ready = 0;
    wifi_status_state.associated = 0;
    wifi_set_status(
        "wifi: Intel controller detected; firmware driver not ready yet");
    return -1;
  }

  wifi_status_state.present = 1;
  wifi_status_state.bus = devs[0].bus;
  wifi_status_state.device = devs[0].device;
  wifi_status_state.function = devs[0].function;
  wifi_status_state.vendor_id = devs[0].vendor_id;
  wifi_status_state.device_id = devs[0].device_id;
  wifi_status_state.chipset = "unsupported wireless controller";
  wifi_status_state.driver = "unsupported";
  wifi_set_status("wifi: wireless controller unsupported");
  return -1;
}

void wifi_poll(void) {
  /* Real RX/TX polling starts once the firmware-backed driver exists. */
}

const wifi_status_t *wifi_get_status(void) {
  if (!wifi_status_state.present && !wifi_status_state.driver_ready) {
    wifi_init();
  }
  return &wifi_status_state;
}

void wifi_format_status(char *buf, size_t size) {
  const wifi_status_t *s;

  if (!buf || size == 0) {
    return;
  }

  s = wifi_get_status();
  snprintf(buf, size,
           "driver=%s present=%s ready=%s associated=%s pci=%04x:%04x "
           "slot=%02x:%02x.%u chipset=%s status=%s",
           s->driver, s->present ? "yes" : "no",
           s->driver_ready ? "yes" : "no", s->associated ? "yes" : "no",
           s->vendor_id, s->device_id, s->bus, s->device,
           (unsigned int)s->function, s->chipset, s->status);
}

int wifi_scan(char *report, size_t report_size) {
  const wifi_status_t *s = wifi_get_status();

  if (!report || report_size == 0) {
    return -1;
  }

  if (!s->present) {
    snprintf(report, report_size,
             "wifi scan: no wireless controller detected\n");
    return -1;
  }

  snprintf(report, report_size,
           "wifi scan: %s detected at %02x:%02x.%u\n"
           "wifi scan: radio scan is not available yet\n"
           "next: load Intel firmware, start command queues, then add 802.11 scan\n",
           s->chipset, s->bus, s->device, (unsigned int)s->function);
  return -1;
}

int wifi_connect(const char *ssid, const char *password, char *report,
                 size_t report_size) {
  const wifi_status_t *s = wifi_get_status();
  UNUSED(password);

  if (!report || report_size == 0) {
    return -1;
  }

  if (!ssid || ssid[0] == '\0') {
    snprintf(report, report_size, "wifi connect: usage wifi connect <ssid>\n");
    return -1;
  }

  if (!s->present) {
    snprintf(report, report_size,
             "wifi connect: no wireless controller detected\n");
    return -1;
  }

  snprintf(report, report_size,
           "wifi connect: saved nothing yet, driver is not ready\n"
           "target ssid: %s\n"
           "detected: %s (%04x:%04x)\n"
           "blocked by: Intel firmware loader + WPA/802.11 association layer\n",
           ssid, s->chipset, s->vendor_id, s->device_id);
  return -1;
}
