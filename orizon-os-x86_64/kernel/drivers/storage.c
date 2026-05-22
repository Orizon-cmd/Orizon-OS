/*
 * Orizon OS x86_64 - Minimal AHCI/NVMe/VirtIO block storage
 *
 * This keeps the storage surface small while the OS is still young: AHCI for
 * the current VM path, a first NVMe namespace path for modern machines, and
 * legacy plus modern VirtIO-blk queues for VM storage profiles.
 */

#include "../include/storage.h"
#include "../include/gui.h"
#include "../include/mmio.h"
#include "../include/pci.h"
#include "../include/sha256.h"
#include "../include/string.h"

#define SATA_SIG_ATA 0x00000101U

#define AHCI_GHC_AE (1U << 31)
#define AHCI_PORT_CMD_ST (1U << 0)
#define AHCI_PORT_CMD_SUD (1U << 1)
#define AHCI_PORT_CMD_POD (1U << 2)
#define AHCI_PORT_CMD_FRE (1U << 4)
#define AHCI_PORT_CMD_FR (1U << 14)
#define AHCI_PORT_CMD_CR (1U << 15)
#define AHCI_PORT_IS_TFES (1U << 30)

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35

#define NVME_ADMIN_QUEUE_DEPTH 32
#define NVME_IO_QUEUE_DEPTH 64
#define NVME_REG_CAP 0x0000
#define NVME_REG_CC 0x0014
#define NVME_REG_CSTS 0x001C
#define NVME_REG_AQA 0x0024
#define NVME_REG_ASQ 0x0028
#define NVME_REG_ACQ 0x0030
#define NVME_DOORBELL_BASE 0x1000
#define NVME_CC_EN 0x00000001U
#define NVME_CC_IOSQES_SHIFT 16
#define NVME_CC_IOCQES_SHIFT 20
#define NVME_CSTS_RDY 0x00000001U
#define NVME_ADMIN_CREATE_IO_SQ 0x01
#define NVME_ADMIN_CREATE_IO_CQ 0x05
#define NVME_ADMIN_IDENTIFY 0x06
#define NVME_CMD_WRITE 0x01
#define NVME_CMD_READ 0x02
#define VIRTIO_PCI_VENDOR 0x1AF4
#define VIRTIO_PCI_DEVICE_BLK_LEGACY 0x1001
#define VIRTIO_PCI_DEVICE_SCSI_LEGACY 0x1004
#define VIRTIO_PCI_DEVICE_BLK_MODERN 0x1042
#define VIRTIO_PCI_DEVICE_SCSI_MODERN 0x1048
#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_NUM 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_ISR 0x13
#define VIRTIO_PCI_CONFIG_NO_MSIX 0x14
#define VIRTIO_PCI_CONFIG_MSIX 0x18
#define VIRTIO_STATUS_ACKNOWLEDGE 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_FAILED 0x80
#define VIRTIO_MSI_NO_VECTOR 0xFFFFU
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_F_VERSION_1_HIGH 0x00000001U
#define VIRTIO_COMMON_DEVICE_FEATURE_SELECT 0x00
#define VIRTIO_COMMON_DEVICE_FEATURE 0x04
#define VIRTIO_COMMON_DRIVER_FEATURE_SELECT 0x08
#define VIRTIO_COMMON_DRIVER_FEATURE 0x0C
#define VIRTIO_COMMON_MSIX_CONFIG 0x10
#define VIRTIO_COMMON_NUM_QUEUES 0x12
#define VIRTIO_COMMON_DEVICE_STATUS 0x14
#define VIRTIO_COMMON_CONFIG_GENERATION 0x15
#define VIRTIO_COMMON_QUEUE_SELECT 0x16
#define VIRTIO_COMMON_QUEUE_SIZE 0x18
#define VIRTIO_COMMON_QUEUE_MSIX_VECTOR 0x1A
#define VIRTIO_COMMON_QUEUE_ENABLE 0x1C
#define VIRTIO_COMMON_QUEUE_NOTIFY_OFF 0x1E
#define VIRTIO_COMMON_QUEUE_DESC 0x20
#define VIRTIO_COMMON_QUEUE_DRIVER 0x28
#define VIRTIO_COMMON_QUEUE_DEVICE 0x30
#define VIRTIO_BLK_F_RO (1U << 5)
#define VIRTIO_BLK_T_IN 0U
#define VIRTIO_BLK_T_OUT 1U
#define VIRTIO_BLK_S_OK 0U
#define VIRTIO_QUEUE_MAX 128U
#define VIRTQ_DESC_F_NEXT 1U
#define VIRTQ_DESC_F_WRITE 2U
#define PCI_CAP_ID_MSIX 0x11
#define STORAGE_LOG_SIZE 4096

typedef enum {
  STORAGE_DRIVER_NONE = 0,
  STORAGE_DRIVER_AHCI,
  STORAGE_DRIVER_NVME,
  STORAGE_DRIVER_VIRTIO_BLK,
} storage_driver_t;

typedef struct {
  storage_driver_t driver;
  void *ahci_port;
  uint64_t sectors;
  char name[24];
  char model[64];
  int writable;
} storage_device_t;

typedef volatile struct {
  uint32_t clb;
  uint32_t clbu;
  uint32_t fb;
  uint32_t fbu;
  uint32_t is;
  uint32_t ie;
  uint32_t cmd;
  uint32_t rsv0;
  uint32_t tfd;
  uint32_t sig;
  uint32_t ssts;
  uint32_t sctl;
  uint32_t serr;
  uint32_t sact;
  uint32_t ci;
  uint32_t sntf;
  uint32_t fbs;
  uint32_t rsv1[11];
  uint32_t vendor[4];
} ahci_port_t;

typedef volatile struct {
  uint32_t cap;
  uint32_t ghc;
  uint32_t is;
  uint32_t pi;
  uint32_t vs;
  uint32_t ccc_ctl;
  uint32_t ccc_pts;
  uint32_t em_loc;
  uint32_t em_ctl;
  uint32_t cap2;
  uint32_t bohc;
  uint32_t rsv[29];
  uint32_t vendor[24];
  ahci_port_t ports[32];
} ahci_mem_t;

typedef struct {
  uint16_t flags;
  uint16_t prdtl;
  uint32_t prdbc;
  uint32_t ctba;
  uint32_t ctbau;
  uint32_t rsv[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct {
  uint32_t dba;
  uint32_t dbau;
  uint32_t rsv0;
  uint32_t dbc;
} __attribute__((packed)) ahci_prdt_t;

typedef struct {
  uint8_t cfis[64];
  uint8_t acmd[16];
  uint8_t rsv[48];
  ahci_prdt_t prdt[1];
} __attribute__((packed)) ahci_cmd_table_t;

typedef struct {
  uint8_t opcode;
  uint8_t flags;
  uint16_t cid;
  uint32_t nsid;
  uint64_t rsv0;
  uint64_t mptr;
  uint64_t prp1;
  uint64_t prp2;
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
} __attribute__((packed)) nvme_cmd_t;

typedef struct {
  uint32_t result;
  uint32_t rsv0;
  uint16_t sq_head;
  uint16_t sq_id;
  uint16_t cid;
  uint16_t status;
} __attribute__((packed)) nvme_cqe_t;

typedef struct {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed)) virtq_desc_t;

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
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
} __attribute__((packed)) virtio_blk_req_header_t;

typedef struct {
  uint8_t bar;
  uint32_t offset;
  uint32_t length;
  uint32_t notify_multiplier;
} virtio_pci_cap_info_t;

static ahci_mem_t *hba = NULL;
static ahci_port_t *disk_port = NULL;
static storage_driver_t storage_driver = STORAGE_DRIVER_NONE;
static int disk_ready = 0;
static uint64_t disk_sectors = 0;
static const char *disk_status = "storage: not initialized";
static int storage_scanned = 0;
static int storage_device_total = 0;
static int storage_selected_index = -1;
static char selected_status[96] = "storage: not initialized";
static char storage_blocker[192] = "";
static storage_device_t storage_devices[ORIZON_STORAGE_MAX_DEVICES];
static char storage_log[STORAGE_LOG_SIZE];
static size_t storage_log_used = 0;

static ahci_cmd_header_t cmd_list[32] __attribute__((aligned(1024)));
static uint8_t fis_area[256] __attribute__((aligned(256)));
static ahci_cmd_table_t cmd_tables[32] __attribute__((aligned(128)));

static volatile uint8_t *nvme_mmio = NULL;
static uint32_t nvme_db_stride = 4;
static int nvme_controller_count = 0;
static int nvme_controller_seen = 0;
static int nvme_namespace_ready = 0;
static uint32_t nvme_namespace_id = 1;
static uint32_t nvme_lba_size = ORIZON_SECTOR_SIZE;
static uint32_t nvme_lba_scale = 1;
static uint16_t nvme_next_cid = 1;
static uint16_t nvme_admin_sq_tail = 0;
static uint16_t nvme_admin_cq_head = 0;
static uint8_t nvme_admin_cq_phase = 1;
static uint16_t nvme_io_sq_tail = 0;
static uint16_t nvme_io_cq_head = 0;
static uint8_t nvme_io_cq_phase = 1;

static nvme_cmd_t nvme_admin_sq[NVME_ADMIN_QUEUE_DEPTH]
    __attribute__((aligned(4096)));
static nvme_cqe_t nvme_admin_cq[NVME_ADMIN_QUEUE_DEPTH]
    __attribute__((aligned(4096)));
static nvme_cmd_t nvme_io_sq[NVME_IO_QUEUE_DEPTH]
    __attribute__((aligned(4096)));
static nvme_cqe_t nvme_io_cq[NVME_IO_QUEUE_DEPTH]
    __attribute__((aligned(4096)));
static uint8_t nvme_identify_buf[4096] __attribute__((aligned(4096)));
static uint8_t nvme_lba_scratch[4096] __attribute__((aligned(4096)));
static char nvme_model[64] = "NVMe namespace 1";
static uint64_t nvme_last_cap = 0;
static uint32_t nvme_last_cc = 0;
static uint32_t nvme_last_csts = 0;
static uint16_t nvme_last_cqe_status = 0;
static uint16_t nvme_last_cqe_cid = 0;
static uint16_t nvme_last_cmd_cid = 0;
static uint8_t storage_read_test_buf[ORIZON_SECTOR_SIZE]
    __attribute__((aligned(4096)));

static uint16_t virtio_blk_io_base = 0;
static uint16_t virtio_blk_queue_size = 0;
static uint16_t virtio_blk_avail_idx = 0;
static uint16_t virtio_blk_used_idx = 0;
static uint16_t virtio_blk_used_offset = 0;
static uint32_t virtio_blk_features = 0;
static uint64_t virtio_blk_capacity = 0;
static int virtio_blk_count = 0;
static int virtio_blk_seen = 0;
static int virtio_blk_ready = 0;
static int virtio_blk_writable = 1;
static int virtio_blk_modern_only = 0;
static int virtio_blk_modern = 0;
static int virtio_scsi_count = 0;
static uint8_t virtio_blk_last_status = 0;
static uint8_t virtio_blk_last_req_status = 0xFF;
static char virtio_blk_model[64] = "VirtIO block device";
static volatile uint8_t *virtio_blk_common_mmio = NULL;
static volatile uint8_t *virtio_blk_notify_mmio = NULL;
static volatile uint8_t *virtio_blk_device_mmio = NULL;
static uint32_t virtio_blk_notify_multiplier = 0;
static uint16_t virtio_blk_queue_notify_off = 0;
static uint8_t virtio_blk_queue[8192] __attribute__((aligned(4096)));
static virtio_blk_req_header_t virtio_blk_req_header
    __attribute__((aligned(16)));
static uint8_t virtio_blk_req_status __attribute__((aligned(16)));

static void storage_log_append(const char *text) {
  size_t len;

  if (!text || !text[0]) {
    return;
  }
  len = strlen(text);
  if (len + 1 >= STORAGE_LOG_SIZE) {
    text += len - (STORAGE_LOG_SIZE / 2);
    len = strlen(text);
    storage_log_used = 0;
  }
  if (storage_log_used + len + 2 >= STORAGE_LOG_SIZE) {
    size_t keep = STORAGE_LOG_SIZE / 2;
    if (keep > storage_log_used) {
      keep = storage_log_used;
    }
    memmove(storage_log, storage_log + storage_log_used - keep, keep);
    storage_log_used = keep;
    storage_log[storage_log_used] = '\0';
  }
  memcpy(storage_log + storage_log_used, text, len);
  storage_log_used += len;
  if (storage_log_used + 1 < STORAGE_LOG_SIZE) {
    storage_log[storage_log_used++] = '\n';
  }
  storage_log[storage_log_used] = '\0';
}

static uint64_t storage_phys_addr(const void *ptr) {
  uint64_t v = (uint64_t)(uintptr_t)ptr;
  if (kernel_phys_base && kernel_virt_base && v >= kernel_virt_base) {
    return kernel_phys_base + (v - kernel_virt_base);
  }
  if (v >= hhdm_offset) {
    return v - hhdm_offset;
  }
  return v;
}

static void set_status(const char *status) {
  disk_status = status;
  storage_log_append(status);
  serial_puts("[storage] ");
  serial_puts(status);
  serial_puts("\n");
}

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline uint16_t inw(uint16_t port) {
  uint16_t ret;
  __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static void io_barrier(void) {
  __asm__ volatile("" ::: "memory");
}

static void storage_set_blocker(const pci_device_info_t *dev,
                                const char *reason) {
  if (storage_blocker[0] || !dev || !reason) {
    return;
  }
  snprintf(storage_blocker, sizeof(storage_blocker),
           "storage: %s at %02x:%02x.%u vendor=%04x device=%04x "
           "class=%02x/%02x/%02x",
           reason, dev->bus, dev->device, dev->function, dev->vendor_id,
           dev->device_id, dev->class_code, dev->subclass, dev->prog_if);
  storage_log_append(storage_blocker);
}

static int storage_is_intel_vmd_rst(const pci_device_info_t *dev) {
  if (!dev || dev->vendor_id != 0x8086) {
    return 0;
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x04) {
    return 1;
  }
  if (dev->class_code == 0x08 &&
      (dev->subclass == 0x07 || dev->subclass == 0x80)) {
    return 1;
  }
  return 0;
}

static int storage_is_virtio_blk(const pci_device_info_t *dev) {
  return dev && dev->vendor_id == VIRTIO_PCI_VENDOR &&
         (dev->device_id == VIRTIO_PCI_DEVICE_BLK_LEGACY ||
          dev->device_id == VIRTIO_PCI_DEVICE_BLK_MODERN);
}

static int storage_is_virtio_scsi(const pci_device_info_t *dev) {
  return dev && dev->vendor_id == VIRTIO_PCI_VENDOR &&
         (dev->device_id == VIRTIO_PCI_DEVICE_SCSI_LEGACY ||
          dev->device_id == VIRTIO_PCI_DEVICE_SCSI_MODERN);
}

static const char *storage_candidate_hint(const pci_device_info_t *dev) {
  if (!dev) {
    return "unknown";
  }
  if (storage_is_virtio_blk(dev)) {
    return (dev->bar[0] & 0x01) ? "virtio-blk-legacy-supported"
                                : "virtio-blk-modern-supported";
  }
  if (storage_is_virtio_scsi(dev)) {
    return "virtio-scsi-diagnostic-only";
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x08) {
    return dev->prog_if == 0x02 ? "nvme-supported" : "nvme-unusual-prog-if";
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x06) {
    return dev->prog_if == 0x01 ? "ahci-supported" : "sata-non-ahci";
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x04) {
    return dev->vendor_id == 0x8086 ? "intel-rst-vmd-unsupported"
                                    : "raid-unsupported";
  }
  if (storage_is_intel_vmd_rst(dev)) {
    return "intel-vmd-rst-diagnostic-only";
  }
  if (dev->class_code == 0x08 && dev->subclass == 0x05) {
    return "sdhci-emmc-unsupported";
  }
  if (dev->class_code == 0x01) {
    return "mass-storage-unsupported";
  }
  return "not-storage";
}

static int storage_is_candidate(const pci_device_info_t *dev) {
  if (!dev) {
    return 0;
  }
  return storage_is_virtio_blk(dev) || storage_is_virtio_scsi(dev) ||
         dev->class_code == 0x01 || storage_is_intel_vmd_rst(dev) ||
         (dev->class_code == 0x08 && dev->subclass == 0x05);
}

static void storage_detect_blockers(void) {
  pci_device_info_t devs[96];
  int total = pci_scan_all(devs, 96);

  for (int i = 0; i < total && i < 96; i++) {
    const pci_device_info_t *dev = &devs[i];
    if (storage_is_intel_vmd_rst(dev)) {
      storage_set_blocker(
          dev,
          "Intel VMD/RST may hide NVMe behind a remapped PCI domain; true VMD driver not implemented");
      return;
    }
    if (dev->class_code == 0x08 && dev->subclass == 0x05) {
      storage_set_blocker(dev, "SDHCI/eMMC storage needs an eMMC driver");
      return;
    }
  }

  for (int i = 0; i < total && i < 96; i++) {
    const pci_device_info_t *dev = &devs[i];
    if (storage_is_candidate(dev)) {
      storage_set_blocker(dev, storage_candidate_hint(dev));
      return;
    }
  }
}

static const char *driver_name(storage_driver_t driver) {
  if (driver == STORAGE_DRIVER_NVME) {
    return "NVMe";
  }
  if (driver == STORAGE_DRIVER_AHCI) {
    return "AHCI";
  }
  if (driver == STORAGE_DRIVER_VIRTIO_BLK) {
    return "VirtIO-blk";
  }
  return "none";
}

static int storage_add_device(storage_driver_t driver, void *ahci_port,
                              uint64_t sectors, const char *model) {
  storage_device_t *dev;
  int index;

  if (storage_device_total >= ORIZON_STORAGE_MAX_DEVICES || sectors == 0) {
    return -1;
  }

  index = storage_device_total++;
  dev = &storage_devices[index];
  memset(dev, 0, sizeof(*dev));
  dev->driver = driver;
  dev->ahci_port = ahci_port;
  dev->sectors = sectors;
  dev->writable =
      driver == STORAGE_DRIVER_VIRTIO_BLK ? virtio_blk_writable : 1;
  snprintf(dev->name, sizeof(dev->name), "disk%d", index);
  snprintf(dev->model, sizeof(dev->model), "%s",
           model && model[0] ? model : driver_name(driver));
  {
    char line[160];
    snprintf(line, sizeof(line), "storage: registered %s driver=%s sectors=%lu",
             dev->name, driver_name(driver), (unsigned long)sectors);
    storage_log_append(line);
  }
  return index;
}

static void select_status_from_device(const storage_device_t *dev) {
  char capacity[40];
  storage_format_size(dev->sectors, capacity, sizeof(capacity));
  snprintf(selected_status, sizeof(selected_status), "storage: %s %s ready",
           dev->name, driver_name(dev->driver));
  disk_status = selected_status;
  serial_puts("[storage] selected ");
  serial_puts(dev->name);
  serial_puts(" ");
  serial_puts(driver_name(dev->driver));
  serial_puts(" ");
  serial_puts(capacity);
  serial_puts("\n");
}

static uint8_t storage_pci_read8(const pci_device_info_t *dev, uint8_t offset) {
  uint32_t val = pci_read32(dev->bus, dev->device, dev->function, offset);
  return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

static int storage_pci_has_cap(const pci_device_info_t *dev, uint8_t cap_id) {
  uint32_t status = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  uint8_t cap = storage_pci_read8(dev, 0x34) & 0xFC;

  if ((status & (1U << 20)) == 0) {
    return 0;
  }
  for (int i = 0; i < 32 && cap >= 0x40; i++) {
    uint8_t id = storage_pci_read8(dev, cap);
    uint8_t next = storage_pci_read8(dev, cap + 1) & 0xFC;
    if (id == cap_id) {
      return 1;
    }
    if (next == cap) {
      break;
    }
    cap = next;
  }
  return 0;
}

static uint64_t storage_pci_bar_base(const pci_device_info_t *dev, uint8_t bar) {
  uint32_t raw;
  uint64_t base;

  if (!dev || bar >= 6) {
    return 0;
  }
  raw = dev->bar[bar];
  if (raw & 0x01) {
    return raw & ~0x3ULL;
  }
  base = raw & ~0xFULL;
  if ((raw & 0x06) == 0x04 && bar + 1 < 6) {
    base |= (uint64_t)dev->bar[bar + 1] << 32;
  }
  return base;
}

static int virtio_find_pci_cap(const pci_device_info_t *dev, uint8_t cfg_type,
                               virtio_pci_cap_info_t *out) {
  uint32_t status = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  uint8_t cap = storage_pci_read8(dev, 0x34) & 0xFC;

  if (!out || (status & (1U << 20)) == 0) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  for (int i = 0; i < 48 && cap >= 0x40; i++) {
    uint8_t id = storage_pci_read8(dev, cap);
    uint8_t next = storage_pci_read8(dev, cap + 1) & 0xFC;
    uint8_t len = storage_pci_read8(dev, cap + 2);
    uint8_t type = storage_pci_read8(dev, cap + 3);
    if (id == 0x09 && type == cfg_type && len >= 16) {
      out->bar = storage_pci_read8(dev, cap + 4);
      out->offset = pci_read32(dev->bus, dev->device, dev->function, cap + 8);
      out->length = pci_read32(dev->bus, dev->device, dev->function, cap + 12);
      if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG && len >= 20) {
        out->notify_multiplier =
            pci_read32(dev->bus, dev->device, dev->function, cap + 16);
      }
      return 0;
    }
    if (next == cap) {
      break;
    }
    cap = next;
  }
  return -1;
}

static uint8_t mmio_read8(volatile uint8_t *base, uint32_t off) {
  return *(volatile uint8_t *)(base + off);
}

static uint16_t mmio_read16(volatile uint8_t *base, uint32_t off) {
  return *(volatile uint16_t *)(base + off);
}

static uint32_t mmio_read32(volatile uint8_t *base, uint32_t off) {
  return *(volatile uint32_t *)(base + off);
}

static uint64_t mmio_read64(volatile uint8_t *base, uint32_t off) {
  uint64_t lo = mmio_read32(base, off);
  uint64_t hi = mmio_read32(base, off + 4);
  return lo | (hi << 32);
}

static void mmio_write8(volatile uint8_t *base, uint32_t off, uint8_t value) {
  *(volatile uint8_t *)(base + off) = value;
}

static void mmio_write16(volatile uint8_t *base, uint32_t off, uint16_t value) {
  *(volatile uint16_t *)(base + off) = value;
}

static void mmio_write32(volatile uint8_t *base, uint32_t off, uint32_t value) {
  *(volatile uint32_t *)(base + off) = value;
}

static void mmio_write64(volatile uint8_t *base, uint32_t off, uint64_t value) {
  mmio_write32(base, off, (uint32_t)value);
  mmio_write32(base, off + 4, (uint32_t)(value >> 32));
}

static virtq_desc_t *virtio_desc(void) {
  return (virtq_desc_t *)virtio_blk_queue;
}

static volatile uint16_t *virtio_avail(void) {
  return (volatile uint16_t *)(virtio_blk_queue +
                               sizeof(virtq_desc_t) * virtio_blk_queue_size);
}

static volatile virtq_used_t *virtio_used(void) {
  return (volatile virtq_used_t *)(virtio_blk_queue + virtio_blk_used_offset);
}

static uint32_t virtio_align4096(uint32_t value) {
  return (value + 4095U) & ~4095U;
}

static int virtio_queue_layout_ok(uint16_t qnum) {
  uint32_t desc_bytes;
  uint32_t avail_bytes;
  uint32_t used_bytes;
  uint32_t used_offset;

  if (qnum < 3 || qnum > VIRTIO_QUEUE_MAX) {
    return 0;
  }
  desc_bytes = (uint32_t)sizeof(virtq_desc_t) * qnum;
  avail_bytes = 4U + 2U * qnum;
  used_offset = virtio_align4096(desc_bytes + avail_bytes);
  used_bytes = 4U + (uint32_t)sizeof(virtq_used_elem_t) * qnum;
  if (used_offset + used_bytes > sizeof(virtio_blk_queue)) {
    return 0;
  }
  virtio_blk_used_offset = (uint16_t)used_offset;
  return 1;
}

static uint8_t virtio_read_status(void) {
  return inb((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_STATUS));
}

static void virtio_write_status(uint8_t status) {
  outb((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_STATUS), status);
  virtio_blk_last_status = status;
}

static uint8_t virtio_modern_read_status(void) {
  if (!virtio_blk_common_mmio) {
    return 0;
  }
  return mmio_read8(virtio_blk_common_mmio, VIRTIO_COMMON_DEVICE_STATUS);
}

static void virtio_modern_write_status(uint8_t status) {
  if (!virtio_blk_common_mmio) {
    return;
  }
  mmio_write8(virtio_blk_common_mmio, VIRTIO_COMMON_DEVICE_STATUS, status);
  virtio_blk_last_status = status;
}

static void virtio_notify_queue(void) {
  if (virtio_blk_modern) {
    uint32_t off = (uint32_t)virtio_blk_queue_notify_off *
                   virtio_blk_notify_multiplier;
    if (virtio_blk_notify_mmio) {
      mmio_write16(virtio_blk_notify_mmio + off, 0, 0);
    }
    return;
  }
  outw((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0);
}

static int virtio_blk_io(uint64_t lba, void *buf, uint32_t sectors,
                         int write) {
  virtq_desc_t *desc;
  volatile uint16_t *avail;
  volatile virtq_used_t *used;

  if (!disk_ready || storage_driver != STORAGE_DRIVER_VIRTIO_BLK ||
      sectors == 0 || !virtio_blk_ready) {
    return disk_ready ? 0 : -1;
  }

  for (uint32_t i = 0; i < sectors; i++) {
    uint8_t *sector = (uint8_t *)buf + (uint64_t)i * ORIZON_SECTOR_SIZE;
    uint16_t used_before;
    uint16_t slot;

    desc = virtio_desc();
    avail = virtio_avail();
    used = virtio_used();
    memset(desc, 0, sizeof(virtq_desc_t) * virtio_blk_queue_size);

    virtio_blk_req_header.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    virtio_blk_req_header.reserved = 0;
    virtio_blk_req_header.sector = lba + i;
    virtio_blk_req_status = 0xFF;
    virtio_blk_last_req_status = 0xFF;

    desc[0].addr = storage_phys_addr(&virtio_blk_req_header);
    desc[0].len = sizeof(virtio_blk_req_header);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = storage_phys_addr(sector);
    desc[1].len = ORIZON_SECTOR_SIZE;
    desc[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    desc[1].next = 2;

    desc[2].addr = storage_phys_addr(&virtio_blk_req_status);
    desc[2].len = 1;
    desc[2].flags = VIRTQ_DESC_F_WRITE;
    desc[2].next = 0;

    slot = (uint16_t)(virtio_blk_avail_idx % virtio_blk_queue_size);
    used_before = used->idx;
    avail[2 + slot] = 0;
    io_barrier();
    virtio_blk_avail_idx++;
    avail[1] = virtio_blk_avail_idx;
    io_barrier();
    virtio_notify_queue();

    for (int spin = 0; spin < 10000000; spin++) {
      if (used->idx != used_before) {
        virtio_blk_used_idx = used->idx;
        virtio_blk_last_req_status = virtio_blk_req_status;
        if (virtio_blk_req_status != VIRTIO_BLK_S_OK) {
          char line[120];
          snprintf(line, sizeof(line),
                   "storage: VirtIO-blk request failed status=%u lba=%lu write=%s",
                   (unsigned)virtio_blk_req_status, (unsigned long)(lba + i),
                   write ? "yes" : "no");
          storage_log_append(line);
          return -1;
        }
        break;
      }
      __asm__ volatile("pause");
      if (spin == 9999999) {
        storage_log_append("storage: VirtIO-blk request timeout");
        return -1;
      }
    }
  }
  return 0;
}

static int virtio_blk_init_modern(const pci_device_info_t *dev) {
  virtio_pci_cap_info_t common;
  virtio_pci_cap_info_t notify;
  virtio_pci_cap_info_t device;
  uint64_t common_phys;
  uint64_t notify_phys;
  uint64_t device_phys;
  uint32_t features_low;
  uint32_t features_high;
  uint32_t driver_features_low;
  uint16_t qmax;
  uint16_t qnum;
  uint8_t status;
  char line[224];

  if (!dev) {
    return -1;
  }

  if (virtio_find_pci_cap(dev, VIRTIO_PCI_CAP_COMMON_CFG, &common) != 0 ||
      virtio_find_pci_cap(dev, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify) != 0 ||
      virtio_find_pci_cap(dev, VIRTIO_PCI_CAP_DEVICE_CFG, &device) != 0) {
    set_status("storage: VirtIO-blk modern PCI caps incomplete");
    return -1;
  }

  common_phys = storage_pci_bar_base(dev, common.bar);
  notify_phys = storage_pci_bar_base(dev, notify.bar);
  device_phys = storage_pci_bar_base(dev, device.bar);
  if (!common_phys || !notify_phys || !device_phys || common.bar >= 6 ||
      notify.bar >= 6 || device.bar >= 6) {
    set_status("storage: VirtIO-blk modern BAR mapping unavailable");
    return -1;
  }
  common_phys += common.offset;
  notify_phys += notify.offset;
  device_phys += device.offset;

  {
    uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1U << 1) | (1U << 2);
    pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);
  }

  virtio_blk_common_mmio = (volatile uint8_t *)(uintptr_t)mmio_map_range(
      common_phys, common.length ? common.length : 0x100);
  virtio_blk_notify_mmio = (volatile uint8_t *)(uintptr_t)mmio_map_range(
      notify_phys, notify.length ? notify.length : 0x1000);
  virtio_blk_device_mmio = (volatile uint8_t *)(uintptr_t)mmio_map_range(
      device_phys, device.length ? device.length : 0x100);
  if (!virtio_blk_common_mmio || !virtio_blk_notify_mmio ||
      !virtio_blk_device_mmio) {
    set_status("storage: VirtIO-blk modern MMIO map failed");
    return -1;
  }

  virtio_blk_modern = 1;
  virtio_blk_io_base = 0;
  virtio_blk_notify_multiplier = notify.notify_multiplier;
  virtio_modern_write_status(0);
  for (int spin = 0; spin < 100000; spin++) {
    if (virtio_modern_read_status() == 0) {
      break;
    }
    __asm__ volatile("pause");
  }

  status = VIRTIO_STATUS_ACKNOWLEDGE;
  virtio_modern_write_status(status);
  status |= VIRTIO_STATUS_DRIVER;
  virtio_modern_write_status(status);

  mmio_write32(virtio_blk_common_mmio,
               VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 0);
  features_low =
      mmio_read32(virtio_blk_common_mmio, VIRTIO_COMMON_DEVICE_FEATURE);
  mmio_write32(virtio_blk_common_mmio,
               VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 1);
  features_high =
      mmio_read32(virtio_blk_common_mmio, VIRTIO_COMMON_DEVICE_FEATURE);
  if ((features_high & VIRTIO_F_VERSION_1_HIGH) == 0) {
    virtio_modern_write_status(VIRTIO_STATUS_FAILED);
    set_status("storage: VirtIO-blk modern device missing VERSION_1");
    return -1;
  }

  driver_features_low = features_low & VIRTIO_BLK_F_RO;
  mmio_write32(virtio_blk_common_mmio,
               VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 0);
  mmio_write32(virtio_blk_common_mmio,
               VIRTIO_COMMON_DRIVER_FEATURE, driver_features_low);
  mmio_write32(virtio_blk_common_mmio,
               VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 1);
  mmio_write32(virtio_blk_common_mmio,
               VIRTIO_COMMON_DRIVER_FEATURE, VIRTIO_F_VERSION_1_HIGH);

  status |= VIRTIO_STATUS_FEATURES_OK;
  virtio_modern_write_status(status);
  if ((virtio_modern_read_status() & VIRTIO_STATUS_FEATURES_OK) == 0) {
    virtio_modern_write_status(VIRTIO_STATUS_FAILED);
    set_status("storage: VirtIO-blk feature negotiation rejected");
    return -1;
  }

  mmio_write16(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_SELECT, 0);
  qmax = mmio_read16(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_SIZE);
  qnum = qmax > VIRTIO_QUEUE_MAX ? VIRTIO_QUEUE_MAX : qmax;
  if (!virtio_queue_layout_ok(qnum)) {
    virtio_modern_write_status(VIRTIO_STATUS_FAILED);
    snprintf(line, sizeof(line),
             "storage: VirtIO-blk modern queue unsupported qmax=%lu max=%lu",
             (unsigned long)qmax, (unsigned long)VIRTIO_QUEUE_MAX);
    set_status(line);
    return -1;
  }

  virtio_blk_queue_size = qnum;
  memset(virtio_blk_queue, 0, sizeof(virtio_blk_queue));
  virtio_blk_avail_idx = 0;
  virtio_blk_used_idx = 0;
  virtio_blk_queue_notify_off =
      mmio_read16(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_NOTIFY_OFF);
  mmio_write16(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_SIZE, qnum);
  mmio_write16(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_MSIX_VECTOR,
               VIRTIO_MSI_NO_VECTOR);
  mmio_write64(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_DESC,
               storage_phys_addr(virtio_desc()));
  mmio_write64(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_DRIVER,
               storage_phys_addr((const void *)virtio_avail()));
  mmio_write64(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_DEVICE,
               storage_phys_addr((const void *)virtio_used()));
  mmio_write16(virtio_blk_common_mmio, VIRTIO_COMMON_QUEUE_ENABLE, 1);

  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_modern_write_status(status);
  virtio_blk_last_status = virtio_modern_read_status();
  virtio_blk_capacity = mmio_read64(virtio_blk_device_mmio, 0);
  if (virtio_blk_capacity == 0) {
    virtio_modern_write_status(VIRTIO_STATUS_FAILED);
    set_status("storage: VirtIO-blk modern capacity is zero");
    return -1;
  }

  virtio_blk_features = features_low;
  virtio_blk_writable = (features_low & VIRTIO_BLK_F_RO) ? 0 : 1;
  snprintf(virtio_blk_model, sizeof(virtio_blk_model),
           "VirtIO block modern q=%lu notify=%lu",
           (unsigned long)virtio_blk_queue_size,
           (unsigned long)virtio_blk_queue_notify_off);
  storage_add_device(STORAGE_DRIVER_VIRTIO_BLK, NULL, virtio_blk_capacity,
                     virtio_blk_model);
  virtio_blk_ready = 1;
  virtio_blk_modern_only = 0;
  snprintf(line, sizeof(line),
           "storage: VirtIO-blk modern ready sectors=%lu qnum=%lu writable=%s features=%08lx status=%02x notify-off=%lu multiplier=%lu",
           (unsigned long)virtio_blk_capacity,
           (unsigned long)virtio_blk_queue_size,
           virtio_blk_writable ? "yes" : "no",
           (unsigned long)virtio_blk_features,
           (unsigned)virtio_blk_last_status,
           (unsigned long)virtio_blk_queue_notify_off,
           (unsigned long)virtio_blk_notify_multiplier);
  storage_log_append(line);
  set_status("storage: VirtIO-blk modern disk detected");
  return 0;
}

static int virtio_blk_init_one(const pci_device_info_t *dev) {
  char line[192];
  uint16_t qnum;
  uint8_t status;
  uint16_t config_offset;
  uint32_t cap_lo;
  uint32_t cap_hi;

  if (!dev) {
    return -1;
  }
  virtio_blk_seen = 1;
  virtio_blk_count++;
  snprintf(line, sizeof(line),
           "storage: probing VirtIO-blk %02x:%02x.%u vendor=%04x device=%04x bar0=%08lx",
           dev->bus, dev->device, dev->function, dev->vendor_id,
           dev->device_id, (unsigned long)dev->bar[0]);
  storage_log_append(line);

  if ((dev->bar[0] & 0x01) == 0) {
    virtio_blk_modern_only = 1;
    return virtio_blk_init_modern(dev);
  }

  virtio_blk_modern = 0;
  virtio_blk_io_base = (uint16_t)(dev->bar[0] & ~0x3U);
  {
    uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1U << 0) | (1U << 2);
    pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);
  }

  virtio_write_status(0);
  virtio_write_status(VIRTIO_STATUS_ACKNOWLEDGE);
  virtio_write_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  virtio_blk_features =
      inl((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_HOST_FEATURES));
  virtio_blk_writable = (virtio_blk_features & VIRTIO_BLK_F_RO) ? 0 : 1;
  outl((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_GUEST_FEATURES),
       virtio_blk_features & VIRTIO_BLK_F_RO);

  outw((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_QUEUE_SEL), 0);
  qnum = inw((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_QUEUE_NUM));
  if (!virtio_queue_layout_ok(qnum)) {
    virtio_write_status(VIRTIO_STATUS_FAILED);
    snprintf(line, sizeof(line),
             "storage: VirtIO-blk queue unsupported qnum=%lu max=%lu",
             (unsigned long)qnum, (unsigned long)VIRTIO_QUEUE_MAX);
    set_status(line);
    return -1;
  }
  virtio_blk_queue_size = qnum;
  memset(virtio_blk_queue, 0, sizeof(virtio_blk_queue));
  virtio_blk_avail_idx = 0;
  virtio_blk_used_idx = 0;
  outl((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_QUEUE_PFN),
       (uint32_t)(storage_phys_addr(virtio_blk_queue) >> 12));

  status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
           VIRTIO_STATUS_DRIVER_OK;
  virtio_write_status(status);
  virtio_blk_last_status = virtio_read_status();

  config_offset = storage_pci_has_cap(dev, PCI_CAP_ID_MSIX)
                      ? VIRTIO_PCI_CONFIG_MSIX
                      : VIRTIO_PCI_CONFIG_NO_MSIX;
  cap_lo = inl((uint16_t)(virtio_blk_io_base + config_offset));
  cap_hi = inl((uint16_t)(virtio_blk_io_base + config_offset + 4));
  virtio_blk_capacity = ((uint64_t)cap_hi << 32) | cap_lo;
  if (virtio_blk_capacity == 0 && config_offset == VIRTIO_PCI_CONFIG_MSIX) {
    cap_lo = inl((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_CONFIG_NO_MSIX));
    cap_hi =
        inl((uint16_t)(virtio_blk_io_base + VIRTIO_PCI_CONFIG_NO_MSIX + 4));
    virtio_blk_capacity = ((uint64_t)cap_hi << 32) | cap_lo;
  }
  if (virtio_blk_capacity == 0) {
    virtio_write_status(VIRTIO_STATUS_FAILED);
    set_status("storage: VirtIO-blk capacity is zero");
    return -1;
  }

  snprintf(virtio_blk_model, sizeof(virtio_blk_model),
           "VirtIO block q=%lu io=0x%lx", (unsigned long)virtio_blk_queue_size,
           (unsigned long)virtio_blk_io_base);
  storage_add_device(STORAGE_DRIVER_VIRTIO_BLK, NULL, virtio_blk_capacity,
                     virtio_blk_model);
  virtio_blk_ready = 1;
  snprintf(line, sizeof(line),
           "storage: VirtIO-blk ready sectors=%lu qnum=%lu writable=%s features=%08lx status=%02x",
           (unsigned long)virtio_blk_capacity,
           (unsigned long)virtio_blk_queue_size,
           virtio_blk_writable ? "yes" : "no",
           (unsigned long)virtio_blk_features, (unsigned)virtio_blk_last_status);
  storage_log_append(line);
  set_status("storage: VirtIO-blk disk detected");
  return 0;
}

static int virtio_blk_scan_controller(void) {
  pci_device_info_t devs[96];
  int total = pci_scan_all(devs, 96);
  int found = 0;

  virtio_blk_count = 0;
  virtio_scsi_count = 0;
  for (int i = 0; i < total && i < 96; i++) {
    if (storage_is_virtio_scsi(&devs[i])) {
      virtio_scsi_count++;
      storage_log_append("storage: VirtIO-scsi visible but driver is diagnostic-only");
      continue;
    }
    if (!storage_is_virtio_blk(&devs[i])) {
      continue;
    }
    if (virtio_blk_init_one(&devs[i]) == 0) {
      found++;
    }
  }
  if (found <= 0) {
    storage_log_append("storage: no usable VirtIO-blk disk");
    return -1;
  }
  return 0;
}

static uint32_t nvme_read32(uint32_t reg) {
  return *(volatile uint32_t *)(nvme_mmio + reg);
}

static void nvme_write32(uint32_t reg, uint32_t value) {
  *(volatile uint32_t *)(nvme_mmio + reg) = value;
}

static uint64_t nvme_read64(uint32_t reg) {
  uint64_t lo = nvme_read32(reg);
  uint64_t hi = nvme_read32(reg + 4);
  return lo | (hi << 32);
}

static void nvme_write64(uint32_t reg, uint64_t value) {
  nvme_write32(reg, (uint32_t)value);
  nvme_write32(reg + 4, (uint32_t)(value >> 32));
}

static uint32_t nvme_doorbell(uint16_t qid, int completion_queue) {
  return NVME_DOORBELL_BASE + ((uint32_t)qid * 2U +
                               (completion_queue ? 1U : 0U)) *
                                  nvme_db_stride;
}

static int nvme_wait_ready(int ready, int timeout) {
  while (timeout-- > 0) {
    int is_ready = (nvme_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) ? 1 : 0;
    if (is_ready == ready) {
      return 0;
    }
    __asm__ volatile("pause");
  }
  return -1;
}

static int nvme_submit_sync(volatile nvme_cmd_t *sq, volatile nvme_cqe_t *cq,
                            uint16_t qid, uint16_t depth, uint16_t *sq_tail,
                            uint16_t *cq_head, uint8_t *cq_phase,
                            const nvme_cmd_t *cmd_in) {
  nvme_cmd_t cmd;
  uint16_t cid = nvme_next_cid++;
  if (nvme_next_cid == 0) {
    nvme_next_cid = 1;
  }

  memcpy(&cmd, cmd_in, sizeof(cmd));
  cmd.cid = cid;
  memcpy((void *)&sq[*sq_tail], &cmd, sizeof(cmd));
  *sq_tail = (uint16_t)((*sq_tail + 1) % depth);
  nvme_write32(nvme_doorbell(qid, 0), *sq_tail);

  for (int i = 0; i < 10000000; i++) {
    volatile nvme_cqe_t *entry = &cq[*cq_head];
    if ((entry->status & 1U) == *cq_phase) {
      uint16_t status = (uint16_t)(entry->status >> 1);
      nvme_last_cqe_status = status;
      nvme_last_cqe_cid = entry->cid;
      nvme_last_cmd_cid = cid;
      if (entry->cid != cid) {
        storage_log_append("storage: NVMe completion CID mismatch");
        return -1;
      }
      *cq_head = (uint16_t)((*cq_head + 1) % depth);
      if (*cq_head == 0) {
        *cq_phase ^= 1U;
      }
      nvme_write32(nvme_doorbell(qid, 1), *cq_head);
      if (status != 0) {
        char line[160];
        snprintf(line, sizeof(line),
                 "storage: NVMe command failed qid=%u opcode=%02x cid=%u status=%04x",
                 (unsigned)qid, (unsigned)cmd.opcode, (unsigned)cid,
                 (unsigned)status);
        storage_log_append(line);
        return -1;
      }
      return 0;
    }
    __asm__ volatile("pause");
  }
  {
    char line[160];
    snprintf(line, sizeof(line),
             "storage: NVMe command timeout qid=%u opcode=%02x cid=%u",
             (unsigned)qid, (unsigned)cmd.opcode, (unsigned)cid);
    storage_log_append(line);
  }
  return -1;
}

static int nvme_admin_cmd(const nvme_cmd_t *cmd) {
  return nvme_submit_sync(nvme_admin_sq, nvme_admin_cq, 0,
                          NVME_ADMIN_QUEUE_DEPTH, &nvme_admin_sq_tail,
                          &nvme_admin_cq_head, &nvme_admin_cq_phase, cmd);
}

static int nvme_io_cmd(const nvme_cmd_t *cmd) {
  return nvme_submit_sync(nvme_io_sq, nvme_io_cq, 1, NVME_IO_QUEUE_DEPTH,
                          &nvme_io_sq_tail, &nvme_io_cq_head,
                          &nvme_io_cq_phase, cmd);
}

static uint64_t nvme_le64(const uint8_t *p) {
  return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) |
         ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
         ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
         ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static int nvme_create_io_queues(void) {
  nvme_cmd_t cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_CREATE_IO_CQ;
  cmd.prp1 = storage_phys_addr(nvme_io_cq);
  cmd.cdw10 = 1U | ((NVME_IO_QUEUE_DEPTH - 1U) << 16);
  cmd.cdw11 = 1U; /* Physically contiguous queue, interrupts disabled. */
  if (nvme_admin_cmd(&cmd) != 0) {
    storage_log_append("storage: NVMe create IO completion queue failed");
    return -1;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_CREATE_IO_SQ;
  cmd.prp1 = storage_phys_addr(nvme_io_sq);
  cmd.cdw10 = 1U | ((NVME_IO_QUEUE_DEPTH - 1U) << 16);
  cmd.cdw11 = 1U | (1U << 16); /* Physically contiguous, CQ id 1. */
  if (nvme_admin_cmd(&cmd) != 0) {
    storage_log_append("storage: NVMe create IO submission queue failed");
    return -1;
  }
  return 0;
}

static int nvme_identify_namespace(void) {
  nvme_cmd_t cmd;
  uint64_t native_lbas;
  memset(nvme_identify_buf, 0, sizeof(nvme_identify_buf));
  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_IDENTIFY;
  cmd.nsid = nvme_namespace_id;
  cmd.prp1 = storage_phys_addr(nvme_identify_buf);
  cmd.cdw10 = 0; /* CNS 0: identify namespace. */
  if (nvme_admin_cmd(&cmd) != 0) {
    char line[96];
    snprintf(line, sizeof(line), "storage: NVMe identify namespace %lu failed",
             (unsigned long)nvme_namespace_id);
    storage_log_append(line);
    return -1;
  }

  uint8_t flbas = nvme_identify_buf[26] & 0x0F;
  uint8_t nlbaf = nvme_identify_buf[25] & 0x1F;
  if (flbas >= 16) {
    storage_log_append("storage: NVMe namespace FLBAS invalid");
    return -1;
  }
  if (flbas > nlbaf) {
    storage_log_append("storage: NVMe namespace active LBAF out of range");
    return -1;
  }
  uint16_t metadata_size =
      (uint16_t)nvme_identify_buf[128 + flbas * 4] |
      ((uint16_t)nvme_identify_buf[128 + flbas * 4 + 1] << 8);
  uint8_t lba_shift = nvme_identify_buf[128 + flbas * 4 + 2];
  uint8_t relative_perf = nvme_identify_buf[128 + flbas * 4 + 3] & 0x03;
  if (lba_shift >= 32) {
    storage_log_append("storage: NVMe namespace LBA shift invalid");
    return -1;
  }
  nvme_lba_size = 1U << lba_shift;
  {
    char line[160];
    snprintf(line, sizeof(line),
             "storage: NVMe namespace format flbas=%u nlbaf=%u lbads=%u ms=%u rp=%u",
             (unsigned)flbas, (unsigned)nlbaf, (unsigned)lba_shift,
             (unsigned)metadata_size, (unsigned)relative_perf);
    storage_log_append(line);
  }
  if (nvme_lba_size != ORIZON_SECTOR_SIZE && nvme_lba_size != 4096U) {
    set_status("storage: NVMe namespace LBA size is unsupported");
    return -1;
  }
  nvme_lba_scale = nvme_lba_size / ORIZON_SECTOR_SIZE;
  nvme_namespace_ready = 1;

  native_lbas = nvme_le64(nvme_identify_buf);
  disk_sectors = native_lbas * nvme_lba_scale;
  {
    char line[160];
    snprintf(line, sizeof(line),
             "storage: NVMe namespace %lu native-lbas=%lu lba-size=%lu sectors512=%lu",
             (unsigned long)nvme_namespace_id, (unsigned long)native_lbas,
             (unsigned long)nvme_lba_size, (unsigned long)disk_sectors);
    storage_log_append(line);
  }
  return disk_sectors > 0 ? 0 : -1;
}

static void nvme_ascii_field(char *out, size_t out_size, const uint8_t *src,
                             size_t src_len) {
  size_t n;
  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!src || src_len == 0) {
    return;
  }
  n = src_len;
  while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\0')) {
    n--;
  }
  if (n >= out_size) {
    n = out_size - 1;
  }
  for (size_t i = 0; i < n; i++) {
    char c = (char)src[i];
    out[i] = (c >= 32 && c <= 126) ? c : ' ';
  }
  out[n] = '\0';
}

static void nvme_identify_controller(void) {
  nvme_cmd_t cmd;
  char model[48];

  memset(nvme_identify_buf, 0, sizeof(nvme_identify_buf));
  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_IDENTIFY;
  cmd.nsid = 0;
  cmd.prp1 = storage_phys_addr(nvme_identify_buf);
  cmd.cdw10 = 1; /* CNS 1: identify controller. */
  if (nvme_admin_cmd(&cmd) != 0) {
    storage_log_append("storage: NVMe identify controller failed");
    return;
  }

  nvme_ascii_field(model, sizeof(model), nvme_identify_buf + 24, 40);
  if (model[0]) {
    snprintf(nvme_model, sizeof(nvme_model), "%s", model);
    storage_log_append("storage: NVMe controller identified");
  }
}

static int nvme_io_native(uint64_t lba, void *buf, int write) {
  nvme_cmd_t cmd;
  uint64_t phys = storage_phys_addr(buf);

  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = write ? NVME_CMD_WRITE : NVME_CMD_READ;
  cmd.nsid = nvme_namespace_id;
  cmd.prp1 = phys;
  if ((phys & 0xFFFU) + nvme_lba_size > 4096U) {
    cmd.prp2 = (phys & ~0xFFFULL) + 4096U;
  }
  cmd.cdw10 = (uint32_t)lba;
  cmd.cdw11 = (uint32_t)(lba >> 32);
  cmd.cdw12 = 0; /* One logical block, zero based. */
  return nvme_io_cmd(&cmd);
}

static int nvme_io(uint64_t lba, void *buf, uint32_t sectors, int write) {
  if (!disk_ready || storage_driver != STORAGE_DRIVER_NVME || sectors == 0) {
    return disk_ready ? 0 : -1;
  }

  uint8_t *bytes = (uint8_t *)buf;
  for (uint32_t i = 0; i < sectors; i++) {
    uint8_t *sector = bytes + (uint64_t)i * ORIZON_SECTOR_SIZE;
    if (nvme_lba_size == ORIZON_SECTOR_SIZE) {
      if (nvme_io_native(lba + i, sector, write) != 0) {
        return -1;
      }
      continue;
    }

    uint64_t native_lba = (lba + i) / nvme_lba_scale;
    uint32_t offset =
        (uint32_t)(((lba + i) % nvme_lba_scale) * ORIZON_SECTOR_SIZE);
    if (nvme_lba_size > sizeof(nvme_lba_scratch)) {
      return -1;
    }
    if (nvme_io_native(native_lba, nvme_lba_scratch, 0) != 0) {
      return -1;
    }
    if (write) {
      memcpy(nvme_lba_scratch + offset, sector, ORIZON_SECTOR_SIZE);
      if (nvme_io_native(native_lba, nvme_lba_scratch, 1) != 0) {
        return -1;
      }
    } else {
      memcpy(sector, nvme_lba_scratch + offset, ORIZON_SECTOR_SIZE);
    }
  }
  return 0;
}

static int nvme_find_namespace(void) {
  for (uint32_t nsid = 1; nsid <= 32; nsid++) {
    nvme_namespace_id = nsid;
    if (nvme_identify_namespace() == 0) {
      return 0;
    }
  }
  set_status("storage: NVMe namespace identify failed");
  return -1;
}

static int nvme_init_one_controller(const pci_device_info_t *dev) {
  char line[192];
  if (!dev) {
    return -1;
  }
  nvme_controller_seen = 1;

  snprintf(line, sizeof(line),
           "storage: probing NVMe %02x:%02x.%u vendor=%04x device=%04x bar0=%08lx",
           dev->bus, dev->device, dev->function, dev->vendor_id,
           dev->device_id, (unsigned long)dev->bar[0]);
  storage_log_append(line);

  uint32_t cmd_reg = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  cmd_reg |= (1U << 2) | (1U << 1);
  pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd_reg);

  if (dev->bar[0] & 0x01) {
    set_status("storage: NVMe BAR is not MMIO");
    return -1;
  }

  uint64_t bar = dev->bar[0] & ~0xFULL;
  if (dev->bar[0] & 0x04) {
    bar |= ((uint64_t)dev->bar[1] << 32);
  }
  if (!bar) {
    set_status("storage: NVMe BAR missing");
    return -1;
  }

  nvme_mmio = (volatile uint8_t *)(uintptr_t)mmio_map_range(bar, 0x4000);
  if (!nvme_mmio) {
    set_status("storage: NVMe MMIO map failed");
    return -1;
  }

  uint64_t cap = nvme_read64(NVME_REG_CAP);
  uint32_t mps_min = (uint32_t)((cap >> 48) & 0x0F);
  nvme_last_cap = cap;
  nvme_last_cc = nvme_read32(NVME_REG_CC);
  nvme_last_csts = nvme_read32(NVME_REG_CSTS);
  snprintf(line, sizeof(line),
           "storage: NVMe regs CAP=%08lx%08lx CC=%08lx CSTS=%08lx stride=%lu",
           (unsigned long)(cap >> 32), (unsigned long)(cap & 0xffffffffU),
           (unsigned long)nvme_last_cc, (unsigned long)nvme_last_csts,
           (unsigned long)(4U << ((cap >> 32) & 0x0F)));
  storage_log_append(line);
  if (mps_min != 0) {
    set_status("storage: NVMe requires page size above 4 KiB");
    return -1;
  }
  nvme_db_stride = 4U << ((cap >> 32) & 0x0F);

  uint32_t cc = nvme_read32(NVME_REG_CC);
  if (cc & NVME_CC_EN) {
    nvme_write32(NVME_REG_CC, cc & ~NVME_CC_EN);
    if (nvme_wait_ready(0, 5000000) != 0) {
      set_status("storage: NVMe disable timeout");
      return -1;
    }
  }

  memset(nvme_admin_sq, 0, sizeof(nvme_admin_sq));
  memset(nvme_admin_cq, 0, sizeof(nvme_admin_cq));
  memset(nvme_io_sq, 0, sizeof(nvme_io_sq));
  memset(nvme_io_cq, 0, sizeof(nvme_io_cq));
  nvme_admin_sq_tail = 0;
  nvme_admin_cq_head = 0;
  nvme_admin_cq_phase = 1;
  nvme_io_sq_tail = 0;
  nvme_io_cq_head = 0;
  nvme_io_cq_phase = 1;
  nvme_lba_size = ORIZON_SECTOR_SIZE;
  nvme_lba_scale = 1;

  nvme_write32(NVME_REG_AQA, (NVME_ADMIN_QUEUE_DEPTH - 1U) |
                                  ((NVME_ADMIN_QUEUE_DEPTH - 1U) << 16));
  nvme_write64(NVME_REG_ASQ, storage_phys_addr(nvme_admin_sq));
  nvme_write64(NVME_REG_ACQ, storage_phys_addr(nvme_admin_cq));

  cc = (6U << NVME_CC_IOSQES_SHIFT) | (4U << NVME_CC_IOCQES_SHIFT) |
       NVME_CC_EN;
  nvme_write32(NVME_REG_CC, cc);
  if (nvme_wait_ready(1, 5000000) != 0) {
    nvme_last_cc = nvme_read32(NVME_REG_CC);
    nvme_last_csts = nvme_read32(NVME_REG_CSTS);
    set_status("storage: NVMe ready timeout");
    return -1;
  }
  nvme_last_cc = nvme_read32(NVME_REG_CC);
  nvme_last_csts = nvme_read32(NVME_REG_CSTS);

  if (nvme_create_io_queues() != 0) {
    set_status("storage: NVMe IO queue creation failed");
    return -1;
  }
  nvme_identify_controller();
  if (nvme_find_namespace() != 0) {
    return -1;
  }

  storage_add_device(STORAGE_DRIVER_NVME, NULL, disk_sectors, nvme_model);
  set_status(nvme_lba_size == ORIZON_SECTOR_SIZE
                 ? "storage: NVMe namespace detected"
                 : "storage: NVMe 4K namespace detected through 512B shim");
  return 0;
}

static int nvme_init_controller(void) {
  pci_device_info_t devs[8];
  int count = pci_scan_class(0x01, 0x08, 0x02, devs, 8);
  if (count <= 0) {
    count = pci_scan_class(0x01, 0x08, 0xFF, devs, 8);
  }
  nvme_controller_count = count > 0 ? count : 0;
  if (count <= 0) {
    storage_log_append("storage: no NVMe controller");
    return -1;
  }
  nvme_controller_seen = 1;
  {
    char line[80];
    snprintf(line, sizeof(line), "storage: NVMe controllers=%d", count);
    storage_log_append(line);
  }
  for (int i = 0; i < count && i < 8; i++) {
    if (nvme_init_one_controller(&devs[i]) == 0) {
      return 0;
    }
  }
  return -1;
}

static int ahci_port_has_disk(ahci_port_t *port) {
  uint32_t ssts = port->ssts;
  uint32_t det = ssts & 0x0F;
  uint32_t ipm = (ssts >> 8) & 0x0F;

  if (det != 3 || ipm != 1) {
    return 0;
  }
  return port->sig == SATA_SIG_ATA;
}

static void ahci_stop_port(ahci_port_t *port) {
  port->cmd &= ~AHCI_PORT_CMD_ST;
  port->cmd &= ~AHCI_PORT_CMD_FRE;

  for (int i = 0; i < 100000; i++) {
    if ((port->cmd & (AHCI_PORT_CMD_FR | AHCI_PORT_CMD_CR)) == 0) {
      break;
    }
  }
}

static void ahci_start_port(ahci_port_t *port) {
  port->cmd |= AHCI_PORT_CMD_FRE;
  port->cmd |= AHCI_PORT_CMD_ST;
}

static int ahci_setup_port(ahci_port_t *port) {
  ahci_stop_port(port);

  memset(cmd_list, 0, sizeof(cmd_list));
  memset(fis_area, 0, sizeof(fis_area));
  memset(cmd_tables, 0, sizeof(cmd_tables));

  uint64_t clb = storage_phys_addr(cmd_list);
  uint64_t fb = storage_phys_addr(fis_area);

  port->clb = (uint32_t)clb;
  port->clbu = (uint32_t)(clb >> 32);
  port->fb = (uint32_t)fb;
  port->fbu = (uint32_t)(fb >> 32);

  for (int i = 0; i < 32; i++) {
    uint64_t ctba = storage_phys_addr(&cmd_tables[i]);
    cmd_list[i].ctba = (uint32_t)ctba;
    cmd_list[i].ctbau = (uint32_t)(ctba >> 32);
  }

  port->serr = 0xFFFFFFFFU;
  port->is = 0xFFFFFFFFU;
  port->ie = 0;
  port->cmd |= AHCI_PORT_CMD_POD | AHCI_PORT_CMD_SUD;
  ahci_start_port(port);
  return 0;
}

static int ahci_find_cmd_slot(ahci_port_t *port) {
  uint32_t slots = port->sact | port->ci;
  for (int i = 0; i < 32; i++) {
    if ((slots & (1U << i)) == 0) {
      return i;
    }
  }
  return -1;
}

static int ahci_io(uint64_t lba, void *buf, uint32_t sectors, int write) {
  if (!disk_ready || sectors == 0) {
    return disk_ready ? 0 : -1;
  }
  if (sectors > 256) {
    return -1;
  }

  ahci_port_t *port = disk_port;
  int spin = 1000000;
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin-- > 0) {
    __asm__ volatile("pause");
  }
  if (spin <= 0) {
    return -1;
  }

  int slot = ahci_find_cmd_slot(port);
  if (slot < 0) {
    return -1;
  }

  ahci_cmd_header_t *header = &cmd_list[slot];
  ahci_cmd_table_t *table = &cmd_tables[slot];
  memset(table, 0, sizeof(*table));

  header->flags = 5; /* Register H2D FIS is 5 dwords. */
  if (write) {
    header->flags |= (1U << 6);
  }
  header->prdtl = 1;
  header->prdbc = 0;

  uint64_t buf_phys = storage_phys_addr(buf);
  table->prdt[0].dba = (uint32_t)buf_phys;
  table->prdt[0].dbau = (uint32_t)(buf_phys >> 32);
  table->prdt[0].dbc = (sectors * ORIZON_SECTOR_SIZE - 1) | (1U << 31);

  uint8_t *fis = table->cfis;
  fis[0] = 0x27; /* FIS_TYPE_REG_H2D */
  fis[1] = 1U << 7;
  fis[2] = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
  fis[4] = (uint8_t)lba;
  fis[5] = (uint8_t)(lba >> 8);
  fis[6] = (uint8_t)(lba >> 16);
  fis[7] = 1U << 6; /* LBA mode */
  fis[8] = (uint8_t)(lba >> 24);
  fis[9] = (uint8_t)(lba >> 32);
  fis[10] = (uint8_t)(lba >> 40);
  fis[12] = (uint8_t)sectors;
  fis[13] = (uint8_t)(sectors >> 8);

  port->is = 0xFFFFFFFFU;
  port->ci = 1U << slot;

  for (int i = 0; i < 5000000; i++) {
    if ((port->ci & (1U << slot)) == 0) {
      return (port->is & AHCI_PORT_IS_TFES) ? -1 : 0;
    }
    if (port->is & AHCI_PORT_IS_TFES) {
      return -1;
    }
    __asm__ volatile("pause");
  }

  return -1;
}

static int ahci_identify(ahci_port_t *port, uint16_t *out_words) {
  if (!port || !out_words) {
    return -1;
  }

  int spin = 1000000;
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin-- > 0) {
    __asm__ volatile("pause");
  }
  if (spin <= 0) {
    return -1;
  }

  int slot = ahci_find_cmd_slot(port);
  if (slot < 0) {
    return -1;
  }

  ahci_cmd_header_t *header = &cmd_list[slot];
  ahci_cmd_table_t *table = &cmd_tables[slot];
  memset(table, 0, sizeof(*table));
  memset(out_words, 0, ORIZON_SECTOR_SIZE);

  header->flags = 5;
  header->prdtl = 1;
  header->prdbc = 0;

  uint64_t buf_phys = storage_phys_addr(out_words);
  table->prdt[0].dba = (uint32_t)buf_phys;
  table->prdt[0].dbau = (uint32_t)(buf_phys >> 32);
  table->prdt[0].dbc = (ORIZON_SECTOR_SIZE - 1) | (1U << 31);

  uint8_t *fis = table->cfis;
  fis[0] = 0x27;
  fis[1] = 1U << 7;
  fis[2] = ATA_CMD_IDENTIFY;

  port->is = 0xFFFFFFFFU;
  port->ci = 1U << slot;

  for (int i = 0; i < 5000000; i++) {
    if ((port->ci & (1U << slot)) == 0) {
      return (port->is & AHCI_PORT_IS_TFES) ? -1 : 0;
    }
    if (port->is & AHCI_PORT_IS_TFES) {
      return -1;
    }
    __asm__ volatile("pause");
  }

  return -1;
}

static uint64_t identify_sector_count(const uint16_t *id) {
  uint64_t sectors = 0;
  if (!id) {
    return 0;
  }
  sectors = ((uint64_t)id[100]) | ((uint64_t)id[101] << 16) |
            ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
  if (sectors == 0) {
    sectors = ((uint64_t)id[60]) | ((uint64_t)id[61] << 16);
  }
  return sectors;
}

static void identify_model_string(const uint16_t *id, char *out,
                                  size_t out_size) {
  size_t o = 0;
  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!id) {
    return;
  }
  for (int i = 27; i <= 46 && o + 1 < out_size; i++) {
    char hi = (char)(id[i] >> 8);
    char lo = (char)(id[i] & 0xFF);
    if (hi && o + 1 < out_size) {
      out[o++] = hi;
    }
    if (lo && o + 1 < out_size) {
      out[o++] = lo;
    }
  }
  while (o > 0 && out[o - 1] == ' ') {
    o--;
  }
  out[o] = '\0';
}

static int ahci_scan_controller(void) {
  pci_device_info_t devs[8];
  int count = pci_scan_class(0x01, 0x06, 0x01, devs, 8);
  int found = 0;
  char line[160];

  if (count <= 0) {
    count = pci_scan_class(0x01, 0x06, 0xFF, devs, 8);
  }
  if (count <= 0) {
    set_status("storage: no AHCI controller");
    return -1;
  }

  for (int dev_index = 0; dev_index < count && dev_index < 8; dev_index++) {
    pci_device_info_t *dev = &devs[dev_index];
    snprintf(line, sizeof(line),
             "storage: probing AHCI %02x:%02x.%u vendor=%04x device=%04x bar5=%08lx",
             dev->bus, dev->device, dev->function, dev->vendor_id,
             dev->device_id, (unsigned long)dev->bar[5]);
    storage_log_append(line);
    uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1U << 2) | (1U << 1);
    pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);

    uint32_t bar5 = dev->bar[5];
    if ((bar5 & 0x01) != 0) {
      set_status("storage: AHCI BAR is not MMIO");
      continue;
    }

    uint64_t abar_phys = (uint64_t)(bar5 & ~0x0FULL);
    if (!abar_phys) {
      set_status("storage: AHCI BAR missing");
      continue;
    }

    uint64_t abar = mmio_map_range(abar_phys, 0x2000);
    if (!abar) {
      set_status("storage: AHCI MMIO map failed");
      continue;
    }

    hba = (ahci_mem_t *)(uintptr_t)abar;
    hba->ghc |= AHCI_GHC_AE;

    uint32_t implemented = hba->pi;
    snprintf(line, sizeof(line), "storage: AHCI pi=%08lx ghc=%08lx cap=%08lx",
             (unsigned long)implemented, (unsigned long)hba->ghc,
             (unsigned long)hba->cap);
    storage_log_append(line);
    for (int i = 0; i < 32; i++) {
      if ((implemented & (1U << i)) == 0) {
        continue;
      }
      if (!ahci_port_has_disk(&hba->ports[i])) {
        snprintf(line, sizeof(line),
                 "storage: AHCI port %d empty sig=%08lx ssts=%08lx serr=%08lx",
                 i, (unsigned long)hba->ports[i].sig,
                 (unsigned long)hba->ports[i].ssts,
                 (unsigned long)hba->ports[i].serr);
        storage_log_append(line);
        continue;
      }
      snprintf(line, sizeof(line),
               "storage: AHCI port %d candidate sig=%08lx ssts=%08lx tfd=%08lx",
               i, (unsigned long)hba->ports[i].sig,
               (unsigned long)hba->ports[i].ssts,
               (unsigned long)hba->ports[i].tfd);
      storage_log_append(line);
      if (ahci_setup_port(&hba->ports[i]) == 0) {
        static uint16_t identify_words[256] __attribute__((aligned(4096)));
        char model[64];
        uint64_t sectors = 0;
        model[0] = '\0';
        if (ahci_identify(&hba->ports[i], identify_words) == 0) {
          sectors = identify_sector_count(identify_words);
          identify_model_string(identify_words, model, sizeof(model));
        }
        if (sectors > 0) {
          storage_add_device(STORAGE_DRIVER_AHCI, (void *)&hba->ports[i],
                             sectors, model);
          found++;
        }
      }
    }
  }

  if (found > 0) {
    set_status("storage: AHCI disk detected");
    return 0;
  }

  set_status("storage: no SATA disk");
  return -1;
}

int storage_init(void) {
  if (disk_ready) {
    return 0;
  }
  if (storage_scanned) {
    if (storage_device_total > 0) {
      return storage_select_device(0);
    }
    return -1;
  }

  storage_scanned = 1;
  storage_device_total = 0;
  storage_selected_index = -1;
  storage_driver = STORAGE_DRIVER_NONE;
  disk_port = NULL;
  disk_sectors = 0;
  storage_blocker[0] = '\0';
  nvme_controller_count = 0;
  nvme_controller_seen = 0;
  nvme_namespace_ready = 0;
  nvme_namespace_id = 0;
  nvme_lba_size = 0;
  nvme_lba_scale = 0;
  nvme_last_cap = 0;
  nvme_last_cc = 0;
  nvme_last_csts = 0;
  nvme_last_cqe_status = 0;
  nvme_last_cqe_cid = 0;
  nvme_last_cmd_cid = 0;
  virtio_blk_io_base = 0;
  virtio_blk_queue_size = 0;
  virtio_blk_avail_idx = 0;
  virtio_blk_used_idx = 0;
  virtio_blk_used_offset = 0;
  virtio_blk_features = 0;
  virtio_blk_capacity = 0;
  virtio_blk_count = 0;
  virtio_blk_seen = 0;
  virtio_blk_ready = 0;
  virtio_blk_writable = 1;
  virtio_blk_modern_only = 0;
  virtio_blk_modern = 0;
  virtio_scsi_count = 0;
  virtio_blk_last_status = 0;
  virtio_blk_last_req_status = 0xFF;
  virtio_blk_common_mmio = NULL;
  virtio_blk_notify_mmio = NULL;
  virtio_blk_device_mmio = NULL;
  virtio_blk_notify_multiplier = 0;
  virtio_blk_queue_notify_off = 0;
  storage_log_used = 0;
  storage_log[0] = '\0';
  storage_log_append("storage: scan begin (read-only detect)");
  snprintf(nvme_model, sizeof(nvme_model), "none");
  snprintf(virtio_blk_model, sizeof(virtio_blk_model), "none");

  nvme_init_controller();
  ahci_scan_controller();
  virtio_blk_scan_controller();

  if (storage_device_total <= 0) {
    storage_detect_blockers();
    set_status(storage_blocker[0] ? storage_blocker
                                  : "storage: no AHCI/NVMe/VirtIO/eMMC disk");
    return -1;
  }
  storage_log_append("storage: scan complete");
  return storage_select_device(0);
}

int storage_available(void) {
  return disk_ready || storage_init() == 0;
}

const char *storage_status(void) {
  return disk_status;
}

uint64_t storage_sector_count(void) {
  if (!storage_available()) {
    return 0;
  }
  return disk_sectors;
}

void storage_format_size(uint64_t sectors, char *out, size_t out_size) {
  uint64_t mib;
  if (!out || out_size == 0) {
    return;
  }
  if (sectors == 0) {
    snprintf(out, out_size, "unknown size");
    return;
  }
  mib = sectors / 2048;
  if (mib >= 1024) {
    snprintf(out, out_size, "%lu GiB (%lu sectors)",
             (unsigned long)(mib / 1024), (unsigned long)sectors);
  } else {
    snprintf(out, out_size, "%lu MiB (%lu sectors)", (unsigned long)mib,
             (unsigned long)sectors);
  }
}

void storage_format_capacity(char *out, size_t out_size) {
  storage_format_size(storage_sector_count(), out, out_size);
}

static void storage_diag_append(char *out, size_t out_size, size_t *used,
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

void storage_format_diagnostics(char *out, size_t out_size) {
  pci_device_info_t devs[96];
  char line[384];
  size_t used = 0;
  int candidates = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!storage_scanned) {
    storage_init();
  }

  snprintf(line, sizeof(line),
           "storage diagnostics:\n"
           "  status: %s\n"
           "  selected: %d\n"
           "  devices: %d\n"
           "  nvme: controllers=%d detected=%s active=%s nsid=%lu lba-size=%lu scale=%lu model=\"%s\"\n"
           "  nvme-regs: CAP=%08lx%08lx CC=%08lx CSTS=%08lx last-cid=%u/%u last-status=%04x\n",
           storage_status(), storage_selected_index + 1, storage_device_total,
           nvme_controller_count, nvme_controller_seen ? "yes" : "no",
           nvme_namespace_ready ? "yes" : "no",
           (unsigned long)nvme_namespace_id, (unsigned long)nvme_lba_size,
           (unsigned long)nvme_lba_scale, nvme_model,
           (unsigned long)(nvme_last_cap >> 32),
           (unsigned long)(nvme_last_cap & 0xffffffffU),
           (unsigned long)nvme_last_cc, (unsigned long)nvme_last_csts,
           (unsigned)nvme_last_cmd_cid, (unsigned)nvme_last_cqe_cid,
           (unsigned)nvme_last_cqe_status);
  storage_diag_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "  virtio-blk: controllers=%d detected=%s active=%s modern=%s modern-only=%s io=0x%lx qnum=%lu sectors=%lu writable=%s features=%08lx status=%02x req-status=%u notify=%lu/%lu model=\"%s\"\n"
           "  virtio-scsi: controllers=%d true-driver=not-implemented fallback=diagnostic-only\n",
           virtio_blk_count, virtio_blk_seen ? "yes" : "no",
           virtio_blk_ready ? "yes" : "no",
           virtio_blk_modern ? "yes" : "no",
           virtio_blk_modern_only ? "yes" : "no",
           (unsigned long)virtio_blk_io_base,
           (unsigned long)virtio_blk_queue_size,
           (unsigned long)virtio_blk_capacity,
           virtio_blk_writable ? "yes" : "no",
           (unsigned long)virtio_blk_features,
           (unsigned)virtio_blk_last_status,
           (unsigned)virtio_blk_last_req_status,
           (unsigned long)virtio_blk_queue_notify_off,
           (unsigned long)virtio_blk_notify_multiplier, virtio_blk_model,
           virtio_scsi_count);
  storage_diag_append(out, out_size, &used, line);
  if (storage_blocker[0]) {
    snprintf(line, sizeof(line), "  blocker: %s\n", storage_blocker);
    storage_diag_append(out, out_size, &used, line);
  }

  storage_diag_append(out, out_size, &used, "  pci-storage-candidates:\n");
  int total = pci_scan_all(devs, 96);
  int vmd_rst = 0;
  for (int i = 0; i < total && i < 96; i++) {
    const pci_device_info_t *dev = &devs[i];
    if (!storage_is_candidate(dev)) {
      continue;
    }
    if (storage_is_intel_vmd_rst(dev)) {
      vmd_rst++;
    }
    candidates++;
    snprintf(line, sizeof(line),
             "    %02x:%02x.%u vendor=%04x device=%04x class=%02x/%02x/%02x "
             "hint=%s bar0=%08lx bar1=%08lx bar4=%08lx bar5=%08lx\n",
             dev->bus, dev->device, dev->function, dev->vendor_id,
             dev->device_id, dev->class_code, dev->subclass, dev->prog_if,
             storage_candidate_hint(dev), (unsigned long)dev->bar[0],
             (unsigned long)dev->bar[1], (unsigned long)dev->bar[4],
             (unsigned long)dev->bar[5]);
    storage_diag_append(out, out_size, &used, line);
  }
  if (candidates == 0) {
    storage_diag_append(out, out_size, &used,
                        "    none; firmware may hide the disk behind a "
                        "non-enumerated controller\n");
  }
  if (storage_device_total > 0) {
    storage_diag_append(out, out_size, &used,
                        "  next-action: disk detected; run disk identify, "
                        "gpt scan and disk read-test last before install/update\n");
  } else if (vmd_rst) {
    storage_diag_append(out, out_size, &used,
                        "  next-action: Intel VMD/RST candidate visible; "
                        "capture pci bars + logs pci + logs storage; true "
                        "VMD remap driver is pending\n");
  } else if (nvme_controller_seen && !nvme_namespace_ready) {
    storage_diag_append(out, out_size, &used,
                        "  next-action: NVMe controller visible but no "
                        "namespace active; capture CAP/CC/CSTS, last-cid and "
                        "last-status from this report\n");
  } else if (virtio_blk_seen && !virtio_blk_ready) {
    storage_diag_append(out, out_size, &used,
                        "  next-action: VirtIO-blk visible but inactive; "
                        "capture modern/io/qnum/status/features/notify and "
                        "the PCI cap BAR lines from this report\n");
  } else if (candidates > 0) {
    storage_diag_append(out, out_size, &used,
                        "  next-action: unsupported storage candidate; "
                        "capture this candidate line and pci bars\n");
  } else {
    storage_diag_append(out, out_size, &used,
                        "  next-action: no storage candidate visible; "
                        "capture full report and firmware storage mode\n");
  }
  snprintf(line, sizeof(line),
           "  intel-vmd-rst: detected=%s true-driver=not-implemented "
           "fallback=diagnostic-only\n",
           vmd_rst ? "yes" : "no");
  storage_diag_append(out, out_size, &used, line);
}

void storage_format_log(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  if (!storage_scanned) {
    storage_init();
  }
  snprintf(out, out_size, "storage log:\n%s", storage_log[0] ? storage_log
                                                              : "(empty)\n");
}

void storage_format_identify(char *out, size_t out_size) {
  char line[768];
  size_t used = 0;
  int count;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!storage_scanned) {
    storage_init();
  }
  count = storage_device_count();
  snprintf(line, sizeof(line),
           "disk identify:\n"
           "  selected: %d\n"
           "  available: %s\n"
           "  status: %s\n"
           "  nvme: controllers=%d detected=%s active=%s nsid=%lu lba-size=%lu scale=%lu model=\"%s\"\n"
           "  nvme-regs: CAP=%08lx%08lx CC=%08lx CSTS=%08lx last-status=%04x\n"
           "  virtio-blk: controllers=%d detected=%s active=%s modern=%s io=0x%lx qnum=%lu sectors=%lu writable=%s req-status=%u model=\"%s\"\n",
           storage_selected_index + 1, storage_available() ? "yes" : "no",
           storage_status(), nvme_controller_count,
           nvme_controller_seen ? "yes" : "no",
           nvme_namespace_ready ? "yes" : "no",
           (unsigned long)nvme_namespace_id, (unsigned long)nvme_lba_size,
           (unsigned long)nvme_lba_scale, nvme_model,
           (unsigned long)(nvme_last_cap >> 32),
           (unsigned long)(nvme_last_cap & 0xffffffffU),
           (unsigned long)nvme_last_cc, (unsigned long)nvme_last_csts,
           (unsigned)nvme_last_cqe_status, virtio_blk_count,
           virtio_blk_seen ? "yes" : "no",
           virtio_blk_ready ? "yes" : "no",
           virtio_blk_modern ? "yes" : "no",
           (unsigned long)virtio_blk_io_base,
           (unsigned long)virtio_blk_queue_size,
           (unsigned long)virtio_blk_capacity,
           virtio_blk_writable ? "yes" : "no",
           (unsigned)virtio_blk_last_req_status, virtio_blk_model);
  storage_diag_append(out, out_size, &used, line);
  for (int i = 0; i < count && i < ORIZON_STORAGE_MAX_DEVICES; i++) {
    storage_device_info_t info;
    char cap[64];
    if (storage_get_device(i, &info) < 0) {
      continue;
    }
    storage_format_size(info.sectors, cap, sizeof(cap));
    snprintf(line, sizeof(line),
             "  disk%d: selected=%s writable=%s driver=%s size=%s model=\"%s\"\n",
             info.index + 1, info.selected ? "yes" : "no",
             info.writable ? "yes" : "no", info.driver, cap, info.model);
    storage_diag_append(out, out_size, &used, line);
  }
  if (count == 0) {
    storage_diag_append(out, out_size, &used,
                        "  no disk registered; see storage diag/logs storage\n");
  }
}

int storage_read_test(uint64_t lba, char *out, size_t out_size) {
  char hash[SHA256_HEX_SIZE];
  const storage_device_t *dev = NULL;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  if (!storage_available()) {
    snprintf(out, out_size,
             "disk read-test: WARN no selected AHCI/NVMe/VirtIO disk; non-destructive read skipped\n");
    return 1;
  }
  if (lba >= storage_sector_count()) {
    snprintf(out, out_size,
             "disk read-test: FAIL lba=%lu outside disk sectors=%lu\n",
             (unsigned long)lba, (unsigned long)storage_sector_count());
    return -1;
  }
  memset(storage_read_test_buf, 0, sizeof(storage_read_test_buf));
  if (storage_read(lba, storage_read_test_buf, 1) != 0) {
    snprintf(out, out_size,
             "disk read-test: FAIL lba=%lu read failed status=\"%s\"\n",
             (unsigned long)lba, storage_status());
    return -1;
  }
  if (storage_selected_index >= 0 &&
      storage_selected_index < storage_device_total) {
    dev = &storage_devices[storage_selected_index];
  }
  sha256_buffer_hex(storage_read_test_buf, sizeof(storage_read_test_buf), hash);
  snprintf(out, out_size,
           "disk read-test: PASS disk=%s driver=%s lba=%lu sectors=%lu bytes=%lu sha256=%s mode=read-only\n",
           dev ? dev->name : "none", driver_name(storage_driver),
           (unsigned long)lba, (unsigned long)storage_sector_count(),
           (unsigned long)sizeof(storage_read_test_buf), hash);
  return 0;
}

int storage_device_count(void) {
  if (!storage_scanned) {
    storage_init();
  }
  return storage_device_total;
}

int storage_selected_device(void) {
  if (!storage_scanned) {
    storage_init();
  }
  return storage_selected_index;
}

int storage_get_device(int index, storage_device_info_t *out) {
  storage_device_t *dev;

  if (!out) {
    return -1;
  }
  if (!storage_scanned) {
    storage_init();
  }
  if (index < 0 || index >= storage_device_total) {
    return -1;
  }
  dev = &storage_devices[index];
  memset(out, 0, sizeof(*out));
  out->index = index;
  out->selected = index == storage_selected_index;
  out->writable = dev->writable;
  out->sectors = dev->sectors;
  snprintf(out->name, sizeof(out->name), "%s", dev->name);
  snprintf(out->driver, sizeof(out->driver), "%s", driver_name(dev->driver));
  snprintf(out->model, sizeof(out->model), "%s", dev->model);
  return 0;
}

int storage_select_device(int index) {
  storage_device_t *dev;

  if (!storage_scanned) {
    storage_init();
  }
  if (index < 0 || index >= storage_device_total) {
    return -1;
  }

  dev = &storage_devices[index];
  storage_driver = dev->driver;
  disk_port = (ahci_port_t *)dev->ahci_port;
  disk_sectors = dev->sectors;
  storage_selected_index = index;
  disk_ready = 1;
  select_status_from_device(dev);
  return 0;
}

int storage_read(uint64_t lba, void *buf, uint32_t sector_count) {
  if (!storage_available()) {
    return -1;
  }
  if (storage_driver == STORAGE_DRIVER_NVME) {
    return nvme_io(lba, buf, sector_count, 0);
  }
  if (storage_driver == STORAGE_DRIVER_VIRTIO_BLK) {
    return virtio_blk_io(lba, buf, sector_count, 0);
  }
  return ahci_io(lba, buf, sector_count, 0);
}

int storage_write(uint64_t lba, const void *buf, uint32_t sector_count) {
  storage_device_t *dev = NULL;
  if (!storage_available()) {
    return -1;
  }
  if (storage_selected_index >= 0 &&
      storage_selected_index < storage_device_total) {
    dev = &storage_devices[storage_selected_index];
  }
  if (dev && !dev->writable) {
    storage_log_append("storage: write blocked; selected disk is read-only");
    return -1;
  }
  if (storage_driver == STORAGE_DRIVER_NVME) {
    return nvme_io(lba, (void *)buf, sector_count, 1);
  }
  if (storage_driver == STORAGE_DRIVER_VIRTIO_BLK) {
    return virtio_blk_io(lba, (void *)buf, sector_count, 1);
  }
  return ahci_io(lba, (void *)buf, sector_count, 1);
}
