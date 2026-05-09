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
    "default_entry: 1\n"
    "\n"
    "/Orizon OS\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    module_path: boot():/EFI/BOOT/BOOTX64.EFI\n"
    "    module_cmdline: orizon-bootx64\n";

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

static void fat_dir_add_dot(uint8_t *dir, size_t *off, uint32_t self,
                            uint32_t parent) {
  fat_dir_add_entry(dir, off, ".          ", 0x10, self, 0);
  fat_dir_add_entry(dir, off, "..         ", 0x10, parent, 0);
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
  memset(sector_buf, 0, sizeof(sector_buf));
  put32(sector_buf, 0x41615252U);
  put32(sector_buf + 484, 0x61417272U);
  put32(sector_buf + 488, 0xFFFFFFFFU);
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
  fat_dir_add_dot(cluster_buf, &off, efi_dir, vol->root_cluster);
  fat_dir_add_entry(cluster_buf, &off, "BOOT       ", 0x10, efi_boot_dir, 0);
  if (fat_write_cluster(vol, efi_dir, cluster_buf) < 0) {
    return -1;
  }

  memset(cluster_buf, 0, sizeof(cluster_buf));
  off = 0;
  fat_dir_add_dot(cluster_buf, &off, efi_boot_dir, efi_dir);
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
  fat_dir_add_dot(cluster_buf, &off, boot_dir, vol->root_cluster);
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
  fat_prepare_fsinfo(&vol);
  if (write_sector(start_lba + 1, sector_buf) < 0 ||
      write_sector(start_lba + 7, sector_buf) < 0) {
    return -1;
  }
  for (uint32_t i = 2; i < vol.reserved_sectors; i++) {
    if (i == 6 || i == 7) {
      continue;
    }
    if (zero_sector(start_lba + i) < 0) {
      return -1;
    }
  }

  return fat32_write_boot_files(&vol, payload);
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
  if (!storage_available()) {
    append_report(report, report_size, "install: no writable AHCI/NVMe disk");
    return -2;
  }
  storage_format_capacity(capacity, sizeof(capacity));
  snprintf(line, sizeof(line), "Disk: %s, %s", storage_status(), capacity);
  append_report(report, report_size, line);

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

  append_report(report, report_size, "[1/5] Writing GPT layout");
  if (install_write_gpt(sectors) < 0) {
    append_report(report, report_size, "install: GPT write failed");
    return -5;
  }

  append_report(report, report_size, "[2/5] Formatting ESP as FAT32");
  if (fat32_format_esp(INSTALL_ESP_START_LBA, esp_sectors, config) < 0) {
    append_report(report, report_size, "install: ESP/FAT32 write failed");
    return -6;
  }

  append_report(report, report_size, "[3/5] Installing UEFI fallback loader");
  append_report(report, report_size, "[4/5] Installing kernel and Limine config");
  append_report(report, report_size, "[5/5] Preserving Orizon data partition");
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
  append_report(report, report_size, "[1/3] Formatting installed ESP");

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

  append_report(report, report_size, "[2/3] Installed updated boot files");
  if (has_rollback) {
    append_report(report, report_size,
                  "Rollback slot: /boot/KROLLBK.ELF and /EFI/BOOT/BOOTX64.ROL");
  }
  append_report(report, report_size, "[3/3] Preserved Orizon data partition");
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
