/*
 * Orizon OS x86_64 - Minimal AHCI/NVMe block storage
 *
 * This keeps the storage surface small while the OS is still young: AHCI for
 * the current VM path, plus a first NVMe namespace path for modern machines.
 */

#include "../include/storage.h"
#include "../include/gui.h"
#include "../include/mmio.h"
#include "../include/pci.h"
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

typedef enum {
  STORAGE_DRIVER_NONE = 0,
  STORAGE_DRIVER_AHCI,
  STORAGE_DRIVER_NVME,
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
static storage_device_t storage_devices[ORIZON_STORAGE_MAX_DEVICES];

static ahci_cmd_header_t cmd_list[32] __attribute__((aligned(1024)));
static uint8_t fis_area[256] __attribute__((aligned(256)));
static ahci_cmd_table_t cmd_tables[32] __attribute__((aligned(128)));

static volatile uint8_t *nvme_mmio = NULL;
static uint32_t nvme_db_stride = 4;
static uint32_t nvme_namespace_id = 1;
static uint32_t nvme_lba_size = ORIZON_SECTOR_SIZE;
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
  serial_puts("[storage] ");
  serial_puts(status);
  serial_puts("\n");
}

static const char *driver_name(storage_driver_t driver) {
  if (driver == STORAGE_DRIVER_NVME) {
    return "NVMe";
  }
  if (driver == STORAGE_DRIVER_AHCI) {
    return "AHCI";
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
  dev->writable = 1;
  snprintf(dev->name, sizeof(dev->name), "disk%d", index);
  snprintf(dev->model, sizeof(dev->model), "%s",
           model && model[0] ? model : driver_name(driver));
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
      if (entry->cid != cid) {
        return -1;
      }
      *cq_head = (uint16_t)((*cq_head + 1) % depth);
      if (*cq_head == 0) {
        *cq_phase ^= 1U;
      }
      nvme_write32(nvme_doorbell(qid, 1), *cq_head);
      return status == 0 ? 0 : -1;
    }
    __asm__ volatile("pause");
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
    return -1;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_CREATE_IO_SQ;
  cmd.prp1 = storage_phys_addr(nvme_io_sq);
  cmd.cdw10 = 1U | ((NVME_IO_QUEUE_DEPTH - 1U) << 16);
  cmd.cdw11 = 1U | (1U << 16); /* Physically contiguous, CQ id 1. */
  return nvme_admin_cmd(&cmd);
}

static int nvme_identify_namespace(void) {
  nvme_cmd_t cmd;
  memset(nvme_identify_buf, 0, sizeof(nvme_identify_buf));
  memset(&cmd, 0, sizeof(cmd));
  cmd.opcode = NVME_ADMIN_IDENTIFY;
  cmd.nsid = nvme_namespace_id;
  cmd.prp1 = storage_phys_addr(nvme_identify_buf);
  cmd.cdw10 = 0; /* CNS 0: identify namespace. */
  if (nvme_admin_cmd(&cmd) != 0) {
    return -1;
  }

  uint8_t flbas = nvme_identify_buf[26] & 0x0F;
  if (flbas >= 16) {
    return -1;
  }
  uint8_t lba_shift = nvme_identify_buf[128 + flbas * 4 + 3];
  if (lba_shift >= 32) {
    return -1;
  }
  nvme_lba_size = 1U << lba_shift;
  if (nvme_lba_size != ORIZON_SECTOR_SIZE) {
    set_status("storage: NVMe namespace is not 512-byte LBA yet");
    return -1;
  }

  disk_sectors = nvme_le64(nvme_identify_buf);
  return disk_sectors > 0 ? 0 : -1;
}

static int nvme_io(uint64_t lba, void *buf, uint32_t sectors, int write) {
  if (!disk_ready || storage_driver != STORAGE_DRIVER_NVME || sectors == 0) {
    return disk_ready ? 0 : -1;
  }

  uint8_t *bytes = (uint8_t *)buf;
  for (uint32_t i = 0; i < sectors; i++) {
    nvme_cmd_t cmd;
    uint64_t phys = storage_phys_addr(bytes + (uint64_t)i * ORIZON_SECTOR_SIZE);

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = write ? NVME_CMD_WRITE : NVME_CMD_READ;
    cmd.nsid = nvme_namespace_id;
    cmd.prp1 = phys;
    if ((phys & 0xFFFU) + ORIZON_SECTOR_SIZE > 4096U) {
      cmd.prp2 = (phys & ~0xFFFULL) + 4096U;
    }
    cmd.cdw10 = (uint32_t)(lba + i);
    cmd.cdw11 = (uint32_t)((lba + i) >> 32);
    cmd.cdw12 = 0; /* One logical block, zero based. */
    if (nvme_io_cmd(&cmd) != 0) {
      return -1;
    }
  }
  return 0;
}

static int nvme_init_controller(void) {
  pci_device_info_t devs[4];
  int count = pci_scan_class(0x01, 0x08, 0x02, devs, 4);
  if (count <= 0) {
    return -1;
  }

  pci_device_info_t *dev = &devs[0];
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

  nvme_write32(NVME_REG_AQA, (NVME_ADMIN_QUEUE_DEPTH - 1U) |
                                  ((NVME_ADMIN_QUEUE_DEPTH - 1U) << 16));
  nvme_write64(NVME_REG_ASQ, storage_phys_addr(nvme_admin_sq));
  nvme_write64(NVME_REG_ACQ, storage_phys_addr(nvme_admin_cq));

  cc = (6U << NVME_CC_IOSQES_SHIFT) | (4U << NVME_CC_IOCQES_SHIFT) |
       NVME_CC_EN;
  nvme_write32(NVME_REG_CC, cc);
  if (nvme_wait_ready(1, 5000000) != 0) {
    set_status("storage: NVMe ready timeout");
    return -1;
  }

  if (nvme_create_io_queues() != 0 || nvme_identify_namespace() != 0) {
    return -1;
  }

  storage_add_device(STORAGE_DRIVER_NVME, NULL, disk_sectors,
                     "NVMe namespace 1");
  set_status("storage: NVMe namespace detected");
  return 0;
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
  pci_device_info_t devs[4];
  int count = pci_scan_class(0x01, 0x06, 0x01, devs, 4);
  int found = 0;

  if (count <= 0) {
    count = pci_scan_class(0x01, 0x06, 0xFF, devs, 4);
  }
  if (count <= 0) {
    set_status("storage: no AHCI controller");
    return -1;
  }

  pci_device_info_t *dev = &devs[0];
  uint32_t cmd = pci_read32(dev->bus, dev->device, dev->function, 0x04);
  cmd |= (1U << 2) | (1U << 1);
  pci_write32(dev->bus, dev->device, dev->function, 0x04, cmd);

  uint32_t bar5 = dev->bar[5];
  if ((bar5 & 0x01) != 0) {
    set_status("storage: AHCI BAR is not MMIO");
    return -1;
  }

  uint64_t abar_phys = (uint64_t)(bar5 & ~0x0FULL);
  if (!abar_phys) {
    set_status("storage: AHCI BAR missing");
    return -1;
  }

  uint64_t abar = mmio_map_range(abar_phys, 0x2000);
  if (!abar) {
    set_status("storage: AHCI MMIO map failed");
    return -1;
  }

  hba = (ahci_mem_t *)(uintptr_t)abar;
  hba->ghc |= AHCI_GHC_AE;

  uint32_t implemented = hba->pi;
  for (int i = 0; i < 32; i++) {
    if ((implemented & (1U << i)) == 0) {
      continue;
    }
    if (!ahci_port_has_disk(&hba->ports[i])) {
      continue;
    }
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

  nvme_init_controller();
  ahci_scan_controller();

  if (storage_device_total <= 0) {
    set_status("storage: no AHCI/NVMe disk");
    return -1;
  }
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
  return ahci_io(lba, buf, sector_count, 0);
}

int storage_write(uint64_t lba, const void *buf, uint32_t sector_count) {
  if (!storage_available()) {
    return -1;
  }
  if (storage_driver == STORAGE_DRIVER_NVME) {
    return nvme_io(lba, (void *)buf, sector_count, 1);
  }
  return ahci_io(lba, (void *)buf, sector_count, 1);
}
