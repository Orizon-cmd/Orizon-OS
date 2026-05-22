/*
 * Orizon OS x86_64 - Exportable hardware report
 */

#ifndef _REPORT_H
#define _REPORT_H

#include "types.h"

#define ORIZON_HARDWARE_REPORT_PATH "/workspace/hardware-report.txt"

int orizon_report_format(char *out, size_t out_size);
int orizon_report_format_hardware_next(char *out, size_t out_size);
int orizon_report_save(char *status, size_t status_size);

#endif /* _REPORT_H */
