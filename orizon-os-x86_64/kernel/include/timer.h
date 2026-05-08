/*
 * Orizon OS x86_64 - PIT timer and uptime
 */

#ifndef _TIMER_H
#define _TIMER_H

#include "types.h"

#define TIMER_HZ 100ULL

void timer_init(void);
uint64_t timer_ticks(void);
uint64_t timer_uptime_seconds(void);
uint64_t timer_hz(void);

#endif /* _TIMER_H */
