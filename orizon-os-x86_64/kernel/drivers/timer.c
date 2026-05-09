/*
 * Orizon OS x86_64 - LAPIC/PIT timer and uptime
 */

#include "../include/timer.h"
#include "../include/acpi.h"
#include "../include/gui.h"
#include "../include/idt.h"
#include "../include/mmio.h"
#include "../include/sched.h"
#include "../include/string.h"

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND 0x43
#define PIT_SPEAKER 0x61
#define PIT_BASE_HZ 1193182ULL
#define PIT_MODE_SQUARE_WAVE 0x36
#define PIT_CH2_MODE0 0xB0

#define MSR_IA32_APIC_BASE 0x1B
#define APIC_BASE_ENABLE (1ULL << 11)
#define APIC_BASE_X2APIC (1ULL << 10)
#define APIC_BASE_ADDR_MASK 0xFFFFFF000ULL

#define LAPIC_EOI 0x0B0
#define LAPIC_SVR 0x0F0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_INITIAL_COUNT 0x380
#define LAPIC_CURRENT_COUNT 0x390
#define LAPIC_DIVIDE_CONFIG 0x3E0

#define LAPIC_TIMER_VECTOR 0xF0
#define LAPIC_SPURIOUS_VECTOR 0xFF
#define LAPIC_ENABLE 0x100
#define LAPIC_LVT_MASKED (1U << 16)
#define LAPIC_TIMER_PERIODIC (1U << 17)
#define LAPIC_DIVIDE_BY_16 0x3
#define LAPIC_FALLBACK_PERIOD 62500U

static volatile uint64_t ticks = 0;
static volatile uint32_t *lapic = NULL;
static uint64_t lapic_phys = 0;
static uint32_t lapic_period = 0;
static int lapic_ready = 0;
static const char *timer_source_text = "none";
static const char *timer_status_text = "timer not initialized";

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static inline void io_wait(void) {
  __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

static inline uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
  uint32_t lo = (uint32_t)value;
  uint32_t hi = (uint32_t)(value >> 32);
  __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static void timer_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a,
                        uint32_t *b, uint32_t *c, uint32_t *d) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(leaf), "c"(subleaf));
  if (a) *a = eax;
  if (b) *b = ebx;
  if (c) *c = ecx;
  if (d) *d = edx;
}

static int cpu_has_apic(void) {
  uint32_t a, b, c, d;
  timer_cpuid(1, 0, &a, &b, &c, &d);
  UNUSED(a);
  UNUSED(b);
  UNUSED(c);
  return (d & (1U << 9)) != 0;
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
  lapic[reg / 4] = value;
  (void)lapic[reg / 4];
}

static inline uint32_t lapic_read(uint32_t reg) {
  return lapic[reg / 4];
}

static void timer_tick_common(void) {
  ticks++;
  sched_tick();
}

static void pit_timer_irq(interrupt_frame_t *frame) {
  UNUSED(frame);
  timer_tick_common();
}

static void lapic_timer_irq(interrupt_frame_t *frame) {
  UNUSED(frame);
  timer_tick_common();
  if (lapic) {
    lapic_write(LAPIC_EOI, 0);
  }
}

static int pit_wait_channel2(uint16_t count) {
  uint8_t old_gate = inb(PIT_SPEAKER);
  unsigned timeout = 20000000U;

  outb(PIT_SPEAKER, (uint8_t)(old_gate & (uint8_t)~0x01U));
  io_wait();
  outb(PIT_COMMAND, PIT_CH2_MODE0);
  outb(PIT_CHANNEL2, (uint8_t)(count & 0xFF));
  outb(PIT_CHANNEL2, (uint8_t)(count >> 8));
  io_wait();
  outb(PIT_SPEAKER, (uint8_t)((old_gate & (uint8_t)~0x02U) | 0x01U));

  while (((inb(PIT_SPEAKER) & 0x20U) == 0) && timeout-- > 0) {
    __asm__ volatile("pause");
  }
  outb(PIT_SPEAKER, old_gate);
  return timeout > 0 ? 0 : -1;
}

static uint32_t lapic_calibrate_period(void) {
  uint16_t pit_count = (uint16_t)(PIT_BASE_HZ / TIMER_HZ);
  uint32_t current;
  uint32_t elapsed;

  lapic_write(LAPIC_DIVIDE_CONFIG, LAPIC_DIVIDE_BY_16);
  lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED | LAPIC_TIMER_VECTOR);
  lapic_write(LAPIC_INITIAL_COUNT, 0xFFFFFFFFU);

  if (pit_wait_channel2(pit_count) != 0) {
    lapic_write(LAPIC_INITIAL_COUNT, 0);
    return LAPIC_FALLBACK_PERIOD;
  }

  current = lapic_read(LAPIC_CURRENT_COUNT);
  lapic_write(LAPIC_INITIAL_COUNT, 0);
  elapsed = 0xFFFFFFFFU - current;
  if (elapsed < 1000U || elapsed == 0xFFFFFFFFU) {
    return LAPIC_FALLBACK_PERIOD;
  }
  return elapsed;
}

static int lapic_init_timer(void) {
  uint64_t apic_base;
  uint64_t phys;
  uint64_t mapped;
  uint32_t svr;

  if (!cpu_has_apic()) {
    timer_status_text = "CPU local APIC unsupported";
    return -1;
  }

  apic_base = rdmsr(MSR_IA32_APIC_BASE);
  if (apic_base & APIC_BASE_X2APIC) {
    timer_status_text = "x2APIC mode not supported yet";
    return -2;
  }

  phys = acpi_has_madt() ? acpi_lapic_address()
                         : (apic_base & APIC_BASE_ADDR_MASK);
  if (!phys) {
    phys = 0xFEE00000ULL;
  }
  lapic_phys = phys;

  wrmsr(MSR_IA32_APIC_BASE,
        (apic_base & ~APIC_BASE_ADDR_MASK) |
            (phys & APIC_BASE_ADDR_MASK) | APIC_BASE_ENABLE);

  mapped = mmio_map_range(phys, 0x1000);
  if (!mapped) {
    timer_status_text = "LAPIC MMIO map failed";
    return -3;
  }
  lapic = (volatile uint32_t *)(uintptr_t)mapped;

  svr = lapic_read(LAPIC_SVR);
  lapic_write(LAPIC_SVR, (svr & 0xFFFFFF00U) | LAPIC_ENABLE |
                            LAPIC_SPURIOUS_VECTOR);

  lapic_period = lapic_calibrate_period();
  idt_register_handler(LAPIC_TIMER_VECTOR, lapic_timer_irq);
  lapic_write(LAPIC_DIVIDE_CONFIG, LAPIC_DIVIDE_BY_16);
  lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_PERIODIC | LAPIC_TIMER_VECTOR);
  lapic_write(LAPIC_INITIAL_COUNT, lapic_period);

  pic_set_mask(0);
  lapic_ready = 1;
  timer_source_text = "lapic";
  timer_status_text = "LAPIC timer online";
  serial_puts("[timer] LAPIC online at target 100 Hz, period=");
  serial_puthex(lapic_period);
  serial_puts("\n");
  return 0;
}

static void pit_init_timer(void) {
  uint16_t divisor = (uint16_t)(PIT_BASE_HZ / TIMER_HZ);
  if (divisor == 0) {
    divisor = 1;
  }

  idt_register_handler(0x20, pit_timer_irq);
  outb(PIT_COMMAND, PIT_MODE_SQUARE_WAVE);
  outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
  outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
  pic_clear_mask(0);
  lapic_ready = 0;
  timer_source_text = "pit";
  timer_status_text = "PIT timer online";
  serial_puts("[timer] PIT online at 100 Hz\n");
}

void timer_init(void) {
  ticks = 0;
  lapic_ready = 0;
  lapic_period = 0;
  timer_source_text = "probing";
  timer_status_text = "probing LAPIC timer";

  if (lapic_init_timer() == 0) {
    return;
  }
  pit_init_timer();
}

void timer_init_pit_only(void) {
  ticks = 0;
  lapic = NULL;
  lapic_ready = 0;
  lapic_period = 0;
  lapic_phys = 0;
  timer_source_text = "pit";
  timer_status_text = "safe PIT timer requested";
  pit_init_timer();
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

const char *timer_source(void) {
  return timer_source_text;
}

const char *timer_status(void) {
  return timer_status_text;
}

uint32_t timer_lapic_period_count(void) {
  return lapic_period;
}

int timer_lapic_ready(void) {
  return lapic_ready;
}

void timer_format_status(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  snprintf(buf, size, "source=%s status=%s lapic=%s lapic-phys=0x%lx period=%lu",
           timer_source_text, timer_status_text,
           lapic_ready ? "ready" : "no",
           (unsigned long)lapic_phys, (unsigned long)lapic_period);
}
