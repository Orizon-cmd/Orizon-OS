/*
 * Orizon OS x86_64 - Kernel log ring buffer
 */

#ifndef _KLOG_H
#define _KLOG_H

#include "types.h"

#define KLOG_BOOT_PATH "/logs/boot.log"

void klog_write_raw(const char *text);
void klog_info(const char *subsystem, const char *message);
size_t klog_snapshot(char *out, size_t out_size);
size_t klog_size(void);
uint64_t klog_dropped_bytes(void);
int klog_persist_boot_if_installed(void);
int klog_boot_persisted(void);

#endif /* _KLOG_H */
