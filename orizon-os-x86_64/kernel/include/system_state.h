/*
 * Orizon OS x86_64 - Installed/live system state helpers
 */

#ifndef _SYSTEM_STATE_H
#define _SYSTEM_STATE_H

#include "types.h"

#define ORIZON_HOSTNAME_PATH "/system/hostname"
#define ORIZON_RESCUE_REPORT_PATH "/workspace/.orizon/rescue-report.txt"

int orizon_system_is_installed(void);
void orizon_system_hostname(char *out, size_t out_size);
int orizon_system_set_hostname(const char *name, char *status,
                               size_t status_size);
void orizon_system_format_status(char *out, size_t out_size);
void orizon_system_format_rescue(char *out, size_t out_size);
void orizon_system_format_services(char *out, size_t out_size);
void orizon_system_format_doctor(char *out, size_t out_size);
void orizon_system_format_firstboot(char *out, size_t out_size);
void orizon_system_format_logs(char *out, size_t out_size);
int orizon_system_run_boot_tasks(char *out, size_t out_size);
int orizon_system_mark_firstboot_done(char *out, size_t out_size);
int orizon_system_repair(char *out, size_t out_size);

#endif /* _SYSTEM_STATE_H */
