/*
 * Orizon OS x86_64 - Simple RAM-based Virtual Filesystem
 */

#include "../include/vfs.h"
#include "../include/kmalloc.h"
#include "../include/storage.h"
#include "../include/string.h"

#define PERSIST_MAGIC "ORZPFS1"
#define PERSIST_VERSION 1U
#define PERSIST_BYTES (256U * 1024U)
#define PERSIST_SECTORS (PERSIST_BYTES / ORIZON_SECTOR_SIZE)
#define PERSIST_HEADER_SIZE ORIZON_SECTOR_SIZE
#define PERSIST_IO_MAX_SECTORS 128U

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

/* Filesystem state */
static inode_t inodes[MAX_FILES];
static int inode_count = 0;
static file_t open_files[MAX_OPEN];
static int vfs_initialized = 0;
static int persist_ready = 0;
static int persist_loading = 0;
static const char *persist_status = "workspace/log persistence not loaded";
static uint8_t persist_buf[PERSIST_BYTES] __attribute__((aligned(4096)));

static int create_inode(const char *path, int type);

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
          path_is_inside(path, "/logs"));
}

static int path_is_persistent_root(const char *path) {
  return path && (str_eq(path, "/workspace") || str_eq(path, "/logs"));
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
  persist_status = status;
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

/* Seed initial filesystem content */
void vfs_seed_content(void) {
  /* Create a small, stable workspace for iterative development. */
  vfs_mkdir("/workspace");
  vfs_mkdir("/system");
  vfs_mkdir("/system/share");
  vfs_mkdir("/home");
  vfs_mkdir("/home/orizon");
  vfs_mkdir("/packages");
  vfs_mkdir("/logs");
  vfs_mkdir("/tmp");

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
}

int vfs_persist_save(void) {
  if (!persist_ready || persist_loading) {
    return -EINVAL;
  }
  if (!storage_available()) {
    persist_set_status("workspace/log persistence unavailable");
    return -EIO;
  }

  memset(persist_buf, 0, sizeof(persist_buf));
  memcpy(persist_buf, PERSIST_MAGIC, 7);
  put_u32(persist_buf + 8, PERSIST_VERSION);

  size_t offset = PERSIST_HEADER_SIZE;
  uint32_t entry_count = 0;

  /* Directories first so loading can recreate parents before files. */
  for (int i = 0; i < inode_count; i++) {
    if (inodes[i].type == 1 && path_should_persist(inodes[i].path) &&
        !path_is_persistent_root(inodes[i].path)) {
      if (persist_append_entry(&offset, &entry_count, &inodes[i]) < 0) {
        persist_set_status("workspace/log persistence full");
        return -ENOSPC;
      }
    }
  }
  for (int i = 0; i < inode_count; i++) {
    if (inodes[i].type == 0 && path_should_persist(inodes[i].path)) {
      if (persist_append_entry(&offset, &entry_count, &inodes[i]) < 0) {
        persist_set_status("workspace/log persistence full");
        return -ENOSPC;
      }
    }
  }

  uint32_t payload_size = (uint32_t)(offset - PERSIST_HEADER_SIZE);
  put_u32(persist_buf + 12, entry_count);
  put_u32(persist_buf + 16, payload_size);
  put_u32(persist_buf + 20,
          persist_checksum(persist_buf + PERSIST_HEADER_SIZE, payload_size));

  uint32_t sectors = (uint32_t)((offset + ORIZON_SECTOR_SIZE - 1) /
                                ORIZON_SECTOR_SIZE);
  if (sectors == 0) {
    sectors = 1;
  }
  if (persist_storage_write(ORIZON_PERSIST_LBA, persist_buf, sectors) < 0) {
    persist_set_status("workspace/log persistence write failed");
    return -EIO;
  }

  persist_set_status("workspace/log persistence active");
  return 0;
}

void vfs_persist_load(void) {
  if (!vfs_initialized) {
    vfs_init();
  }

  if (!storage_available()) {
    persist_ready = 0;
    persist_set_status("workspace/log persistence unavailable");
    return;
  }

  if (persist_storage_read(ORIZON_PERSIST_LBA, persist_buf, PERSIST_SECTORS) < 0) {
    persist_ready = 0;
    persist_set_status("workspace/log persistence read failed");
    return;
  }

  persist_loading = 1;
  persist_ready = 1;

  if (memcmp(persist_buf, PERSIST_MAGIC, 7) != 0 ||
      get_u32(persist_buf + 8) != PERSIST_VERSION) {
    persist_loading = 0;
    persist_set_status("workspace/log persistence initialized");
    vfs_persist_save();
    return;
  }

  uint32_t entry_count = get_u32(persist_buf + 12);
  uint32_t payload_size = get_u32(persist_buf + 16);
  uint32_t checksum = get_u32(persist_buf + 20);

  if (payload_size > PERSIST_BYTES - PERSIST_HEADER_SIZE ||
      checksum != persist_checksum(persist_buf + PERSIST_HEADER_SIZE,
                                   payload_size)) {
    persist_loading = 0;
    persist_set_status("workspace/log persistence checksum failed");
    return;
  }

  clear_directory_contents("/workspace");
  clear_directory_contents("/logs");

  size_t offset = PERSIST_HEADER_SIZE;
  for (uint32_t entry = 0; entry < entry_count; entry++) {
    if (offset + 7 > PERSIST_HEADER_SIZE + payload_size) {
      break;
    }

    int type = persist_buf[offset];
    uint16_t path_len = get_u16(persist_buf + offset + 1);
    uint32_t data_size = get_u32(persist_buf + offset + 3);
    offset += 7;

    if (path_len == 0 || path_len >= MAX_PATH ||
        offset + path_len + data_size > PERSIST_HEADER_SIZE + payload_size) {
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
    } else {
      file_t *f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
      if (f) {
        vfs_write(f, persist_buf + offset, data_size);
        vfs_close(f);
      }
    }
    offset += data_size;
  }

  persist_loading = 0;
  persist_set_status("workspace/log persistence active");
}

int vfs_persist_available(void) {
  return persist_ready && storage_available();
}

const char *vfs_persist_status(void) {
  return persist_status;
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
