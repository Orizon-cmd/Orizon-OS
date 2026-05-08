/*
 * Orizon OS x86_64 - Power control helpers
 */

#include "../include/power.h"
#include "../include/string.h"
#include "../include/timer.h"

extern void serial_puts(const char *s);

static int shutdown_pending = 0;
static uint64_t shutdown_deadline = 0;

static inline void outw(uint16_t port, uint16_t val) {
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
  __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

void power_schedule_shutdown(uint64_t delay_ticks) {
  shutdown_pending = 1;
  shutdown_deadline = timer_ticks() + delay_ticks;
}

int power_shutdown_pending(void) {
  return shutdown_pending;
}

void power_shutdown_now(void) {
  serial_puts("[power] shutdown requested\n");
  __asm__ volatile("cli");

  /* Common emulator/ACPI PM ports. Real ACPI parsing will replace this later. */
  outw(0x604, 0x2000);  /* QEMU/OVMF ACPI PM1a_CNT */
  io_wait();
  outw(0xB004, 0x2000); /* Bochs/QEMU fallback */
  io_wait();
  outw(0x4004, 0x3400); /* VirtualBox fallback */
  io_wait();

  for (;;) {
    __asm__ volatile("hlt");
  }
}

void power_poll(void) {
  if (shutdown_pending && timer_ticks() >= shutdown_deadline) {
    power_shutdown_now();
  }
}
