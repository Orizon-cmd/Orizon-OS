/*
 * Orizon OS x86_64 - Power control helpers
 */

#ifndef _POWER_H
#define _POWER_H

#include "types.h"

void power_schedule_shutdown(uint64_t delay_ticks);
void power_schedule_reboot(uint64_t delay_ticks);
int power_shutdown_pending(void);
int power_reboot_pending(void);
void power_poll(void);
void power_shutdown_now(void);
void power_reboot_now(void);

#endif /* _POWER_H */
