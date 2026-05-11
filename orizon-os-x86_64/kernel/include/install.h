/*
 * Orizon OS x86_64 - Disk Installer
 */

#ifndef _INSTALL_H
#define _INSTALL_H

#include "types.h"

typedef struct {
  const char *language;
  const char *keyboard;
  const char *disk_mode;
  const char *hostname;
  int disk_index;
  const char *disk_name;
  int data_partition_index;
} orizon_install_config_t;

typedef struct {
  int index;
  int usable_for_data;
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t sectors;
  char type[32];
  char name[64];
} orizon_install_partition_info_t;

int orizon_install_get_partition(int partition_index,
                                 orizon_install_partition_info_t *out);
int orizon_install_format_partitions(char *report, size_t report_size);
int orizon_install_run(const orizon_install_config_t *config, char *report,
                       size_t report_size);
int orizon_install_boot_check(char *report, size_t report_size);
int orizon_install_dualboot_check(char *report, size_t report_size);
int orizon_install_repair_boot(char *report, size_t report_size);
int orizon_install_update_esp(const void *kernel, size_t kernel_size,
                              const void *efi, size_t efi_size,
                              const char *limine_conf,
                              size_t limine_conf_size,
                              const char *update_text,
                              size_t update_text_size, char *report,
                              size_t report_size);
int orizon_install_update_esp_with_rollback(
    const void *kernel, size_t kernel_size, const void *efi, size_t efi_size,
    const void *rollback_kernel, size_t rollback_kernel_size,
    const void *rollback_efi, size_t rollback_efi_size,
    const char *limine_conf, size_t limine_conf_size,
    const char *update_text, size_t update_text_size, char *report,
    size_t report_size);

#endif /* _INSTALL_H */
