/*
 * Orizon OS x86_64 - Intel LPSS DesignWare I2C + I2C-HID probe
 *
 * This is intentionally conservative. It brings up the Alder Lake-N I2C
 * controllers found in the Lenovo 500w Yoga Gen 4 and probes HID-over-I2C
 * devices discovered from the laptop's Linux inventory. Full multitouch parsing
 * comes later; today we get real bus/device visibility and boot-safe polling.
 */

#include "../include/gui.h"
#include "../include/i2c_hid.h"
#include "../include/mmio.h"
#include "../include/pci.h"
#include "../include/ps2.h"
#include "../include/string.h"

#define I2C_MAX_CONTROLLERS 4
#define I2C_HID_MAX_DEVICES 4
#define I2C_HID_MAX_REPORT 64

#define DW_IC_CON 0x00
#define DW_IC_TAR 0x04
#define DW_IC_DATA_CMD 0x10
#define DW_IC_SS_SCL_HCNT 0x14
#define DW_IC_SS_SCL_LCNT 0x18
#define DW_IC_FS_SCL_HCNT 0x1C
#define DW_IC_FS_SCL_LCNT 0x20
#define DW_IC_INTR_MASK 0x30
#define DW_IC_RAW_INTR_STAT 0x34
#define DW_IC_RX_TL 0x38
#define DW_IC_TX_TL 0x3C
#define DW_IC_CLR_INTR 0x40
#define DW_IC_ENABLE 0x6C
#define DW_IC_STATUS 0x70
#define DW_IC_TXFLR 0x74
#define DW_IC_RXFLR 0x78
#define DW_IC_TX_ABRT_SOURCE 0x80
#define DW_IC_ENABLE_STATUS 0x9C
#define DW_IC_COMP_TYPE 0xFC

#define DW_CON_MASTER (1U << 0)
#define DW_CON_SPEED_FAST (2U << 1)
#define DW_CON_RESTART_EN (1U << 5)
#define DW_CON_SLAVE_DISABLE (1U << 6)

#define DW_DATA_CMD_READ (1U << 8)
#define DW_DATA_CMD_STOP (1U << 9)
#define DW_DATA_CMD_RESTART (1U << 10)

#define DW_STATUS_ACTIVITY (1U << 0)
#define DW_STATUS_TFNF (1U << 1)
#define DW_STATUS_RFNE (1U << 3)

typedef struct {
  int present;
  int ready;
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t pci_device_id;
  uint64_t mmio;
  const char *name;
  unsigned long transfers;
  unsigned long errors;
} i2c_controller_t;

typedef struct {
  int present;
  int ready;
  int controller;
  uint8_t address;
  const char *name;
  uint16_t hid_desc_reg;
  uint16_t report_desc_reg;
  uint16_t input_reg;
  uint16_t command_reg;
  uint16_t data_reg;
  uint16_t max_input_len;
  uint16_t vendor_id;
  uint16_t product_id;
  unsigned long reports;
  unsigned long relative_reports;
  unsigned long errors;
} i2c_hid_device_t;

static i2c_controller_t controllers[I2C_MAX_CONTROLLERS];
static i2c_hid_device_t devices[I2C_HID_MAX_DEVICES];
static int controller_count = 0;
static int device_count = 0;
static int initialized = 0;
static unsigned long poll_count = 0;
static const char *i2c_status = "i2c-hid: not initialized";

static uint32_t dw_read32(const i2c_controller_t *ctl, uint32_t reg) {
  return *(volatile uint32_t *)(uintptr_t)(ctl->mmio + reg);
}

static void dw_write32(const i2c_controller_t *ctl, uint32_t reg,
                       uint32_t value) {
  *(volatile uint32_t *)(uintptr_t)(ctl->mmio + reg) = value;
}

static int dw_wait_bits(const i2c_controller_t *ctl, uint32_t reg,
                        uint32_t mask, uint32_t expect, int loops) {
  while (loops-- > 0) {
    if ((dw_read32(ctl, reg) & mask) == expect) {
      return 0;
    }
    __asm__ volatile("pause");
  }
  return -1;
}

static int dw_set_enabled(i2c_controller_t *ctl, int enabled) {
  dw_write32(ctl, DW_IC_ENABLE, enabled ? 1U : 0U);
  if (dw_wait_bits(ctl, DW_IC_ENABLE_STATUS, 1U, enabled ? 1U : 0U,
                   200000) != 0) {
    ctl->errors++;
    return -1;
  }
  return 0;
}

static int dw_wait_idle(const i2c_controller_t *ctl) {
  return dw_wait_bits(ctl, DW_IC_STATUS, DW_STATUS_ACTIVITY, 0, 200000);
}

static int dw_init_controller(i2c_controller_t *ctl) {
  if (!ctl || !ctl->mmio) {
    return -1;
  }

  if (dw_read32(ctl, DW_IC_COMP_TYPE) == 0) {
    ctl->errors++;
    return -1;
  }

  if (dw_set_enabled(ctl, 0) != 0) {
    return -1;
  }
  dw_write32(ctl, DW_IC_CON,
             DW_CON_MASTER | DW_CON_SPEED_FAST | DW_CON_RESTART_EN |
                 DW_CON_SLAVE_DISABLE);
  dw_write32(ctl, DW_IC_INTR_MASK, 0);
  dw_write32(ctl, DW_IC_RX_TL, 0);
  dw_write32(ctl, DW_IC_TX_TL, 0);

  /* Safe fallbacks for DesignWare instances whose firmware left counts at 0. */
  if (dw_read32(ctl, DW_IC_SS_SCL_HCNT) == 0) {
    dw_write32(ctl, DW_IC_SS_SCL_HCNT, 0x190);
    dw_write32(ctl, DW_IC_SS_SCL_LCNT, 0x1D6);
  }
  if (dw_read32(ctl, DW_IC_FS_SCL_HCNT) == 0) {
    dw_write32(ctl, DW_IC_FS_SCL_HCNT, 0x3C);
    dw_write32(ctl, DW_IC_FS_SCL_LCNT, 0x82);
  }

  if (dw_set_enabled(ctl, 1) != 0) {
    return -1;
  }
  ctl->ready = 1;
  return 0;
}

static int dw_set_target(i2c_controller_t *ctl, uint8_t addr) {
  if (dw_set_enabled(ctl, 0) != 0) {
    return -1;
  }
  dw_write32(ctl, DW_IC_TAR, addr & 0x7FU);
  (void)dw_read32(ctl, DW_IC_CLR_INTR);
  return dw_set_enabled(ctl, 1);
}

static int dw_wait_tx_room(const i2c_controller_t *ctl) {
  return dw_wait_bits(ctl, DW_IC_STATUS, DW_STATUS_TFNF, DW_STATUS_TFNF,
                      200000);
}

static int dw_wait_rx_data(const i2c_controller_t *ctl) {
  return dw_wait_bits(ctl, DW_IC_STATUS, DW_STATUS_RFNE, DW_STATUS_RFNE,
                      200000);
}

static int i2c_read_reg(i2c_controller_t *ctl, uint8_t addr, uint16_t reg,
                        uint8_t *buf, int len) {
  if (!ctl || !ctl->ready || !buf || len <= 0 || len > I2C_HID_MAX_REPORT) {
    return -1;
  }
  if (dw_wait_idle(ctl) != 0 || dw_set_target(ctl, addr) != 0) {
    ctl->errors++;
    return -1;
  }

  if (dw_wait_tx_room(ctl) != 0) goto fail;
  dw_write32(ctl, DW_IC_DATA_CMD, reg & 0xFFU);
  if (dw_wait_tx_room(ctl) != 0) goto fail;
  dw_write32(ctl, DW_IC_DATA_CMD, (reg >> 8) & 0xFFU);

  for (int i = 0; i < len; i++) {
    uint32_t cmd = DW_DATA_CMD_READ;
    if (i == 0) {
      cmd |= DW_DATA_CMD_RESTART;
    }
    if (i == len - 1) {
      cmd |= DW_DATA_CMD_STOP;
    }
    if (dw_wait_tx_room(ctl) != 0) goto fail;
    dw_write32(ctl, DW_IC_DATA_CMD, cmd);
  }

  for (int i = 0; i < len; i++) {
    if (dw_wait_rx_data(ctl) != 0) goto fail;
    buf[i] = (uint8_t)(dw_read32(ctl, DW_IC_DATA_CMD) & 0xFFU);
  }

  ctl->transfers++;
  return 0;

fail:
  ctl->errors++;
  (void)dw_read32(ctl, DW_IC_TX_ABRT_SOURCE);
  (void)dw_read32(ctl, DW_IC_CLR_INTR);
  return -1;
}

static int hid_desc_valid(const uint8_t *d) {
  uint16_t desc_len = (uint16_t)(d[0] | (d[1] << 8));
  uint16_t version = (uint16_t)(d[2] | (d[3] << 8));
  uint16_t report_len = (uint16_t)(d[4] | (d[5] << 8));
  uint16_t max_input = (uint16_t)(d[10] | (d[11] << 8));

  return desc_len >= 30 && desc_len <= 256 && version >= 0x0100 &&
         report_len > 0 && report_len <= 4096 && max_input >= 4;
}

static uint16_t le16_at(const uint8_t *d, int off) {
  return (uint16_t)(d[off] | (d[off + 1] << 8));
}

static int probe_hid_device(i2c_hid_device_t *dev) {
  static const uint16_t candidate_regs[] = {0x0001, 0x0000, 0x0020, 0x002C};
  uint8_t desc[30];

  if (!dev || dev->controller < 0 || dev->controller >= controller_count) {
    return -1;
  }
  i2c_controller_t *ctl = &controllers[dev->controller];
  if (!ctl->ready) {
    return -1;
  }

  for (size_t i = 0; i < sizeof(candidate_regs) / sizeof(candidate_regs[0]);
       i++) {
    memset(desc, 0, sizeof(desc));
    if (i2c_read_reg(ctl, dev->address, candidate_regs[i], desc,
                     sizeof(desc)) != 0) {
      continue;
    }
    if (!hid_desc_valid(desc)) {
      continue;
    }

    dev->hid_desc_reg = candidate_regs[i];
    dev->report_desc_reg = le16_at(desc, 6);
    dev->input_reg = le16_at(desc, 8);
    dev->max_input_len = le16_at(desc, 10);
    dev->command_reg = le16_at(desc, 16);
    dev->data_reg = le16_at(desc, 18);
    dev->vendor_id = le16_at(desc, 20);
    dev->product_id = le16_at(desc, 22);
    if (dev->max_input_len > I2C_HID_MAX_REPORT) {
      dev->max_input_len = I2C_HID_MAX_REPORT;
    }
    dev->ready = 1;
    return 0;
  }

  dev->errors++;
  return -1;
}

static int add_controller(const pci_device_info_t *pci, const char *name) {
  if (!pci || controller_count >= I2C_MAX_CONTROLLERS) {
    return -1;
  }
  uint64_t bar = pci->bar[0] & ~0xFULL;
  if (pci->bar[0] & 0x04) {
    bar |= ((uint64_t)pci->bar[1]) << 32;
  }
  if (!bar || (pci->bar[0] & 0x01)) {
    return -1;
  }

  i2c_controller_t *ctl = &controllers[controller_count];
  memset(ctl, 0, sizeof(*ctl));
  ctl->present = 1;
  ctl->bus = pci->bus;
  ctl->device = pci->device;
  ctl->function = pci->function;
  ctl->pci_device_id = pci->device_id;
  ctl->name = name;
  ctl->mmio = mmio_map_range(bar, 0x1000);
  if (!ctl->mmio) {
    ctl->errors++;
  }

  uint32_t cmd = pci_read32(pci->bus, pci->device, pci->function, 0x04);
  cmd |= (1U << 1) | (1U << 2);
  pci_write32(pci->bus, pci->device, pci->function, 0x04, cmd);

  controller_count++;
  return (int)(controller_count - 1);
}

static int controller_for_pci_id(uint16_t pci_device_id) {
  for (int i = 0; i < controller_count; i++) {
    if (controllers[i].pci_device_id == pci_device_id) {
      return i;
    }
  }
  return -1;
}

static void add_lenovo_hid_targets(void) {
  typedef struct {
    uint16_t pci_device_id;
    uint8_t address;
    const char *name;
  } target_t;
  static const target_t targets[] = {
      {0x54E9, 0x15, "ELAN0647 touchpad"},
      {0x54E8, 0x0A, "WCOM508E touch/stylus"},
  };

  for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
    int ctl = controller_for_pci_id(targets[i].pci_device_id);
    if (ctl < 0 || device_count >= I2C_HID_MAX_DEVICES) {
      continue;
    }
    i2c_hid_device_t *dev = &devices[device_count++];
    memset(dev, 0, sizeof(*dev));
    dev->present = 1;
    dev->controller = ctl;
    dev->address = targets[i].address;
    dev->name = targets[i].name;
    (void)probe_hid_device(dev);
  }
}

static void parse_basic_mouse_report(i2c_hid_device_t *dev, const uint8_t *buf,
                                     int len) {
  if (!dev || !buf || len < 5) {
    return;
  }

  /*
   * This only accepts compact relative mouse-style reports. Lenovo's ELAN
   * touchpad is usually multitouch absolute HID, so a full parser is still
   * required before gestures feel native.
   */
  int payload = len - 2;
  const uint8_t *r = buf + 2;
  int offset = (payload >= 4 && r[0] <= 8) ? 1 : 0;
  if (payload - offset < 3 || payload > 8) {
    return;
  }

  int buttons = r[offset] & 0x07;
  int dx = (int8_t)r[offset + 1];
  int dy = (int8_t)r[offset + 2];
  int wheel = payload - offset >= 4 ? (int8_t)r[offset + 3] : 0;
  if (dx == 0 && dy == 0 && wheel == 0) {
    return;
  }
  if (dx < -64 || dx > 64 || dy < -64 || dy > 64) {
    return;
  }
  ps2_inject_mouse_relative(dx, dy, buttons, wheel);
  dev->relative_reports++;
}

void i2c_hid_init(void) {
  pci_device_info_t devs[8];
  int count;

  if (initialized) {
    return;
  }
  initialized = 1;
  controller_count = 0;
  device_count = 0;
  memset(controllers, 0, sizeof(controllers));
  memset(devices, 0, sizeof(devices));

  count = pci_scan_class(0x0C, 0x80, 0xFF, devs, 8);
  for (int i = 0; i < count && i < 8; i++) {
    if (devs[i].vendor_id != 0x8086) {
      continue;
    }
    if (devs[i].device_id == 0x54E8) {
      (void)add_controller(&devs[i], "Intel LPSS I2C0");
    } else if (devs[i].device_id == 0x54E9) {
      (void)add_controller(&devs[i], "Intel LPSS I2C1");
    }
  }

  for (int i = 0; i < controller_count; i++) {
    (void)dw_init_controller(&controllers[i]);
  }
  add_lenovo_hid_targets();

  int ready = 0;
  for (int i = 0; i < device_count; i++) {
    if (devices[i].ready) {
      ready++;
    }
  }
  if (ready > 0) {
    i2c_status = "i2c-hid: Lenovo HID devices ready";
  } else if (controller_count > 0) {
    i2c_status = "i2c-hid: controllers ready, HID probe pending";
  } else {
    i2c_status = "i2c-hid: no supported Intel LPSS I2C controller";
  }
  serial_puts("[i2c-hid] ");
  serial_puts(i2c_status);
  serial_puts("\n");
}

void i2c_hid_poll(void) {
  uint8_t report[I2C_HID_MAX_REPORT];

  if (!initialized) {
    return;
  }
  poll_count++;
  if ((poll_count & 0x1F) != 0) {
    return;
  }

  for (int i = 0; i < device_count; i++) {
    i2c_hid_device_t *dev = &devices[i];
    if (!dev->ready || dev->input_reg == 0) {
      continue;
    }
    i2c_controller_t *ctl = &controllers[dev->controller];
    int len = dev->max_input_len;
    if (len < 8) len = 8;
    if (len > I2C_HID_MAX_REPORT) len = I2C_HID_MAX_REPORT;
    memset(report, 0, sizeof(report));
    if (i2c_read_reg(ctl, dev->address, dev->input_reg, report, len) != 0) {
      dev->errors++;
      continue;
    }
    uint16_t actual = (uint16_t)(report[0] | (report[1] << 8));
    if (actual < 3 || actual > len) {
      continue;
    }
    dev->reports++;
    parse_basic_mouse_report(dev, report, actual);
  }
}

void i2c_hid_format_status(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  unsigned long reports = 0;
  unsigned long rel = 0;
  unsigned long errors = 0;
  int ready = 0;
  for (int i = 0; i < device_count; i++) {
    if (devices[i].ready) ready++;
    reports += devices[i].reports;
    rel += devices[i].relative_reports;
    errors += devices[i].errors;
  }
  for (int i = 0; i < controller_count; i++) {
    errors += controllers[i].errors;
  }
  snprintf(buf, size,
           "%s controllers=%d devices=%d ready=%d reports=%lu relative=%lu errors=%lu",
           i2c_status, controller_count, device_count, ready, reports, rel,
           errors);
}
