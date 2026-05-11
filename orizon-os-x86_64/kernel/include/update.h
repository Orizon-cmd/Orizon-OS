/*
 * Orizon OS x86_64 - In-kernel Update Manager
 */

#ifndef _UPDATE_H
#define _UPDATE_H

#include "types.h"

typedef void (*orizon_update_progress_fn)(const char *line, void *ctx);

void orizon_update_set_progress(orizon_update_progress_fn fn, void *ctx);
int orizon_update_full_upgrade(char *report, size_t report_size);
int orizon_update_rollback(char *report, size_t report_size);
void orizon_update_boot_guard_check(void);
void orizon_update_boot_guard_status(char *out, size_t out_size);
int orizon_update_boot_guard_confirm(char *report, size_t report_size);
const char *orizon_update_status(void);

#endif /* _UPDATE_H */
