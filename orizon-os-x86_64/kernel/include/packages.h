/*
 * Orizon OS x86_64 - Minimal Package Manager
 */

#ifndef _PACKAGES_H
#define _PACKAGES_H

#include "types.h"

int orizon_pkg_init(void);
int orizon_pkg_refresh_database(void);
int orizon_pkg_install_file(const char *path, char *report, size_t report_size);
int orizon_pkg_install_buffer(const char *source_name, const void *data,
                              size_t size, char *report, size_t report_size);
int orizon_pkg_list(char *out, size_t out_size);
int orizon_pkg_status(char *out, size_t out_size);
int orizon_pkg_hash_file(const char *path, char *out, size_t out_size);
int orizon_pkg_write_sample(char *report, size_t report_size);

#endif /* _PACKAGES_H */
