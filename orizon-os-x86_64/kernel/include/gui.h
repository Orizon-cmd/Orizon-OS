/*
 * Orizon OS x86_64 - GUI System Header
 */

#ifndef _GUI_H
#define _GUI_H

#include "types.h"

/* ========== Constants ========== */
#define ICON_SIZE 56
#define MAX_WINDOWS 16
#define MAX_TITLE_LEN 64
#define TITLE_BAR_HEIGHT 28
#define BUTTON_SIZE 12
#define BUTTON_MARGIN 12
#define BUTTON_SPACING 8

/* ========== Window State ========== */
typedef enum {
  WINDOW_NORMAL,
  WINDOW_MINIMIZED,
  WINDOW_MAXIMIZED
} window_state_t;

/* Forward declaration */
struct window_t;

/* ========== Window Type ========== */
typedef struct window_t {
  uint32_t id;
  char title[MAX_TITLE_LEN];
  int x, y;
  int width, height;
  bool visible;
  bool focused;
  window_state_t state;
  void (*on_draw)(struct window_t *win);
} window_t;

/* Screen dimensions (set by framebuffer) */
extern uint32_t screen_width;
extern uint32_t screen_height;
extern uint32_t screen_pitch;
extern uint32_t *framebuffer;
extern uint32_t *backbuffer;
/* Physical display size in millimeters (from EDID, if available) */
extern int screen_mm_width;
extern int screen_mm_height;

/* UI scale factor (1x, 2x, 3x) */
extern int ui_scale;

/* ========== Framebuffer Functions ========== */
void fb_init(void *fb_addr, uint32_t width, uint32_t height, uint32_t pitch);
void fb_put_pixel(int x, int y, color_t color);
void fb_fill_rect(int x, int y, int width, int height, color_t color);
void fb_draw_rect(int x, int y, int width, int height, color_t color);
void fb_swap_buffers(void);
void fb_fill_rect_alpha(int x, int y, int width, int height, color_t color);
void fb_fill_gradient_v(int x, int y, int width, int height, color_t c1,
                        color_t c2);

/* ========== Font Functions ========== */
void font_init(void);
void font_draw_string(int x, int y, const char *str, color_t color);
int font_string_width(const char *str);

/* ========== GUI Functions ========== */
typedef struct {
  int clients;
  int mapped;
  int hidden;
  int workspace;
  int active_workspace;
  int focused;
  int focused_client_id;
  uint32_t focused_address;
  int pinned;
  int fullscreen;
  int pseudo;
  int urgent;
  int overlay_visible;
  char focused_title[48];
  char focused_app_id[32];
} orizon_desktop_app_runtime_t;

void gui_init(void);
void gui_compose(void);
void gui_main_loop(void);
int gui_timer_irq_active(void);
int gui_timer_fallback_active(void);
void gui_desktop_set_enabled(int enabled);
int gui_desktop_enabled(void);
void gui_desktop_open_terminal(void);
void gui_desktop_close_terminal(void);
void gui_desktop_toggle_terminal(void);
int gui_desktop_terminal_visible(void);
void gui_desktop_show_launcher(void);
void gui_desktop_hide_launcher(void);
void gui_desktop_toggle_launcher(void);
int gui_desktop_launcher_visible(void);
void gui_desktop_reload_session(void);
int gui_desktop_switch_workspace(int workspace);
int gui_desktop_move_terminal_to_workspace(int workspace);
int gui_desktop_spawn_terminal_client(void);
int gui_desktop_spawn_app_client(const char *app, char *out, size_t out_size);
int gui_desktop_get_app_runtime(const char *app_id,
                                orizon_desktop_app_runtime_t *runtime);
int gui_desktop_close_active_client(void);
int gui_desktop_focus_next_client(void);
int gui_desktop_focus_prev_client(void);
int gui_desktop_dispatch(const char *dispatcher, const char *args, char *out,
                         size_t out_size);
int gui_desktop_dispatch_json(const char *dispatcher, const char *args,
                              char *out, size_t out_size);
void gui_desktop_format_workspaces(char *out, size_t out_size);
void gui_desktop_format_windows(char *out, size_t out_size);
void gui_desktop_format_clients_json(char *out, size_t out_size);
void gui_desktop_format_workspaces_json(char *out, size_t out_size);
void gui_desktop_format_activewindow_json(char *out, size_t out_size);
void gui_desktop_format_activeworkspace_json(char *out, size_t out_size);
void gui_desktop_format_focus_history_json(char *out, size_t out_size);
void gui_desktop_format_workspace_stack_json(char *out, size_t out_size);
void gui_desktop_format_client_model_json(char *out, size_t out_size);
void gui_desktop_format_rule_matches_json(char *out, size_t out_size);
void gui_desktop_format_layout_state_json(char *out, size_t out_size);
void gui_desktop_format_layout_tree_json(char *out, size_t out_size);
void gui_desktop_format_client_model(char *out, size_t out_size);
void gui_desktop_format_rule_matches(char *out, size_t out_size);
void gui_desktop_format_activewindow(char *out, size_t out_size);
void gui_desktop_format_activeworkspace(char *out, size_t out_size);
void gui_desktop_format_focus_history(char *out, size_t out_size);
void gui_desktop_format_workspace_stack(char *out, size_t out_size);
void gui_desktop_format_monitors(char *out, size_t out_size);
void gui_desktop_format_monitors_json(char *out, size_t out_size);
void gui_desktop_format_layers(char *out, size_t out_size);
void gui_desktop_format_layers_json(char *out, size_t out_size);
void gui_desktop_format_binds(char *out, size_t out_size);
void gui_desktop_format_binds_json(char *out, size_t out_size);
void gui_desktop_format_layouts(char *out, size_t out_size);
void gui_desktop_format_layouts_json(char *out, size_t out_size);
void gui_desktop_format_layout_state(char *out, size_t out_size);
void gui_desktop_format_layout_tree(char *out, size_t out_size);
void gui_desktop_format_animations(char *out, size_t out_size);
void gui_desktop_format_animations_json(char *out, size_t out_size);
void gui_desktop_format_decorations(char *out, size_t out_size);
void gui_desktop_format_decorations_json(char *out, size_t out_size);
void gui_desktop_format_render(char *out, size_t out_size);
void gui_desktop_format_render_json(char *out, size_t out_size);
void gui_desktop_format_descriptions(char *out, size_t out_size);
void gui_desktop_format_descriptions_json(char *out, size_t out_size);
void gui_desktop_format_instances(char *out, size_t out_size);
void gui_desktop_format_instances_json(char *out, size_t out_size);
void gui_desktop_format_submap(char *out, size_t out_size);
void gui_desktop_format_submap_json(char *out, size_t out_size);
void gui_desktop_format_status(char *out, size_t out_size);
void gui_desktop_format_pointer(char *out, size_t out_size);
void gui_desktop_format_devices(char *out, size_t out_size);
void gui_desktop_format_devices_json(char *out, size_t out_size);
void gui_desktop_format_keymap(char *out, size_t out_size);
void gui_desktop_format_keymap_json(char *out, size_t out_size);
void gui_desktop_format_systeminfo(char *out, size_t out_size);
void gui_desktop_format_systeminfo_json(char *out, size_t out_size);
void gui_desktop_format_hyprctl_version(char *out, size_t out_size);
void gui_desktop_format_hyprctl_version_json(char *out, size_t out_size);
void gui_desktop_format_cursorpos(char *out, size_t out_size);
void gui_desktop_format_cursorpos_json(char *out, size_t out_size);
void gui_desktop_format_splash(char *out, size_t out_size);
void gui_desktop_format_splash_json(char *out, size_t out_size);

/* Direct framebuffer access for low-level debug */
extern uint32_t *g_fb_ptr;
extern uint32_t g_fb_width;
extern uint32_t g_fb_height;
extern uint32_t g_fb_pitch;

void debug_rect(int x, int y, int w, int h, uint32_t color);
void serial_puts(const char *s);
void serial_puthex(uint64_t val);

#endif /* _GUI_H */
