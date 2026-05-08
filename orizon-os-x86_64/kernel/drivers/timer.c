/*
 * Orizon OS x86_64 - PIT timer
 */

#include "../include/timer.h"
#include "../include/gui.h"
#include "../include/idt.h"
#include "../include/sched.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_HZ 1193182ULL
#define PIT_MODE_SQUARE_WAVE 0x36

static volatile uint64_t ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void timer_irq(interrupt_frame_t *frame) {
  UNUSED(frame);
  ticks++;
  sched_tick();
}

void timer_init(void) {
  uint16_t divisor = (uint16_t)(PIT_BASE_HZ / TIMER_HZ);
  if (divisor == 0) {
    divisor = 1;
  }

  idt_register_handler(0x20, timer_irq);
  outb(PIT_COMMAND, PIT_MODE_SQUARE_WAVE);
  outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
  outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
  pic_clear_mask(0);
  serial_puts("[timer] PIT online at 100 Hz\n");
}

uint64_t timer_ticks(void) {
  return ticks;
}

uint64_t timer_uptime_seconds(void) {
  return ticks / TIMER_HZ;
}

uint64_t timer_hz(void) {
  return TIMER_HZ;
}
