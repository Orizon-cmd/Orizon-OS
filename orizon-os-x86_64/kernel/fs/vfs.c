/*
 * Orizon OS x86_64 - Simple RAM-based Virtual Filesystem
 */

#include "../include/vfs.h"
#include "../include/kmalloc.h"
#include "../include/storage.h"
#include "../include/string.h"

#define PERSIST_MAGIC "ORZPFS1"
#define PERSIST_VERSION 2U
#define PERSIST_LEGACY_VERSION 1U
#define PERSIST_BYTES (1024U * 1024U)
#define PERSIST_SECTORS (PERSIST_BYTES / ORIZON_SECTOR_SIZE)
#define PERSIST_SLOT_COUNT 2U
#define PERSIST_HEADER_SIZE ORIZON_SECTOR_SIZE
/*
 * Keep persistence boot I/O deliberately small while the AHCI path still uses a
 * single PRDT entry. Large early reads can reset QEMU/OVMF instead of failing
 * cleanly, which looks like a black screen followed by a Limine reboot loop.
 */
#define PERSIST_IO_MAX_SECTORS 1U
#define PERSIST_SEQUENCE_OFFSET 24U

/* Inode structure */
typedef struct inode {
  char name[MAX_NAME];
  char path[MAX_PATH];
  int type;           /* 0 = file, 1 = directory */
  uint8_t *data;
  size_t size;
  size_t capacity;
  int parent;         /* Parent inode index, -1 for root */
} inode_t;

typedef struct persist_snapshot_meta {
  int valid;
  uint32_t version;
  uint32_t entry_count;
  uint32_t payload_size;
  uint32_t checksum;
  uint32_t sequence;
  uint32_t slot;
} persist_snapshot_meta_t;

/* Filesystem state */
static inode_t inodes[MAX_FILES];
static int inode_count = 0;
static file_t open_files[MAX_OPEN];
static int vfs_initialized = 0;
static int persist_ready = 0;
static int persist_loading = 0;
static const char *persist_status = "Orizon data persistence not loaded";
static uint64_t persist_lba = ORIZON_PERSIST_LBA;
static uint64_t persist_data_sectors = 0;
static uint32_t persist_slots_available = 0;
static uint32_t persist_sequence = 0;
static uint32_t persist_last_entry_count = 0;
static uint32_t persist_last_payload_size = 0;
static uint32_t persist_last_checksum = 0;
static uint32_t persist_last_version = 0;
static int persist_active_slot = -1;
static uint8_t persist_buf[PERSIST_BYTES] __attribute__((aligned(4096)));
static uint8_t persist_sector[ORIZON_SECTOR_SIZE] __attribute__((aligned(4096)));
static char persist_status_buf[192];

static int create_inode(const char *path, int type);
static void persist_set_status(const char *status);
static int persist_storage_read(uint64_t lba, void *buf, uint32_t sectors);
static int persist_read_slot_snapshot(uint32_t slot,
                                      persist_snapshot_meta_t *meta);

/* String helpers */
static int str_eq(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b) return 0;
    a++; b++;
  }
  return *a == *b;
}

static void str_cpy(char *dst, const char *src, int max) {
  int i = 0;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/* Find inode by path */
static int find_inode(const char *path) {
  for (int i = 0; i < inode_count; i++) {
    if (str_eq(inodes[i].path, path)) {
      return i;
    }
  }
  return -1;
}

/* Get parent path */
static void get_parent_path(const char *path, char *parent) {
  str_cpy(parent, path, MAX_PATH);
  int len = strlen(parent);
  
  /* Remove trailing slash */
  if (len > 1 && parent[len-1] == '/') {
    parent[--len] = '\0';
  }
  
  /* Find last slash */
  int last_slash = -1;
  for (int i = 0; i < len; i++) {
    if (parent[i] == '/') last_slash = i;
  }
  
  if (last_slash <= 0) {
    parent[0] = '/';
    parent[1] = '\0';
  } else {
    parent[last_slash] = '\0';
  }
}

/* Get filename from path */
static void get_filename(const char *path, char *name) {
  int len = strlen(path);
  int last_slash = -1;
  
  for (int i = 0; i < len; i++) {
    if (path[i] == '/') last_slash = i;
  }
  
  if (last_slash < 0) {
    str_cpy(name, path, MAX_NAME);
  } else {
    str_cpy(name, path + last_slash + 1, MAX_NAME);
  }
}

static int path_is_inside(const char *path, const char *prefix) {
  int len = strlen(prefix);
  return strncmp(path, prefix, (size_t)len) == 0 &&
         (path[len] == '\0' || path[len] == '/');
}

static int path_should_persist(const char *path) {
  return path &&
         (path_is_inside(path, "/workspace") ||
          path_is_inside(path, "/home") ||
          path_is_inside(path, "/system") ||
          path_is_inside(path, "/packages") ||
          path_is_inside(path, "/logs"));
}

static int path_is_persistent_root(const char *path) {
  return path && (str_eq(path, "/workspace") || str_eq(path, "/home") ||
                  str_eq(path, "/system") || str_eq(path, "/packages") ||
                  str_eq(path, "/logs"));
}

static uint32_t persist_get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t persist_get_u64(const uint8_t *p) {
  return (uint64_t)persist_get_u32(p) |
         ((uint64_t)persist_get_u32(p + 4) << 32);
}

static int persist_gpt_name_is_orizon_data(const uint8_t *entry) {
  static const char name[] = "orizon-data";

  if (!entry) {
    return 0;
  }
  for (size_t i = 0; i < sizeof(name) - 1; i++) {
    uint8_t ch = entry[56 + i * 2];
    uint8_t hi = entry[56 + i * 2 + 1];
    if (ch >= 'A' && ch <= 'Z') {
      ch = (uint8_t)(ch - 'A' + 'a');
    }
    if (hi != 0 || ch != (uint8_t)name[i]) {
      return 0;
    }
  }
  return entry[56 + (sizeof(name) - 1) * 2] == 0 &&
         entry[56 + (sizeof(name) - 1) * 2 + 1] == 0;
}

static int persist_orizon_data_partition_present(void) {
  static const uint8_t data_type[16] = {
      0x4f, 0x52, 0x5a, 0x44, 0x41, 0x54, 0x41, 0x00,
      0x9a, 0x3d, 0x20, 0x26, 0x05, 0x11, 0x00, 0x01};
  uint64_t entries_lba;
  uint32_t entry_count;
  uint32_t entry_size;
  uint32_t scan_count;

  persist_slots_available = 0;
  persist_data_sectors = 0;
  if (!storage_available()) {
    return 0;
  }
  if (storage_read(1, persist_sector, 1) < 0 ||
      memcmp(persist_sector, "EFI PART", 8) != 0) {
    return 0;
  }

  entries_lba = persist_get_u64(persist_sector + 72);
  entry_count = persist_get_u32(persist_sector + 80);
  entry_size = persist_get_u32(persist_sector + 84);
  if (entries_lba == 0 || entry_size != 128 || entry_count == 0) {
    return 0;
  }

  scan_count = entry_count < 128 ? entry_count : 128;
  for (uint32_t i = 0; i < scan_count; i++) {
    uint64_t lba = entries_lba + ((uint64_t)i * entry_size) / ORIZON_SECTOR_SIZE;
    uint32_t off = (uint32_t)(((uint64_t)i * entry_size) % ORIZON_SECTOR_SIZE);
    uint8_t *entry;

    if (storage_read(lba, persist_sector, 1) < 0) {
      return 0;
    }
    entry = persist_sector + off;
    if (memcmp(entry, data_type, sizeof(data_type)) == 0 ||
        persist_gpt_name_is_orizon_data(entry)) {
      uint64_t first_lba = persist_get_u64(entry + 32);
      uint64_t last_lba = persist_get_u64(entry + 40);
      if (first_lba >= 34 && last_lba >= first_lba) {
        uint64_t sectors = last_lba - first_lba + 1;
        if (sectors < PERSIST_SECTORS) {
          continue;
        }
        persist_lba = first_lba;
        persist_data_sectors = sectors;
        persist_slots_available =
            sectors >= (uint64_t)PERSIST_SECTORS * PERSIST_SLOT_COUNT
                ? PERSIST_SLOT_COUNT
                : 1U;
        return 1;
      }
    }
  }
  return 0;
}

static void persist_format_status_active(void) {
  char status[192];
  snprintf(status, sizeof(status),
           "Orizon data persistence active at LBA %lu slot=%d/%lu seq=%lu entries=%lu bytes=%lu",
           (unsigned long)persist_lba, persist_active_slot,
           (unsigned long)persist_slots_available,
           (unsigned long)persist_sequence,
           (unsigned long)persist_last_entry_count,
           (unsigned long)persist_last_payload_size);
  persist_set_status(status);
}

static void persist_format_status_initialized(void) {
  char status[160];
  snprintf(status, sizeof(status),
           "Orizon data persistence initialized at LBA %lu slots=%lu",
           (unsigned long)persist_lba,
           (unsigned long)persist_slots_available);
  persist_set_status(status);
}

static uint64_t persist_slot_lba(uint32_t slot) {
  return persist_lba + (uint64_t)slot * PERSIST_SECTORS;
}

static int append_path_component(char *path, size_t size, const char *component,
                                 size_t component_len) {
  size_t path_len = strlen(path);
  if (component_len == 0) {
    return 0;
  }
  if (path_len > 1) {
    if (path_len + 1 >= size) {
      return -1;
    }
    path[path_len++] = '/';
    path[path_len] = '\0';
  }
  if (path_len + component_len >= size) {
    return -1;
  }
  for (size_t i = 0; i < component_len; i++) {
    path[path_len + i] = component[i];
  }
  path[path_len + component_len] = '\0';
  return 0;
}

static void put_u16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16(const uint8_t *src) {
  return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t get_u32(const uint8_t *src) {
  return (uint32_t)src[0] |
         ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

static uint32_t persist_checksum(const uint8_t *buf, size_t size) {
  uint32_t sum = 0;
  for (size_t i = 0; i < size; i++) {
    sum = (sum << 5) | (sum >> 27);
    sum += buf[i];
  }
  return sum;
}

static int persist_snapshot_validate(const uint8_t *buf,
                                     persist_snapshot_meta_t *meta,
                                     uint32_t slot) {
  uint32_t version;
  uint32_t entry_count;
  uint32_t payload_size;
  uint32_t checksum;

  if (!buf || !meta) {
    return -1;
  }
  memset(meta, 0, sizeof(*meta));
  meta->slot = slot;

  if (memcmp(buf, PERSIST_MAGIC, 7) != 0) {
    return -1;
  }
  version = get_u32(buf + 8);
  if (version != PERSIST_VERSION && version != PERSIST_LEGACY_VERSION) {
    return -1;
  }

  entry_count = get_u32(buf + 12);
  payload_size = get_u32(buf + 16);
  checksum = get_u32(buf + 20);
  if (entry_count > MAX_FILES ||
      payload_size > PERSIST_BYTES - PERSIST_HEADER_SIZE) {
    return -1;
  }
  if (checksum != persist_checksum(buf + PERSIST_HEADER_SIZE, payload_size)) {
    return -1;
  }

  meta->valid = 1;
  meta->version = version;
  meta->entry_count = entry_count;
  meta->payload_size = payload_size;
  meta->checksum = checksum;
  meta->sequence =
      version >= PERSIST_VERSION ? get_u32(buf + PERSIST_SEQUENCE_OFFSET) : 0;
  return 0;
}

static int persist_snapshot_is_newer(const persist_snapshot_meta_t *candidate,
                                     const persist_snapshot_meta_t *best) {
  if (!candidate || !candidate->valid) {
    return 0;
  }
  if (!best || !best->valid) {
    return 1;
  }
  if (candidate->version != best->version) {
    return candidate->version > best->version;
  }
  if (candidate->sequence != best->sequence) {
    return candidate->sequence > best->sequence;
  }
  return candidate->slot < best->slot;
}

static void persist_append_text(char *out, size_t out_size, size_t *used,
                                const char *text) {
  size_t len;

  if (!out || !used || !text || *used >= out_size) {
    return;
  }
  len = strlen(text);
  if (*used + len >= out_size) {
    len = out_size - *used - 1;
  }
  if (len > 0) {
    memcpy(out + *used, text, len);
    *used += len;
  }
  out[*used] = '\0';
}

static int persist_read_slot_meta(uint32_t slot,
                                  persist_snapshot_meta_t *meta) {
  return persist_read_slot_snapshot(slot, meta);
}

static int persist_append_entry(size_t *offset, uint32_t *entry_count,
                                const inode_t *node) {
  size_t path_len = strlen(node->path);
  size_t data_size = node->type == 0 ? node->size : 0;
  size_t needed = 1 + 2 + 4 + path_len + data_size;

  if (path_len == 0 || path_len > 0xFFFF || data_size > 0xFFFFFFFFU) {
    return -1;
  }
  if (*offset + needed > PERSIST_BYTES) {
    return -1;
  }

  uint8_t *p = persist_buf + *offset;
  p[0] = (uint8_t)node->type;
  put_u16(p + 1, (uint16_t)path_len);
  put_u32(p + 3, (uint32_t)data_size);
  memcpy(p + 7, node->path, path_len);
  if (data_size > 0 && node->data) {
    memcpy(p + 7 + path_len, node->data, data_size);
  }

  *offset += needed;
  (*entry_count)++;
  return 0;
}

static void persist_set_status(const char *status) {
  if (!status) {
    status = "Orizon data persistence status unavailable";
  }
  snprintf(persist_status_buf, sizeof(persist_status_buf), "%s", status);
  persist_status = persist_status_buf;
}

static int persist_storage_read(uint64_t lba, void *buf, uint32_t sectors) {
  uint8_t *bytes = (uint8_t *)buf;
  uint32_t done = 0;

  while (done < sectors) {
    uint32_t chunk = sectors - done;
    if (chunk > PERSIST_IO_MAX_SECTORS) {
      chunk = PERSIST_IO_MAX_SECTORS;
    }
    if (storage_read(lba + done, bytes + (uint64_t)done * ORIZON_SECTOR_SIZE,
                     chunk) < 0) {
      return -1;
    }
    done += chunk;
  }
  return 0;
}

static uint32_t persist_snapshot_sector_count(uint32_t payload_size) {
  uint64_t bytes = (uint64_t)PERSIST_HEADER_SIZE + payload_size;
  uint64_t sectors = (bytes + ORIZON_SECTOR_SIZE - 1U) / ORIZON_SECTOR_SIZE;
  if (sectors == 0) {
    sectors = 1;
  }
  if (sectors > PERSIST_SECTORS) {
    sectors = PERSIST_SECTORS;
  }
  return (uint32_t)sectors;
}

static int persist_read_slot_snapshot(uint32_t slot,
                                      persist_snapshot_meta_t *meta) {
  uint64_t lba;
  uint32_t version;
  uint32_t entry_count;
  uint32_t payload_size;
  uint32_t sectors;

  if (!meta || slot >= persist_slots_available) {
    return -1;
  }
  memset(meta, 0, sizeof(*meta));
  meta->slot = slot;

  lba = persist_slot_lba(slot);
  if (persist_storage_read(lba, persist_buf, 1) < 0) {
    return -1;
  }
  if (memcmp(persist_buf, PERSIST_MAGIC, 7) != 0) {
    return -2;
  }

  version = get_u32(persist_buf + 8);
  entry_count = get_u32(persist_buf + 12);
  payload_size = get_u32(persist_buf + 16);
  if ((version != PERSIST_VERSION && version != PERSIST_LEGACY_VERSION) ||
      entry_count > MAX_FILES ||
      payload_size > PERSIST_BYTES - PERSIST_HEADER_SIZE) {
    return -2;
  }

  sectors = persist_snapshot_sector_count(payload_size);
  if (sectors > 1 &&
      persist_storage_read(lba + 1, persist_buf + ORIZON_SECTOR_SIZE,
                           sectors - 1) < 0) {
    return -1;
  }
  return persist_snapshot_validate(persist_buf, meta, slot) == 0 ? 0 : -2;
}

static int persist_storage_write(uint64_t lba, const void *buf,
                                 uint32_t sectors) {
  const uint8_t *bytes = (const uint8_t *)buf;
  uint32_t done = 0;

  while (done < sectors) {
    uint32_t chunk = sectors - done;
    if (chunk > PERSIST_IO_MAX_SECTORS) {
      chunk = PERSIST_IO_MAX_SECTORS;
    }
    if (storage_write(lba + done,
                      bytes + (uint64_t)done * ORIZON_SECTOR_SIZE,
                      chunk) < 0) {
      return -1;
    }
    done += chunk;
  }
  return 0;
}

static int find_child_inode(int parent) {
  for (int i = 0; i < inode_count; i++) {
    if (inodes[i].parent == parent) {
      return i;
    }
  }
  return -1;
}

static void delete_inode_index(int idx) {
  if (idx <= 0 || idx >= inode_count) {
    return;
  }

  while (1) {
    int child = find_child_inode(idx);
    if (child < 0) {
      break;
    }
    delete_inode_index(child);
  }

  if (inodes[idx].data) {
    kfree(inodes[idx].data);
  }

  if (idx < inode_count - 1) {
    inodes[idx] = inodes[inode_count - 1];
    for (int i = 0; i < inode_count - 1; i++) {
      if (inodes[i].parent == inode_count - 1) {
        inodes[i].parent = idx;
      }
    }
  }
  inode_count--;
}

static void clear_directory_contents(const char *path) {
  int root = find_inode(path);
  if (root < 0) {
    return;
  }

  while (1) {
    int child = find_child_inode(root);
    if (child < 0) {
      break;
    }
    delete_inode_index(child);
    root = find_inode(path);
    if (root < 0) {
      return;
    }
  }
}

static int ensure_parent_dirs(const char *path) {
  char cur[MAX_PATH];
  const char *p = path;

  if (!path || path[0] != '/') {
    return -EINVAL;
  }

  strcpy(cur, "/");
  p++;
  while (*p) {
    const char *start = p;
    size_t len = 0;

    while (p[len] && p[len] != '/') {
      len++;
    }

    if (p[len] == '\0') {
      break; /* final component */
    }

    if (append_path_component(cur, sizeof(cur), start, len) < 0) {
      return -ENAMETOOLONG;
    }
    if (find_inode(cur) < 0 && create_inode(cur, 1) < 0) {
      return -ENOENT;
    }

    p += len;
    while (*p == '/') {
      p++;
    }
  }

  return 0;
}

static void maybe_persist_path(const char *path) {
  if (persist_ready && !persist_loading && path_should_persist(path)) {
    vfs_persist_save();
  }
}

static void maybe_persist_inode(int idx) {
  if (idx >= 0 && idx < inode_count) {
    maybe_persist_path(inodes[idx].path);
  }
}

/* Create inode */
static int create_inode(const char *path, int type) {
  if (inode_count >= MAX_FILES) return -ENOSPC;
  if (find_inode(path) >= 0) return -EEXIST;

  char parent_path[MAX_PATH];
  get_parent_path(path, parent_path);
  int parent = find_inode(parent_path);
  if (!str_eq(path, "/") && parent < 0) {
    return -ENOENT;
  }
  
  int idx = inode_count++;
  inode_t *node = &inodes[idx];
  
  str_cpy(node->path, path, MAX_PATH);
  get_filename(path, node->name);
  node->type = type;
  node->data = NULL;
  node->size = 0;
  node->capacity = 0;
  
  node->parent = parent;
  
  return idx;
}

/* Initialize VFS */
void vfs_init(void) {
  if (vfs_initialized) return;
  
  kmalloc_init();
  
  /* Clear state */
  memset(inodes, 0, sizeof(inodes));
  memset(open_files, 0, sizeof(open_files));
  inode_count = 0;
  
  /* Create root directory */
  create_inode("/", 1);
  
  vfs_initialized = 1;
}

static void vfs_ensure_data_roots(void) {
  vfs_mkdir("/workspace");
  vfs_mkdir("/system");
  vfs_mkdir("/system/share");
  vfs_mkdir("/system/firmware");
  vfs_mkdir("/home");
  vfs_mkdir("/home/orizon");
  vfs_mkdir("/packages");
  vfs_mkdir("/logs");
  vfs_mkdir("/tmp");
}

static void vfs_write_default_file(const char *path, const char *text) {
  if (vfs_exists(path)) {
    return;
  }
  file_t *f = vfs_open(path, O_CREAT | O_WRONLY);
  if (f) {
    vfs_write(f, text, strlen(text));
    vfs_close(f);
  }
}

/* Seed initial filesystem content */
void vfs_seed_content(void) {
  /* Create a small, stable workspace for iterative development. */
  vfs_ensure_data_roots();

  file_t *f;

  f = vfs_open("/workspace/README.txt", O_CREAT | O_WRONLY);
  if (f) {
    const char *txt =
        "Orizon OS\n"
        "\n"
        "This x86_64 target is a clean development base.\n"
        "Use /workspace for experiments, notes and small tests.\n";
    vfs_write(f, txt, strlen(txt));
    vfs_close(f);
  }

  f = vfs_open("/workspace/ROADMAP.txt", O_CREAT | O_WRONLY);
  if (f) {
    const char *txt =
        "Next ideas:\n"
        "- grow the scheduler and memory layers\n"
        "- harden drivers one by one\n"
        "- add only the tools you really want to own\n";
    vfs_write(f, txt, strlen(txt));
    vfs_close(f);
  }

  f = vfs_open("/system/hostname", O_CREAT | O_WRONLY);
  if (f) {
    vfs_write(f, "orizon-os", 9);
    vfs_close(f);
  }

  f = vfs_open("/system/version", O_CREAT | O_WRONLY);
  if (f) {
    vfs_write(f, "core-x86_64", 11);
    vfs_close(f);
  }

  f = vfs_open("/system/profile", O_CREAT | O_WRONLY);
  if (f) {
    const char *txt = "minimal-development\n";
    vfs_write(f, txt, strlen(txt));
    vfs_close(f);
  }

  vfs_write_default_file(
      "/system/data-layout",
      "version 1\nroots /system /home /packages /logs /workspace\n");
  vfs_write_default_file("/system/network.conf", "mode dhcp\n");
  vfs_write_default_file("/home/orizon/README.txt",
                         "Home directory for Orizon OS user files.\n");
  vfs_write_default_file(
      "/packages/README.txt",
      "Local package cache and installed package metadata.\n");
  vfs_write_default_file("/logs/README.txt",
                         "Persistent boot, install and update logs.\n");
}

static void persist_write_default_data_files(void) {
  vfs_ensure_data_roots();
  vfs_write_default_file(
      "/system/data-layout",
      "version 1\nroots /system /home /packages /logs /workspace\n");
  vfs_mkdir("/system/firmware");
  vfs_write_default_file("/system/hostname", "orizon-os");
  vfs_write_default_file("/system/version", "core-x86_64");
  vfs_write_default_file("/system/profile", "minimal-development\n");
  vfs_write_default_file("/system/network.conf", "mode dhcp\n");
  vfs_write_default_file("/home/orizon/README.txt",
                         "Home directory for Orizon OS user files.\n");
  vfs_write_default_file(
      "/packages/README.txt",
      "Local package cache and installed package metadata.\n");
  vfs_write_default_file("/logs/README.txt",
                         "Persistent boot, install and update logs.\n");
}

static void persist_set_loaded_meta(const persist_snapshot_meta_t *meta) {
  if (!meta || !meta->valid) {
    return;
  }
  persist_active_slot = (int)meta->slot;
  persist_sequence = meta->sequence;
  persist_last_entry_count = meta->entry_count;
  persist_last_payload_size = meta->payload_size;
  persist_last_checksum = meta->checksum;
  persist_last_version = meta->version;
}

static int persist_apply_snapshot_entries(const persist_snapshot_meta_t *meta) {
  int malformed = 0;

  if (!meta || !meta->valid) {
    return -EINVAL;
  }

  clear_directory_contents("/workspace");
  clear_directory_contents("/home");
  clear_directory_contents("/system");
  clear_directory_contents("/packages");
  clear_directory_contents("/logs");

  size_t offset = PERSIST_HEADER_SIZE;
  for (uint32_t entry = 0; entry < meta->entry_count; entry++) {
    if (offset + 7 > PERSIST_HEADER_SIZE + meta->payload_size) {
      malformed = 1;
      break;
    }

    int type = persist_buf[offset];
    uint16_t path_len = get_u16(persist_buf + offset + 1);
    uint32_t data_size = get_u32(persist_buf + offset + 3);
    offset += 7;

    if (path_len == 0 || path_len >= MAX_PATH ||
        offset + path_len + data_size >
            PERSIST_HEADER_SIZE + meta->payload_size) {
      malformed = 1;
      break;
    }

    char path[MAX_PATH];
    memcpy(path, persist_buf + offset, path_len);
    path[path_len] = '\0';
    offset += path_len;

    if (!path_should_persist(path) || path_is_persistent_root(path)) {
      offset += data_size;
      continue;
    }

    ensure_parent_dirs(path);

    if (type == 1) {
      if (find_inode(path) < 0) {
        create_inode(path, 1);
      }
    } else if (type == 0) {
      file_t *f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
      if (f) {
        vfs_write(f, persist_buf + offset, data_size);
        vfs_close(f);
      }
    } else {
      malformed = 1;
    }
    offset += data_size;
  }

  persist_write_default_data_files();
  return malformed ? -EINVAL : 0;
}

static void persist_prepare_metadata_for_save(void) {
  persist_snapshot_meta_t best;
  persist_snapshot_meta_t meta;
  uint32_t slots;

  memset(&best, 0, sizeof(best));
  slots = persist_slots_available;
  if (slots == 0 || slots > PERSIST_SLOT_COUNT) {
    slots = 1;
  }
  for (uint32_t slot = 0; slot < slots; slot++) {
    if (persist_read_slot_snapshot(slot, &meta) == 0 &&
        persist_snapshot_is_newer(&meta, &best)) {
      best = meta;
    }
  }
  if (best.valid) {
    persist_set_loaded_meta(&best);
    persist_set_status(
        "Orizon data persistence prepared for explicit save from latest slot");
  } else {
    persist_active_slot = -1;
    persist_sequence = 0;
    persist_last_entry_count = 0;
    persist_last_payload_size = 0;
    persist_last_checksum = 0;
    persist_last_version = 0;
    persist_format_status_initialized();
  }
}

int vfs_persist_save(void) {
  uint32_t target_slot;
  uint32_t next_sequence;
  if (persist_loading) {
    return -EINVAL;
  }
  if (!storage_available()) {
    persist_set_status("Orizon data persistence unavailable");
    return -EIO;
  }
  if (!persist_orizon_data_partition_present()) {
    persist_ready = 0;
    persist_set_status("Orizon data persistence disabled: no Orizon data partition");
    return -EIO;
  }
  if (!persist_ready) {
    persist_ready = 1;
    persist_prepare_metadata_for_save();
  }

  target_slot = 0;
  if (persist_slots_available > 1U && persist_active_slot >= 0) {
    target_slot = (uint32_t)((persist_active_slot + 1) %
                             (int)persist_slots_available);
  }
  next_sequence = persist_sequence + 1U;
  if (next_sequence == 0) {
    next_sequence = 1U;
  }

  memset(persist_buf, 0, sizeof(persist_buf));
  memcpy(persist_buf, PERSIST_MAGIC, 7);
  put_u32(persist_buf + 8, PERSIST_VERSION);
  put_u32(persist_buf + PERSIST_SEQUENCE_OFFSET, next_sequence);

  size_t offset = PERSIST_HEADER_SIZE;
  uint32_t entry_count = 0;

  /* Directories first so loading can recreate parents before files. */
  for (int i = 0; i < inode_count; i++) {
    if (inodes[i].type == 1 && path_should_persist(inodes[i].path) &&
        !path_is_persistent_root(inodes[i].path)) {
      if (persist_append_entry(&offset, &entry_count, &inodes[i]) < 0) {
        persist_set_status("Orizon data persistence full");
        return -ENOSPC;
      }
    }
  }
  for (int i = 0; i < inode_count; i++) {
    if (inodes[i].type == 0 && path_should_persist(inodes[i].path)) {
      if (persist_append_entry(&offset, &entry_count, &inodes[i]) < 0) {
        persist_set_status("Orizon data persistence full");
        return -ENOSPC;
      }
    }
  }

  uint32_t payload_size = (uint32_t)(offset - PERSIST_HEADER_SIZE);
  uint32_t checksum = persist_checksum(persist_buf + PERSIST_HEADER_SIZE,
                                       payload_size);
  put_u32(persist_buf + 12, entry_count);
  put_u32(persist_buf + 16, payload_size);
  put_u32(persist_buf + 20, checksum);

  uint32_t sectors = (uint32_t)((offset + ORIZON_SECTOR_SIZE - 1) /
                                ORIZON_SECTOR_SIZE);
  if (sectors == 0) {
    sectors = 1;
  }
  if (persist_storage_write(persist_slot_lba(target_slot), persist_buf,
                            sectors) < 0) {
    persist_set_status("Orizon data persistence write failed");
    return -EIO;
  }

  persist_active_slot = (int)target_slot;
  persist_sequence = next_sequence;
  persist_last_entry_count = entry_count;
  persist_last_payload_size = payload_size;
  persist_last_checksum = checksum;
  persist_last_version = PERSIST_VERSION;
  persist_format_status_active();
  return 0;
}

void vfs_persist_load(void) {
  persist_snapshot_meta_t best;
  persist_snapshot_meta_t meta;
  uint32_t slots;
  int read_error = 0;
  int loaded_legacy = 0;

  if (!vfs_initialized) {
    vfs_init();
  }

  if (!storage_available()) {
    persist_ready = 0;
    persist_set_status("Orizon data persistence unavailable");
    return;
  }
  if (!persist_orizon_data_partition_present()) {
    persist_ready = 0;
    persist_set_status("Orizon data persistence disabled: no Orizon data partition");
    return;
  }

  persist_loading = 1;
  persist_ready = 1;
  memset(&best, 0, sizeof(best));

  slots = persist_slots_available;
  if (slots == 0 || slots > PERSIST_SLOT_COUNT) {
    slots = 1;
  }
  for (uint32_t slot = 0; slot < slots; slot++) {
    int slot_status = persist_read_slot_snapshot(slot, &meta);
    if (slot_status < 0) {
      if (slot_status == -1) {
        read_error = 1;
      }
      continue;
    }
    if (persist_snapshot_is_newer(&meta, &best)) {
      best = meta;
    }
  }

  if (!best.valid) {
    persist_active_slot = -1;
    persist_sequence = 0;
    persist_last_entry_count = 0;
    persist_last_payload_size = 0;
    persist_last_checksum = 0;
    persist_last_version = 0;
    persist_loading = 0;
    if (read_error) {
      persist_set_status(
          "Orizon data persistence read failed; initialized fresh snapshot");
    } else {
      persist_format_status_initialized();
    }
    vfs_persist_save();
    return;
  }

  if (persist_read_slot_snapshot(best.slot, &meta) < 0) {
    persist_loading = 0;
    persist_set_status("Orizon data persistence selected snapshot reread failed");
    return;
  }
  best = meta;
  persist_set_loaded_meta(&best);
  loaded_legacy = best.version == PERSIST_LEGACY_VERSION;

  if (persist_apply_snapshot_entries(&best) < 0) {
    persist_set_status(
        "Orizon data persistence snapshot restored with malformed entries skipped");
  }
  persist_loading = 0;
  persist_format_status_active();
  if (loaded_legacy) {
    vfs_persist_save();
  }
}

int vfs_persist_enable_installed(void) {
  if (!vfs_initialized) {
    vfs_init();
  }
  if (!storage_available()) {
    persist_ready = 0;
    persist_set_status("Orizon data persistence unavailable");
    return -EIO;
  }
  if (!persist_orizon_data_partition_present()) {
    persist_ready = 0;
    persist_set_status("Orizon data persistence disabled: no Orizon data partition");
    return -EIO;
  }
  persist_loading = 0;
  persist_ready = 1;
  persist_active_slot = -1;
  persist_format_status_initialized();
  return 0;
}

int vfs_persist_available(void) {
  return persist_ready && storage_available();
}

const char *vfs_persist_status(void) {
  return persist_status;
}

void vfs_persist_format_status(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "persistence:\n"
           "  ready=%s storage=%s loading=%s\n"
           "  lba=%lu data-sectors=%lu slot-size=%lu slots=%lu active-slot=%d\n"
           "  version=%lu sequence=%lu entries=%lu payload-bytes=%lu checksum=0x%08x\n"
           "  roots=/workspace,/home,/system,/packages,/logs\n"
           "  mode=%s\n"
           "  status=%s\n",
           persist_ready ? "yes" : "no",
           storage_available() ? "yes" : "no",
           persist_loading ? "yes" : "no",
           (unsigned long)persist_lba,
           (unsigned long)persist_data_sectors,
           (unsigned long)PERSIST_BYTES,
           (unsigned long)persist_slots_available,
           persist_active_slot,
           (unsigned long)persist_last_version,
           (unsigned long)persist_sequence,
           (unsigned long)persist_last_entry_count,
           (unsigned long)persist_last_payload_size,
           persist_last_checksum,
           vfs_persist_available() ? "persistent" : "memory",
           vfs_persist_status());
}

void vfs_persist_format_slots(char *out, size_t out_size) {
  size_t used = 0;
  char line[192];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (storage_available()) {
    persist_orizon_data_partition_present();
  }
  snprintf(line, sizeof(line),
           "persistence slots: lba=%lu slots=%lu active=%d\n",
           (unsigned long)persist_lba,
           (unsigned long)persist_slots_available,
           persist_active_slot);
  persist_append_text(out, out_size, &used, line);

  if (!storage_available()) {
    persist_append_text(out, out_size, &used,
                        "  storage unavailable; no slots readable\n");
    return;
  }
  if (persist_slots_available == 0) {
    persist_append_text(out, out_size, &used,
                        "  no Orizon data partition detected\n");
    return;
  }
  for (uint32_t slot = 0; slot < persist_slots_available; slot++) {
    persist_snapshot_meta_t meta;
    if (persist_read_slot_meta(slot, &meta) == 0) {
      snprintf(line, sizeof(line),
               "  slot %lu: valid=yes active=%s version=%lu seq=%lu entries=%lu payload=%lu checksum=0x%08x lba=%lu\n",
               (unsigned long)slot,
               (persist_active_slot == (int)slot) ? "yes" : "no",
               (unsigned long)meta.version,
               (unsigned long)meta.sequence,
               (unsigned long)meta.entry_count,
               (unsigned long)meta.payload_size,
               meta.checksum,
               (unsigned long)persist_slot_lba(slot));
    } else {
      snprintf(line, sizeof(line),
               "  slot %lu: valid=no active=%s lba=%lu\n",
               (unsigned long)slot,
               (persist_active_slot == (int)slot) ? "yes" : "no",
               (unsigned long)persist_slot_lba(slot));
    }
    persist_append_text(out, out_size, &used, line);
  }
  persist_append_text(out, out_size, &used,
                      "  restore: persist restore previous | persist restore slot <n>\n");
}

int vfs_persist_restore_slot(int slot, char *out, size_t out_size) {
  persist_snapshot_meta_t meta;
  int apply_rc;
  int save_rc;

  if (!vfs_initialized) {
    vfs_init();
  }
  if (!storage_available()) {
    persist_ready = 0;
    persist_set_status("Orizon data persistence unavailable");
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence restore: FAIL\nreason=storage unavailable\n");
    }
    return -EIO;
  }
  if (!persist_orizon_data_partition_present()) {
    persist_ready = 0;
    persist_set_status(
        "Orizon data persistence disabled: no Orizon data partition");
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence restore: FAIL\nreason=no Orizon data partition\n");
    }
    return -EIO;
  }
  if (slot < 0 || (uint32_t)slot >= persist_slots_available) {
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence restore: FAIL\nreason=slot out of range slots=%lu\n",
               (unsigned long)persist_slots_available);
    }
    return -EINVAL;
  }
  if (persist_read_slot_meta((uint32_t)slot, &meta) < 0) {
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence restore: FAIL\nreason=slot %d is not a valid snapshot\n",
               slot);
    }
    return -EIO;
  }

  persist_ready = 1;
  persist_loading = 1;
  persist_set_loaded_meta(&meta);
  apply_rc = persist_apply_snapshot_entries(&meta);
  persist_loading = 0;
  if (apply_rc < 0) {
    persist_set_status(
        "Orizon data persistence restore failed: malformed snapshot entries");
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence restore: FAIL\nreason=malformed snapshot entries slot=%d\n",
               slot);
    }
    return apply_rc;
  }

  save_rc = vfs_persist_save();
  if (out && out_size) {
    size_t used = 0;
    snprintf(out, out_size,
             "persistence restore: %s\nrestored-slot=%d restored-seq=%lu promoted=%s\n",
             save_rc == 0 ? "PASS" : "WARN", slot,
             (unsigned long)meta.sequence,
             save_rc == 0 ? "yes" : "no");
    used = strlen(out);
    if (used < out_size) {
      vfs_persist_format_status(out + used, out_size - used);
    }
  }
  return save_rc;
}

int vfs_persist_restore_previous(char *out, size_t out_size) {
  persist_snapshot_meta_t previous;

  if (!storage_available() || !persist_orizon_data_partition_present()) {
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence restore: FAIL\nreason=no readable Orizon data slots\n");
    }
    return -EIO;
  }
  memset(&previous, 0, sizeof(previous));
  for (uint32_t slot = 0; slot < persist_slots_available; slot++) {
    persist_snapshot_meta_t meta;
    if (persist_active_slot == (int)slot) {
      continue;
    }
    if (persist_read_slot_meta(slot, &meta) == 0 &&
        persist_snapshot_is_newer(&meta, &previous)) {
      previous = meta;
    }
  }
  if (!previous.valid) {
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence restore: FAIL\nreason=no previous valid snapshot\n");
    }
    return -ENOENT;
  }
  return vfs_persist_restore_slot((int)previous.slot, out, out_size);
}

int vfs_persist_repair(char *out, size_t out_size) {
  int rc;

  if (!vfs_initialized) {
    vfs_init();
  }
  if (!storage_available()) {
    persist_ready = 0;
    persist_set_status("Orizon data persistence unavailable");
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence repair: FAIL\nreason=storage unavailable\n");
    }
    return -EIO;
  }
  if (!persist_orizon_data_partition_present()) {
    persist_ready = 0;
    persist_set_status(
        "Orizon data persistence disabled: no Orizon data partition");
    if (out && out_size) {
      snprintf(out, out_size,
               "persistence repair: FAIL\nreason=no Orizon data partition\n");
    }
    return -EIO;
  }

  persist_ready = 1;
  persist_loading = 0;
  rc = vfs_persist_save();
  if (out && out_size) {
    size_t used = 0;
    snprintf(out, out_size, "persistence repair: %s\n",
             rc == 0 ? "PASS" : "FAIL");
    used = strlen(out);
    if (used < out_size) {
      vfs_persist_format_status(out + used, out_size - used);
    }
  }
  return rc;
}

/* Open file */
file_t *vfs_open(const char *path, int flags) {
  if (!vfs_initialized) vfs_init();
  
  /* Find free file handle */
  int fd = -1;
  for (int i = 0; i < MAX_OPEN; i++) {
    if (!open_files[i].valid) {
      fd = i;
      break;
    }
  }
  if (fd < 0) return NULL;
  
  /* Find or create inode */
  int idx = find_inode(path);
  
  if (idx < 0) {
    if (flags & O_CREAT) {
      idx = create_inode(path, 0); /* Create file */
      if (idx < 0) return NULL;
    } else {
      return NULL;
    }
  }
  
  /* Don't open directories as files */
  if (inodes[idx].type == 1) return NULL;
  
  /* Truncate if requested */
  if (flags & O_TRUNC) {
    if (inodes[idx].data) {
      kfree(inodes[idx].data);
      inodes[idx].data = NULL;
    }
    inodes[idx].size = 0;
    inodes[idx].capacity = 0;
  }
  
  /* Setup file handle */
  file_t *f = &open_files[fd];
  f->valid = 1;
  f->inode = idx;
  f->flags = flags;
  f->pos = (flags & O_APPEND) ? inodes[idx].size : 0;
  
  return f;
}

/* Close file */
void vfs_close(file_t *file) {
  if (file && file->valid) {
    int inode = file->inode;
    int flags = file->flags;
    file->valid = 0;
    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) {
      maybe_persist_inode(inode);
    }
  }
}

/* Read from file */
ssize_t vfs_read(file_t *file, void *buf, size_t count) {
  if (!file || !file->valid) return -EBADF;
  
  inode_t *node = &inodes[file->inode];
  if (node->type == 1) return -EISDIR;
  
  if (file->pos >= node->size) return 0;
  
  size_t available = node->size - file->pos;
  size_t to_read = (count < available) ? count : available;
  
  if (node->data) {
    memcpy(buf, node->data + file->pos, to_read);
  }
  
  file->pos += to_read;
  return to_read;
}

/* Write to file */
ssize_t vfs_write(file_t *file, const void *buf, size_t count) {
  if (!file || !file->valid) return -EBADF;
  if (count == 0) return 0;
  
  inode_t *node = &inodes[file->inode];
  if (node->type == 1) return -EISDIR;
  
  size_t new_size = file->pos + count;
  
  /* Grow buffer if needed */
  if (new_size > node->capacity) {
    size_t new_cap = (new_size + 4095) & ~4095; /* Round up to 4KB */
    uint8_t *new_data = kmalloc(new_cap);
    if (!new_data) return -ENOMEM;
    
    if (node->data) {
      memcpy(new_data, node->data, node->size);
      kfree(node->data);
    }
    
    node->data = new_data;
    node->capacity = new_cap;
  }
  
  memcpy(node->data + file->pos, buf, count);
  file->pos += count;
  
  if (file->pos > node->size) {
    node->size = file->pos;
  }
  
  return count;
}

/* Seek in file */
int vfs_seek(file_t *file, int offset, int whence) {
  if (!file || !file->valid) return -EBADF;
  
  inode_t *node = &inodes[file->inode];
  size_t new_pos;
  
  switch (whence) {
    case SEEK_SET: new_pos = offset; break;
    case SEEK_CUR: new_pos = file->pos + offset; break;
    case SEEK_END: new_pos = node->size + offset; break;
    default: return -EINVAL;
  }
  
  file->pos = new_pos;
  return new_pos;
}

/* Create directory */
int vfs_mkdir(const char *path) {
  if (!vfs_initialized) vfs_init();
  int result = create_inode(path, 1);
  if (result >= 0) {
    maybe_persist_path(path);
  }
  return result;
}

/* Read directory */
int vfs_readdir(const char *path, dirent_t *entries, int max_entries) {
  if (!vfs_initialized) vfs_init();
  
  int dir_idx = find_inode(path);
  if (dir_idx < 0) return -ENOENT;
  if (inodes[dir_idx].type != 1) return -ENOTDIR;
  
  int count = 0;
  
  for (int i = 0; i < inode_count && count < max_entries; i++) {
    if (inodes[i].parent == dir_idx) {
      str_cpy(entries[count].name, inodes[i].name, MAX_NAME);
      entries[count].type = inodes[i].type;
      entries[count].size = inodes[i].size;
      count++;
    }
  }
  
  return count;
}

/* Get file info */
int vfs_stat(const char *path, size_t *size, int *is_dir) {
  int idx = find_inode(path);
  if (idx < 0) return -ENOENT;
  
  if (size) *size = inodes[idx].size;
  if (is_dir) *is_dir = inodes[idx].type;
  
  return 0;
}

/* Check if file exists */
int vfs_exists(const char *path) {
  return find_inode(path) >= 0;
}

/* Create file */
int vfs_create(const char *path) {
  if (!vfs_initialized) vfs_init();
  int result = create_inode(path, 0);
  if (result >= 0) {
    maybe_persist_path(path);
  }
  return result;
}

/* Delete file/directory */
int vfs_delete(const char *path) {
  int idx = find_inode(path);
  int should_persist = path_should_persist(path);
  if (idx < 0) return -ENOENT;
  if (idx == 0) return -EINVAL; /* Can't delete root */
  
  /* Check if directory is empty */
  if (inodes[idx].type == 1) {
    for (int i = 0; i < inode_count; i++) {
      if (inodes[i].parent == idx) {
        return -ENOTEMPTY;
      }
    }
  }
  
  /* Free data */
  if (inodes[idx].data) {
    kfree(inodes[idx].data);
  }
  
  /* Remove by swapping with last */
  if (idx < inode_count - 1) {
    inodes[idx] = inodes[inode_count - 1];
    /* Update children's parent pointers */
    for (int i = 0; i < inode_count - 1; i++) {
      if (inodes[i].parent == inode_count - 1) {
        inodes[i].parent = idx;
      }
    }
  }
  inode_count--;

  if (should_persist) {
    vfs_persist_save();
  }
  return 0;
}

/* Rename file/directory */
int vfs_rename(const char *oldpath, const char *newpath) {
  int idx = find_inode(oldpath);
  int should_persist = path_should_persist(oldpath) || path_should_persist(newpath);
  if (idx < 0) return -ENOENT;
  if (idx == 0) return -EINVAL; /* Can't rename root */
  if (find_inode(newpath) >= 0) return -EEXIST;

  char parent_path[MAX_PATH];
  get_parent_path(newpath, parent_path);
  int parent = find_inode(parent_path);
  if (parent < 0) return -ENOENT;
  if (inodes[parent].type != 1) return -ENOTDIR;
  if (inodes[idx].type == 1 && path_is_inside(newpath, oldpath)) {
    return -EINVAL;
  }

  int old_len = strlen(oldpath);
  int new_len = strlen(newpath);

  if (inodes[idx].type == 1) {
    for (int i = 0; i < inode_count; i++) {
      if (i != idx && path_is_inside(inodes[i].path, oldpath)) {
        int suffix_len = strlen(inodes[i].path + old_len);
        if (new_len + suffix_len >= MAX_PATH) {
          return -ENAMETOOLONG;
        }
      }
    }
  }

  if (inodes[idx].type == 1) {
    for (int i = 0; i < inode_count; i++) {
      if (i != idx && path_is_inside(inodes[i].path, oldpath)) {
        char updated[MAX_PATH];
        snprintf(updated, sizeof(updated), "%s%s", newpath,
                 inodes[i].path + old_len);
        str_cpy(inodes[i].path, updated, MAX_PATH);
      }
    }
  }
  
  str_cpy(inodes[idx].path, newpath, MAX_PATH);
  get_filename(newpath, inodes[idx].name);
  inodes[idx].parent = parent;

  if (should_persist) {
    vfs_persist_save();
  }
  return 0;
}
