/*
 * Orizon OS x86_64 - First scheduler/process accounting layer
 *
 * This is not preemptive userspace yet. It gives the kernel real process
 * identities, CPU tick accounting, and an idle state that can sleep with HLT.
 */

#include "../include/sched.h"
#include "../include/string.h"
#include "../include/timer.h"

static sched_process_t processes[SCHED_MAX_PROCESSES];
static int process_count = 0;
static int current_slot = -1;
static uint64_t context_switch_count = 0;

static int sched_find(const char *name) {
  for (int i = 0; i < process_count; i++) {
    if (processes[i].state != SCHED_UNUSED && strcmp(processes[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

void sched_init(void) {
  if (process_count > 0) {
    return;
  }
  memset(processes, 0, sizeof(processes));
  sched_create_process("kernel", SCHED_RUNNING);
  sched_create_process("idle", SCHED_IDLE);
  sched_create_process("gui-shell", SCHED_READY);
  sched_create_process("input", SCHED_READY);
  sched_create_process("update-manager", SCHED_SLEEPING);
  current_slot = sched_find("kernel");
}

int sched_create_process(const char *name, sched_state_t state) {
  if (!name || *name == '\0') {
    return -1;
  }
  int existing = sched_find(name);
  if (existing >= 0) {
    return processes[existing].pid;
  }
  if (process_count >= SCHED_MAX_PROCESSES) {
    return -1;
  }

  sched_process_t *proc = &processes[process_count];
  proc->pid = process_count + 1;
  strncpy(proc->name, name, SCHED_NAME_LEN - 1);
  proc->name[SCHED_NAME_LEN - 1] = '\0';
  proc->state = state;
  proc->cpu_ticks = 0;
  proc->wake_tick = 0;
  process_count++;
  return proc->pid;
}

int sched_enter_process(const char *name) {
  int slot = sched_find(name);
  if (slot < 0) {
    int pid = sched_create_process(name, SCHED_READY);
    if (pid < 0) {
      return -1;
    }
    slot = sched_find(name);
  }
  if (slot < 0) {
    return -1;
  }

  if (current_slot >= 0 && processes[current_slot].state == SCHED_RUNNING) {
    processes[current_slot].state = SCHED_READY;
  }
  if (current_slot != slot) {
    context_switch_count++;
  }
  current_slot = slot;
  processes[current_slot].state = SCHED_RUNNING;
  return processes[current_slot].pid;
}

int sched_set_process_state(const char *name, sched_state_t state) {
  int slot = sched_find(name);
  if (slot < 0) {
    return -1;
  }

  processes[slot].state = state;
  if (state != SCHED_SLEEPING) {
    processes[slot].wake_tick = 0;
  }
  return processes[slot].pid;
}

void sched_enter_idle(void) {
  int slot = sched_find("idle");
  if (slot < 0) {
    return;
  }
  if (current_slot >= 0 && processes[current_slot].state == SCHED_RUNNING) {
    processes[current_slot].state = SCHED_READY;
  }
  if (current_slot != slot) {
    context_switch_count++;
  }
  current_slot = slot;
  processes[current_slot].state = SCHED_IDLE;
}

void sched_tick(void) {
  if (current_slot >= 0 && current_slot < process_count) {
    processes[current_slot].cpu_ticks++;
  }

  uint64_t now = timer_ticks();
  for (int i = 0; i < process_count; i++) {
    if (processes[i].state == SCHED_SLEEPING && processes[i].wake_tick &&
        processes[i].wake_tick <= now) {
      processes[i].state = SCHED_READY;
      processes[i].wake_tick = 0;
    }
  }
}

uint64_t sched_context_switches(void) {
  return context_switch_count;
}

const char *sched_state_name(sched_state_t state) {
  switch (state) {
    case SCHED_RUNNING:
      return "running";
    case SCHED_READY:
      return "ready";
    case SCHED_SLEEPING:
      return "sleep";
    case SCHED_IDLE:
      return "idle";
    default:
      return "unused";
  }
}

int sched_snapshot(sched_process_t *out, int max_out) {
  int count = process_count < max_out ? process_count : max_out;
  for (int i = 0; i < count; i++) {
    out[i] = processes[i];
  }
  return count;
}
