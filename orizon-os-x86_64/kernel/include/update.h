/*
 * Orizon OS x86_64 - In-kernel Update Manager
 */

#ifndef _UPDATE_H
#define _UPDATE_H

#include "types.h"

int orizon_update_full_upgrade(char *report, size_t report_size);
int orizon_update_rollback(char *report, size_t report_size);
const char *orizon_update_status(void);

#endif /* _UPDATE_H */
