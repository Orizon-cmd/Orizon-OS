/*
 * Orizon OS x86_64 - Minimal Ethernet drivers
 *
 * This is intentionally small: PCI discovery, MMIO setup, link/MAC reporting,
 * and raw Ethernet TX/RX rings for Intel e1000/e1000e and Realtek RTL8139.
 * Higher network protocols live above this.
 */

#include "../include/net.h"
#include "../include/gui.h"
#include "../include/mmio.h"
#include "../include/pci.h"
#include "../include/string.h"

#define NET_RX_DESC_COUNT 16
#define NET_TX_DESC_COUNT 8
#define NET_RX_BUF_SIZE 2048
#define NET_TX_BUF_SIZE 2048
#define NET_MAX_FRAME 1518

#define RTL8139_RX_BUF_SIZE (8192 + 16 + 1500)
#define RTL8139_TX_DESC_COUNT 4
#define RTL_IDR0 0x00
#define RTL_TSD0 0x10
#define RTL_TSAD0 0x20
#define RTL_RBSTART 0x30
#define RTL_CR 0x37
#define RTL_CAPR 0x38
#define RTL_IMR 0x3C
#define RTL_ISR 0x3E
#define RTL_TCR 0x40
#define RTL_RCR 0x44
#define RTL_CONFIG1 0x52
#define RTL_MSR 0x58
#define RTL_CR_BUFE 0x01
#define RTL_CR_TE 0x04
#define RTL_CR_RE 0x08
#define RTL_CR_RST 0x10
#define RTL_RCR_AAP 0x01
#define RTL_RCR_APM 0x02
#define RTL_RCR_AM 0x04
#define RTL_RCR_AB 0x08
#define RTL_RCR_WRAP 0x80
#define RTL_TX_OWN (1U << 13)
#define RTL_TX_TOK (1U << 15)

#define E1000_REG_CTRL 0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD 0x0014
#define E1000_REG_IMC 0x00D8
#define E1000_REG_RCTL 0x0100
#define E1000_REG_TCTL 0x0400
#define E1000_REG_TIPG 0x0410
#define E1000_REG_RDBAL 0x2800
#define E1000_REG_RDBAH 0x2804
#define E1000_REG_RDLEN 0x2808
#define E1000_REG_RDH 0x2810
#define E1000_REG_RDT 0x2818
#define E1000_REG_TDBAL 0x3800
#define E1000_REG_TDBAH 0x3804
#define E1000_REG_TDLEN 0x3808
#define E1000_REG_TDH 0x3810
#define E1000_REG_TDT 0x3818
#define E1000_REG_RAL 0x5400
#define E1000_REG_RAH 0x5404

#define E1000_CTRL_SLU (1U << 6)
#define E1000_STATUS_LU (1U << 1)

#define E1000_RCTL_EN (1U << 1)
#define E1000_RCTL_SBP (1U << 2)
#define E1000_RCTL_UPE (1U << 3)
#define E1000_RCTL_MPE (1U << 4)
#define E1000_RCTL_BAM (1U << 15)
#define E1000_RCTL_SECRC (1U << 26)

#define E1000_TCTL_EN (1U << 1)
#define E1000_TCTL_PSP (1U << 3)
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD_SHIFT 12

#define E1000_TX_CMD_EOP (1U << 0)
#define E1000_TX_CMD_IFCS (1U << 1)
#define E1000_TX_CMD_RS (1U << 3)
#define E1000_TX_STATUS_DD (1U << 0)
#define E1000_RX_STATUS_DD (1U << 0)
#define E1000_RX_STATUS_EOP (1U << 1)

typedef enum {
  NET_DRIVER_NONE = 0,
  NET_DRIVER_E1000,
  NET_DRIVER_RTL8139,
} net_driver_kind_t;

typedef struct {
  uint64_t addr;
  uint16_t length;
  uint16_t checksum;
  uint8_t status;
  uint8_t errors;
  uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
  uint64_t addr;
  uint16_t length;
  uint8_t cso;
  uint8_t cmd;
  uint8_t status;
  uint8_t css;
  uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

static volatile uint8_t *net_mmio = NULL;
static net_driver_kind_t net_driver_kind = NET_DRIVER_NONE;
static net_device_status_t net_status_state = {
    .present = 0,
    .initialized = 0,
    .link_up = 0,
    .vendor_id = 0,
    .device_id = 0,
    .mac = {0},
    .driver = "none",
    .status = "network: not initialized",
};

static e1000_rx_desc_t rx_desc[NET_RX_DESC_COUNT] __attribute__((aligned(16)));
static e1000_tx_desc_t tx_desc[NET_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t rx_buf[NET_RX_DESC_COUNT][NET_RX_BUF_SIZE]
    __attribute__((aligned(16)));
static uint8_t tx_buf[NET_TX_DESC_COUNT][NET_TX_BUF_SIZE]
    __attribute__((aligned(16)));
static uint32_t rx_next = 0;
static uint32_t tx_tail = 0;

static volatile uint8_t *rtl_mmio = NULL;
static uint8_t rtl_rx_buf[RTL8139_RX_BUF_SIZE] __attribute__((aligned(4096)));
static uint8_t rtl_tx_buf[RTL8139_TX_DESC_COUNT][NET_TX_BUF_SIZE]
    __attribute__((aligned(4)));
static uint32_t rtl_rx_offset = 0;
static uint32_t rtl_tx_cur = 0;

static uint64_t net_phys_addr(const void *ptr) {
  uint64_t v = (uint64_t)(uintptr_t)ptr;
  if (kernel_phys_base && kernel_virt_base && v >= kernel_virt_base) {
    return kernel_phys_base + (v - kernel_virt_base);
  }
  if (v >= hhdm_offset) {
    return v - hhdm_offset;
  }
  return v;
}

static uint32_t e1000_read(uint32_t reg) {
  return *(volatile uint32_t *)(net_mmio + reg);
}

static void e1000_write(uint32_t reg, uint32_t value) {
  *(volatile uint32_t *)(net_mmio + reg) = value;
}

static uint8_t rtl_read8(uint32_t reg) {
  return *(volatile uint8_t *)(rtl_mmio + reg);
}

static uint32_t rtl_read32(uint32_t reg) {
  return *(volatile uint32_t *)(rtl_mmio + reg);
}

static void rtl_write8(uint32_t reg, uint8_t value) {
  *(volatile uint8_t *)(rtl_mmio + reg) = value;
}

static void rtl_write16(uint32_t reg, uint16_t value) {
  *(volatile uint16_t *)(rtl_mmio + reg) = value;
}

static void rtl_write32(uint32_t reg, uint32_t value) {
  *(volatile uint32_t *)(rtl_mmio + reg) = value;
}

static void net_set_status(const char *status) {
  net_status_state.status = status;
  serial_puts("[net] ");
  serial_puts(status);
  serial_puts("\n");
}

static int is_supported_intel_nic(uint16_t device_id) {
  switch (device_id) {
    case 0x1004:
    case 0x1008:
    case 0x1009:
    case 0x100C:
    case 0x100E: /* 82540EM, QEMU e1000 */
    case 0x100F:
    case 0x1010:
    case 0x1011:
    case 0x1012:
    case 0x1013:
    case 0x1015:
    case 0x1016:
    case 0x1017:
    case 0x1019:
    case 0x101D:
    case 0x1026:
    case 0x1027:
    case 0x1028:
    case 0x1075:
    case 0x1076:
    case 0x1077:
    case 0x1078:
    case 0x1079:
    case 0x107A:
    case 0x107B:
    case 0x107C:
    case 0x108A:
    case 0x1096:
    case 0x1098:
    case 0x1099:
    case 0x109A:
    case 0x10A4:
    case 0x10A5:
    case 0x10B9:
    case 0x10BA:
    case 0x10BB:
    case 0x10BC:
    case 0x10C4:
    case 0x10C5:
    case 0x10D3: /* 82574L, QEMU e1000e */
    case 0x10D5:
    case 0x10EA:
    case 0x10F6:
    case 0x1502:
    case 0x1503:
    case 0x150C:
    case 0x153A:
    case 0x153B:
    case 0x1559:
    case 0x155A:
    case 0x15A0:
    case 0x15A1:
    case 0x15B7:
    case 0x15B8:
    case 0x15D7:
    case 0x15D8:
    case 0x15E3:
    case 0x15F9:
      return 1;
    default:
      return 0;
  }
}

static int is_supported_rtl8139(uint16_t vendor_id, uint16_t device_id) {
  return vendor_id == 0x10EC && device_id == 0x8139;
}

static void e1000_refresh_link(void) {
  uint32_t status;
  if (!net_mmio) {
    net_status_state.link_up = 0;
    return;
  }
  status = e1000_read(E1000_REG_STATUS);
  net_status_state.link_up = (status & E1000_STATUS_LU) ? 1 : 0;
}

static void e1000_read_mac(void) {
  uint32_t ral = e1000_read(E1000_REG_RAL);
  uint32_t rah = e1000_read(E1000_REG_RAH);

  net_status_state.mac[0] = (uint8_t)(ral & 0xFF);
  net_status_state.mac[1] = (uint8_t)((ral >> 8) & 0xFF);
  net_status_state.mac[2] = (uint8_t)((ral >> 16) & 0xFF);
  net_status_state.mac[3] = (uint8_t)((ral >> 24) & 0xFF);
  net_status_state.mac[4] = (uint8_t)(rah & 0xFF);
  net_status_state.mac[5] = (uint8_t)((rah >> 8) & 0xFF);

  if ((ral | (rah & 0xFFFF)) == 0) {
    net_status_state.mac[0] = 0x52;
    net_status_state.mac[1] = 0x54;
    net_status_state.mac[2] = 0x00;
    net_status_state.mac[3] = 0x12;
    net_status_state.mac[4] = 0x34;
    net_status_state.mac[5] = 0x56;
    e1000_write(E1000_REG_RAL,
                ((uint32_t)net_status_state.mac[3] << 24) |
                    ((uint32_t)net_status_state.mac[2] << 16) |
                    ((uint32_t)net_status_state.mac[1] << 8) |
                    net_status_state.mac[0]);
    e1000_write(E1000_REG_RAH,
                (1U << 31) | ((uint32_t)net_status_state.mac[5] << 8) |
                    net_status_state.mac[4]);
  }
}

static void e1000_setup_rx(void) {
  memset(rx_desc, 0, sizeof(rx_desc));
  memset(rx_buf, 0, sizeof(rx_buf));

  for (int i = 0; i < NET_RX_DESC_COUNT; i++) {
    rx_desc[i].addr = net_phys_addr(rx_buf[i]);
  }

  uint64_t rx_phys = net_phys_addr(rx_desc);
  e1000_write(E1000_REG_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFFU));
  e1000_write(E1000_REG_RDBAH, (uint32_t)(rx_phys >> 32));
  e1000_write(E1000_REG_RDLEN, sizeof(rx_desc));
  e1000_write(E1000_REG_RDH, 0);
  e1000_write(E1000_REG_RDT, NET_RX_DESC_COUNT - 1);
  rx_next = 0;

  e1000_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_SBP |
                                  E1000_RCTL_UPE | E1000_RCTL_MPE |
                                  E1000_RCTL_BAM | E1000_RCTL_SECRC);
}

static void e1000_setup_tx(void) {
  memset(tx_desc, 0, sizeof(tx_desc));
  memset(tx_buf, 0, sizeof(tx_buf));

  for (int i = 0; i < NET_TX_DESC_COUNT; i++) {
    tx_desc[i].addr = net_phys_addr(tx_buf[i]);
    tx_desc[i].status = E1000_TX_STATUS_DD;
  }

  uint64_t tx_phys = net_phys_addr(tx_desc);
  e1000_write(E1000_REG_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFFU));
  e1000_write(E1000_REG_TDBAH, (uint32_t)(tx_phys >> 32));
  e1000_write(E1000_REG_TDLEN, sizeof(tx_desc));
  e1000_write(E1000_REG_TDH, 0);
  e1000_write(E1000_REG_TDT, 0);
  tx_tail = 0;

  e1000_write(E1000_REG_TCTL,
              E1000_TCTL_EN | E1000_TCTL_PSP | (15U << E1000_TCTL_CT_SHIFT) |
                  (64U << E1000_TCTL_COLD_SHIFT));
  e1000_write(E1000_REG_TIPG, 10U | (8U << 10) | (6U << 20));
}

static void rtl8139_refresh_link(void) {
  if (!rtl_mmio) {
    net_status_state.link_up = 0;
    return;
  }
  net_status_state.link_up = (rtl_read8(RTL_MSR) & 0x04) ? 0 : 1;
}

static void rtl8139_read_mac(void) {
  for (int i = 0; i < 6; i++) {
    net_status_state.mac[i] = rtl_read8(RTL_IDR0 + (uint32_t)i);
  }
}

static int rtl8139_init_device(pci_device_info_t *dev) {
  if (!dev) {
    return -1;
  }

  net_status_state.present = 1;
  net_status_state.vendor_id = dev->vendor_id;
  net_status_state.device_id = dev->device_id;
  net_status_state.driver = "realtek-rtl8139";
  net_driver_kind = NET_DRIVER_RTL8139;

  uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  cmd |= (1U << 1) | (1U << 2); /* memory space + bus master */
  pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);

  uint32_t bar = 0;
  if ((dev->bar[1] & 0x1) == 0 && (dev->bar[1] & ~0xFULL)) {
    bar = dev->bar[1] & ~0xFULL;
  } else if ((dev->bar[0] & 0x1) == 0 && (dev->bar[0] & ~0xFULL)) {
    bar = dev->bar[0] & ~0xFULL;
  }
  if (!bar) {
    net_set_status("network: rtl8139 MMIO BAR missing");
    return -1;
  }

  rtl_mmio = (volatile uint8_t *)(uintptr_t)mmio_map_range(bar, 0x1000);
  if (!rtl_mmio) {
    net_set_status("network: rtl8139 MMIO map failed");
    return -1;
  }

  rtl_write8(RTL_CONFIG1, 0x00);
  rtl_write8(RTL_CR, RTL_CR_RST);
  for (int i = 0; i < 1000000; i++) {
    if ((rtl_read8(RTL_CR) & RTL_CR_RST) == 0) {
      break;
    }
    __asm__ volatile("pause");
  }
  if (rtl_read8(RTL_CR) & RTL_CR_RST) {
    net_set_status("network: rtl8139 reset timeout");
    return -1;
  }

  memset(rtl_rx_buf, 0, sizeof(rtl_rx_buf));
  memset(rtl_tx_buf, 0, sizeof(rtl_tx_buf));
  rtl_rx_offset = 0;
  rtl_tx_cur = 0;

  rtl8139_read_mac();
  rtl_write32(RTL_RBSTART, (uint32_t)net_phys_addr(rtl_rx_buf));
  rtl_write16(RTL_IMR, 0);
  rtl_write32(RTL_RCR, RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB |
                           RTL_RCR_WRAP | (7U << 8));
  rtl_write32(RTL_TCR, 0x03000000U);
  rtl_write8(RTL_CR, RTL_CR_RE | RTL_CR_TE);

  net_status_state.initialized = 1;
  rtl8139_refresh_link();
  net_set_status(net_status_state.link_up ? "network: rtl8139 link up"
                                          : "network: rtl8139 ready, link down");
  return net_status_state.link_up ? 0 : -1;
}

int net_init(void) {
  pci_device_info_t devs[8];
  int count;

  if (net_status_state.initialized) {
    if (net_driver_kind == NET_DRIVER_RTL8139) {
      rtl8139_refresh_link();
    } else {
      e1000_refresh_link();
    }
    return net_status_state.link_up ? 0 : -1;
  }

  count = pci_scan_class(0x02, 0x00, 0xFF, devs, 8);
  if (count <= 0) {
    net_set_status("network: no ethernet controller");
    return -1;
  }

  pci_device_info_t *dev = NULL;
  pci_device_info_t *rtl_dev = NULL;
  for (int i = 0; i < count; i++) {
    if (devs[i].vendor_id == 0x8086 && is_supported_intel_nic(devs[i].device_id)) {
      dev = &devs[i];
      break;
    }
    if (!rtl_dev && is_supported_rtl8139(devs[i].vendor_id, devs[i].device_id)) {
      rtl_dev = &devs[i];
    }
  }

  if (!dev) {
    if (rtl_dev) {
      return rtl8139_init_device(rtl_dev);
    }
    net_status_state.present = 1;
    net_status_state.vendor_id = devs[0].vendor_id;
    net_status_state.device_id = devs[0].device_id;
    net_set_status("network: ethernet controller unsupported");
    return -1;
  }

  net_status_state.present = 1;
  net_status_state.vendor_id = dev->vendor_id;
  net_status_state.device_id = dev->device_id;
  net_status_state.driver = "intel-e1000";
  net_driver_kind = NET_DRIVER_E1000;

  uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  cmd |= (1U << 1) | (1U << 2); /* memory space + bus master */
  pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);

  if (dev->bar[0] & 0x1) {
    net_set_status("network: e1000 BAR is not MMIO");
    return -1;
  }

  uint64_t bar = dev->bar[0] & ~0xFULL;
  if (dev->bar[0] & 0x4) {
    bar |= ((uint64_t)dev->bar[1] << 32);
  }
  if (!bar) {
    net_set_status("network: e1000 MMIO BAR missing");
    return -1;
  }

  net_mmio = (volatile uint8_t *)(uintptr_t)mmio_map_range(bar, 0x20000);
  if (!net_mmio) {
    net_set_status("network: e1000 MMIO map failed");
    return -1;
  }

  e1000_write(E1000_REG_IMC, 0xFFFFFFFFU);
  e1000_write(E1000_REG_CTRL, e1000_read(E1000_REG_CTRL) | E1000_CTRL_SLU);
  e1000_read_mac();
  e1000_setup_rx();
  e1000_setup_tx();

  net_status_state.initialized = 1;
  e1000_refresh_link();
  net_set_status(net_status_state.link_up ? "network: ethernet link up"
                                          : "network: ethernet ready, link down");
  return net_status_state.link_up ? 0 : -1;
}

void net_poll(void) {
  if (net_status_state.initialized) {
    if (net_driver_kind == NET_DRIVER_RTL8139) {
      rtl8139_refresh_link();
    } else {
      e1000_refresh_link();
    }
  }
}

int net_link_up(void) {
  if (!net_status_state.initialized) {
    net_init();
  } else if (net_driver_kind == NET_DRIVER_RTL8139) {
    rtl8139_refresh_link();
  } else {
    e1000_refresh_link();
  }
  return net_status_state.link_up;
}

const net_device_status_t *net_get_status(void) {
  if (!net_status_state.initialized) {
    net_init();
  } else if (net_driver_kind == NET_DRIVER_RTL8139) {
    rtl8139_refresh_link();
  } else {
    e1000_refresh_link();
  }
  return &net_status_state;
}

const char *net_status(void) {
  return net_get_status()->status;
}

void net_format_status(char *buf, size_t size) {
  const net_device_status_t *s = net_get_status();
  snprintf(buf, size,
           "driver=%s present=%s initialized=%s link=%s pci=%04x:%04x "
           "mac=%02x:%02x:%02x:%02x:%02x:%02x",
           s->driver, s->present ? "yes" : "no",
           s->initialized ? "yes" : "no", s->link_up ? "up" : "down",
           s->vendor_id, s->device_id, s->mac[0], s->mac[1], s->mac[2],
           s->mac[3], s->mac[4], s->mac[5]);
}

int net_send_ethernet(const uint8_t dst_mac[6], uint16_t ether_type,
                      const void *payload, size_t payload_len) {
  if (!net_link_up()) {
    return -1;
  }
  if (!dst_mac || (!payload && payload_len > 0) ||
      payload_len + 14 > NET_MAX_FRAME) {
    return -1;
  }

  if (net_driver_kind == NET_DRIVER_RTL8139) {
    uint32_t slot = rtl_tx_cur;
    uint32_t tsd = rtl_read32(RTL_TSD0 + slot * 4U);
    if (tsd & RTL_TX_OWN) {
      return -1;
    }

    uint8_t *frame = rtl_tx_buf[slot];
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, net_status_state.mac, 6);
    frame[12] = (uint8_t)(ether_type >> 8);
    frame[13] = (uint8_t)(ether_type & 0xFF);
    if (payload_len > 0) {
      memcpy(frame + 14, payload, payload_len);
    }

    size_t frame_len = payload_len + 14;
    if (frame_len < 60) {
      memset(frame + frame_len, 0, 60 - frame_len);
      frame_len = 60;
    }

    rtl_write32(RTL_TSAD0 + slot * 4U, (uint32_t)net_phys_addr(frame));
    rtl_write32(RTL_TSD0 + slot * 4U, (uint32_t)frame_len);
    rtl_tx_cur = (rtl_tx_cur + 1) % RTL8139_TX_DESC_COUNT;
    return (int)frame_len;
  }

  e1000_tx_desc_t *desc = &tx_desc[tx_tail];
  if (!(desc->status & E1000_TX_STATUS_DD)) {
    return -1;
  }

  uint8_t *frame = tx_buf[tx_tail];
  memcpy(frame, dst_mac, 6);
  memcpy(frame + 6, net_status_state.mac, 6);
  frame[12] = (uint8_t)(ether_type >> 8);
  frame[13] = (uint8_t)(ether_type & 0xFF);
  if (payload_len > 0) {
    memcpy(frame + 14, payload, payload_len);
  }

  size_t frame_len = payload_len + 14;
  if (frame_len < 60) {
    memset(frame + frame_len, 0, 60 - frame_len);
    frame_len = 60;
  }

  desc->length = (uint16_t)frame_len;
  desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
  desc->status = 0;

  tx_tail = (tx_tail + 1) % NET_TX_DESC_COUNT;
  e1000_write(E1000_REG_TDT, tx_tail);
  return (int)frame_len;
}

int net_recv_ethernet(uint8_t src_mac[6], uint16_t *ether_type,
                      void *payload, size_t payload_cap) {
  if (!net_status_state.initialized || !payload || !ether_type) {
    return -1;
  }

  if (net_driver_kind == NET_DRIVER_RTL8139) {
    if (rtl_read8(RTL_CR) & RTL_CR_BUFE) {
      return 0;
    }

    uint32_t offset = rtl_rx_offset;
    uint16_t status = *(uint16_t *)(rtl_rx_buf + offset);
    uint16_t length = *(uint16_t *)(rtl_rx_buf + offset + 2);
    int result = -1;

    if ((status & 0x01) && length >= 18) {
      uint8_t *frame = rtl_rx_buf + offset + 4;
      size_t payload_len = (size_t)length - 4 - 14; /* Drop Ethernet CRC. */
      if (payload_len <= payload_cap) {
        if (src_mac) {
          memcpy(src_mac, frame + 6, 6);
        }
        *ether_type = ((uint16_t)frame[12] << 8) | frame[13];
        memcpy(payload, frame + 14, payload_len);
        result = (int)payload_len;
      }
    }

    offset = (offset + length + 4 + 3) & ~3U;
    while (offset >= 8192U) {
      offset -= 8192U;
    }
    rtl_rx_offset = offset;
    rtl_write16(RTL_CAPR, (uint16_t)((offset - 16U) & 0xFFFFU));
    return result;
  }

  e1000_rx_desc_t *desc = &rx_desc[rx_next];
  if (!(desc->status & E1000_RX_STATUS_DD)) {
    return 0;
  }

  int result = -1;
  if ((desc->status & E1000_RX_STATUS_EOP) && desc->length >= 14) {
    uint8_t *frame = rx_buf[rx_next];
    size_t payload_len = desc->length - 14;
    if (payload_len <= payload_cap) {
      if (src_mac) {
        memcpy(src_mac, frame + 6, 6);
      }
      *ether_type = ((uint16_t)frame[12] << 8) | frame[13];
      memcpy(payload, frame + 14, payload_len);
      result = (int)payload_len;
    }
  }

  desc->status = 0;
  e1000_write(E1000_REG_RDT, rx_next);
  rx_next = (rx_next + 1) % NET_RX_DESC_COUNT;
  return result;
}
