/*
 * Orizon compositor backend API implementation.
 *
 * The active implementation is intentionally the VM framebuffer backend. This
 * file gives the compositor a stable internal API so a future Wayland/wlroots
 * backend can be prepared without pretending it exists today.
 */

#include "../include/compositor_backend.h"
#include "../include/gui.h"

static uint32_t framebuffer_backend_width(void) { return screen_width; }

static uint32_t framebuffer_backend_height(void) { return screen_height; }

static uint32_t framebuffer_backend_pitch(void) { return screen_pitch; }

static const orizon_compositor_backend_t framebuffer_backend = {
    "compositor-backend-v0",
    "framebuffer-vm",
    "software-framebuffer",
    "software-backbuffer",
    "orizon-desktop-ipc-v0",
    1,
    1,
    0,
    0,
    0,
    0,
    framebuffer_backend_width,
    framebuffer_backend_height,
    framebuffer_backend_pitch,
    fb_put_pixel,
    fb_fill_rect,
    fb_draw_rect,
    fb_fill_rect_alpha,
    fb_fill_gradient_v,
    fb_swap_buffers};

const orizon_compositor_backend_t *orizon_compositor_backend_current(void) {
  return &framebuffer_backend;
}

const char *orizon_compositor_backend_api(void) {
  return framebuffer_backend.api;
}

const char *orizon_compositor_backend_name(void) {
  return framebuffer_backend.name;
}

uint32_t orizon_compositor_backend_width(void) {
  return framebuffer_backend.width ? framebuffer_backend.width() : 0;
}

uint32_t orizon_compositor_backend_height(void) {
  return framebuffer_backend.height ? framebuffer_backend.height() : 0;
}

uint32_t orizon_compositor_backend_pitch(void) {
  return framebuffer_backend.pitch ? framebuffer_backend.pitch() : 0;
}

void orizon_compositor_backend_put_pixel(int x, int y, color_t color) {
  if (framebuffer_backend.put_pixel) {
    framebuffer_backend.put_pixel(x, y, color);
  }
}

void orizon_compositor_backend_fill_rect(int x, int y, int width, int height,
                                         color_t color) {
  if (framebuffer_backend.fill_rect) {
    framebuffer_backend.fill_rect(x, y, width, height, color);
  }
}

void orizon_compositor_backend_draw_rect(int x, int y, int width, int height,
                                         color_t color) {
  if (framebuffer_backend.draw_rect) {
    framebuffer_backend.draw_rect(x, y, width, height, color);
  }
}

void orizon_compositor_backend_fill_rect_alpha(int x, int y, int width,
                                               int height, color_t color) {
  if (framebuffer_backend.fill_rect_alpha) {
    framebuffer_backend.fill_rect_alpha(x, y, width, height, color);
  }
}

void orizon_compositor_backend_fill_gradient_v(int x, int y, int width,
                                               int height, color_t c1,
                                               color_t c2) {
  if (framebuffer_backend.fill_gradient_v) {
    framebuffer_backend.fill_gradient_v(x, y, width, height, c1, c2);
  }
}

void orizon_compositor_backend_present(void) {
  if (framebuffer_backend.present) {
    framebuffer_backend.present();
  }
}
