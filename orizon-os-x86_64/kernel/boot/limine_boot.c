/*
 * Orizon OS x86_64 - Kernel Entry Point
 * Uses the Limine Boot Protocol for clean 64-bit entry
 */

#include "../include/gui.h"
#include "../include/limine.h"
#include "../include/bootinfo.h"
#include "../include/klog.h"
#include "../include/idt.h"
#include "../include/acpi.h"
#include "../include/sched.h"
#include "../include/string.h"
#include "../include/timer.h"
#include "../include/types.h"

/* ========== Limine Requests ========== */

/* Place requests in dedicated section */
__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_requests_start_marker[4] = {0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf,
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9};

/* Base revision - Use 0 to get full physical memory mapping including device MMIO */
__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_base_revision[3] = {0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 0};

/* Framebuffer request */
__attribute__((
    used,
    section(
        ".limine_requests"))) static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                                  0x9d5827dcd881dd75, 0xa3148604f6fab11b},
                           .revision = 0,
                           .response = NULL};

/* RSDP request (ACPI root pointer) */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_rsdp_request
    rsdp_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                           0xc5e77b6b397e7b43, 0x27637845accdcf3c},
                    .revision = 0,
                    .response = NULL};

/* HHDM request (Higher Half Direct Map offset) */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_hhdm_request
    hhdm_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                           0x48dcf1cb8ad2b852, 0x63984e959a98244b},
                    .revision = 0,
                    .response = NULL};

/* Kernel address request (physical/virtual base) */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_kernel_address_request
    kernel_address_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                                     0x71ba76863cc55f63, 0xb2644a48c516a487},
                              .revision = 0,
                              .response = NULL};

/* Kernel file request, used by the installer to copy the current kernel. */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_kernel_file_request
    kernel_file_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                                  0xad97e90e83f1ed67, 0x31eb5d1c5ff23b69},
                           .revision = 0,
                           .response = NULL};

static volatile struct limine_internal_module boot_efi_module = {
    .path = "/EFI/BOOT/BOOTX64.EFI",
    .cmdline = "orizon-bootx64",
    .flags = 0};

static volatile struct limine_internal_module *boot_modules[] = {
    (struct limine_internal_module *)&boot_efi_module};

/* Module request, used by the installer to copy the fallback UEFI loader. */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_module_request
    module_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                             0x3e7e279702be32af, 0xca1c4f3bd1280cee},
                      .revision = 1,
                      .response = NULL,
                      .internal_module_count = 1,
                      .internal_modules =
                          (struct limine_internal_module **)boot_modules};

/* Global HHDM offset for drivers to use */
uint64_t hhdm_offset = 0xFFFF800000000000ULL;
uint64_t kernel_phys_base = 0;
uint64_t kernel_virt_base = 0;

static const void *loaded_kernel_image = NULL;
static size_t loaded_kernel_image_size = 0;
static const void *loaded_boot_efi_image = NULL;
static size_t loaded_boot_efi_image_size = 0;
static const char *loaded_payload_status = "boot payloads not captured";
static const char *loaded_kernel_cmdline = "";

/* Debug framebuffer access */
uint32_t *g_fb_ptr = NULL;
uint32_t g_fb_width = 0;
uint32_t g_fb_height = 0;
uint32_t g_fb_pitch = 0;

void debug_rect(int x, int y, int w, int h, uint32_t color) {
  if (!g_fb_ptr) return;
  for (int dy = 0; dy < h; dy++) {
    if (y + dy >= (int)g_fb_height) break;
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)g_fb_ptr + (y + dy) * g_fb_pitch);
    for (int dx = 0; dx < w; dx++) {
      if (x + dx >= (int)g_fb_width) break;
      row[x + dx] = color;
    }
  }
}

/* Request end marker */
__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_requests_end_marker[2] = {0xadc0e0531bb10d03, 0x9572709f31764c62};

/* ========== Kernel State ========== */

static struct limine_framebuffer *fb = NULL;

static void capture_boot_payloads(void) {
  loaded_kernel_image = NULL;
  loaded_kernel_image_size = 0;
  loaded_boot_efi_image = NULL;
  loaded_boot_efi_image_size = 0;
  loaded_kernel_cmdline = "";

  if (kernel_file_request.response &&
      kernel_file_request.response->kernel_file &&
      kernel_file_request.response->kernel_file->address &&
      kernel_file_request.response->kernel_file->size > 0) {
    struct limine_file *kernel_file = kernel_file_request.response->kernel_file;
    loaded_kernel_image = kernel_file->address;
    loaded_kernel_image_size = kernel_file->size;
    loaded_kernel_cmdline = kernel_file->cmdline ? kernel_file->cmdline : "";
  }

  if (module_request.response && module_request.response->modules) {
    for (uint64_t i = 0; i < module_request.response->module_count; i++) {
      struct limine_file *file = module_request.response->modules[i];
      if (!file || !file->address || file->size == 0) {
        continue;
      }
      if ((file->cmdline && strstr(file->cmdline, "orizon-bootx64")) ||
          (file->path && strstr(file->path, "BOOTX64.EFI"))) {
        loaded_boot_efi_image = file->address;
        loaded_boot_efi_image_size = file->size;
      }
    }
  }

  if (loaded_kernel_image && loaded_boot_efi_image) {
    loaded_payload_status = "boot payloads ready";
  } else if (!loaded_kernel_image) {
    loaded_payload_status = "kernel payload missing";
  } else {
    loaded_payload_status = "UEFI loader payload missing";
  }
}

const void *boot_kernel_image(void) { return loaded_kernel_image; }
size_t boot_kernel_image_size(void) { return loaded_kernel_image_size; }
const void *boot_efi_image(void) { return loaded_boot_efi_image; }
size_t boot_efi_image_size(void) { return loaded_boot_efi_image_size; }
const char *boot_payload_status(void) { return loaded_payload_status; }
int boot_payloads_ready(void) {
  return loaded_kernel_image && loaded_boot_efi_image &&
         loaded_kernel_image_size > 0 && loaded_boot_efi_image_size > 0;
}
const char *boot_cmdline(void) { return loaded_kernel_cmdline; }
int boot_cmdline_has(const char *needle) {
  return needle && needle[0] && loaded_kernel_cmdline &&
         strstr(loaded_kernel_cmdline, needle) != NULL;
}
void *boot_rsdp_address(void) {
  if (rsdp_request.response && rsdp_request.response->address) {
    return rsdp_request.response->address;
  }
  return NULL;
}
int boot_find_module(const char *needle, const void **address, size_t *size,
                     const char **path, const char **cmdline) {
  if (!needle || !needle[0] || !module_request.response ||
      !module_request.response->modules) {
    return -1;
  }
  for (uint64_t i = 0; i < module_request.response->module_count; i++) {
    struct limine_file *file = module_request.response->modules[i];
    if (!file || !file->address || file->size == 0) {
      continue;
    }
    if ((file->path && strstr(file->path, needle)) ||
        (file->cmdline && strstr(file->cmdline, needle))) {
      if (address) {
        *address = file->address;
      }
      if (size) {
        *size = file->size;
      }
      if (path) {
        *path = file->path ? file->path : "";
      }
      if (cmdline) {
        *cmdline = file->cmdline ? file->cmdline : "";
      }
      return 0;
    }
  }
  return -1;
}

/* ========== Early Serial Debug ========== */

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static void serial_init(void) {
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x80);
  outb(COM1 + 0, 0x03);
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x03);
  outb(COM1 + 2, 0xC7);
  outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
  while ((inb(COM1 + 5) & 0x20) == 0)
    ;
  outb(COM1, c);
}

void serial_puts(const char *s) {
  klog_write_raw(s);
  while (*s) {
    if (*s == '\n')
      serial_putc('\r');
    serial_putc(*s++);
  }
}

void serial_puthex(uint64_t val) {
  const char *hex = "0123456789ABCDEF";
  char logged[17];
  int pos = 0;
  serial_puts("0x");
  for (int i = 60; i >= 0; i -= 4) {
    char c = hex[(val >> i) & 0xF];
    serial_putc(c);
    logged[pos++] = c;
  }
  logged[pos] = '\0';
  klog_write_raw(logged);
}

/* Parse EDID to get physical size (mm). Returns 0 on success. */
static int edid_get_mm(const uint8_t *edid, uint64_t size,
                       int *out_mm_w, int *out_mm_h) {
  if (!edid || size < 128 || !out_mm_w || !out_mm_h) {
    return -1;
  }
  /* EDID header: 00 FF FF FF FF FF FF 00 */
  if (!(edid[0] == 0x00 && edid[1] == 0xFF && edid[2] == 0xFF &&
        edid[3] == 0xFF && edid[4] == 0xFF && edid[5] == 0xFF &&
        edid[6] == 0xFF && edid[7] == 0x00)) {
    return -1;
  }
  /* Bytes 21/22: horizontal/vertical size in cm */
  int cm_w = edid[21];
  int cm_h = edid[22];
  if (cm_w == 0 || cm_h == 0) {
    return -1;
  }
  *out_mm_w = cm_w * 10;
  *out_mm_h = cm_h * 10;
  return 0;
}

/* ========== Simple Halt ========== */

static void halt(void) {
  for (;;) {
    __asm__ volatile("hlt");
  }
}

/* ========== Direct Screen Test ========== */

static void direct_put_pixel(void *fb_addr, uint64_t pitch, uint16_t bpp,
                             uint64_t x, uint64_t y, uint32_t argb) {
  volatile uint8_t *row = (volatile uint8_t *)fb_addr + y * pitch;

  if (bpp == 32) {
    ((volatile uint32_t *)row)[x] = argb;
  } else if (bpp == 24) {
    volatile uint8_t *p = row + x * 3;
    p[0] = (uint8_t)(argb & 0xFF);
    p[1] = (uint8_t)((argb >> 8) & 0xFF);
    p[2] = (uint8_t)((argb >> 16) & 0xFF);
  } else if (bpp == 16) {
    uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(argb & 0xFF);
    ((volatile uint16_t *)row)[x] =
        (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
}

static void direct_fill_rect(void *fb_addr, uint64_t width, uint64_t height,
                             uint64_t pitch, uint16_t bpp, uint64_t x,
                             uint64_t y, uint64_t w, uint64_t h,
                             uint32_t argb) {
  if (!fb_addr || bpp < 16) {
    return;
  }
  if (x >= width || y >= height) {
    return;
  }
  if (x + w > width) {
    w = width - x;
  }
  if (y + h > height) {
    h = height - y;
  }
  for (uint64_t py = y; py < y + h; py++) {
    for (uint64_t px = x; px < x + w; px++) {
      direct_put_pixel(fb_addr, pitch, bpp, px, py, argb);
    }
  }
}

static void direct_boot_stage(uint32_t stage, uint32_t color) {
  if (!fb) {
    return;
  }
  direct_fill_rect(fb->address, fb->width, fb->height, fb->pitch, fb->bpp, 0, 0,
                   fb->width, fb->height, 0xFF07101E);
  direct_fill_rect(fb->address, fb->width, fb->height, fb->pitch, fb->bpp, 0, 0,
                   fb->width, 28, 0xFF101A2A);
  direct_fill_rect(fb->address, fb->width, fb->height, fb->pitch, fb->bpp, 0,
                   28, fb->width, 2, color);
  for (uint32_t i = 0; i < stage && i < 12; i++) {
    direct_fill_rect(fb->address, fb->width, fb->height, fb->pitch, fb->bpp,
                     24 + i * 28, 48, 18, 90, color);
  }
  direct_fill_rect(fb->address, fb->width, fb->height, fb->pitch, fb->bpp,
                   fb->width / 2 - 120, fb->height / 2 - 70, 240, 140,
                   0xFF121B2D);
  direct_fill_rect(fb->address, fb->width, fb->height, fb->pitch, fb->bpp,
                   fb->width / 2 - 120, fb->height / 2 - 70, 8, 140, color);
}

static void boot_draw_text_stage(const char *stage) {
  if (!backbuffer) {
    return;
  }
  fb_fill_gradient_v(0, 0, (int)screen_width, (int)screen_height,
                     MAKE_COLOR(7, 13, 26), MAKE_COLOR(17, 25, 39));
  font_draw_string(32, 36, "Orizon OS early boot", COLOR_WHITE);
  font_draw_string(32, 68, stage ? stage : "Starting kernel services",
                   MAKE_COLOR(148, 210, 255));
  font_draw_string(32, 104,
                   "If this screen freezes, tell Codex the last visible line.",
                   MAKE_COLOR(176, 188, 204));
  fb_swap_buffers();
}

/* Draw directly to framebuffer without relying on the GUI backbuffer. */
static void direct_screen_test(void *fb_addr, uint64_t width, uint64_t height,
                               uint64_t pitch, uint16_t bpp) {
  serial_puts("Drawing test pattern...\n");

  for (uint64_t y = 0; y < height; y++) {
    for (uint64_t x = 0; x < width; x++) {
      uint8_t r = 50;
      uint8_t g = (y * 50 / height) + 20;
      uint8_t b = 100 + (y * 100 / height);
      direct_put_pixel(fb_addr, pitch, bpp, x, y,
                       0xFF000000 | (r << 16) | (g << 8) | b);
    }
  }

  serial_puts("Test pattern complete!\n");

  direct_fill_rect(fb_addr, width, height, pitch, bpp, width / 2 - 100,
                   height / 2 - 50, 200, 100, 0xFFFFFFFF);
  direct_fill_rect(fb_addr, width, height, pitch, bpp, width / 2 - 80,
                   height / 2 - 20, 160, 18, 0xFF00E5FF);
}

/* ========== Kernel Main ========== */

void _start(void) {
  /* Initialize serial for debug output */
  serial_init();
  serial_puts("\n\n=== Orizon OS core-x86_64 ===\n");
  serial_puts("Kernel entry point reached!\n");

  /* Put pixels on screen before ACPI/timers/storage/USB can block the boot. */
  if (framebuffer_request.response == NULL ||
      framebuffer_request.response->framebuffer_count < 1) {
    serial_puts("ERROR: No framebuffer available before core init!\n");
    halt();
  }

  fb = framebuffer_request.response->framebuffers[0];
  g_fb_ptr = (uint32_t *)fb->address;
  g_fb_width = (uint32_t)fb->width;
  g_fb_height = (uint32_t)fb->height;
  g_fb_pitch = (uint32_t)fb->pitch;
  direct_boot_stage(1, 0xFF2B8CFF);

  /* Verify base revision was accepted */
  if (limine_base_revision[2] != 0) {
    direct_boot_stage(1, 0xFFFF3232);
    serial_puts("ERROR: Limine base revision mismatch\n");
    serial_puts("Revision value: ");
    serial_puthex(limine_base_revision[2]);
    serial_puts("\n");
    halt();
  }
  serial_puts("Limine base revision OK\n");

  /* Get HHDM offset for physical memory access */
  if (hhdm_request.response) {
    hhdm_offset = hhdm_request.response->offset;
    serial_puts("HHDM offset: ");
    serial_puthex(hhdm_offset);
    serial_puts("\n");
  }

  /* Get kernel physical/virtual base */
  if (kernel_address_request.response) {
    kernel_phys_base = kernel_address_request.response->physical_base;
    kernel_virt_base = kernel_address_request.response->virtual_base;
    serial_puts("Kernel phys base: ");
    serial_puthex(kernel_phys_base);
    serial_puts("\nKernel virt base: ");
    serial_puthex(kernel_virt_base);
    serial_puts("\n");
  }

  capture_boot_payloads();
  serial_puts("Installer payload status: ");
  serial_puts(boot_payload_status());
  serial_puts("\n");

  serial_puts("Framebuffer acquired:\n");
  serial_puts("  Address: ");
  serial_puthex((uint64_t)fb->address);
  serial_puts("\n  Width: ");
  serial_puthex(fb->width);
  serial_puts("\n  Height: ");
  serial_puthex(fb->height);
  serial_puts("\n  Pitch: ");
  serial_puthex(fb->pitch);
  serial_puts("\n  BPP: ");
  serial_puthex(fb->bpp);
  serial_puts("\n");

  /* Try to read physical display size from EDID */
  if (fb->edid && fb->edid_size >= 128) {
    int mm_w = 0, mm_h = 0;
    if (edid_get_mm((const uint8_t *)fb->edid, fb->edid_size, &mm_w, &mm_h) ==
        0) {
      screen_mm_width = mm_w;
      screen_mm_height = mm_h;
      serial_puts("  EDID size (mm): ");
      serial_puthex((uint64_t)mm_w);
      serial_puts(" x ");
      serial_puthex((uint64_t)mm_h);
      serial_puts("\n");
    }
  }

  /* First: Direct screen test to verify framebuffer works */
  serial_puts("Starting direct framebuffer test...\n");
  direct_screen_test(fb->address, fb->width, fb->height, fb->pitch, fb->bpp);

  /* Visual checkpoint: RED = starting fb_init */
  direct_boot_stage(2, 0xFFFF4040);
  {
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)fb->address + 10 * fb->pitch);
    if (fb->bpp == 32) {
      for (int i = 0; i < 50; i++) row[10 + i] = 0xFFFF0000;
    }
  }

  serial_puts("Initializing framebuffer...\n");
  fb_init(fb->address, fb->width, fb->height, fb->pitch);
  font_init();
  boot_draw_text_stage("Framebuffer online. Installing interrupt table...");

  idt_init();
  pic_init();
  sched_init();
  boot_draw_text_stage("Interrupt table ready. Starting Orizon shell...");

  serial_puts("Initializing GUI...\n");
  gui_init();

  boot_draw_text_stage("Shell created. Enabling interrupts...");

  /* Enable interrupts globally - REQUIRED for USB/PS2 to work! */
  serial_puts("Enabling interrupts...\n");
  __asm__ volatile("sti");

  serial_puts("Starting main loop...\n");

  /* Main rendering loop */
  gui_main_loop();

  /* Should never reach here */
  halt();
}
