/*
 * Orizon OS x86_64 - First process/scheduler model
 */

#ifndef _SCHED_H
#define _SCHED_H

#include "types.h"

#define SCHED_MAX_PROCESSES 16
#define SCHED_NAME_LEN 32

typedef enum {
  SCHED_UNUSED = 0,
  SCHED_RUNNING,
  SCHED_READY,
  SCHED_SLEEPING,
  SCHED_IDLE,
} sched_state_t;

typedef struct {
  int pid;
  char name[SCHED_NAME_LEN];
  sched_state_t state;
  uint64_t cpu_ticks;
  uint64_t wake_tick;
} sched_process_t;

void sched_init(void);
int sched_create_process(const char *name, sched_state_t state);
int sched_enter_process(const char *name);
int sched_set_process_state(const char *name, sched_state_t state);
void sched_enter_idle(void);
void sched_tick(void);
uint64_t sched_context_switches(void);
const char *sched_state_name(sched_state_t state);
int sched_snapshot(sched_process_t *out, int max_out);

#endif /* _SCHED_H */
