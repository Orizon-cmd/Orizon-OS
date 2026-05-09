/*
 * Orizon OS x86_64 - PIT timer and uptime
 */

#ifndef _TIMER_H
#define _TIMER_H

#include "types.h"

#define TIMER_HZ 100ULL

void timer_init(void);
void timer_init_pit_only(void);
uint64_t timer_ticks(void);
uint64_t timer_uptime_seconds(void);
uint64_t timer_hz(void);
const char *timer_source(void);
const char *timer_status(void);
uint32_t timer_lapic_period_count(void);
int timer_lapic_ready(void);
void timer_format_status(char *buf, size_t size);

#endif /* _TIMER_H */
