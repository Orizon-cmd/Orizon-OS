/*
 * Orizon OS x86_64 - Kernel log ring buffer
 */

#include "../include/klog.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/vfs.h"

#define KLOG_BUFFER_SIZE (32U * 1024U)

static char klog_buffer[KLOG_BUFFER_SIZE];
static char klog_save_buffer[KLOG_BUFFER_SIZE];
static size_t klog_head = 0;
static size_t klog_count = 0;
static uint64_t klog_dropped = 0;
static int klog_saved_boot = 0;

static void klog_putc(char c) {
  klog_buffer[klog_head] = c;
  klog_head = (klog_head + 1) % KLOG_BUFFER_SIZE;
  if (klog_count < KLOG_BUFFER_SIZE) {
    klog_count++;
  } else {
    klog_dropped++;
  }
}

void klog_write_raw(const char *text) {
  if (!text) {
    return;
  }
  while (*text) {
    klog_putc(*text++);
  }
}

void klog_info(const char *subsystem, const char *message) {
  char line[256];
  snprintf(line, sizeof(line), "[%lu] [%s] %s\n",
           (unsigned long)timer_ticks(),
           subsystem && subsystem[0] ? subsystem : "kernel",
           message ? message : "");
  klog_write_raw(line);
}

size_t klog_snapshot(char *out, size_t out_size) {
  size_t copy;
  size_t start;

  if (!out || out_size == 0) {
    return 0;
  }

  copy = klog_count;
  if (copy > out_size - 1) {
    copy = out_size - 1;
  }

  if (copy < klog_count) {
    start = (klog_head + KLOG_BUFFER_SIZE - copy) % KLOG_BUFFER_SIZE;
  } else {
    start = (klog_head + KLOG_BUFFER_SIZE - klog_count) % KLOG_BUFFER_SIZE;
  }

  for (size_t i = 0; i < copy; i++) {
    out[i] = klog_buffer[(start + i) % KLOG_BUFFER_SIZE];
  }
  out[copy] = '\0';
  return copy;
}

size_t klog_size(void) {
  return klog_count;
}

uint64_t klog_dropped_bytes(void) {
  return klog_dropped;
}

static int klog_installed_marker_present(void) {
  char state[256];
  file_t *f;
  size_t used = 0;
  ssize_t n = 0;

  if (vfs_exists("/workspace/.orizon/installed")) {
    return 1;
  }

  f = vfs_open("/workspace/.orizon/install-state", O_RDONLY);
  if (!f) {
    return 0;
  }
  while (used < sizeof(state) - 1 &&
         (n = vfs_read(f, state + used, (sizeof(state) - 1) - used)) > 0) {
    used += (size_t)n;
  }
  vfs_close(f);
  if (n < 0) {
    return 0;
  }
  state[used] = '\0';
  return strstr(state, "install complete") != NULL;
}

int klog_persist_boot_if_installed(void) {
  char header[192];
  file_t *f;
  size_t n;

  if (klog_saved_boot) {
    return 0;
  }
  if (!klog_installed_marker_present()) {
    return -1;
  }
  if (!vfs_persist_available()) {
    return -3;
  }

  vfs_mkdir("/logs");
  f = vfs_open(KLOG_BOOT_PATH, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -2;
  }

  snprintf(header, sizeof(header),
           "Orizon OS boot log\n"
           "built: " __DATE__ " " __TIME__ "\n"
           "ticks-at-save: %lu\n"
           "ring-bytes: %lu dropped-bytes: %lu\n"
           "\n",
           (unsigned long)timer_ticks(), (unsigned long)klog_count,
           (unsigned long)klog_dropped);
  vfs_write(f, header, strlen(header));

  n = klog_snapshot(klog_save_buffer, sizeof(klog_save_buffer));
  if (n > 0) {
    vfs_write(f, klog_save_buffer, n);
    if (klog_save_buffer[n - 1] != '\n') {
      vfs_write(f, "\n", 1);
    }
  }
  vfs_close(f);
  klog_saved_boot = 1;
  return 0;
}

int klog_boot_persisted(void) {
  return klog_saved_boot;
}
