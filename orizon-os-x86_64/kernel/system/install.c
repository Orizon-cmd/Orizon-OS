/*
 * Orizon OS x86_64 - Guided Disk Installer
 *
 * This is intentionally small and explicit: it installs the currently booted
 * kernel and UEFI fallback loader to a GPT disk with a FAT32 ESP plus an
 * Orizon data partition used by /workspace persistence.
 */

#include "../include/bootinfo.h"
#include "../include/install.h"
#include "../include/storage.h"
#include "../include/string.h"

#define INSTALL_ESP_START_LBA 2048ULL
#define INSTALL_DATA_START_LBA ORIZON_PERSIST_LBA
#define INSTALL_GPT_ENTRY_COUNT 128U
#define INSTALL_GPT_ENTRY_SIZE 128U
#define INSTALL_GPT_ENTRY_SECTORS 32U
#define INSTALL_FAT_SECTORS_MAX 2048U
#define INSTALL_CLUSTER_BYTES 4096U
#define INSTALL_SECTORS_PER_CLUSTER 8U

typedef struct {
  uint64_t start_lba;
  uint32_t total_sectors;
  uint32_t sectors_per_cluster;
  uint32_t reserved_sectors;
  uint32_t fat_count;
  uint32_t sectors_per_fat;
  uint32_t root_cluster;
  uint32_t data_start_sector;
  uint32_t next_cluster;
  uint32_t cluster_count;
} fat32_volume_t;

typedef struct {
  const void *kernel;
  size_t kernel_size;
  const void *efi;
  size_t efi_size;
  const char *limine_conf;
  size_t limine_conf_size;
  const char *install_text;
  size_t install_text_size;
  const void *rollback_kernel;
  size_t rollback_kernel_size;
  const void *rollback_efi;
  size_t rollback_efi_size;
} install_boot_payload_t;

typedef struct {
  uint32_t index;
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t sectors;
} gpt_partition_t;

static uint8_t sector_buf[ORIZON_SECTOR_SIZE] __attribute__((aligned(4096)));
static uint8_t cluster_buf[INSTALL_CLUSTER_BYTES] __attribute__((aligned(4096)));
static uint8_t gpt_entries[INSTALL_GPT_ENTRY_SECTORS * ORIZON_SECTOR_SIZE]
    __attribute__((aligned(4096)));
static uint8_t install_fat[INSTALL_FAT_SECTORS_MAX * ORIZON_SECTOR_SIZE]
    __attribute__((aligned(4096)));

static const char install_limine_conf[] =
    "# Limine Configuration File\n"
    "# Orizon OS x86_64 installed disk\n"
    "\n"
    "timeout: 5\n"
    "interface_resolution: 1024x768\n"
    "interface_branding: Orizon OS\n"
    "default_entry: 1\n"
    "\n"
    "/Orizon OS\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.safe=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Minimal display debug\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.minimal=1 orizon.notimer=1 orizon.nohw=1 orizon.noinput=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Lenovo hardware probe\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.safe=1 orizon.i2chid=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Native display\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    cmdline: orizon.safe=1 orizon.native=1\n";

static const char dualboot_limine_conf[] =
    "# Limine Configuration File\n"
    "# Orizon OS x86_64 dual boot side-by-side ESP\n"
    "\n"
    "timeout: 5\n"
    "interface_resolution: 1024x768\n"
    "interface_branding: Orizon OS\n"
    "default_entry: 1\n"
    "\n"
    "/Orizon OS\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/EFI/Orizon/kernel.elf\n"
    "    cmdline: orizon.safe=1 orizon.dualboot=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Minimal display debug\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/EFI/Orizon/kernel.elf\n"
    "    cmdline: orizon.minimal=1 orizon.notimer=1 orizon.nohw=1 orizon.noinput=1 orizon.dualboot=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Lenovo hardware probe\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/EFI/Orizon/kernel.elf\n"
    "    cmdline: orizon.safe=1 orizon.i2chid=1 orizon.dualboot=1\n"
    "    resolution: 1024x768x32\n"
    "\n"
    "/Orizon OS - Native display\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/EFI/Orizon/kernel.elf\n"
    "    cmdline: orizon.safe=1 orizon.native=1 orizon.dualboot=1\n";

static void append_report(char *report, size_t report_size, const char *line) {
  size_t used;
  if (!report || report_size == 0 || !line) {
    return;
  }
  used = strlen(report);
  if (used + 1 >= report_size) {
    return;
  }
  snprintf(report + used, report_size - used, "%s\n", line);
}

static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v) {
  put32(p, (uint32_t)v);
  put32(p + 4, (uint32_t)(v >> 32));
}

static uint16_t get16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p) {
  return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int)(crc & 1));
    }
  }
  return ~crc;
}

static int write_sector(uint64_t lba, const void *buf) {
  return storage_write(lba, buf, 1);
}

static int read_sector(uint64_t lba, void *buf) {
  return storage_read(lba, buf, 1);
}

static int zero_sector(uint64_t lba) {
  memset(sector_buf, 0, sizeof(sector_buf));
  return write_sector(lba, sector_buf);
}

static void write_gpt_name(uint8_t *entry, const char *name) {
  size_t i = 0;
  while (name && name[i] && i < 36) {
    put16(entry + 56 + i * 2, (uint16_t)(uint8_t)name[i]);
    i++;
  }
}

static void write_gpt_entry(uint8_t *entry, const uint8_t type_guid[16],
                            const uint8_t unique_guid[16], uint64_t first_lba,
                            uint64_t last_lba, const char *name) {
  memset(entry, 0, INSTALL_GPT_ENTRY_SIZE);
  memcpy(entry, type_guid, 16);
  memcpy(entry + 16, unique_guid, 16);
  put64(entry + 32, first_lba);
  put64(entry + 40, last_lba);
  put64(entry + 48, 0);
  write_gpt_name(entry, name);
}

static void write_gpt_header(uint8_t *sector, uint64_t current_lba,
                             uint64_t backup_lba, uint64_t first_usable,
                             uint64_t last_usable, uint64_t entries_lba,
                             uint32_t entries_crc) {
  static const uint8_t disk_guid[16] = {
      0x4f, 0x72, 0x69, 0x7a, 0x6f, 0x6e, 0x4f, 0x53,
      0x88, 0x16, 0x20, 0x26, 0x05, 0x08, 0x00, 0x01};
  uint32_t header_crc;

  memset(sector, 0, ORIZON_SECTOR_SIZE);
  memcpy(sector, "EFI PART", 8);
  put32(sector + 8, 0x00010000U);
  put32(sector + 12, 92);
  put64(sector + 24, current_lba);
  put64(sector + 32, backup_lba);
  put64(sector + 40, first_usable);
  put64(sector + 48, last_usable);
  memcpy(sector + 56, disk_guid, 16);
  put64(sector + 72, entries_lba);
  put32(sector + 80, INSTALL_GPT_ENTRY_COUNT);
  put32(sector + 84, INSTALL_GPT_ENTRY_SIZE);
  put32(sector + 88, entries_crc);
  header_crc = crc32_update(0, sector, 92);
  put32(sector + 16, header_crc);
}

static int install_write_gpt(uint64_t sectors) {
  static const uint8_t esp_type[16] = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8,
                                      0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0,
                                      0xc9, 0x3e, 0xc9, 0x3b};
  static const uint8_t data_type[16] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84,
                                       0x72, 0x47, 0x8e, 0x79, 0x3d, 0x69,
                                       0xd8, 0x47, 0x7d, 0xe4};
  static const uint8_t esp_guid[16] = {0x4f, 0x53, 0x45, 0x53, 0x50, 0x00,
                                      0x00, 0x00, 0x9a, 0x3d, 0x20, 0x26,
                                      0x05, 0x08, 0x00, 0x01};
  static const uint8_t data_guid[16] = {0x4f, 0x53, 0x44, 0x41, 0x54, 0x41,
                                       0x00, 0x00, 0x9a, 0x3d, 0x20, 0x26,
                                       0x05, 0x08, 0x00, 0x02};
  uint64_t last_lba = sectors - 1;
  uint64_t esp_last = INSTALL_DATA_START_LBA - 1;
  uint64_t data_last = last_lba - INSTALL_GPT_ENTRY_SECTORS - 1;
  uint32_t protective_size =
      sectors > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)(sectors - 1);
  uint32_t entries_crc;

  if (sectors < INSTALL_DATA_START_LBA + 65536 ||
      data_last <= INSTALL_DATA_START_LBA) {
    return -1;
  }

  memset(sector_buf, 0, sizeof(sector_buf));
  sector_buf[446 + 4] = 0xEE;
  put32(sector_buf + 446 + 8, 1);
  put32(sector_buf + 446 + 12, protective_size);
  sector_buf[510] = 0x55;
  sector_buf[511] = 0xAA;
  if (write_sector(0, sector_buf) < 0) {
    return -1;
  }

  memset(gpt_entries, 0, sizeof(gpt_entries));
  write_gpt_entry(gpt_entries, esp_type, esp_guid, INSTALL_ESP_START_LBA,
                  esp_last, "Orizon ESP");
  write_gpt_entry(gpt_entries + INSTALL_GPT_ENTRY_SIZE, data_type, data_guid,
                  INSTALL_DATA_START_LBA, data_last, "Orizon Data");
  entries_crc = crc32_update(0, gpt_entries, sizeof(gpt_entries));

  for (uint32_t i = 0; i < INSTALL_GPT_ENTRY_SECTORS; i++) {
    if (write_sector(2 + i, gpt_entries + i * ORIZON_SECTOR_SIZE) < 0 ||
        write_sector(last_lba - INSTALL_GPT_ENTRY_SECTORS + i,
                     gpt_entries + i * ORIZON_SECTOR_SIZE) < 0) {
      return -1;
    }
  }

  write_gpt_header(sector_buf, 1, last_lba, 34, data_last,
                   2, entries_crc);
  if (write_sector(1, sector_buf) < 0) {
    return -1;
  }
  write_gpt_header(sector_buf, last_lba, 1, 34, data_last,
                   last_lba - INSTALL_GPT_ENTRY_SECTORS, entries_crc);
  if (write_sector(last_lba, sector_buf) < 0) {
    return -1;
  }

  return 0;
}

static int fat32_init(fat32_volume_t *vol, uint64_t start_lba,
                      uint32_t total_sectors) {
  uint32_t data_sectors;
  uint32_t clusters;
  uint32_t spf;

  if (!vol || total_sectors < 65536) {
    return -1;
  }
  memset(vol, 0, sizeof(*vol));
  vol->start_lba = start_lba;
  vol->total_sectors = total_sectors;
  vol->sectors_per_cluster = INSTALL_SECTORS_PER_CLUSTER;
  vol->reserved_sectors = 32;
  vol->fat_count = 2;
  vol->root_cluster = 2;

  spf = 1;
  for (;;) {
    data_sectors = total_sectors - vol->reserved_sectors - vol->fat_count * spf;
    clusters = data_sectors / vol->sectors_per_cluster;
    uint32_t needed = ((clusters + 2) * 4 + ORIZON_SECTOR_SIZE - 1) /
                      ORIZON_SECTOR_SIZE;
    if (needed == spf) {
      break;
    }
    spf = needed;
    if (spf > INSTALL_FAT_SECTORS_MAX) {
      return -1;
    }
  }

  vol->sectors_per_fat = spf;
  vol->cluster_count = clusters;
  vol->data_start_sector =
      vol->reserved_sectors + vol->fat_count * vol->sectors_per_fat;
  vol->next_cluster = 3;
  memset(install_fat, 0, vol->sectors_per_fat * ORIZON_SECTOR_SIZE);
  return 0;
}

static void fat_set(uint32_t cluster, uint32_t value) {
  put32(install_fat + cluster * 4, value);
}

static uint32_t fat_alloc_chain(fat32_volume_t *vol, size_t bytes) {
  uint32_t cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;
  uint32_t count = (uint32_t)((bytes + cluster_bytes - 1) / cluster_bytes);
  uint32_t first = vol->next_cluster;

  if (count == 0) {
    count = 1;
  }
  if (first + count >= vol->cluster_count) {
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    uint32_t cluster = first + i;
    fat_set(cluster, i + 1 == count ? 0x0FFFFFFFU : cluster + 1);
  }
  vol->next_cluster += count;
  return first;
}

static uint64_t fat_cluster_lba(const fat32_volume_t *vol, uint32_t cluster) {
  return vol->start_lba + vol->data_start_sector +
         (uint64_t)(cluster - 2) * vol->sectors_per_cluster;
}

static uint8_t fat_lfn_checksum(const char short_name[11]) {
  uint8_t sum = 0;
  for (int i = 0; i < 11; i++) {
    sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) +
                    (uint8_t)short_name[i]);
  }
  return sum;
}

static void fat_dir_add_lfn(uint8_t *dir, size_t *off, const char *long_name,
                            const char short_name[11]) {
  static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16,
                                  18, 20, 22, 24, 28, 30};
  uint8_t *entry = dir + *off;
  size_t len = strlen(long_name);
  uint8_t checksum = fat_lfn_checksum(short_name);

  memset(entry, 0xFF, 32);
  entry[0] = 0x41;
  entry[11] = 0x0F;
  entry[12] = 0;
  entry[13] = checksum;
  put16(entry + 26, 0);
  for (size_t i = 0; i < 13; i++) {
    uint16_t c = 0xFFFF;
    if (i < len) {
      c = (uint16_t)(uint8_t)long_name[i];
    } else if (i == len) {
      c = 0;
    }
    put16(entry + pos[i], c);
  }
  *off += 32;
}

static void fat_dir_add_entry(uint8_t *dir, size_t *off,
                              const char short_name[11], uint8_t attr,
                              uint32_t first_cluster, uint32_t file_size) {
  uint8_t *entry = dir + *off;
  memset(entry, 0, 32);
  memcpy(entry, short_name, 11);
  entry[11] = attr;
  put16(entry + 20, (uint16_t)(first_cluster >> 16));
  put16(entry + 26, (uint16_t)first_cluster);
  put32(entry + 28, file_size);
  *off += 32;
}

static uint32_t fat_dotdot_cluster(const fat32_volume_t *vol,
                                   uint32_t parent) {
  return parent == vol->root_cluster ? 0 : parent;
}

static void fat_dir_add_dot(const fat32_volume_t *vol, uint8_t *dir,
                            size_t *off, uint32_t self, uint32_t parent) {
  fat_dir_add_entry(dir, off, ".          ", 0x10, self, 0);
  fat_dir_add_entry(dir, off, "..         ", 0x10,
                    fat_dotdot_cluster(vol, parent), 0);
}

static int fat_write_cluster(const fat32_volume_t *vol, uint32_t cluster,
                             const uint8_t *data) {
  return storage_write(fat_cluster_lba(vol, cluster), data,
                       vol->sectors_per_cluster);
}

static int fat_write_file_data(const fat32_volume_t *vol, uint32_t first_cluster,
                               const void *data, size_t size) {
  const uint8_t *src = (const uint8_t *)data;
  uint32_t cluster = first_cluster;
  size_t done = 0;

  while (done < size) {
    size_t chunk = size - done;
    if (chunk > INSTALL_CLUSTER_BYTES) {
      chunk = INSTALL_CLUSTER_BYTES;
    }
    memset(cluster_buf, 0, sizeof(cluster_buf));
    memcpy(cluster_buf, src + done, chunk);
    if (fat_write_cluster(vol, cluster, cluster_buf) < 0) {
      return -1;
    }
    done += chunk;
    cluster++;
  }
  return 0;
}

static void fat_prepare_boot_sector(const fat32_volume_t *vol) {
  memset(sector_buf, 0, sizeof(sector_buf));
  sector_buf[0] = 0xEB;
  sector_buf[1] = 0x58;
  sector_buf[2] = 0x90;
  memcpy(sector_buf + 3, "ORIZON  ", 8);
  put16(sector_buf + 11, ORIZON_SECTOR_SIZE);
  sector_buf[13] = (uint8_t)vol->sectors_per_cluster;
  put16(sector_buf + 14, (uint16_t)vol->reserved_sectors);
  sector_buf[16] = (uint8_t)vol->fat_count;
  sector_buf[21] = 0xF8;
  put16(sector_buf + 24, 63);
  put16(sector_buf + 26, 255);
  put32(sector_buf + 28, (uint32_t)vol->start_lba);
  put32(sector_buf + 32, vol->total_sectors);
  put32(sector_buf + 36, vol->sectors_per_fat);
  put32(sector_buf + 44, vol->root_cluster);
  put16(sector_buf + 48, 1);
  put16(sector_buf + 50, 6);
  sector_buf[64] = 0x80;
  sector_buf[66] = 0x29;
  put32(sector_buf + 67, 0x20260508U);
  memcpy(sector_buf + 71, "ORIZON ESP ", 11);
  memcpy(sector_buf + 82, "FAT32   ", 8);
  sector_buf[510] = 0x55;
  sector_buf[511] = 0xAA;
}

static void fat_prepare_fsinfo(const fat32_volume_t *vol) {
  uint32_t used_clusters = vol->next_cluster > 2 ? vol->next_cluster - 2 : 0;
  uint32_t free_clusters = vol->cluster_count > used_clusters
                               ? vol->cluster_count - used_clusters
                               : 0;
  memset(sector_buf, 0, sizeof(sector_buf));
  put32(sector_buf, 0x41615252U);
  put32(sector_buf + 484, 0x61417272U);
  put32(sector_buf + 488, free_clusters);
  put32(sector_buf + 492, vol->next_cluster);
  sector_buf[510] = 0x55;
  sector_buf[511] = 0xAA;
}

static int fat_write_fats(const fat32_volume_t *vol) {
  fat_set(0, 0x0FFFFFF8U);
  fat_set(1, 0x0FFFFFFFU);
  fat_set(vol->root_cluster, 0x0FFFFFFFU);

  for (uint32_t fat = 0; fat < vol->fat_count; fat++) {
    uint64_t fat_lba =
        vol->start_lba + vol->reserved_sectors + fat * vol->sectors_per_fat;
    for (uint32_t s = 0; s < vol->sectors_per_fat; s++) {
      if (write_sector(fat_lba + s, install_fat + s * ORIZON_SECTOR_SIZE) <
          0) {
        return -1;
      }
    }
  }
  return 0;
}

static int fat32_write_boot_files(const fat32_volume_t *vol,
                                  const install_boot_payload_t *payload) {
  static const char limine_short[11] = {'L', 'I', 'M', 'I', 'N', 'E',
                                       '~', '1', 'C', 'O', 'N'};
  static const char rollback_kernel_short[11] = {
      'K', 'R', 'O', 'L', 'L', 'B', 'K', ' ', 'E', 'L', 'F'};
  static const char rollback_efi_short[11] = {
      'B', 'O', 'O', 'T', 'X', '6', '4', ' ', 'R', 'O', 'L'};
  uint32_t efi_dir;
  uint32_t efi_boot_dir;
  uint32_t boot_dir;
  uint32_t bootx64_file;
  uint32_t rollback_efi_file = 0;
  uint32_t kernel_file;
  uint32_t rollback_kernel_file = 0;
  uint32_t limine_root_file;
  uint32_t limine_boot_file;
  uint32_t limine_efi_file;
  uint32_t install_txt_file;
  int has_rollback;
  size_t off;

  if (!vol || !payload || !payload->kernel || payload->kernel_size == 0 ||
      !payload->efi || payload->efi_size == 0 || !payload->limine_conf ||
      payload->limine_conf_size == 0 || !payload->install_text ||
      payload->install_text_size == 0) {
    return -1;
  }
  has_rollback = payload->rollback_kernel && payload->rollback_efi &&
                 payload->rollback_kernel_size > 0 &&
                 payload->rollback_efi_size > 0;
  if ((payload->rollback_kernel || payload->rollback_efi ||
       payload->rollback_kernel_size || payload->rollback_efi_size) &&
      !has_rollback) {
    return -1;
  }

  efi_dir = fat_alloc_chain((fat32_volume_t *)vol, INSTALL_CLUSTER_BYTES);
  efi_boot_dir = fat_alloc_chain((fat32_volume_t *)vol, INSTALL_CLUSTER_BYTES);
  boot_dir = fat_alloc_chain((fat32_volume_t *)vol, INSTALL_CLUSTER_BYTES);
  bootx64_file = fat_alloc_chain((fat32_volume_t *)vol, payload->efi_size);
  if (has_rollback) {
    rollback_efi_file =
        fat_alloc_chain((fat32_volume_t *)vol, payload->rollback_efi_size);
  }
  kernel_file = fat_alloc_chain((fat32_volume_t *)vol, payload->kernel_size);
  if (has_rollback) {
    rollback_kernel_file =
        fat_alloc_chain((fat32_volume_t *)vol, payload->rollback_kernel_size);
  }
  limine_root_file =
      fat_alloc_chain((fat32_volume_t *)vol, payload->limine_conf_size);
  limine_boot_file =
      fat_alloc_chain((fat32_volume_t *)vol, payload->limine_conf_size);
  limine_efi_file =
      fat_alloc_chain((fat32_volume_t *)vol, payload->limine_conf_size);
  install_txt_file =
      fat_alloc_chain((fat32_volume_t *)vol, payload->install_text_size);

  if (!efi_dir || !efi_boot_dir || !boot_dir || !bootx64_file ||
      !kernel_file || !limine_root_file || !limine_boot_file ||
      !limine_efi_file || !install_txt_file ||
      (has_rollback && (!rollback_efi_file || !rollback_kernel_file))) {
    return -1;
  }

  if (fat_write_fats(vol) < 0) {
    return -1;
  }

  memset(cluster_buf, 0, sizeof(cluster_buf));
  off = 0;
  fat_dir_add_entry(cluster_buf, &off, "ORIZON ESP ", 0x08, 0, 0);
  fat_dir_add_entry(cluster_buf, &off, "EFI        ", 0x10, efi_dir, 0);
  fat_dir_add_entry(cluster_buf, &off, "BOOT       ", 0x10, boot_dir, 0);
  fat_dir_add_lfn(cluster_buf, &off, "limine.conf", limine_short);
  fat_dir_add_entry(cluster_buf, &off, limine_short, 0x20, limine_root_file,
                    payload->limine_conf_size);
  fat_dir_add_entry(cluster_buf, &off, "INSTALL TXT", 0x20, install_txt_file,
                    payload->install_text_size);
  if (fat_write_cluster(vol, vol->root_cluster, cluster_buf) < 0) {
    return -1;
  }

  memset(cluster_buf, 0, sizeof(cluster_buf));
  off = 0;
  fat_dir_add_dot(vol, cluster_buf, &off, efi_dir, vol->root_cluster);
  fat_dir_add_entry(cluster_buf, &off, "BOOT       ", 0x10, efi_boot_dir, 0);
  if (fat_write_cluster(vol, efi_dir, cluster_buf) < 0) {
    return -1;
  }

  memset(cluster_buf, 0, sizeof(cluster_buf));
  off = 0;
  fat_dir_add_dot(vol, cluster_buf, &off, efi_boot_dir, efi_dir);
  fat_dir_add_entry(cluster_buf, &off, "BOOTX64 EFI", 0x20, bootx64_file,
                    payload->efi_size);
  if (has_rollback) {
    fat_dir_add_entry(cluster_buf, &off, rollback_efi_short, 0x20,
                      rollback_efi_file, payload->rollback_efi_size);
  }
  fat_dir_add_lfn(cluster_buf, &off, "limine.conf", limine_short);
  fat_dir_add_entry(cluster_buf, &off, limine_short, 0x20, limine_efi_file,
                    payload->limine_conf_size);
  if (fat_write_cluster(vol, efi_boot_dir, cluster_buf) < 0) {
    return -1;
  }

  memset(cluster_buf, 0, sizeof(cluster_buf));
  off = 0;
  fat_dir_add_dot(vol, cluster_buf, &off, boot_dir, vol->root_cluster);
  fat_dir_add_entry(cluster_buf, &off, "KERNEL  ELF", 0x20, kernel_file,
                    payload->kernel_size);
  if (has_rollback) {
    fat_dir_add_entry(cluster_buf, &off, rollback_kernel_short, 0x20,
                      rollback_kernel_file, payload->rollback_kernel_size);
  }
  fat_dir_add_lfn(cluster_buf, &off, "limine.conf", limine_short);
  fat_dir_add_entry(cluster_buf, &off, limine_short, 0x20, limine_boot_file,
                    payload->limine_conf_size);
  if (fat_write_cluster(vol, boot_dir, cluster_buf) < 0) {
    return -1;
  }

  if (fat_write_file_data(vol, bootx64_file, payload->efi,
                          payload->efi_size) < 0 ||
      fat_write_file_data(vol, kernel_file, payload->kernel,
                          payload->kernel_size) < 0 ||
      (has_rollback &&
       (fat_write_file_data(vol, rollback_efi_file, payload->rollback_efi,
                            payload->rollback_efi_size) < 0 ||
        fat_write_file_data(vol, rollback_kernel_file,
                            payload->rollback_kernel,
                            payload->rollback_kernel_size) < 0)) ||
      fat_write_file_data(vol, limine_root_file, payload->limine_conf,
                          payload->limine_conf_size) < 0 ||
      fat_write_file_data(vol, limine_boot_file, payload->limine_conf,
                          payload->limine_conf_size) < 0 ||
      fat_write_file_data(vol, limine_efi_file, payload->limine_conf,
                          payload->limine_conf_size) < 0 ||
      fat_write_file_data(vol, install_txt_file, payload->install_text,
                          payload->install_text_size) < 0) {
    return -1;
  }

  return 0;
}

static int fat32_format_esp_payload(uint64_t start_lba, uint32_t total_sectors,
                                    const install_boot_payload_t *payload) {
  fat32_volume_t vol;

  if (fat32_init(&vol, start_lba, total_sectors) < 0) {
    return -1;
  }

  fat_prepare_boot_sector(&vol);
  if (write_sector(start_lba, sector_buf) < 0 ||
      write_sector(start_lba + 6, sector_buf) < 0) {
    return -1;
  }
  for (uint32_t i = 1; i < vol.reserved_sectors; i++) {
    if (i == 6) {
      continue;
    }
    if (zero_sector(start_lba + i) < 0) {
      return -1;
    }
  }

  if (fat32_write_boot_files(&vol, payload) < 0) {
    return -1;
  }

  fat_prepare_fsinfo(&vol);
  if (write_sector(start_lba + 1, sector_buf) < 0 ||
      write_sector(start_lba + 7, sector_buf) < 0) {
    return -1;
  }

  return 0;
}

static int fat32_format_esp(uint64_t start_lba, uint32_t total_sectors,
                            const orizon_install_config_t *config) {
  char install_text[512];
  install_boot_payload_t payload;

  snprintf(install_text, sizeof(install_text),
           "Orizon OS installed\nlanguage=%s\nkeyboard=%s\nhostname=%s\n"
           "next=shutdown-remove-installer\n",
           config->language, config->keyboard, config->hostname);

  payload.kernel = boot_kernel_image();
  payload.kernel_size = boot_kernel_image_size();
  payload.efi = boot_efi_image();
  payload.efi_size = boot_efi_image_size();
  payload.limine_conf = install_limine_conf;
  payload.limine_conf_size = sizeof(install_limine_conf) - 1;
  payload.install_text = install_text;
  payload.install_text_size = strlen(install_text);
  payload.rollback_kernel = NULL;
  payload.rollback_kernel_size = 0;
  payload.rollback_efi = NULL;
  payload.rollback_efi_size = 0;
  return fat32_format_esp_payload(start_lba, total_sectors, &payload);
}

static void append_check_line(char *report, size_t report_size,
                              const char *label, int ok) {
  char line[192];
  snprintf(line, sizeof(line), "%s %s", ok ? "[OK]" : "[!!]", label);
  append_report(report, report_size, line);
}

static int gpt_find_esp(gpt_partition_t *out, char *report,
                        size_t report_size) {
  static const uint8_t esp_type[16] = {
      0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
      0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
  uint64_t entries_lba;
  uint32_t entry_count;
  uint32_t entry_size;
  uint32_t scan_count;
  char line[192];

  if (!out) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (!storage_available()) {
    append_check_line(report, report_size, "writable AHCI/NVMe disk", 0);
    return -1;
  }
  if (read_sector(1, sector_buf) < 0 ||
      memcmp(sector_buf, "EFI PART", 8) != 0) {
    append_check_line(report, report_size, "existing GPT disk", 0);
    return -1;
  }
  append_check_line(report, report_size, "existing GPT disk", 1);

  entries_lba = get64(sector_buf + 72);
  entry_count = get32(sector_buf + 80);
  entry_size = get32(sector_buf + 84);
  if (entries_lba == 0 || entry_count == 0 ||
      entry_size != INSTALL_GPT_ENTRY_SIZE) {
    append_check_line(report, report_size, "GPT partition entries", 0);
    return -1;
  }
  append_check_line(report, report_size, "GPT partition entries", 1);

  scan_count = entry_count < INSTALL_GPT_ENTRY_COUNT
                   ? entry_count
                   : INSTALL_GPT_ENTRY_COUNT;
  for (uint32_t i = 0; i < scan_count; i++) {
    uint64_t lba =
        entries_lba + ((uint64_t)i * entry_size) / ORIZON_SECTOR_SIZE;
    uint32_t off =
        (uint32_t)(((uint64_t)i * entry_size) % ORIZON_SECTOR_SIZE);
    uint8_t *entry;
    uint64_t first;
    uint64_t last;

    if (read_sector(lba, sector_buf) < 0) {
      append_check_line(report, report_size, "read GPT entry", 0);
      return -1;
    }
    entry = sector_buf + off;
    if (memcmp(entry, esp_type, sizeof(esp_type)) != 0) {
      continue;
    }
    first = get64(entry + 32);
    last = get64(entry + 40);
    if (last <= first || first < 34 || last >= storage_sector_count()) {
      continue;
    }
    out->index = i + 1;
    out->first_lba = first;
    out->last_lba = last;
    out->sectors = last - first + 1;
    snprintf(line, sizeof(line), "ESP partition found: GPT #%lu LBA %lu..%lu",
             (unsigned long)out->index, (unsigned long)out->first_lba,
             (unsigned long)out->last_lba);
    append_check_line(report, report_size, line, 1);
    return 0;
  }

  append_check_line(report, report_size, "EFI System Partition found", 0);
  return -1;
}

static int installed_gpt_layout_valid(char *report, size_t report_size,
                                      int verbose) {
  static const uint8_t esp_type[16] = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8,
                                      0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0,
                                      0xc9, 0x3e, 0xc9, 0x3b};
  static const uint8_t data_type[16] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84,
                                       0x72, 0x47, 0x8e, 0x79, 0x3d, 0x69,
                                       0xd8, 0x47, 0x7d, 0xe4};
  uint64_t sectors = storage_sector_count();
  uint64_t esp_last = INSTALL_DATA_START_LBA - 1;
  uint64_t data_last =
      sectors > INSTALL_GPT_ENTRY_SECTORS + 1
          ? sectors - INSTALL_GPT_ENTRY_SECTORS - 2
          : 0;
  int ok = 1;

  if (!storage_available()) {
    if (verbose) {
      append_check_line(report, report_size, "writable AHCI/NVMe disk", 0);
    }
    return 0;
  }
  if (sectors < INSTALL_DATA_START_LBA + 65536 || data_last <= INSTALL_DATA_START_LBA) {
    if (verbose) {
      append_check_line(report, report_size, "disk large enough for Orizon layout", 0);
    }
    return 0;
  }
  if (verbose) {
    append_check_line(report, report_size, "disk large enough for Orizon layout", 1);
  }

  if (read_sector(0, sector_buf) < 0 || sector_buf[510] != 0x55 ||
      sector_buf[511] != 0xAA || sector_buf[446 + 4] != 0xEE) {
    ok = 0;
  }
  if (verbose) {
    append_check_line(report, report_size, "protective MBR", ok);
  }
  if (!ok) {
    return 0;
  }

  ok = read_sector(1, sector_buf) == 0 &&
       memcmp(sector_buf, "EFI PART", 8) == 0 &&
       get64(sector_buf + 24) == 1 &&
       get64(sector_buf + 32) == sectors - 1 &&
       get64(sector_buf + 72) == 2 &&
       get32(sector_buf + 80) >= 2 &&
       get32(sector_buf + 84) == INSTALL_GPT_ENTRY_SIZE;
  if (verbose) {
    append_check_line(report, report_size, "primary GPT header", ok);
  }
  if (!ok) {
    return 0;
  }

  ok = read_sector(2, gpt_entries) == 0 &&
       memcmp(gpt_entries, esp_type, sizeof(esp_type)) == 0 &&
       get64(gpt_entries + 32) == INSTALL_ESP_START_LBA &&
       get64(gpt_entries + 40) == esp_last &&
       memcmp(gpt_entries + INSTALL_GPT_ENTRY_SIZE, data_type,
              sizeof(data_type)) == 0 &&
       get64(gpt_entries + INSTALL_GPT_ENTRY_SIZE + 32) ==
           INSTALL_DATA_START_LBA &&
       get64(gpt_entries + INSTALL_GPT_ENTRY_SIZE + 40) == data_last;
  if (verbose) {
    append_check_line(report, report_size, "Orizon ESP/Data GPT entries", ok);
  }
  return ok;
}

static int fat32_mount_installed_esp(fat32_volume_t *vol, char *report,
                                     size_t report_size) {
  uint32_t total;
  uint32_t data_sectors;
  uint32_t cluster_bytes;

  if (!vol || read_sector(INSTALL_ESP_START_LBA, sector_buf) < 0) {
    append_check_line(report, report_size, "FAT32 ESP boot sector", 0);
    return -1;
  }

  memset(vol, 0, sizeof(*vol));
  vol->start_lba = INSTALL_ESP_START_LBA;
  vol->sectors_per_cluster = sector_buf[13];
  vol->reserved_sectors = get16(sector_buf + 14);
  vol->fat_count = sector_buf[16];
  total = get16(sector_buf + 19);
  if (total == 0) {
    total = get32(sector_buf + 32);
  }
  vol->total_sectors = total;
  vol->sectors_per_fat = get32(sector_buf + 36);
  vol->root_cluster = get32(sector_buf + 44);
  vol->data_start_sector =
      vol->reserved_sectors + vol->fat_count * vol->sectors_per_fat;
  if (vol->data_start_sector < vol->total_sectors) {
    data_sectors = vol->total_sectors - vol->data_start_sector;
  } else {
    data_sectors = 0;
  }
  vol->cluster_count =
      vol->sectors_per_cluster ? data_sectors / vol->sectors_per_cluster : 0;
  cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;

  if (get16(sector_buf + 11) != ORIZON_SECTOR_SIZE ||
      sector_buf[510] != 0x55 || sector_buf[511] != 0xAA ||
      vol->total_sectors !=
          (uint32_t)(INSTALL_DATA_START_LBA - INSTALL_ESP_START_LBA) ||
      vol->sectors_per_cluster == 0 || cluster_bytes > INSTALL_CLUSTER_BYTES ||
      vol->reserved_sectors < 8 || vol->fat_count != 2 ||
      vol->sectors_per_fat == 0 || vol->root_cluster < 2 ||
      vol->cluster_count < 65525) {
    append_check_line(report, report_size, "FAT32 ESP boot sector", 0);
    return -1;
  }

  append_check_line(report, report_size, "FAT32 ESP boot sector", 1);
  return 0;
}

static int fat32_mount_existing_esp(fat32_volume_t *vol,
                                    const gpt_partition_t *part,
                                    char *report, size_t report_size) {
  uint32_t total;
  uint32_t data_sectors;
  uint32_t cluster_bytes;

  if (!vol || !part || part->sectors == 0 ||
      read_sector(part->first_lba, sector_buf) < 0) {
    append_check_line(report, report_size, "existing FAT32 ESP boot sector", 0);
    return -1;
  }

  memset(vol, 0, sizeof(*vol));
  vol->start_lba = part->first_lba;
  vol->sectors_per_cluster = sector_buf[13];
  vol->reserved_sectors = get16(sector_buf + 14);
  vol->fat_count = sector_buf[16];
  total = get16(sector_buf + 19);
  if (total == 0) {
    total = get32(sector_buf + 32);
  }
  if (total == 0 || total > part->sectors) {
    total = (uint32_t)part->sectors;
  }
  vol->total_sectors = total;
  vol->sectors_per_fat = get32(sector_buf + 36);
  vol->root_cluster = get32(sector_buf + 44);
  vol->data_start_sector =
      vol->reserved_sectors + vol->fat_count * vol->sectors_per_fat;
  data_sectors = vol->data_start_sector < vol->total_sectors
                     ? vol->total_sectors - vol->data_start_sector
                     : 0;
  vol->cluster_count =
      vol->sectors_per_cluster ? data_sectors / vol->sectors_per_cluster : 0;
  cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;

  if (get16(sector_buf + 11) != ORIZON_SECTOR_SIZE ||
      sector_buf[510] != 0x55 || sector_buf[511] != 0xAA ||
      memcmp(sector_buf + 82, "FAT32", 5) != 0 ||
      vol->sectors_per_cluster == 0 || cluster_bytes > INSTALL_CLUSTER_BYTES ||
      vol->reserved_sectors == 0 || vol->fat_count == 0 ||
      vol->fat_count > 2 || vol->sectors_per_fat == 0 ||
      vol->sectors_per_fat > INSTALL_FAT_SECTORS_MAX ||
      vol->root_cluster < 2 || vol->cluster_count < 128) {
    append_check_line(report, report_size, "existing FAT32 ESP boot sector", 0);
    return -1;
  }

  append_check_line(report, report_size, "existing FAT32 ESP boot sector", 1);
  return 0;
}

static int fat_read_cluster(const fat32_volume_t *vol, uint32_t cluster,
                            uint8_t *out) {
  if (!vol || !out || cluster < 2 ||
      cluster >= vol->cluster_count + 2 ||
      vol->sectors_per_cluster * ORIZON_SECTOR_SIZE > INSTALL_CLUSTER_BYTES) {
    return -1;
  }
  return storage_read(fat_cluster_lba(vol, cluster), out,
                      vol->sectors_per_cluster);
}

static int fat_find_entry_short(const fat32_volume_t *vol, uint32_t dir_cluster,
                                const char short_name[11], uint8_t *attr,
                                uint32_t *cluster, uint32_t *size) {
  uint32_t cluster_bytes;

  if (fat_read_cluster(vol, dir_cluster, cluster_buf) < 0) {
    return -1;
  }
  cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;
  for (uint32_t off = 0; off + 32 <= cluster_bytes; off += 32) {
    uint8_t *entry = cluster_buf + off;
    uint8_t entry_attr;
    if (entry[0] == 0x00) {
      break;
    }
    if (entry[0] == 0xE5) {
      continue;
    }
    entry_attr = entry[11];
    if (entry_attr == 0x0F) {
      continue;
    }
    if (memcmp(entry, short_name, 11) != 0) {
      continue;
    }
    if (attr) {
      *attr = entry_attr;
    }
    if (cluster) {
      *cluster = ((uint32_t)get16(entry + 20) << 16) | get16(entry + 26);
    }
    if (size) {
      *size = get32(entry + 28);
    }
    return 0;
  }
  return -1;
}

static uint32_t fat_get(uint32_t cluster) {
  return get32(install_fat + cluster * 4) & 0x0FFFFFFFU;
}

static void fat_free_chain(const fat32_volume_t *vol, uint32_t cluster) {
  uint32_t limit = vol ? vol->cluster_count + 2 : 0;

  while (cluster >= 2 && cluster < limit) {
    uint32_t next = fat_get(cluster);
    fat_set(cluster, 0);
    if (next >= 0x0FFFFFF8U || next == cluster) {
      break;
    }
    cluster = next;
  }
}

static int fat32_load_fat(const fat32_volume_t *vol) {
  if (!vol || vol->sectors_per_fat == 0 ||
      vol->sectors_per_fat > INSTALL_FAT_SECTORS_MAX) {
    return -1;
  }
  memset(install_fat, 0, sizeof(install_fat));
  return storage_read(vol->start_lba + vol->reserved_sectors, install_fat,
                      vol->sectors_per_fat);
}

static uint32_t fat_alloc_chain_scan(fat32_volume_t *vol, size_t bytes) {
  uint32_t cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;
  uint32_t count = (uint32_t)((bytes + cluster_bytes - 1) / cluster_bytes);
  uint32_t limit;

  if (!vol || cluster_bytes == 0 || vol->cluster_count < 4) {
    return 0;
  }
  if (count == 0) {
    count = 1;
  }
  limit = vol->cluster_count + 2;
  for (uint32_t first = 2; first + count < limit; first++) {
    int free_run = 1;
    for (uint32_t i = 0; i < count; i++) {
      if (fat_get(first + i) != 0) {
        free_run = 0;
        first += i;
        break;
      }
    }
    if (!free_run) {
      continue;
    }
    for (uint32_t i = 0; i < count; i++) {
      fat_set(first + i, i + 1 == count ? 0x0FFFFFFFU : first + i + 1);
    }
    return first;
  }
  return 0;
}

static int fat_dir_find_short_offset(const fat32_volume_t *vol,
                                     uint32_t dir_cluster,
                                     const char short_name[11],
                                     uint8_t *attr, uint32_t *cluster,
                                     uint32_t *size, uint32_t *entry_off) {
  uint32_t cluster_bytes;

  if (fat_read_cluster(vol, dir_cluster, cluster_buf) < 0) {
    return -1;
  }
  cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;
  for (uint32_t off = 0; off + 32 <= cluster_bytes; off += 32) {
    uint8_t *entry = cluster_buf + off;
    uint8_t entry_attr;
    if (entry[0] == 0x00) {
      break;
    }
    if (entry[0] == 0xE5) {
      continue;
    }
    entry_attr = entry[11];
    if (entry_attr == 0x0F || memcmp(entry, short_name, 11) != 0) {
      continue;
    }
    if (attr) {
      *attr = entry_attr;
    }
    if (cluster) {
      *cluster = ((uint32_t)get16(entry + 20) << 16) | get16(entry + 26);
    }
    if (size) {
      *size = get32(entry + 28);
    }
    if (entry_off) {
      *entry_off = off;
    }
    return 0;
  }
  return -1;
}

static int fat_dir_find_free_slots(const fat32_volume_t *vol,
                                   uint32_t dir_cluster, uint32_t slots,
                                   uint32_t *entry_off) {
  uint32_t cluster_bytes;
  uint32_t run = 0;
  uint32_t start = 0;

  if (!entry_off || slots == 0 ||
      fat_read_cluster(vol, dir_cluster, cluster_buf) < 0) {
    return -1;
  }
  cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;
  for (uint32_t off = 0; off + 32 <= cluster_bytes; off += 32) {
    uint8_t first = cluster_buf[off];
    if (first == 0x00 || first == 0xE5) {
      if (run == 0) {
        start = off;
      }
      run++;
      if (run >= slots) {
        *entry_off = start;
        return 0;
      }
    } else {
      run = 0;
    }
  }
  return -1;
}

static int fat_dir_write_slots(const fat32_volume_t *vol, uint32_t dir_cluster,
                               uint32_t entry_off, const uint8_t *entries,
                               uint32_t slots) {
  uint32_t cluster_bytes;

  if (!entries || slots == 0 ||
      fat_read_cluster(vol, dir_cluster, cluster_buf) < 0) {
    return -1;
  }
  cluster_bytes = vol->sectors_per_cluster * ORIZON_SECTOR_SIZE;
  if (entry_off + slots * 32 > cluster_bytes) {
    return -1;
  }
  memcpy(cluster_buf + entry_off, entries, slots * 32);
  return fat_write_cluster(vol, dir_cluster, cluster_buf);
}

static int fat_dir_delete_short(const fat32_volume_t *vol, uint32_t dir_cluster,
                                const char short_name[11]) {
  uint8_t attr = 0;
  uint32_t cluster = 0;
  uint32_t entry_off = 0;

  if (fat_dir_find_short_offset(vol, dir_cluster, short_name, &attr, &cluster,
                                NULL, &entry_off) < 0) {
    return 0;
  }
  if (fat_read_cluster(vol, dir_cluster, cluster_buf) < 0) {
    return -1;
  }
  if ((attr & 0x10) == 0 && cluster >= 2) {
    fat_free_chain(vol, cluster);
  }
  cluster_buf[entry_off] = 0xE5;
  while (entry_off >= 32 && cluster_buf[entry_off - 32 + 11] == 0x0F) {
    entry_off -= 32;
    cluster_buf[entry_off] = 0xE5;
  }
  return fat_write_cluster(vol, dir_cluster, cluster_buf);
}

static int fat_dir_add_short_entry(const fat32_volume_t *vol,
                                   uint32_t dir_cluster,
                                   const char short_name[11],
                                   const char *long_name, uint8_t attr,
                                   uint32_t first_cluster,
                                   uint32_t file_size) {
  uint8_t entries[64];
  uint32_t slots = long_name && long_name[0] ? 2 : 1;
  uint32_t entry_off = 0;
  size_t off = 0;

  if (fat_dir_find_free_slots(vol, dir_cluster, slots, &entry_off) < 0) {
    return -1;
  }
  memset(entries, 0, sizeof(entries));
  if (long_name && long_name[0]) {
    fat_dir_add_lfn(entries, &off, long_name, short_name);
  }
  fat_dir_add_entry(entries, &off, short_name, attr, first_cluster, file_size);
  return fat_dir_write_slots(vol, dir_cluster, entry_off, entries, slots);
}

static int fat_ensure_dir_short(fat32_volume_t *vol, uint32_t parent_cluster,
                                const char short_name[11],
                                uint32_t *out_cluster) {
  uint8_t attr = 0;
  uint32_t cluster = 0;

  if (!vol || !out_cluster) {
    return -1;
  }
  if (fat_dir_find_short_offset(vol, parent_cluster, short_name, &attr,
                                &cluster, NULL, NULL) == 0) {
    if ((attr & 0x10) == 0 || cluster < 2) {
      return -1;
    }
    *out_cluster = cluster;
    return 0;
  }

  cluster = fat_alloc_chain_scan(vol, INSTALL_CLUSTER_BYTES);
  if (!cluster) {
    return -1;
  }
  memset(cluster_buf, 0, sizeof(cluster_buf));
  size_t off = 0;
  fat_dir_add_dot(vol, cluster_buf, &off, cluster, parent_cluster);
  if (fat_write_cluster(vol, cluster, cluster_buf) < 0) {
    return -1;
  }
  if (fat_dir_add_short_entry(vol, parent_cluster, short_name, NULL, 0x10,
                              cluster, 0) < 0) {
    return -1;
  }
  *out_cluster = cluster;
  return 0;
}

static int fat_write_regular_file_short(fat32_volume_t *vol,
                                        uint32_t dir_cluster,
                                        const char short_name[11],
                                        const char *long_name,
                                        const void *data, size_t size) {
  uint32_t cluster;

  if (!vol || !data || size == 0) {
    return -1;
  }
  if (fat_dir_delete_short(vol, dir_cluster, short_name) < 0) {
    return -1;
  }
  cluster = fat_alloc_chain_scan(vol, size);
  if (!cluster) {
    return -1;
  }
  if (fat_write_file_data(vol, cluster, data, size) < 0) {
    return -1;
  }
  return fat_dir_add_short_entry(vol, dir_cluster, short_name, long_name, 0x20,
                                 cluster, (uint32_t)size);
}

static int boot_check_append_entry(char *report, size_t report_size,
                                   const fat32_volume_t *vol,
                                   uint32_t dir_cluster,
                                   const char short_name[11],
                                   const char *label, uint8_t required_attr,
                                   uint32_t min_size, uint32_t *out_cluster) {
  uint8_t attr = 0;
  uint32_t cluster = 0;
  uint32_t size = 0;
  char line[192];
  int ok = fat_find_entry_short(vol, dir_cluster, short_name, &attr, &cluster,
                                &size) == 0;

  if (ok && required_attr == 0x10) {
    ok = (attr & 0x10) != 0 && cluster >= 2;
  } else if (ok && required_attr == 0x20) {
    ok = (attr & 0x10) == 0 && size >= min_size && cluster >= 2;
  } else if (ok && required_attr) {
    ok = (attr & required_attr) != 0;
  }

  if (out_cluster) {
    *out_cluster = ok ? cluster : 0;
  }
  if (ok && required_attr == 0x20) {
    snprintf(line, sizeof(line), "%s (%lu bytes)", label,
             (unsigned long)size);
    append_check_line(report, report_size, line, 1);
  } else {
    append_check_line(report, report_size, label, ok);
  }
  return ok ? 0 : -1;
}

static int dualboot_write_side_by_side_esp(
    fat32_volume_t *vol, const install_boot_payload_t *payload, char *report,
    size_t report_size) {
  static const char efi_short[11] = {'E', 'F', 'I', ' ', ' ', ' ', ' ',
                                    ' ', ' ', ' ', ' '};
  static const char orizon_short[11] = {'O', 'R', 'I', 'Z', 'O', 'N', ' ',
                                       ' ', ' ', ' ', ' '};
  static const char bootx64_short[11] = {'B', 'O', 'O', 'T', 'X', '6', '4',
                                        ' ', 'E', 'F', 'I'};
  static const char kernel_short[11] = {'K', 'E', 'R', 'N', 'E', 'L', ' ',
                                       ' ', 'E', 'L', 'F'};
  static const char limine_short[11] = {'L', 'I', 'M', 'I', 'N', 'E',
                                       '~', '1', 'C', 'O', 'N'};
  static const char install_short[11] = {'I', 'N', 'S', 'T', 'A', 'L', 'L',
                                        ' ', 'T', 'X', 'T'};
  uint32_t efi_dir = 0;
  uint32_t orizon_dir = 0;

  if (!vol || !payload || !payload->kernel || payload->kernel_size == 0 ||
      !payload->efi || payload->efi_size == 0 || !payload->limine_conf ||
      payload->limine_conf_size == 0 || !payload->install_text ||
      payload->install_text_size == 0) {
    append_report(report, report_size, "dualboot: invalid boot payload");
    return -1;
  }
  if (fat32_load_fat(vol) < 0) {
    append_report(report, report_size, "dualboot: cannot read ESP FAT");
    return -1;
  }

  if (fat_ensure_dir_short(vol, vol->root_cluster, efi_short, &efi_dir) < 0 ||
      fat_ensure_dir_short(vol, efi_dir, orizon_short, &orizon_dir) < 0) {
    append_report(report, report_size,
                  "dualboot: cannot create /EFI/Orizon directory");
    return -1;
  }

  if (fat_write_regular_file_short(vol, orizon_dir, bootx64_short, NULL,
                                   payload->efi, payload->efi_size) < 0 ||
      fat_write_regular_file_short(vol, orizon_dir, kernel_short, NULL,
                                   payload->kernel, payload->kernel_size) < 0 ||
      fat_write_regular_file_short(vol, orizon_dir, limine_short,
                                   "limine.conf", payload->limine_conf,
                                   payload->limine_conf_size) < 0 ||
      fat_write_regular_file_short(vol, orizon_dir, install_short, NULL,
                                   payload->install_text,
                                   payload->install_text_size) < 0) {
    append_report(report, report_size,
                  "dualboot: cannot write Orizon files to ESP");
    return -1;
  }

  if (fat_write_fats(vol) < 0) {
    append_report(report, report_size, "dualboot: cannot update ESP FAT");
    return -1;
  }

  append_report(report, report_size,
                "dualboot: wrote /EFI/Orizon without rewriting partitions");
  return 0;
}

static int dualboot_check_impl(char *report, size_t report_size,
                               int reset_report) {
  static const char efi_short[11] = {'E', 'F', 'I', ' ', ' ', ' ', ' ',
                                    ' ', ' ', ' ', ' '};
  static const char orizon_short[11] = {'O', 'R', 'I', 'Z', 'O', 'N', ' ',
                                       ' ', ' ', ' ', ' '};
  static const char bootx64_short[11] = {'B', 'O', 'O', 'T', 'X', '6', '4',
                                        ' ', 'E', 'F', 'I'};
  static const char kernel_short[11] = {'K', 'E', 'R', 'N', 'E', 'L', ' ',
                                       ' ', 'E', 'L', 'F'};
  static const char limine_short[11] = {'L', 'I', 'M', 'I', 'N', 'E',
                                       '~', '1', 'C', 'O', 'N'};
  static const char install_short[11] = {'I', 'N', 'S', 'T', 'A', 'L', 'L',
                                        ' ', 'T', 'X', 'T'};
  gpt_partition_t esp;
  fat32_volume_t vol;
  uint32_t efi_dir = 0;
  uint32_t orizon_dir = 0;
  int failures = 0;

  if (reset_report && report && report_size > 0) {
    report[0] = '\0';
  }
  append_report(report, report_size, "\033[1;36mOrizon dual boot check\033[0m");
  if (gpt_find_esp(&esp, report, report_size) < 0 ||
      fat32_mount_existing_esp(&vol, &esp, report, report_size) < 0) {
    append_report(report, report_size,
                  "Dual boot check: FAILED. No writable existing FAT32 ESP.");
    return -1;
  }
  if (boot_check_append_entry(report, report_size, &vol, vol.root_cluster,
                              efi_short, "/EFI directory", 0x10, 0,
                              &efi_dir) < 0) {
    failures++;
  }
  if (efi_dir &&
      boot_check_append_entry(report, report_size, &vol, efi_dir,
                              orizon_short, "/EFI/Orizon directory", 0x10, 0,
                              &orizon_dir) < 0) {
    failures++;
  } else if (!efi_dir) {
    failures++;
  }
  if (orizon_dir) {
    if (boot_check_append_entry(report, report_size, &vol, orizon_dir,
                                bootx64_short,
                                "/EFI/Orizon/BOOTX64.EFI", 0x20, 8192,
                                NULL) < 0) {
      failures++;
    }
    if (boot_check_append_entry(report, report_size, &vol, orizon_dir,
                                kernel_short, "/EFI/Orizon/KERNEL.ELF", 0x20,
                                65536, NULL) < 0) {
      failures++;
    }
    if (boot_check_append_entry(report, report_size, &vol, orizon_dir,
                                limine_short, "/EFI/Orizon/limine.conf", 0x20,
                                32, NULL) < 0) {
      failures++;
    }
    if (boot_check_append_entry(report, report_size, &vol, orizon_dir,
                                install_short, "/EFI/Orizon/INSTALL.TXT",
                                0x20, 8, NULL) < 0) {
      failures++;
    }
  } else {
    failures++;
  }

  if (failures == 0) {
    append_report(report, report_size,
                  "Dual boot check: OK. Boot file is /EFI/Orizon/BOOTX64.EFI.");
    return 0;
  }
  append_report(report, report_size,
                "Dual boot check: FAILED. Run install dual-boot mode again.");
  return -2;
}

static int boot_check_impl(char *report, size_t report_size, int reset_report) {
  static const char limine_short[11] = {'L', 'I', 'M', 'I', 'N', 'E',
                                       '~', '1', 'C', 'O', 'N'};
  fat32_volume_t vol;
  uint32_t efi_dir = 0;
  uint32_t efi_boot_dir = 0;
  uint32_t boot_dir = 0;
  int failures = 0;

  if (reset_report && report && report_size > 0) {
    report[0] = '\0';
  }
  append_report(report, report_size, "\033[1;36mOrizon boot check\033[0m");

  if (!installed_gpt_layout_valid(report, report_size, 1)) {
    append_report(report, report_size,
                  "Boot check: FAILED. Run install or repair the disk layout.");
    return -1;
  }
  if (fat32_mount_installed_esp(&vol, report, report_size) < 0) {
    append_report(report, report_size,
                  "Boot check: FAILED. Run repair-boot from the live ISO.");
    return -2;
  }

  if (boot_check_append_entry(report, report_size, &vol, vol.root_cluster,
                              "ORIZON ESP ", "ESP volume label", 0x08, 0,
                              NULL) < 0) {
    failures++;
  }
  if (boot_check_append_entry(report, report_size, &vol, vol.root_cluster,
                              "EFI        ", "/EFI directory", 0x10, 0,
                              &efi_dir) < 0) {
    failures++;
  }
  if (boot_check_append_entry(report, report_size, &vol, vol.root_cluster,
                              "BOOT       ", "/BOOT directory", 0x10, 0,
                              &boot_dir) < 0) {
    failures++;
  }
  if (boot_check_append_entry(report, report_size, &vol, vol.root_cluster,
                              limine_short, "/limine.conf", 0x20, 32,
                              NULL) < 0) {
    failures++;
  }
  if (efi_dir &&
      boot_check_append_entry(report, report_size, &vol, efi_dir,
                              "BOOT       ", "/EFI/BOOT directory", 0x10, 0,
                              &efi_boot_dir) < 0) {
    failures++;
  } else if (!efi_dir) {
    failures++;
  }
  if (efi_boot_dir) {
    if (boot_check_append_entry(report, report_size, &vol, efi_boot_dir,
                                "BOOTX64 EFI", "/EFI/BOOT/BOOTX64.EFI",
                                0x20, 8192, NULL) < 0) {
      failures++;
    }
    if (boot_check_append_entry(report, report_size, &vol, efi_boot_dir,
                                limine_short, "/EFI/BOOT/limine.conf",
                                0x20, 32, NULL) < 0) {
      failures++;
    }
  } else {
    failures++;
  }
  if (boot_dir) {
    if (boot_check_append_entry(report, report_size, &vol, boot_dir,
                                "KERNEL  ELF", "/BOOT/KERNEL.ELF", 0x20,
                                65536, NULL) < 0) {
      failures++;
    }
    if (boot_check_append_entry(report, report_size, &vol, boot_dir,
                                limine_short, "/BOOT/limine.conf", 0x20,
                                32, NULL) < 0) {
      failures++;
    }
  } else {
    failures++;
  }
  if (boot_check_append_entry(report, report_size, &vol, vol.root_cluster,
                              "INSTALL TXT", "/INSTALL.TXT marker", 0x20, 8,
                              NULL) < 0) {
    failures++;
  }

  if (failures == 0) {
    append_report(report, report_size,
                  "Boot check: OK. Installed disk has UEFI fallback boot files.");
    return 0;
  }
  append_report(report, report_size,
                "Boot check: FAILED. Run repair-boot from the live ISO.");
  return -3;
}

int orizon_install_boot_check(char *report, size_t report_size) {
  return boot_check_impl(report, report_size, 1);
}

int orizon_install_dualboot_check(char *report, size_t report_size) {
  return dualboot_check_impl(report, report_size, 1);
}

int orizon_install_repair_boot(char *report, size_t report_size) {
  uint32_t esp_sectors =
      (uint32_t)(INSTALL_DATA_START_LBA - INSTALL_ESP_START_LBA);
  char install_text[256];
  install_boot_payload_t payload;

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  append_report(report, report_size, "\033[1;36mOrizon boot repair\033[0m");

  if (!installed_gpt_layout_valid(report, report_size, 0)) {
    append_report(report, report_size,
                  "repair-boot: no installed Orizon GPT layout found.");
    append_report(report, report_size,
                  "repair-boot: run install first; repair will not repartition.");
    return -1;
  }
  if (!boot_payloads_ready()) {
    char line[160];
    snprintf(line, sizeof(line), "repair-boot: %s", boot_payload_status());
    append_report(report, report_size, line);
    return -2;
  }

  snprintf(install_text, sizeof(install_text),
           "Orizon OS boot repaired\nsource=repair-boot\n"
           "next=reboot-installed-disk\n");
  payload.kernel = boot_kernel_image();
  payload.kernel_size = boot_kernel_image_size();
  payload.efi = boot_efi_image();
  payload.efi_size = boot_efi_image_size();
  payload.limine_conf = install_limine_conf;
  payload.limine_conf_size = sizeof(install_limine_conf) - 1;
  payload.install_text = install_text;
  payload.install_text_size = strlen(install_text);
  payload.rollback_kernel = NULL;
  payload.rollback_kernel_size = 0;
  payload.rollback_efi = NULL;
  payload.rollback_efi_size = 0;

  append_report(report, report_size, "[1/3] Rewriting FAT32 ESP boot files");
  if (fat32_format_esp_payload(INSTALL_ESP_START_LBA, esp_sectors,
                               &payload) < 0) {
    append_report(report, report_size, "repair-boot: ESP write failed");
    return -3;
  }
  append_report(report, report_size, "[2/3] Preserved Orizon data partition");
  append_report(report, report_size, "[3/3] Verifying installed boot files");
  return boot_check_impl(report, report_size, 0);
}

static int orizon_install_dualboot_prepare(
    const orizon_install_config_t *config, char *report, size_t report_size) {
  gpt_partition_t esp;
  fat32_volume_t vol;
  install_boot_payload_t payload;
  char install_text[512];
  char line[192];

  append_report(report, report_size,
                "\033[1;36mOrizon dual boot ESP prepare\033[0m");
  append_report(report, report_size,
                "Mode: non-destructive; existing partitions are preserved.");
  append_report(report, report_size,
                "Scope: writes only /EFI/Orizon on the existing ESP.");

  if (!boot_payloads_ready()) {
    snprintf(line, sizeof(line), "dualboot: %s", boot_payload_status());
    append_report(report, report_size, line);
    return -1;
  }
  snprintf(line, sizeof(line), "Payloads: kernel=%lu bytes, BOOTX64.EFI=%lu bytes",
           (unsigned long)boot_kernel_image_size(),
           (unsigned long)boot_efi_image_size());
  append_report(report, report_size, line);

  if (gpt_find_esp(&esp, report, report_size) < 0 ||
      fat32_mount_existing_esp(&vol, &esp, report, report_size) < 0) {
    append_report(report, report_size,
                  "dualboot: no compatible existing FAT32 ESP found");
    return -2;
  }

  snprintf(install_text, sizeof(install_text),
           "Orizon OS dual boot files\nlanguage=%s\nkeyboard=%s\nhostname=%s\n"
           "mode=dual-boot-esp\n"
           "boot-file=/EFI/Orizon/BOOTX64.EFI\n"
           "data=not-installed\n"
           "next=firmware-boot-file-or-boot-entry\n",
           config->language, config->keyboard, config->hostname);
  payload.kernel = boot_kernel_image();
  payload.kernel_size = boot_kernel_image_size();
  payload.efi = boot_efi_image();
  payload.efi_size = boot_efi_image_size();
  payload.limine_conf = dualboot_limine_conf;
  payload.limine_conf_size = sizeof(dualboot_limine_conf) - 1;
  payload.install_text = install_text;
  payload.install_text_size = strlen(install_text);
  payload.rollback_kernel = NULL;
  payload.rollback_kernel_size = 0;
  payload.rollback_efi = NULL;
  payload.rollback_efi_size = 0;

  append_report(report, report_size, "[1/3] Writing side-by-side ESP files");
  if (dualboot_write_side_by_side_esp(&vol, &payload, report, report_size) < 0) {
    return -3;
  }
  append_report(report, report_size, "[2/3] Preserved existing partitions");
  append_report(report, report_size, "[3/3] Verifying side-by-side boot files");
  if (dualboot_check_impl(report, report_size, 0) < 0) {
    append_report(report, report_size, "dualboot: verification failed");
    return -4;
  }
  append_report(report, report_size,
                "Dual boot prepare complete. Boot file: /EFI/Orizon/BOOTX64.EFI");
  append_report(report, report_size,
                "No Orizon data partition was created; update/pkg stay disabled.");
  return 0;
}

int orizon_install_run(const orizon_install_config_t *config, char *report,
                       size_t report_size) {
  uint64_t sectors;
  uint32_t esp_sectors = (uint32_t)(INSTALL_DATA_START_LBA -
                                    INSTALL_ESP_START_LBA);
  char line[192];
  char capacity[80];

  if (report && report_size > 0) {
    report[0] = '\0';
  }
  append_report(report, report_size, "\033[1;36mOrizon disk install\033[0m");

  if (!config || !config->language || !config->keyboard ||
      !config->hostname) {
    append_report(report, report_size, "install: invalid configuration");
    return -1;
  }
  if (config->disk_index < 0 ||
      storage_select_device(config->disk_index) < 0) {
    append_report(report, report_size, "install: target disk not selectable");
    return -1;
  }
  if (!storage_available()) {
    append_report(report, report_size, "install: no writable AHCI/NVMe disk");
    return -2;
  }
  storage_format_capacity(capacity, sizeof(capacity));
  snprintf(line, sizeof(line), "Target disk: %s, %s",
           config->disk_name ? config->disk_name : storage_status(), capacity);
  append_report(report, report_size, line);

  if (strcmp(config->disk_mode, "dual-boot-esp") == 0) {
    return orizon_install_dualboot_prepare(config, report, report_size);
  }

  if (!boot_payloads_ready()) {
    snprintf(line, sizeof(line), "install: %s", boot_payload_status());
    append_report(report, report_size, line);
    return -3;
  }
  snprintf(line, sizeof(line), "Payloads: kernel=%lu bytes, BOOTX64.EFI=%lu bytes",
           (unsigned long)boot_kernel_image_size(),
           (unsigned long)boot_efi_image_size());
  append_report(report, report_size, line);

  sectors = storage_sector_count();
  if (sectors < INSTALL_DATA_START_LBA + 65536) {
    append_report(report, report_size, "install: disk is too small");
    return -4;
  }

  append_report(report, report_size, "[1/6] Writing GPT layout");
  if (install_write_gpt(sectors) < 0) {
    append_report(report, report_size, "install: GPT write failed");
    return -5;
  }

  append_report(report, report_size, "[2/6] Formatting ESP as FAT32");
  if (fat32_format_esp(INSTALL_ESP_START_LBA, esp_sectors, config) < 0) {
    append_report(report, report_size, "install: ESP/FAT32 write failed");
    return -6;
  }

  append_report(report, report_size, "[3/6] Installing UEFI fallback loader");
  append_report(report, report_size, "[4/6] Installing kernel and Limine config");
  append_report(report, report_size, "[5/6] Verifying installed boot files");
  if (boot_check_impl(report, report_size, 0) < 0) {
    append_report(report, report_size,
                  "install: boot verification failed before final marker");
    return -7;
  }
  append_report(report, report_size, "[6/6] Preserving Orizon data partition");
  append_report(report, report_size,
                "Install complete: shutdown will start so installer media can be removed.");
  return 0;
}

static int orizon_install_update_esp_payload(
    const void *kernel, size_t kernel_size, const void *efi, size_t efi_size,
    const void *rollback_kernel, size_t rollback_kernel_size,
    const void *rollback_efi, size_t rollback_efi_size,
    const char *limine_conf, size_t limine_conf_size,
    const char *update_text, size_t update_text_size, char *report,
    size_t report_size) {
  uint64_t sectors;
  uint32_t esp_sectors =
      (uint32_t)(INSTALL_DATA_START_LBA - INSTALL_ESP_START_LBA);
  install_boot_payload_t payload;
  char line[160];
  int has_rollback =
      rollback_kernel && rollback_efi && rollback_kernel_size > 0 &&
      rollback_efi_size > 0;

  if (!kernel || kernel_size == 0 || !efi || efi_size == 0 ||
      !limine_conf || limine_conf_size == 0 || !update_text ||
      update_text_size == 0) {
    append_report(report, report_size, "update: invalid boot payload");
    return -1;
  }
  if ((rollback_kernel || rollback_efi || rollback_kernel_size ||
       rollback_efi_size) &&
      !has_rollback) {
    append_report(report, report_size, "update: invalid rollback payload");
    return -1;
  }
  if (!storage_available()) {
    append_report(report, report_size, "update: no writable AHCI/NVMe disk");
    return -2;
  }
  sectors = storage_sector_count();
  if (sectors < INSTALL_DATA_START_LBA + 65536) {
    append_report(report, report_size, "update: disk is too small");
    return -3;
  }

  snprintf(line, sizeof(line), "Payloads: kernel=%lu bytes, BOOTX64.EFI=%lu bytes",
           (unsigned long)kernel_size, (unsigned long)efi_size);
  append_report(report, report_size, line);
  append_report(report, report_size, "[1/4] Formatting installed ESP");

  payload.kernel = kernel;
  payload.kernel_size = kernel_size;
  payload.efi = efi;
  payload.efi_size = efi_size;
  payload.limine_conf = limine_conf;
  payload.limine_conf_size = limine_conf_size;
  payload.install_text = update_text;
  payload.install_text_size = update_text_size;
  payload.rollback_kernel = rollback_kernel;
  payload.rollback_kernel_size = rollback_kernel_size;
  payload.rollback_efi = rollback_efi;
  payload.rollback_efi_size = rollback_efi_size;

  if (fat32_format_esp_payload(INSTALL_ESP_START_LBA, esp_sectors,
                               &payload) < 0) {
    append_report(report, report_size, "update: ESP/FAT32 write failed");
    return -4;
  }

  append_report(report, report_size, "[2/4] Installed updated boot files");
  if (has_rollback) {
    append_report(report, report_size,
                  "Rollback slot: /boot/KROLLBK.ELF and /EFI/BOOT/BOOTX64.ROL");
  }
  append_report(report, report_size, "[3/4] Verifying installed boot files");
  if (boot_check_impl(report, report_size, 0) < 0) {
    append_report(report, report_size, "update: boot verification failed");
    return -5;
  }
  append_report(report, report_size, "[4/4] Preserved Orizon data partition");
  return 0;
}

int orizon_install_update_esp(const void *kernel, size_t kernel_size,
                              const void *efi, size_t efi_size,
                              const char *limine_conf,
                              size_t limine_conf_size,
                              const char *update_text,
                              size_t update_text_size, char *report,
                              size_t report_size) {
  return orizon_install_update_esp_payload(
      kernel, kernel_size, efi, efi_size, NULL, 0, NULL, 0, limine_conf,
      limine_conf_size, update_text, update_text_size, report, report_size);
}

int orizon_install_update_esp_with_rollback(
    const void *kernel, size_t kernel_size, const void *efi, size_t efi_size,
    const void *rollback_kernel, size_t rollback_kernel_size,
    const void *rollback_efi, size_t rollback_efi_size,
    const char *limine_conf, size_t limine_conf_size,
    const char *update_text, size_t update_text_size, char *report,
    size_t report_size) {
  return orizon_install_update_esp_payload(
      kernel, kernel_size, efi, efi_size, rollback_kernel,
      rollback_kernel_size, rollback_efi, rollback_efi_size, limine_conf,
      limine_conf_size, update_text, update_text_size, report, report_size);
}
