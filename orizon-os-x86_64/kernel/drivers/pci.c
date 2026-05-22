/*
 * Minimal PCI enumeration (bus 0-255, device 0-31, function 0-7)
 */

#include "../include/pci.h"
#include "../include/string.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline void outl(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline uint32_t pci_config_addr(uint8_t bus, uint8_t device,
                                       uint8_t function, uint8_t offset) {
  return (uint32_t)(0x80000000U |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)device << 11) |
                    ((uint32_t)function << 8) |
                    (offset & 0xFC));
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  outl(PCI_CONFIG_ADDR, pci_config_addr(bus, device, function, offset));
  return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t device, uint8_t function,
                 uint8_t offset, uint32_t value) {
  outl(PCI_CONFIG_ADDR, pci_config_addr(bus, device, function, offset));
  outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  uint32_t val = pci_read32(bus, device, function, offset);
  return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

static uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  uint32_t val = pci_read32(bus, device, function, offset);
  return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

static void pci_fill_bars(pci_device_info_t *info) {
  for (int i = 0; i < 6; i++) {
    info->bar[i] = pci_read32(info->bus, info->device, info->function, 0x10 + i * 4);
  }
}

int pci_scan_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                   pci_device_info_t *out, int max_out) {
  int found = 0;
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t device = 0; device < 32; device++) {
      for (uint8_t function = 0; function < 8; function++) {
        uint16_t vendor = pci_read16(bus, device, function, 0x00);
        if (vendor == 0xFFFF) {
          if (function == 0) {
            break; /* no device */
          }
          continue;
        }

        uint8_t cls = pci_read8(bus, device, function, 0x0B);
        uint8_t sub = pci_read8(bus, device, function, 0x0A);
        uint8_t prog = pci_read8(bus, device, function, 0x09);

        if (cls == class_code && sub == subclass &&
            (prog_if == 0xFF || prog == prog_if)) {
          if (out && found < max_out) {
            pci_device_info_t *info = &out[found];
            info->bus = (uint8_t)bus;
            info->device = device;
            info->function = function;
            info->vendor_id = vendor;
            info->device_id = pci_read16(bus, device, function, 0x02);
            info->class_code = cls;
            info->subclass = sub;
            info->prog_if = prog;
            pci_fill_bars(info);
          }
          found++;
        }

        /* If not multi-function, skip remaining functions */
        if (function == 0) {
          uint8_t header = pci_read8(bus, device, function, 0x0E);
          if ((header & 0x80) == 0) {
            break;
          }
        }
      }
    }
  }
  return found;
}

int pci_scan_all(pci_device_info_t *out, int max_out) {
  int found = 0;
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t device = 0; device < 32; device++) {
      for (uint8_t function = 0; function < 8; function++) {
        uint16_t vendor = pci_read16(bus, device, function, 0x00);
        if (vendor == 0xFFFF) {
          if (function == 0) {
            break;
          }
          continue;
        }

        if (out && found < max_out) {
          pci_device_info_t *info = &out[found];
          info->bus = (uint8_t)bus;
          info->device = device;
          info->function = function;
          info->vendor_id = vendor;
          info->device_id = pci_read16(bus, device, function, 0x02);
          info->class_code = pci_read8(bus, device, function, 0x0B);
          info->subclass = pci_read8(bus, device, function, 0x0A);
          info->prog_if = pci_read8(bus, device, function, 0x09);
          pci_fill_bars(info);
        }
        found++;

        if (function == 0) {
          uint8_t header = pci_read8(bus, device, function, 0x0E);
          if ((header & 0x80) == 0) {
            break;
          }
        }
      }
    }
  }
  return found;
}

static void pci_diag_append(char *out, size_t out_size, size_t *used,
                            const char *text) {
  size_t len;
  if (!out || !used || !text || *used >= out_size) {
    return;
  }
  len = strlen(text);
  if (*used + len >= out_size) {
    len = out_size - *used - 1;
  }
  memcpy(out + *used, text, len);
  *used += len;
  out[*used] = '\0';
}

static const char *pci_storage_hint(const pci_device_info_t *dev) {
  if (!dev) {
    return "unknown";
  }
  if (dev->vendor_id == 0x1AF4 &&
      (dev->device_id == 0x1001 || dev->device_id == 0x1042)) {
    return "virtio-blk-storage";
  }
  if (dev->vendor_id == 0x1AF4 &&
      (dev->device_id == 0x1004 || dev->device_id == 0x1048)) {
    return "virtio-scsi-driver-needed";
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x08) {
    return dev->prog_if == 0x02 ? "nvme-controller" : "nvme-unusual-prog-if";
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x06) {
    return dev->prog_if == 0x01 ? "ahci-controller" : "sata-non-ahci";
  }
  if (dev->vendor_id == 0x8086 && dev->class_code == 0x01 &&
      dev->subclass == 0x04) {
    return "intel-rst-raid-remap";
  }
  if (dev->vendor_id == 0x8086 && dev->class_code == 0x08 &&
      (dev->subclass == 0x07 || dev->subclass == 0x80)) {
    return "intel-vmd-diagnostic-only";
  }
  if (dev->class_code == 0x08 && dev->subclass == 0x05) {
    return "sdhci-emmc-driver-needed";
  }
  if (dev->class_code == 0x01) {
    return "mass-storage-driver-needed";
  }
  return "not-storage";
}

void pci_format_diagnostics(char *out, size_t out_size) {
  pci_device_info_t devs[128];
  char line[224];
  size_t used = 0;
  int storage = 0;
  int vmd = 0;
  int total;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  total = pci_scan_all(devs, 128);
  snprintf(line, sizeof(line), "pci diagnostics:\n  devices=%d scanned=bus0-255\n",
           total);
  pci_diag_append(out, out_size, &used, line);
  for (int i = 0; i < total && i < 128; i++) {
    const pci_device_info_t *dev = &devs[i];
    int is_virtio_storage =
        dev->vendor_id == 0x1AF4 &&
        (dev->device_id == 0x1001 || dev->device_id == 0x1042 ||
         dev->device_id == 0x1004 || dev->device_id == 0x1048);
    int is_storage = is_virtio_storage || dev->class_code == 0x01 ||
                     (dev->class_code == 0x08 &&
                      (dev->subclass == 0x05 || dev->subclass == 0x07 ||
                       dev->subclass == 0x80));
    int is_bridge = dev->class_code == 0x06 && dev->subclass == 0x04;
    if (dev->vendor_id == 0x8086 &&
        ((dev->class_code == 0x01 && dev->subclass == 0x04) ||
         (dev->class_code == 0x08 &&
          (dev->subclass == 0x07 || dev->subclass == 0x80)))) {
      vmd++;
    }
    if (!is_storage && !is_bridge) {
      continue;
    }
    if (is_storage) {
      storage++;
      snprintf(line, sizeof(line),
               "  storage-candidate %02x:%02x.%u vendor=%04x device=%04x "
               "class=%02x/%02x/%02x hint=%s bars=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx\n",
               dev->bus, dev->device, dev->function, dev->vendor_id,
               dev->device_id, dev->class_code, dev->subclass, dev->prog_if,
               pci_storage_hint(dev), (unsigned long)dev->bar[0],
               (unsigned long)dev->bar[1], (unsigned long)dev->bar[2],
               (unsigned long)dev->bar[3], (unsigned long)dev->bar[4],
               (unsigned long)dev->bar[5]);
      pci_diag_append(out, out_size, &used, line);
    }
    if (is_bridge) {
      uint32_t buses = pci_read32(dev->bus, dev->device, dev->function, 0x18);
      snprintf(line, sizeof(line),
               "  pci-bridge %02x:%02x.%u primary=%lu secondary=%lu subordinate=%lu\n",
               dev->bus, dev->device, dev->function,
               (unsigned long)(buses & 0xffU),
               (unsigned long)((buses >> 8) & 0xffU),
               (unsigned long)((buses >> 16) & 0xffU));
      pci_diag_append(out, out_size, &used, line);
    }
  }
  if (storage == 0) {
    pci_diag_append(out, out_size, &used,
                    "  no PCI mass-storage candidates visible to the OS\n");
  }
  snprintf(line, sizeof(line),
           "  intel-vmd-rst=%s driver=not-implemented action=capture pci bars + storage diag\n",
           vmd ? "detected" : "not-detected");
  pci_diag_append(out, out_size, &used, line);
  if (total > 128) {
    pci_diag_append(out, out_size, &used, "  [truncated]\n");
  }
}
