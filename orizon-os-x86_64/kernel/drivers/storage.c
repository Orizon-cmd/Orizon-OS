/*
 * Orizon OS x86_64 - Minimal AHCI block storage
 *
 * This driver intentionally supports the VM's first AHCI/SATA disk only.
 * It is enough for the first persistent /workspace backend and keeps the
 * storage surface small while the OS is still young.
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
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35

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

static ahci_mem_t *hba = NULL;
static ahci_port_t *disk_port = NULL;
static int disk_ready = 0;
static const char *disk_status = "storage: not initialized";

static ahci_cmd_header_t cmd_list[32] __attribute__((aligned(1024)));
static uint8_t fis_area[256] __attribute__((aligned(256)));
static ahci_cmd_table_t cmd_tables[32] __attribute__((aligned(128)));

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

int storage_init(void) {
  if (disk_ready) {
    return 0;
  }

  pci_device_info_t devs[4];
  int count = pci_scan_class(0x01, 0x06, 0x01, devs, 4);
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
      disk_port = &hba->ports[i];
      disk_ready = 1;
      set_status("storage: AHCI disk ready");
      return 0;
    }
  }

  set_status("storage: no SATA disk");
  return -1;
}

int storage_available(void) {
  return disk_ready || storage_init() == 0;
}

const char *storage_status(void) {
  return disk_status;
}

int storage_read(uint64_t lba, void *buf, uint32_t sector_count) {
  if (!storage_available()) {
    return -1;
  }
  return ahci_io(lba, buf, sector_count, 0);
}

int storage_write(uint64_t lba, const void *buf, uint32_t sector_count) {
  if (!storage_available()) {
    return -1;
  }
  return ahci_io(lba, (void *)buf, sector_count, 1);
}
