/*
 * Orizon compositor backend API.
 *
 * This is the internal seam between the Hyprland-style compositor model and
 * the concrete renderer. The VM build currently binds it to the framebuffer
 * backend; a future Wayland/wlroots backend should implement this contract
 * instead of reaching into compositor state directly.
 */

#ifndef _COMPOSITOR_BACKEND_H
#define _COMPOSITOR_BACKEND_H

#include "types.h"

typedef struct {
  const char *api;
  const char *name;
  const char *kind;
  const char *renderer;
  const char *protocol;
  int implemented;
  int vm_ready;
  int wayland;
  int wlroots;
  int manual_drag;
  int external_clients;
  uint32_t (*width)(void);
  uint32_t (*height)(void);
  uint32_t (*pitch)(void);
  void (*put_pixel)(int x, int y, color_t color);
  void (*fill_rect)(int x, int y, int width, int height, color_t color);
  void (*draw_rect)(int x, int y, int width, int height, color_t color);
  void (*fill_rect_alpha)(int x, int y, int width, int height, color_t color);
  void (*fill_gradient_v)(int x, int y, int width, int height, color_t c1,
                          color_t c2);
  void (*present)(void);
} orizon_compositor_backend_t;

const orizon_compositor_backend_t *orizon_compositor_backend_current(void);
const char *orizon_compositor_backend_api(void);
const char *orizon_compositor_backend_name(void);
uint32_t orizon_compositor_backend_width(void);
uint32_t orizon_compositor_backend_height(void);
uint32_t orizon_compositor_backend_pitch(void);
void orizon_compositor_backend_put_pixel(int x, int y, color_t color);
void orizon_compositor_backend_fill_rect(int x, int y, int width, int height,
                                         color_t color);
void orizon_compositor_backend_draw_rect(int x, int y, int width, int height,
                                         color_t color);
void orizon_compositor_backend_fill_rect_alpha(int x, int y, int width,
                                               int height, color_t color);
void orizon_compositor_backend_fill_gradient_v(int x, int y, int width,
                                               int height, color_t c1,
                                               color_t c2);
void orizon_compositor_backend_present(void);

#endif /* _COMPOSITOR_BACKEND_H */
