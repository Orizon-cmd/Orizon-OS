/*
 * Orizon OS x86_64 - Power control helpers
 */

#ifndef _POWER_H
#define _POWER_H

#include "types.h"

void power_schedule_shutdown(uint64_t delay_ticks);
int power_shutdown_pending(void);
void power_poll(void);
void power_shutdown_now(void);

#endif /* _POWER_H */
