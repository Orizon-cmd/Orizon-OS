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
} orizon_install_config_t;

int orizon_install_run(const orizon_install_config_t *config, char *report,
                       size_t report_size);
int orizon_install_update_esp(const void *kernel, size_t kernel_size,
                              const void *efi, size_t efi_size,
                              const char *limine_conf,
                              size_t limine_conf_size,
                              const char *update_text,
                              size_t update_text_size, char *report,
                              size_t report_size);

#endif /* _INSTALL_H */
