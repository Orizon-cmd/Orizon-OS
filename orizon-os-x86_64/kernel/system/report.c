/*
 * Orizon OS x86_64 - Exportable hardware report
 *
 * The report is intentionally read-only: it snapshots diagnostics and writes
 * only to the VFS report path so operators can share hardware evidence without
 * starting an install or touching disk layout.
 */

#include "../include/report.h"
#include "../include/acpi.h"
#include "../include/bootinfo.h"
#include "../include/gui.h"
#include "../include/i2c_hid.h"
#include "../include/klog.h"
#include "../include/kmalloc.h"
#include "../include/net.h"
#include "../include/netstack.h"
#include "../include/pci.h"
#include "../include/ps2.h"
#include "../include/ssh.h"
#include "../include/selftest.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/update.h"
#include "../include/usb.h"
#include "../include/vfs.h"
#include "../include/wifi.h"

#define REPORT_LOG_TAIL_BYTES 1200

static void report_append(char *out, size_t out_size, size_t *used,
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

static void report_append_block(char *out, size_t out_size, size_t *used,
                                const char *title, const char *body) {
  if (title && title[0]) {
    report_append(out, out_size, used, "\n## ");
    report_append(out, out_size, used, title);
    report_append(out, out_size, used, "\n");
  }
  if (body && body[0]) {
    report_append(out, out_size, used, body);
    if (body[strlen(body) - 1] != '\n') {
      report_append(out, out_size, used, "\n");
    }
  } else {
    report_append(out, out_size, used, "(empty)\n");
  }
}

static void report_append_file_tail(char *out, size_t out_size, size_t *used,
                                    const char *title, const char *path,
                                    size_t max_bytes) {
  static char buf[REPORT_LOG_TAIL_BYTES + 1];
  file_t *f;
  size_t file_size = 0;
  size_t start = 0;
  ssize_t n;

  report_append(out, out_size, used, "\n## ");
  report_append(out, out_size, used, title);
  report_append(out, out_size, used, "\n");
  report_append(out, out_size, used, "path: ");
  report_append(out, out_size, used, path);
  report_append(out, out_size, used, "\n");

  if (!vfs_exists(path) || vfs_stat(path, &file_size, NULL) < 0) {
    report_append(out, out_size, used, "(not available)\n");
    return;
  }
  f = vfs_open(path, O_RDONLY);
  if (!f) {
    report_append(out, out_size, used, "(open failed)\n");
    return;
  }
  if (max_bytes > REPORT_LOG_TAIL_BYTES) {
    max_bytes = REPORT_LOG_TAIL_BYTES;
  }
  if (file_size > max_bytes) {
    if (vfs_seek(f, (int)(file_size - max_bytes), SEEK_SET) == 0) {
      report_append(out, out_size, used, "[tail]\n");
    }
  }
  n = vfs_read(f, buf, max_bytes);
  vfs_close(f);
  if (n <= 0) {
    report_append(out, out_size, used, "(empty)\n");
    return;
  }
  buf[(size_t)n] = '\0';
  if (file_size > max_bytes) {
    while (start < (size_t)n && buf[start] != '\n') {
      start++;
    }
    if (start < (size_t)n) {
      start++;
    } else {
      start = 0;
    }
  }
  report_append(out, out_size, used, buf + start);
  if (out[*used ? *used - 1 : 0] != '\n') {
    report_append(out, out_size, used, "\n");
  }
}

static void report_append_pci_bars(char *out, size_t out_size, size_t *used) {
  pci_device_info_t devs[96];
  char line[192];
  int total = pci_scan_all(devs, 96);

  snprintf(line, sizeof(line), "PCI devices: %d\n", total);
  report_append(out, out_size, used, line);
  for (int i = 0; i < total && i < 96; i++) {
    const pci_device_info_t *dev = &devs[i];
    snprintf(line, sizeof(line),
             "%02x:%02x.%u vendor=%04x device=%04x class=%02x/%02x/%02x "
             "bars=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx\n",
             dev->bus, dev->device, dev->function, dev->vendor_id,
             dev->device_id, dev->class_code, dev->subclass, dev->prog_if,
             (unsigned long)dev->bar[0], (unsigned long)dev->bar[1],
             (unsigned long)dev->bar[2], (unsigned long)dev->bar[3],
             (unsigned long)dev->bar[4], (unsigned long)dev->bar[5]);
    report_append(out, out_size, used, line);
  }
  if (total > 96) {
    report_append(out, out_size, used, "[truncated]\n");
  }
}

int orizon_report_format(char *out, size_t out_size) {
  char line[256];
  char block[4096];
  char cap[64];
  size_t used = 0;
  size_t block_used = 0;
  kmalloc_stats_t stats;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';

  report_append(out, out_size, &used, "Orizon hardware report\n");
  report_append(out, out_size, &used,
                "mode: read-only diagnostics; no install or disk layout write\n");
  snprintf(line, sizeof(line), "cmdline: %s\n",
           boot_cmdline()[0] ? boot_cmdline() : "(none)");
  report_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "boot: uptime=%lus ticks=%lu hz=%lu timer=%s/%s "
           "boot-log=%luB dropped=%lu saved=%s\n",
           (unsigned long)timer_uptime_seconds(),
           (unsigned long)timer_ticks(), (unsigned long)timer_hz(),
           gui_timer_irq_active() ? "irq" : "no-irq",
           gui_timer_fallback_active() ? "poll" : "hlt",
           (unsigned long)klog_size(), (unsigned long)klog_dropped_bytes(),
           klog_boot_persisted() ? "yes" : "no");
  report_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "install-state: %s\n",
           vfs_exists("/workspace/.orizon/installed") ? "installed"
                                                       : "live-or-unmarked");
  report_append(out, out_size, &used, line);

  timer_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "Timer", block);
  acpi_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "ACPI", block);

  kmalloc_get_stats(&stats);
  snprintf(block, sizeof(block),
           "total-kb=%lu used-kb=%lu free-kb=%lu largest-kb=%lu\n"
           "blocks-used=%lu blocks-free=%lu blocks-total=%lu\n",
           (unsigned long)(stats.total / 1024),
           (unsigned long)(stats.used / 1024),
           (unsigned long)(stats.free / 1024),
           (unsigned long)(stats.largest_free / 1024),
           (unsigned long)stats.used_blocks,
           (unsigned long)stats.free_blocks,
           (unsigned long)stats.blocks);
  report_append_block(out, out_size, &used, "Memory", block);

  storage_format_capacity(cap, sizeof(cap));
  snprintf(block, sizeof(block), "status=%s\ncapacity=%s\npersistence=%s\n",
           storage_available() ? storage_status() : "unavailable", cap,
           vfs_persist_status());
  report_append_block(out, out_size, &used, "Storage", block);
  vfs_persist_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "Persistence", block);
  storage_format_diagnostics(block, sizeof(block));
  report_append_block(out, out_size, &used, "Storage Diagnostics", block);

  block[0] = '\0';
  block_used = 0;
  report_append_pci_bars(block, sizeof(block), &block_used);
  report_append_block(out, out_size, &used, "PCI Bars", block);
  pci_format_diagnostics(block, sizeof(block));
  report_append_block(out, out_size, &used, "PCI Diagnostics", block);

  net_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "Ethernet", block);
  netstack_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "IPv4", block);
  netstack_format_route(block, sizeof(block));
  report_append_block(out, out_size, &used, "Route", block);

  usb_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "USB", block);
  usb_format_port_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "USB Ports", block);
  usb_format_device_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "USB Devices", block);
  usb_format_net_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "USB Ethernet", block);

  wifi_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "Wi-Fi", block);
  i2c_hid_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "I2C-HID", block);
  ps2_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "PS/2", block);

  ssh_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "SSH", block);
  orizon_update_boot_guard_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "Boot Guard", block);
  orizon_update_format_status(block, sizeof(block));
  report_append_block(out, out_size, &used, "Update", block);
  orizon_selftest_format("all", block, sizeof(block));
  report_append_block(out, out_size, &used, "Selftest", block);

  report_append_file_tail(out, out_size, &used, "Boot Log", KLOG_BOOT_PATH,
                          REPORT_LOG_TAIL_BYTES);
  storage_format_log(block, sizeof(block));
  report_append_block(out, out_size, &used, "Storage Log", block);
  pci_format_diagnostics(block, sizeof(block));
  report_append_block(out, out_size, &used, "PCI Log", block);
  report_append_file_tail(out, out_size, &used, "Network Log",
                          netstack_log_path(), REPORT_LOG_TAIL_BYTES);
  report_append_file_tail(out, out_size, &used, "USB Log", usb_log_path(),
                          REPORT_LOG_TAIL_BYTES);
  report_append_file_tail(out, out_size, &used, "Wi-Fi Log", "/logs/wifi.log",
                          REPORT_LOG_TAIL_BYTES);
  report_append_file_tail(out, out_size, &used, "SSH Log", ORIZON_SSH_LOG_PATH,
                          REPORT_LOG_TAIL_BYTES);
  report_append_file_tail(out, out_size, &used, "Update Log",
                          "/workspace/.orizon/update.log",
                          REPORT_LOG_TAIL_BYTES);

  report_append(out, out_size, &used, "\n## Live Kernel Log Tail\n");
  {
    size_t n = klog_snapshot(block, sizeof(block) - 1);
    block[n] = '\0';
    report_append(out, out_size, &used, n ? block : "(empty)\n");
    if (n && block[n - 1] != '\n') {
      report_append(out, out_size, &used, "\n");
    }
  }
  return (int)used;
}

int orizon_report_save(char *status, size_t status_size) {
  static char report[32768];
  int n = orizon_report_format(report, sizeof(report));
  file_t *f;

  if (n < 0) {
    if (status && status_size > 0) {
      snprintf(status, status_size, "report save: format failed\n");
    }
    return -1;
  }
  f = vfs_open(ORIZON_HARDWARE_REPORT_PATH, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    if (status && status_size > 0) {
      snprintf(status, status_size, "report save: open failed: %s\n",
               ORIZON_HARDWARE_REPORT_PATH);
    }
    return -1;
  }
  if (vfs_write(f, report, (size_t)n) != n) {
    vfs_close(f);
    if (status && status_size > 0) {
      snprintf(status, status_size, "report save: write failed\n");
    }
    return -1;
  }
  vfs_close(f);
  if (status && status_size > 0) {
    snprintf(status, status_size, "report save: wrote %s (%lu bytes)\n",
             ORIZON_HARDWARE_REPORT_PATH, (unsigned long)n);
  }
  return 0;
}
