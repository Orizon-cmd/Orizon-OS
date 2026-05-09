/*
 * Orizon OS x86_64 - Wi-Fi driver staging
 *
 * This file intentionally stops before touching Intel Wi-Fi MMIO. Modern Intel
 * CNVi devices require firmware loading, DMA command queues, MAC/radio setup,
 * 802.11 management frames, and WPA handshakes. For real hardware safety, the
 * first milestone is reliable detection and user-visible diagnostics.
 */

#include "../include/wifi.h"
#include "../include/bootinfo.h"
#include "../include/gui.h"
#include "../include/pci.h"
#include "../include/string.h"
#include "../include/vfs.h"

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
    .firmware_present = 0,
    .firmware_valid = 0,
    .firmware_size = 0,
    .firmware_version = 0,
    .firmware_build = 0,
    .firmware_tlv_count = 0,
    .firmware_inst_bytes = 0,
    .firmware_data_bytes = 0,
    .firmware_section_count = 0,
    .firmware_api_count = 0,
    .firmware_capa_count = 0,
    .firmware_parse_errors = 0,
    .firmware_human = "",
    .firmware_name = "none",
    .firmware_source = "none",
};

#define IWL_TLV_UCODE_MAGIC 0x0a4c5749U
#define IWL_TLV_HEADER_SIZE 88U
#define IWL_UCODE_TLV_INST 1U
#define IWL_UCODE_TLV_DATA 2U
#define IWL_UCODE_TLV_INIT 3U
#define IWL_UCODE_TLV_INIT_DATA 4U
#define IWL_UCODE_TLV_BOOT 5U
#define IWL_UCODE_TLV_WOWLAN_INST 16U
#define IWL_UCODE_TLV_WOWLAN_DATA 17U
#define IWL_UCODE_TLV_SEC_RT 19U
#define IWL_UCODE_TLV_SEC_INIT 20U
#define IWL_UCODE_TLV_SEC_WOWLAN 21U
#define IWL_UCODE_TLV_SECURE_SEC_RT 24U
#define IWL_UCODE_TLV_SECURE_SEC_INIT 25U
#define IWL_UCODE_TLV_SECURE_SEC_WOWLAN 26U
#define IWL_UCODE_TLV_API_CHANGES_SET 29U
#define IWL_UCODE_TLV_ENABLED_CAPABILITIES 30U

static const char *intel_fw_candidates[] = {
    "iwlwifi-so-a0-hr-b0-89.ucode", "iwlwifi-so-a0-hr-b0-86.ucode",
    "iwlwifi-so-a0-hr-b0-83.ucode", "iwlwifi-so-a0-hr-b0-77.ucode",
    "iwlwifi-so-a0-hr-b0-74.ucode", "iwlwifi-so-a0-gf-a0-89.ucode",
    "iwlwifi-so-a0-gf-a0-86.ucode", "iwlwifi-so-a0-gf-a0-83.ucode",
    "iwlwifi-QuZ-a0-hr-b0-77.ucode", "iwlwifi-QuZ-a0-hr-b0-74.ucode",
    "iwlwifi-cc-a0-77.ucode",       "iwlwifi-cc-a0-72.ucode",
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

static int name_has_ucode_suffix(const char *name) {
  size_t len = name ? strlen(name) : 0;
  return len > 6 && strcmp(name + len - 6, ".ucode") == 0;
}

static uint32_t wifi_read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static size_t wifi_align4(size_t value) {
  return (value + 3U) & ~(size_t)3U;
}

static int wifi_is_printable_ascii(uint8_t c) {
  return c >= 32 && c <= 126;
}

static void wifi_reset_firmware_parse(void) {
  wifi_status_state.firmware_valid = 0;
  wifi_status_state.firmware_version = 0;
  wifi_status_state.firmware_build = 0;
  wifi_status_state.firmware_tlv_count = 0;
  wifi_status_state.firmware_inst_bytes = 0;
  wifi_status_state.firmware_data_bytes = 0;
  wifi_status_state.firmware_section_count = 0;
  wifi_status_state.firmware_api_count = 0;
  wifi_status_state.firmware_capa_count = 0;
  wifi_status_state.firmware_parse_errors = 0;
  memset(wifi_status_state.firmware_human, 0,
         sizeof(wifi_status_state.firmware_human));
}

static int wifi_tlv_is_code_section(uint32_t type) {
  return type == IWL_UCODE_TLV_INST || type == IWL_UCODE_TLV_INIT ||
         type == IWL_UCODE_TLV_BOOT || type == IWL_UCODE_TLV_WOWLAN_INST ||
         type == IWL_UCODE_TLV_SEC_RT || type == IWL_UCODE_TLV_SEC_INIT ||
         type == IWL_UCODE_TLV_SEC_WOWLAN ||
         type == IWL_UCODE_TLV_SECURE_SEC_RT ||
         type == IWL_UCODE_TLV_SECURE_SEC_INIT ||
         type == IWL_UCODE_TLV_SECURE_SEC_WOWLAN;
}

static int wifi_tlv_is_data_section(uint32_t type) {
  return type == IWL_UCODE_TLV_DATA || type == IWL_UCODE_TLV_INIT_DATA ||
         type == IWL_UCODE_TLV_WOWLAN_DATA;
}

static void wifi_parse_firmware_blob(const void *data, size_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  size_t offset;
  uint32_t zero;
  uint32_t magic;

  wifi_reset_firmware_parse();

  if (!bytes || size < IWL_TLV_HEADER_SIZE) {
    wifi_status_state.firmware_parse_errors++;
    return;
  }

  zero = wifi_read_le32(bytes);
  magic = wifi_read_le32(bytes + 4);
  if (zero != 0 || magic != IWL_TLV_UCODE_MAGIC) {
    wifi_status_state.firmware_parse_errors++;
    return;
  }

  for (size_t i = 0; i < 64; i++) {
    uint8_t c = bytes[8 + i];
    wifi_status_state.firmware_human[i] =
        wifi_is_printable_ascii(c) ? (char)c : '\0';
    if (c == '\0') {
      break;
    }
  }
  for (int i = 63; i >= 0; i--) {
    if (wifi_status_state.firmware_human[i] == ' ') {
      wifi_status_state.firmware_human[i] = '\0';
      continue;
    }
    if (wifi_status_state.firmware_human[i] != '\0') {
      break;
    }
  }

  wifi_status_state.firmware_version = wifi_read_le32(bytes + 72);
  wifi_status_state.firmware_build = wifi_read_le32(bytes + 76);

  offset = IWL_TLV_HEADER_SIZE;
  while (offset + 8 <= size) {
    uint32_t type = wifi_read_le32(bytes + offset);
    uint32_t len = wifi_read_le32(bytes + offset + 4);
    size_t next;

    offset += 8;
    if ((size_t)len > size - offset) {
      wifi_status_state.firmware_parse_errors++;
      break;
    }

    wifi_status_state.firmware_tlv_count++;
    if (wifi_tlv_is_code_section(type)) {
      wifi_status_state.firmware_inst_bytes += len;
      wifi_status_state.firmware_section_count++;
    } else if (wifi_tlv_is_data_section(type)) {
      wifi_status_state.firmware_data_bytes += len;
      wifi_status_state.firmware_section_count++;
    } else if (type == IWL_UCODE_TLV_API_CHANGES_SET) {
      wifi_status_state.firmware_api_count++;
    } else if (type == IWL_UCODE_TLV_ENABLED_CAPABILITIES) {
      wifi_status_state.firmware_capa_count++;
    }

    next = offset + wifi_align4((size_t)len);
    if (next < offset || next > size) {
      wifi_status_state.firmware_parse_errors++;
      break;
    }
    offset = next;
  }

  if (offset != size && offset < size) {
    wifi_status_state.firmware_parse_errors++;
  }

  wifi_status_state.firmware_valid =
      wifi_status_state.firmware_tlv_count > 0 &&
      wifi_status_state.firmware_parse_errors == 0;
}

static int firmware_name_is_candidate(const char *name) {
  if (!name || !name[0]) {
    return 0;
  }
  for (size_t i = 0; i < sizeof(intel_fw_candidates) /
                             sizeof(intel_fw_candidates[0]);
       i++) {
    if (strcmp(name, intel_fw_candidates[i]) == 0) {
      return 1;
    }
  }
  return strstr(name, "iwlwifi-") != NULL && name_has_ucode_suffix(name);
}

static int wifi_find_firmware_module(void) {
  const void *addr = NULL;
  size_t size = 0;
  const char *path = NULL;
  const char *cmdline = NULL;

  for (size_t i = 0; i < sizeof(intel_fw_candidates) /
                             sizeof(intel_fw_candidates[0]);
       i++) {
    if (boot_find_module(intel_fw_candidates[i], &addr, &size, &path,
                         &cmdline) == 0) {
      UNUSED(cmdline);
      wifi_status_state.firmware_present = 1;
      wifi_status_state.firmware_size = size;
      wifi_status_state.firmware_name = intel_fw_candidates[i];
      wifi_status_state.firmware_source = path && path[0] ? path : "boot module";
      wifi_parse_firmware_blob(addr, size);
      return 0;
    }
  }

  if (boot_find_module("iwlwifi-", &addr, &size, &path, &cmdline) == 0 &&
      path && name_has_ucode_suffix(path)) {
    UNUSED(cmdline);
    wifi_status_state.firmware_present = 1;
    wifi_status_state.firmware_size = size;
    wifi_status_state.firmware_name = path;
    wifi_status_state.firmware_source = "boot module";
    wifi_parse_firmware_blob(addr, size);
    return 0;
  }
  return -1;
}

static int wifi_find_firmware_vfs_dir(const char *dir) {
  dirent_t entries[24];
  int count = vfs_readdir(dir, entries, 24);

  if (count <= 0) {
    return -1;
  }

  for (int i = 0; i < count; i++) {
    char path[MAX_PATH];
    size_t size = 0;
    int is_dir = 0;

    if (entries[i].type != 0 || !firmware_name_is_candidate(entries[i].name)) {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, entries[i].name);
    if (vfs_stat(path, &size, &is_dir) == 0 && !is_dir && size > 0) {
      wifi_status_state.firmware_present = 1;
      wifi_status_state.firmware_size = size;
      wifi_status_state.firmware_name = entries[i].name;
      wifi_status_state.firmware_source = dir;
      wifi_reset_firmware_parse();
      return 0;
    }
  }
  return -1;
}

static int wifi_find_firmware(void) {
  wifi_status_state.firmware_present = 0;
  wifi_status_state.firmware_valid = 0;
  wifi_status_state.firmware_size = 0;
  wifi_status_state.firmware_name = "none";
  wifi_status_state.firmware_source = "none";
  wifi_reset_firmware_parse();

  if (wifi_find_firmware_module() == 0) {
    return 0;
  }
  if (wifi_find_firmware_vfs_dir("/system/firmware") == 0 ||
      wifi_find_firmware_vfs_dir("/packages/firmware") == 0) {
    return 0;
  }
  return -1;
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
    if (wifi_find_firmware() == 0) {
      wifi_set_status(
          "wifi: Intel controller and firmware detected; command queues pending");
    } else {
      wifi_set_status(
          "wifi: Intel controller detected; firmware missing");
    }
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
           "slot=%02x:%02x.%u chipset=%s firmware=%s source=%s size=%lu "
           "valid=%s tlvs=%lu sections=%lu status=%s",
           s->driver, s->present ? "yes" : "no",
           s->driver_ready ? "yes" : "no", s->associated ? "yes" : "no",
           s->vendor_id, s->device_id, s->bus, s->device,
           (unsigned int)s->function, s->chipset,
           s->firmware_present ? s->firmware_name : "missing",
           s->firmware_source, (unsigned long)s->firmware_size,
           s->firmware_valid ? "yes" : "no", s->firmware_tlv_count,
           s->firmware_section_count, s->status);
}

int wifi_firmware_probe(char *report, size_t report_size) {
  const wifi_status_t *s;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi firmware: no Intel Wi-Fi controller detected\n");
    return -1;
  }

  if (s->firmware_present) {
    wifi_status_state.status =
        "wifi: firmware detected; command queues pending";
    if (s->firmware_valid) {
      snprintf(report, report_size,
               "wifi firmware: valid Intel TLV firmware\n"
               "name: %s\n"
               "source: %s\n"
               "size: %lu bytes\n"
               "human: %s\n"
               "version: 0x%08x build=%u\n"
               "tlvs: %lu sections=%lu api=%lu capa=%lu\n"
               "payload: inst=%lu bytes data=%lu bytes\n"
               "next: implement Intel DMA command queues and alive check\n",
               s->firmware_name, s->firmware_source,
               (unsigned long)s->firmware_size,
               s->firmware_human[0] ? s->firmware_human : "unknown",
               s->firmware_version, s->firmware_build, s->firmware_tlv_count,
               s->firmware_section_count, s->firmware_api_count,
               s->firmware_capa_count, s->firmware_inst_bytes,
               s->firmware_data_bytes);
      return 0;
    }

    snprintf(report, report_size,
             "wifi firmware: found %s, but TLV validation is unavailable\n"
             "source: %s\n"
             "size: %lu bytes\n"
             "parse-errors: %lu\n"
             "hint: firmware loaded from boot module gives full validation\n",
             s->firmware_name, s->firmware_source,
             (unsigned long)s->firmware_size, s->firmware_parse_errors);
    return -1;
  }

  snprintf(report, report_size,
           "wifi firmware: missing for %s (%04x:%04x)\n"
           "expected examples: %s, %s\n"
           "place firmware in /system/firmware or local ISO folder "
           "orizon-os-x86_64/firmware before build\n",
           s->chipset, s->vendor_id, s->device_id, intel_fw_candidates[0],
           intel_fw_candidates[1]);
  return -1;
}

int wifi_scan(char *report, size_t report_size) {
  const wifi_status_t *s;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi scan: no wireless controller detected\n");
    return -1;
  }

  if (!s->firmware_present) {
    snprintf(report, report_size,
             "wifi scan: %s detected, but firmware is missing\n"
             "run: wifi firmware\n",
             s->chipset);
    return -1;
  }

  snprintf(report, report_size,
           "wifi scan: %s detected at %02x:%02x.%u\n"
           "firmware: %s (%lu bytes, valid=%s)\n"
           "wifi scan: radio scan is not available yet\n"
           "next: start Intel command queues, then add 802.11 scan\n",
           s->chipset, s->bus, s->device, (unsigned int)s->function,
           s->firmware_name, (unsigned long)s->firmware_size,
           s->firmware_valid ? "yes" : "no");
  return -1;
}

int wifi_connect(const char *ssid, const char *password, char *report,
                 size_t report_size) {
  const wifi_status_t *s;
  UNUSED(password);

  if (!report || report_size == 0) {
    return -1;
  }

  if (!ssid || ssid[0] == '\0') {
    snprintf(report, report_size, "wifi connect: usage wifi connect <ssid>\n");
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

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
