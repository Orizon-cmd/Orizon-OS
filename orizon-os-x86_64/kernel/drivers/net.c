/*
 * Orizon OS x86_64 - Minimal Ethernet drivers
 *
 * This is intentionally small: PCI discovery, MMIO setup, link/MAC reporting,
 * and raw Ethernet TX/RX rings for Intel e1000/e1000e, Realtek RTL8139 and
 * VirtIO-net. Higher network protocols live above this.
 */

#include "../include/net.h"
#include "../include/gui.h"
#include "../include/mmio.h"
#include "../include/pci.h"
#include "../include/string.h"
#include "../include/usb.h"

#define NET_RX_DESC_COUNT 16
#define NET_TX_DESC_COUNT 8
#define NET_RX_BUF_SIZE 2048
#define NET_TX_BUF_SIZE 2048
#define NET_MAX_FRAME 1518

#define VIRTIO_QUEUE_MAX 256
#define VIRTIO_QUEUE_MEM_SIZE 16384
#define VIRTIO_RX_DESC_COUNT 32
#define VIRTIO_TX_DESC_COUNT 16
#define VIRTIO_NET_HDR_LEGACY_SIZE 10
#define VIRTIO_NET_HDR_MODERN_SIZE 12
#define VIRTIO_NET_HDR_MAX_SIZE 12

#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_NUM 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_ISR 0x13
#define VIRTIO_PCI_CONFIG 0x14

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_FAILED 0x80

#define VIRTIO_NET_F_MAC (1U << 5)
#define VIRTIO_NET_F_STATUS (1U << 16)
#define VIRTIO_F_VERSION_1_HIGH (1U << 0)
#define VIRTIO_NET_S_LINK_UP 0x0001

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

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
  NET_DRIVER_VIRTIO,
  NET_DRIVER_USB,
} net_driver_kind_t;

typedef struct {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[VIRTIO_QUEUE_MAX];
} __attribute__((packed)) virtq_avail_t;

typedef struct {
  uint32_t id;
  uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  virtq_used_elem_t ring[VIRTIO_QUEUE_MAX];
} __attribute__((packed)) virtq_used_t;

typedef struct {
  virtq_desc_t *desc;
  virtq_avail_t *avail;
  virtq_used_t *used;
  uint16_t size;
  uint16_t last_used;
  volatile uint16_t *notify;
} virtio_queue_t;

typedef struct {
  uint32_t device_feature_select;
  uint32_t device_feature;
  uint32_t driver_feature_select;
  uint32_t driver_feature;
  uint16_t msix_config;
  uint16_t num_queues;
  uint8_t device_status;
  uint8_t config_generation;
  uint16_t queue_select;
  uint16_t queue_size;
  uint16_t queue_msix_vector;
  uint16_t queue_enable;
  uint16_t queue_notify_off;
  uint64_t queue_desc;
  uint64_t queue_driver;
  uint64_t queue_device;
} virtio_modern_common_cfg_t;

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

static uint16_t virtio_io_base = 0;
static int virtio_modern = 0;
static uint32_t virtio_guest_features = 0;
static volatile virtio_modern_common_cfg_t *virtio_common_cfg = NULL;
static volatile uint8_t *virtio_notify_base = NULL;
static volatile uint8_t *virtio_isr_cfg = NULL;
static volatile uint8_t *virtio_device_cfg = NULL;
static uint32_t virtio_notify_multiplier = 0;
static virtio_queue_t virtio_rxq;
static virtio_queue_t virtio_txq;
static uint8_t virtio_rx_queue_mem[VIRTIO_QUEUE_MEM_SIZE]
    __attribute__((aligned(4096)));
static uint8_t virtio_tx_queue_mem[VIRTIO_QUEUE_MEM_SIZE]
    __attribute__((aligned(4096)));
static uint8_t virtio_rx_buf[VIRTIO_RX_DESC_COUNT]
                            [VIRTIO_NET_HDR_MAX_SIZE + NET_MAX_FRAME]
    __attribute__((aligned(16)));
static uint8_t virtio_tx_buf[VIRTIO_TX_DESC_COUNT]
                            [VIRTIO_NET_HDR_MAX_SIZE + NET_MAX_FRAME]
    __attribute__((aligned(16)));
static uint8_t virtio_tx_inflight[VIRTIO_TX_DESC_COUNT];
static uint16_t virtio_rx_count = 0;
static uint16_t virtio_tx_count = 0;
static uint16_t virtio_tx_next = 0;
static uint16_t virtio_net_hdr_size = VIRTIO_NET_HDR_LEGACY_SIZE;
static uint8_t usb_tx_frame[NET_MAX_FRAME] __attribute__((aligned(16)));
static uint8_t usb_rx_frame[NET_MAX_FRAME] __attribute__((aligned(16)));

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

static void net_io_barrier(void) {
  __asm__ volatile("" ::: "memory");
}

static uint8_t io_in8(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static uint16_t io_in16(uint16_t port) {
  uint16_t value;
  __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static uint32_t io_in32(uint16_t port) {
  uint32_t value;
  __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static void io_out8(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_out16(uint16_t port, uint16_t value) {
  __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void io_out32(uint16_t port, uint32_t value) {
  __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t pci_cfg_read8(const pci_device_info_t *dev, uint8_t offset) {
  uint32_t value = pci_read32(dev->bus, dev->device, dev->function, offset);
  return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

static uint16_t pci_cfg_read16(const pci_device_info_t *dev, uint8_t offset) {
  uint32_t value = pci_read32(dev->bus, dev->device, dev->function, offset);
  return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

static uint32_t pci_cfg_read32(const pci_device_info_t *dev, uint8_t offset) {
  return pci_read32(dev->bus, dev->device, dev->function, offset);
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

static uint8_t virtio_read8(uint16_t reg) {
  return io_in8((uint16_t)(virtio_io_base + reg));
}

static uint16_t virtio_read16(uint16_t reg) {
  return io_in16((uint16_t)(virtio_io_base + reg));
}

static uint32_t virtio_read32(uint16_t reg) {
  return io_in32((uint16_t)(virtio_io_base + reg));
}

static void virtio_write8(uint16_t reg, uint8_t value) {
  io_out8((uint16_t)(virtio_io_base + reg), value);
}

static void virtio_write16(uint16_t reg, uint16_t value) {
  io_out16((uint16_t)(virtio_io_base + reg), value);
}

static void virtio_write32(uint16_t reg, uint32_t value) {
  io_out32((uint16_t)(virtio_io_base + reg), value);
}

static uint8_t virtio_device_status_read(void) {
  if (virtio_modern && virtio_common_cfg) {
    return virtio_common_cfg->device_status;
  }
  return virtio_read8(VIRTIO_PCI_STATUS);
}

static void virtio_device_status_write(uint8_t value) {
  if (virtio_modern && virtio_common_cfg) {
    virtio_common_cfg->device_status = value;
  } else {
    virtio_write8(VIRTIO_PCI_STATUS, value);
  }
  net_io_barrier();
}

static void virtio_device_status_add(uint8_t value) {
  virtio_device_status_write((uint8_t)(virtio_device_status_read() | value));
}

static void virtio_device_fail(void) {
  virtio_device_status_add(VIRTIO_STATUS_FAILED);
}

static uint8_t virtio_config_read8(uint16_t offset) {
  if (virtio_modern && virtio_device_cfg) {
    return *(volatile uint8_t *)(virtio_device_cfg + offset);
  }
  return virtio_read8((uint16_t)(VIRTIO_PCI_CONFIG + offset));
}

static void virtio_notify_queue(uint16_t queue_index, virtio_queue_t *queue) {
  if (virtio_modern && queue && queue->notify) {
    *queue->notify = queue_index;
  } else {
    virtio_write16(VIRTIO_PCI_QUEUE_NOTIFY, queue_index);
  }
  net_io_barrier();
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

static int is_supported_virtio_net(uint16_t vendor_id, uint16_t device_id) {
  if (vendor_id != 0x1AF4) {
    return 0;
  }
  return device_id == 0x1000 || device_id == 0x1041;
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

static size_t virtio_align_up(size_t value, size_t align) {
  return (value + align - 1) & ~(align - 1);
}

static int virtio_find_legacy_io_bar(const pci_device_info_t *dev,
                                     uint16_t *io_base) {
  if (!dev || !io_base) {
    return -1;
  }

  for (int i = 0; i < 6; i++) {
    if ((dev->bar[i] & 0x1) == 0) {
      continue;
    }
    uint32_t base = dev->bar[i] & ~0x3U;
    if (base && base <= 0xFFFFU) {
      *io_base = (uint16_t)base;
      return 0;
    }
  }
  return -1;
}

static uint64_t virtio_pci_bar_base(const pci_device_info_t *dev,
                                    uint8_t bar_index) {
  if (!dev || bar_index >= 6) {
    return 0;
  }

  uint32_t raw = dev->bar[bar_index];
  if (!raw || raw == 0xFFFFFFFFU || (raw & 0x1)) {
    return 0;
  }

  uint64_t base = raw & ~0xFULL;
  if ((raw & 0x6) == 0x4 && bar_index + 1 < 6) {
    base |= ((uint64_t)dev->bar[bar_index + 1] << 32);
  }
  return base;
}

static volatile uint8_t *virtio_map_modern_cap(const pci_device_info_t *dev,
                                               uint8_t bar, uint32_t offset,
                                               uint32_t length) {
  uint64_t bar_base = virtio_pci_bar_base(dev, bar);
  if (!bar_base || length == 0) {
    return NULL;
  }
  return (volatile uint8_t *)(uintptr_t)mmio_map_range(bar_base + offset,
                                                       length);
}

static int virtio_modern_discover(const pci_device_info_t *dev) {
  virtio_common_cfg = NULL;
  virtio_notify_base = NULL;
  virtio_isr_cfg = NULL;
  virtio_device_cfg = NULL;
  virtio_notify_multiplier = 0;

  if ((pci_cfg_read16(dev, 0x06) & 0x10) == 0) {
    return -1;
  }

  uint8_t cap = (uint8_t)(pci_cfg_read8(dev, 0x34) & ~0x3U);
  for (int guard = 0; cap && guard < 48; guard++) {
    uint8_t cap_vndr = pci_cfg_read8(dev, cap);
    uint8_t cap_next = (uint8_t)(pci_cfg_read8(dev, cap + 1) & ~0x3U);
    uint8_t cap_len = pci_cfg_read8(dev, cap + 2);

    if (cap_vndr == 0x09 && cap_len >= 16) {
      uint8_t cfg_type = pci_cfg_read8(dev, cap + 3);
      uint8_t bar = pci_cfg_read8(dev, cap + 4);
      uint32_t offset = pci_cfg_read32(dev, cap + 8);
      uint32_t length = pci_cfg_read32(dev, cap + 12);
      volatile uint8_t *mapped =
          virtio_map_modern_cap(dev, bar, offset, length);

      if (mapped) {
        if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
          virtio_common_cfg = (volatile virtio_modern_common_cfg_t *)mapped;
        } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
          virtio_notify_base = mapped;
          if (cap_len >= 20) {
            virtio_notify_multiplier = pci_cfg_read32(dev, cap + 16);
          }
        } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
          virtio_isr_cfg = mapped;
        } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
          virtio_device_cfg = mapped;
        }
      }
    }

    cap = cap_next;
  }

  if (!virtio_common_cfg || !virtio_notify_base || !virtio_isr_cfg ||
      !virtio_device_cfg) {
    return -1;
  }
  return 0;
}

static void virtio_net_refresh_link(void) {
  if ((!virtio_modern && !virtio_io_base) ||
      (virtio_modern && !virtio_common_cfg)) {
    net_status_state.link_up = 0;
    return;
  }

  net_status_state.link_up = 1;
}

static void usb_net_refresh_link(void) {
  net_status_state.link_up = usb_net_link_up();
}

static void virtio_net_read_mac(void) {
  if (virtio_guest_features & VIRTIO_NET_F_MAC) {
    for (int i = 0; i < 6; i++) {
      net_status_state.mac[i] = virtio_config_read8((uint16_t)i);
    }
    return;
  }

  net_status_state.mac[0] = 0x52;
  net_status_state.mac[1] = 0x54;
  net_status_state.mac[2] = 0x00;
  net_status_state.mac[3] = 0x4f;
  net_status_state.mac[4] = 0x53;
  net_status_state.mac[5] = 0x01;
}

static int virtio_queue_prepare(uint16_t queue_size, virtio_queue_t *queue,
                                uint8_t *queue_mem) {
  if (!queue || !queue_mem) {
    return -1;
  }

  if (queue_size == 0 || queue_size > VIRTIO_QUEUE_MAX) {
    return -1;
  }

  size_t desc_size = sizeof(virtq_desc_t) * queue_size;
  size_t avail_size = 4 + sizeof(uint16_t) * queue_size;
  size_t used_offset = virtio_align_up(desc_size + avail_size, 4096);
  size_t used_size = 4 + sizeof(virtq_used_elem_t) * queue_size;
  if (used_offset + used_size > VIRTIO_QUEUE_MEM_SIZE) {
    return -1;
  }

  memset(queue_mem, 0, VIRTIO_QUEUE_MEM_SIZE);
  queue->desc = (virtq_desc_t *)queue_mem;
  queue->avail = (virtq_avail_t *)(queue_mem + desc_size);
  queue->used = (virtq_used_t *)(queue_mem + used_offset);
  queue->size = queue_size;
  queue->last_used = 0;
  queue->notify = NULL;

  return 0;
}

static int virtio_legacy_queue_setup(uint16_t queue_index,
                                     virtio_queue_t *queue,
                                     uint8_t *queue_mem) {
  virtio_write16(VIRTIO_PCI_QUEUE_SEL, queue_index);
  uint16_t queue_size = virtio_read16(VIRTIO_PCI_QUEUE_NUM);
  if (virtio_queue_prepare(queue_size, queue, queue_mem) != 0) {
    return -1;
  }

  uint64_t phys = net_phys_addr(queue_mem);
  if ((phys & 0xFFFU) || (phys >> 12) > 0xFFFFFFFFULL) {
    return -1;
  }

  virtio_write32(VIRTIO_PCI_QUEUE_PFN, (uint32_t)(phys >> 12));
  return 0;
}

static int virtio_modern_queue_setup(uint16_t queue_index,
                                     virtio_queue_t *queue,
                                     uint8_t *queue_mem) {
  if (!virtio_common_cfg || !virtio_notify_base) {
    return -1;
  }

  virtio_common_cfg->queue_select = queue_index;
  net_io_barrier();
  uint16_t device_queue_size = virtio_common_cfg->queue_size;
  uint16_t queue_size = device_queue_size;
  if (queue_size > VIRTIO_QUEUE_MAX) {
    queue_size = VIRTIO_QUEUE_MAX;
  }
  if (virtio_queue_prepare(queue_size, queue, queue_mem) != 0) {
    return -1;
  }

  uint64_t desc_phys = net_phys_addr(queue->desc);
  uint64_t avail_phys = net_phys_addr(queue->avail);
  uint64_t used_phys = net_phys_addr(queue->used);
  if (!desc_phys || !avail_phys || !used_phys) {
    return -1;
  }

  virtio_common_cfg->queue_size = queue_size;
  virtio_common_cfg->queue_desc = desc_phys;
  virtio_common_cfg->queue_driver = avail_phys;
  virtio_common_cfg->queue_device = used_phys;
  uint16_t notify_off = virtio_common_cfg->queue_notify_off;
  queue->notify =
      (volatile uint16_t *)(virtio_notify_base +
                            (uint64_t)notify_off * virtio_notify_multiplier);
  net_io_barrier();
  virtio_common_cfg->queue_enable = 1;
  net_io_barrier();
  return 0;
}

static void virtio_net_seed_rx(void) {
  virtio_rx_count = virtio_rxq.size;
  if (virtio_rx_count > VIRTIO_RX_DESC_COUNT) {
    virtio_rx_count = VIRTIO_RX_DESC_COUNT;
  }

  for (uint16_t i = 0; i < virtio_rx_count; i++) {
    virtio_rxq.desc[i].addr = net_phys_addr(virtio_rx_buf[i]);
    virtio_rxq.desc[i].len = sizeof(virtio_rx_buf[i]);
    virtio_rxq.desc[i].flags = VIRTQ_DESC_F_WRITE;
    virtio_rxq.desc[i].next = 0;
    virtio_rxq.avail->ring[virtio_rxq.avail->idx % virtio_rxq.size] = i;
    net_io_barrier();
    virtio_rxq.avail->idx++;
  }

  net_io_barrier();
  virtio_notify_queue(0, &virtio_rxq);
}

static void virtio_net_reclaim_tx(void) {
  if (!virtio_txq.used || virtio_txq.size == 0) {
    return;
  }

  volatile virtq_used_t *used = (volatile virtq_used_t *)virtio_txq.used;
  uint16_t used_idx = used->idx;
  while (virtio_txq.last_used != used_idx) {
    uint16_t ring_idx = virtio_txq.last_used % virtio_txq.size;
    uint32_t id = used->ring[ring_idx].id;
    if (id < virtio_tx_count) {
      virtio_tx_inflight[id] = 0;
    }
    virtio_txq.last_used++;
  }
}

static int virtio_net_send_ethernet(const uint8_t dst_mac[6],
                                    uint16_t ether_type, const void *payload,
                                    size_t payload_len) {
  if (virtio_tx_count == 0 || virtio_txq.size == 0) {
    return -1;
  }

  virtio_net_reclaim_tx();

  int slot = -1;
  for (uint16_t i = 0; i < virtio_tx_count; i++) {
    uint16_t candidate = (uint16_t)((virtio_tx_next + i) % virtio_tx_count);
    if (!virtio_tx_inflight[candidate]) {
      slot = candidate;
      break;
    }
  }
  if (slot < 0) {
    return -1;
  }

  uint8_t *packet = virtio_tx_buf[slot];
  memset(packet, 0, virtio_net_hdr_size);
  uint8_t *frame = packet + virtio_net_hdr_size;
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

  virtio_tx_inflight[slot] = 1;
  virtio_txq.desc[slot].addr = net_phys_addr(packet);
  virtio_txq.desc[slot].len = (uint32_t)(virtio_net_hdr_size + frame_len);
  virtio_txq.desc[slot].flags = 0;
  virtio_txq.desc[slot].next = 0;

  net_io_barrier();
  virtio_txq.avail->ring[virtio_txq.avail->idx % virtio_txq.size] =
      (uint16_t)slot;
  net_io_barrier();
  virtio_txq.avail->idx++;
  net_io_barrier();
  virtio_notify_queue(1, &virtio_txq);
  virtio_tx_next = (uint16_t)((slot + 1) % virtio_tx_count);
  return (int)frame_len;
}

static int virtio_net_recv_ethernet(uint8_t src_mac[6], uint16_t *ether_type,
                                    void *payload, size_t payload_cap) {
  volatile virtq_used_t *used = (volatile virtq_used_t *)virtio_rxq.used;
  if (!used || virtio_rxq.size == 0 || virtio_rx_count == 0) {
    return -1;
  }

  uint16_t used_idx = used->idx;
  if (virtio_rxq.last_used == used_idx) {
    return 0;
  }

  uint16_t ring_idx = virtio_rxq.last_used % virtio_rxq.size;
  uint32_t id = used->ring[ring_idx].id;
  uint32_t len = used->ring[ring_idx].len;
  virtio_rxq.last_used++;

  int result = -1;
  if (id < virtio_rx_count && len > virtio_net_hdr_size + 14) {
    uint8_t *frame = virtio_rx_buf[id] + virtio_net_hdr_size;
    size_t frame_len = len - virtio_net_hdr_size;
    if (frame_len > NET_MAX_FRAME) {
      frame_len = NET_MAX_FRAME;
    }
    size_t payload_len = frame_len - 14;
    if (payload_len <= payload_cap) {
      if (src_mac) {
        memcpy(src_mac, frame + 6, 6);
      }
      *ether_type = ((uint16_t)frame[12] << 8) | frame[13];
      memcpy(payload, frame + 14, payload_len);
      result = (int)payload_len;
    }
  }

  if (id < virtio_rx_count) {
    virtio_rxq.desc[id].addr = net_phys_addr(virtio_rx_buf[id]);
    virtio_rxq.desc[id].len = sizeof(virtio_rx_buf[id]);
    virtio_rxq.desc[id].flags = VIRTQ_DESC_F_WRITE;
    virtio_rxq.desc[id].next = 0;
    net_io_barrier();
    virtio_rxq.avail->ring[virtio_rxq.avail->idx % virtio_rxq.size] =
        (uint16_t)id;
    net_io_barrier();
    virtio_rxq.avail->idx++;
    net_io_barrier();
    virtio_notify_queue(0, &virtio_rxq);
  }

  return result;
}

static int virtio_net_init_legacy_device(pci_device_info_t *dev) {
  if (!dev) {
    return -1;
  }

  net_status_state.present = 1;
  net_status_state.vendor_id = dev->vendor_id;
  net_status_state.device_id = dev->device_id;
  net_status_state.driver = "virtio-net";
  net_driver_kind = NET_DRIVER_VIRTIO;
  net_status_state.initialized = 0;
  net_status_state.link_up = 0;
  virtio_modern = 0;
  virtio_net_hdr_size = VIRTIO_NET_HDR_LEGACY_SIZE;

  uint16_t io_base = 0;
  if (virtio_find_legacy_io_bar(dev, &io_base) != 0) {
    net_set_status("network: virtio legacy I/O BAR missing");
    return -1;
  }

  uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  cmd |= (1U << 0) | (1U << 2); /* I/O space + bus master */
  pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);

  virtio_io_base = io_base;
  virtio_device_status_write(0);
  virtio_device_status_add(VIRTIO_STATUS_ACKNOWLEDGE);
  virtio_device_status_add(VIRTIO_STATUS_DRIVER);

  uint32_t host_features = virtio_read32(VIRTIO_PCI_HOST_FEATURES);
  virtio_guest_features = host_features & VIRTIO_NET_F_MAC;
  virtio_write32(VIRTIO_PCI_GUEST_FEATURES, virtio_guest_features);

  if (virtio_legacy_queue_setup(0, &virtio_rxq, virtio_rx_queue_mem) != 0 ||
      virtio_legacy_queue_setup(1, &virtio_txq, virtio_tx_queue_mem) != 0) {
    virtio_device_fail();
    net_set_status("network: virtio queue setup failed");
    return -1;
  }

  virtio_tx_count = virtio_txq.size;
  if (virtio_tx_count > VIRTIO_TX_DESC_COUNT) {
    virtio_tx_count = VIRTIO_TX_DESC_COUNT;
  }
  if (virtio_tx_count == 0) {
    virtio_device_fail();
    net_set_status("network: virtio TX queue unavailable");
    return -1;
  }

  memset(virtio_tx_buf, 0, sizeof(virtio_tx_buf));
  memset(virtio_rx_buf, 0, sizeof(virtio_rx_buf));
  memset(virtio_tx_inflight, 0, sizeof(virtio_tx_inflight));
  virtio_tx_next = 0;

  virtio_net_read_mac();
  virtio_net_seed_rx();
  virtio_device_status_add(VIRTIO_STATUS_DRIVER_OK);
  (void)virtio_read8(VIRTIO_PCI_ISR);

  net_status_state.initialized = 1;
  virtio_net_refresh_link();
  net_set_status(net_status_state.link_up ? "network: virtio link up"
                                          : "network: virtio ready, link down");
  return net_status_state.link_up ? 0 : -1;
}

static int virtio_net_init_modern_device(pci_device_info_t *dev) {
  if (!dev) {
    return -1;
  }

  net_status_state.present = 1;
  net_status_state.vendor_id = dev->vendor_id;
  net_status_state.device_id = dev->device_id;
  net_status_state.driver = "virtio-net";
  net_driver_kind = NET_DRIVER_VIRTIO;
  net_status_state.initialized = 0;
  net_status_state.link_up = 0;
  virtio_modern = 1;
  virtio_net_hdr_size = VIRTIO_NET_HDR_MODERN_SIZE;
  virtio_io_base = 0;

  uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  cmd |= (1U << 1) | (1U << 2); /* memory space + bus master */
  pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);

  if (virtio_modern_discover(dev) != 0) {
    net_set_status("network: virtio modern caps missing");
    return -1;
  }

  virtio_device_status_write(0);
  virtio_device_status_add(VIRTIO_STATUS_ACKNOWLEDGE);
  virtio_device_status_add(VIRTIO_STATUS_DRIVER);

  virtio_common_cfg->device_feature_select = 0;
  net_io_barrier();
  uint32_t host_low = virtio_common_cfg->device_feature;
  virtio_common_cfg->device_feature_select = 1;
  net_io_barrier();
  uint32_t host_high = virtio_common_cfg->device_feature;
  if ((host_high & VIRTIO_F_VERSION_1_HIGH) == 0) {
    virtio_device_fail();
    net_set_status("network: virtio modern version bit missing");
    return -1;
  }

  uint32_t guest_low = host_low & VIRTIO_NET_F_MAC;
  uint32_t guest_high = VIRTIO_F_VERSION_1_HIGH;
  virtio_guest_features = guest_low;

  virtio_common_cfg->driver_feature_select = 0;
  virtio_common_cfg->driver_feature = guest_low;
  virtio_common_cfg->driver_feature_select = 1;
  virtio_common_cfg->driver_feature = guest_high;
  net_io_barrier();
  virtio_device_status_add(VIRTIO_STATUS_FEATURES_OK);
  if ((virtio_device_status_read() & VIRTIO_STATUS_FEATURES_OK) == 0) {
    virtio_device_fail();
    net_set_status("network: virtio feature negotiation failed");
    return -1;
  }

  if (virtio_modern_queue_setup(0, &virtio_rxq, virtio_rx_queue_mem) != 0 ||
      virtio_modern_queue_setup(1, &virtio_txq, virtio_tx_queue_mem) != 0) {
    virtio_device_fail();
    net_set_status("network: virtio modern queue setup failed");
    return -1;
  }

  virtio_tx_count = virtio_txq.size;
  if (virtio_tx_count > VIRTIO_TX_DESC_COUNT) {
    virtio_tx_count = VIRTIO_TX_DESC_COUNT;
  }
  if (virtio_tx_count == 0) {
    virtio_device_fail();
    net_set_status("network: virtio TX queue unavailable");
    return -1;
  }

  memset(virtio_tx_buf, 0, sizeof(virtio_tx_buf));
  memset(virtio_rx_buf, 0, sizeof(virtio_rx_buf));
  memset(virtio_tx_inflight, 0, sizeof(virtio_tx_inflight));
  virtio_tx_next = 0;

  virtio_net_read_mac();
  virtio_net_seed_rx();
  virtio_device_status_add(VIRTIO_STATUS_DRIVER_OK);
  (void)*virtio_isr_cfg;

  net_status_state.initialized = 1;
  virtio_net_refresh_link();
  net_set_status(net_status_state.link_up ? "network: virtio link up"
                                          : "network: virtio ready, link down");
  return net_status_state.link_up ? 0 : -1;
}

static int virtio_net_init_device(pci_device_info_t *dev) {
  if (!dev) {
    return -1;
  }

  if (dev->device_id >= 0x1040) {
    return virtio_net_init_modern_device(dev);
  }

  if (virtio_net_init_legacy_device(dev) == 0) {
    return 0;
  }
  return virtio_net_init_modern_device(dev);
}

int net_init(void) {
  pci_device_info_t devs[8];
  int count;

  if (net_status_state.initialized) {
    if (net_driver_kind == NET_DRIVER_RTL8139) {
      rtl8139_refresh_link();
    } else if (net_driver_kind == NET_DRIVER_VIRTIO) {
      virtio_net_refresh_link();
    } else if (net_driver_kind == NET_DRIVER_USB) {
      usb_net_refresh_link();
    } else {
      e1000_refresh_link();
    }
    return net_status_state.link_up ? 0 : -1;
  }

  count = pci_scan_class(0x02, 0x00, 0xFF, devs, 8);
  if (count <= 0) {
    usb_net_info_t usb_info;
    if (usb_get_net_info(&usb_info) == 0) {
      net_status_state.present = 1;
      net_status_state.vendor_id = usb_info.vendor_id;
      net_status_state.device_id = usb_info.product_id;
      if (usb_net_ready()) {
        net_driver_kind = NET_DRIVER_USB;
        net_status_state.driver = usb_info.driver_hint;
        net_status_state.initialized = 1;
        net_status_state.link_up = usb_info.link_up;
        memcpy(net_status_state.mac, usb_info.mac, sizeof(net_status_state.mac));
        net_set_status(net_status_state.link_up
                           ? "network: USB Ethernet link up"
                           : "network: USB Ethernet ready, link down");
        return net_status_state.link_up ? 0 : -1;
      }
      net_status_state.driver = "usb-ethernet-pending";
      net_status_state.initialized = 0;
      net_status_state.link_up = 0;
      net_set_status("network: USB Ethernet detected, driver pending");
      return -1;
    }
    net_set_status("network: no ethernet controller");
    return -1;
  }

  pci_device_info_t *dev = NULL;
  pci_device_info_t *rtl_dev = NULL;
  pci_device_info_t *virtio_dev = NULL;
  for (int i = 0; i < count; i++) {
    if (devs[i].vendor_id == 0x8086 && is_supported_intel_nic(devs[i].device_id)) {
      dev = &devs[i];
      break;
    }
    if (!rtl_dev && is_supported_rtl8139(devs[i].vendor_id, devs[i].device_id)) {
      rtl_dev = &devs[i];
    }
    if (!virtio_dev &&
        is_supported_virtio_net(devs[i].vendor_id, devs[i].device_id)) {
      virtio_dev = &devs[i];
    }
  }

  if (!dev) {
    if (virtio_dev && virtio_net_init_device(virtio_dev) == 0) {
      return 0;
    }
    if (rtl_dev) {
      return rtl8139_init_device(rtl_dev);
    }
    if (virtio_dev) {
      return -1;
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
    } else if (net_driver_kind == NET_DRIVER_VIRTIO) {
      virtio_net_refresh_link();
    } else if (net_driver_kind == NET_DRIVER_USB) {
      usb_net_refresh_link();
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
  } else if (net_driver_kind == NET_DRIVER_VIRTIO) {
    virtio_net_refresh_link();
  } else if (net_driver_kind == NET_DRIVER_USB) {
    usb_net_refresh_link();
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
  } else if (net_driver_kind == NET_DRIVER_VIRTIO) {
    virtio_net_refresh_link();
  } else if (net_driver_kind == NET_DRIVER_USB) {
    usb_net_refresh_link();
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

  if (net_driver_kind == NET_DRIVER_VIRTIO) {
    return virtio_net_send_ethernet(dst_mac, ether_type, payload, payload_len);
  }

  if (net_driver_kind == NET_DRIVER_USB) {
    memcpy(usb_tx_frame, dst_mac, 6);
    memcpy(usb_tx_frame + 6, net_status_state.mac, 6);
    usb_tx_frame[12] = (uint8_t)(ether_type >> 8);
    usb_tx_frame[13] = (uint8_t)(ether_type & 0xFF);
    if (payload_len > 0) {
      memcpy(usb_tx_frame + 14, payload, payload_len);
    }
    size_t frame_len = payload_len + 14;
    if (frame_len < 60) {
      memset(usb_tx_frame + frame_len, 0, 60 - frame_len);
      frame_len = 60;
    }
    return usb_net_send_frame(usb_tx_frame, frame_len);
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

  if (net_driver_kind == NET_DRIVER_VIRTIO) {
    return virtio_net_recv_ethernet(src_mac, ether_type, payload, payload_cap);
  }

  if (net_driver_kind == NET_DRIVER_USB) {
    int frame_len = usb_net_recv_frame(usb_rx_frame, sizeof(usb_rx_frame));
    if (frame_len <= 0) {
      return frame_len;
    }
    if (frame_len < 14) {
      return -1;
    }
    if (src_mac) {
      memcpy(src_mac, usb_rx_frame + 6, 6);
    }
    *ether_type = (uint16_t)(((uint16_t)usb_rx_frame[12] << 8) |
                             usb_rx_frame[13]);
    size_t payload_len = (size_t)frame_len - 14;
    if (payload_len > payload_cap) {
      payload_len = payload_cap;
    }
    memcpy(payload, usb_rx_frame + 14, payload_len);
    return (int)payload_len;
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
