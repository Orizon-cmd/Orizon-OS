/*
 * Orizon OS x86_64 - Minimal Package Manager
 */

#ifndef _PACKAGES_H
#define _PACKAGES_H

#include "types.h"

int orizon_pkg_init(void);
int orizon_pkg_refresh_database(void);
int orizon_pkg_install_named(const char *name, char *report,
                             size_t report_size);
int orizon_pkg_install_file(const char *path, char *report, size_t report_size);
int orizon_pkg_install_buffer(const char *source_name, const void *data,
                              size_t size, char *report, size_t report_size);
int orizon_pkg_list(char *out, size_t out_size);
int orizon_pkg_status(char *out, size_t out_size);
int orizon_pkg_audit(char *out, size_t out_size);
int orizon_pkg_doctor(char *out, size_t out_size);
int orizon_pkg_cache(char *out, size_t out_size);
int orizon_pkg_search(const char *query, char *out, size_t out_size);
int orizon_pkg_remote(char *out, size_t out_size);
int orizon_pkg_remote_verify(char *out, size_t out_size);
int orizon_pkg_upgrade_plan(char *out, size_t out_size);
int orizon_pkg_info(const char *name, char *out, size_t out_size);
int orizon_pkg_remove(const char *name, char *report, size_t report_size);
int orizon_pkg_rollback(const char *name, char *report, size_t report_size);
int orizon_pkg_hash_file(const char *path, char *out, size_t out_size);
int orizon_pkg_verify_file(const char *path, char *out, size_t out_size);
int orizon_pkg_simulate_file(const char *path, char *out, size_t out_size);
int orizon_pkg_history(char *out, size_t out_size);
int orizon_pkg_write_sample(char *report, size_t report_size);
int orizon_pkg_write_desktop_sample(char *report, size_t report_size);
int orizon_pkg_write_desktop_module_sample(const char *name, char *report,
                                           size_t report_size);

#endif /* _PACKAGES_H */
