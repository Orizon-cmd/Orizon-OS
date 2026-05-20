/*
 * Orizon OS x86_64 - Power control helpers
 */

#include "../include/power.h"
#include "../include/string.h"
#include "../include/timer.h"

extern void serial_puts(const char *s);

static int shutdown_pending = 0;
static uint64_t shutdown_deadline = 0;
static int reboot_pending = 0;
static uint64_t reboot_deadline = 0;

typedef struct idtr_ptr {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed)) idtr_ptr_t;

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

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

void power_schedule_reboot(uint64_t delay_ticks) {
  reboot_pending = 1;
  reboot_deadline = timer_ticks() + delay_ticks;
}

int power_shutdown_pending(void) {
  return shutdown_pending;
}

int power_reboot_pending(void) {
  return reboot_pending;
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

void power_reboot_now(void) {
  static idtr_ptr_t empty_idt = {0, 0};

  serial_puts("[power] reboot requested\n");
  __asm__ volatile("cli");

  /* QEMU/Bochs and many PCs still honor the i8042 reset pulse. */
  outb(0x64, 0xFE);
  for (int i = 0; i < 100000; i++) {
    io_wait();
  }

  /* If the reset pulse is ignored, triple fault to force a VM reset. */
  __asm__ volatile("lidt %0\n\tint $3" : : "m"(empty_idt));
  for (;;) {
    __asm__ volatile("hlt");
  }
}

void power_poll(void) {
  if (reboot_pending && timer_ticks() >= reboot_deadline) {
    power_reboot_now();
  }
  if (shutdown_pending && timer_ticks() >= shutdown_deadline) {
    power_shutdown_now();
  }
}
