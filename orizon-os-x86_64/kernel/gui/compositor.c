/*
 * Orizon OS x86_64 - Minimal Development Compositor
 *
 * This shell keeps the x86_64 VM stable and focused on development:
 * one console, one clean frame, and one deliberate core workspace.
 */

#include "../include/gui.h"
#include "../include/acpi.h"
#include "../include/bootinfo.h"
#include "../include/desktop.h"
#include "../include/i2c_hid.h"
#include "../include/input_layout.h"
#include "../include/klog.h"
#include "../include/net.h"
#include "../include/packages.h"
#include "../include/power.h"
#include "../include/ps2.h"
#include "../include/sched.h"
#include "../include/ssh.h"
#include "../include/string.h"
#include "../include/system_state.h"
#include "../include/terminal.h"
#include "../include/timer.h"
#include "../include/update.h"
#include "../include/usb.h"
#include "../include/vfs.h"
#include "../include/wifi.h"

extern void serial_puts(const char *s);

#define TOP_BAR_HEIGHT 30
#define FOOTER_HEIGHT 28
#define PANEL_PADDING 12
#define PANEL_TITLE_HEIGHT 30
#define TERM_CONTENT_WIDTH (TERM_COLS * TERM_CHAR_W + TERM_PADDING * 2)
#define TERM_CONTENT_HEIGHT (TERM_ROWS * TERM_CHAR_H + TERM_PADDING * 2)
#define SHELL_WIDTH (TERM_CONTENT_WIDTH + PANEL_PADDING * 2)
#define SHELL_HEIGHT (PANEL_TITLE_HEIGHT + TERM_CONTENT_HEIGHT + PANEL_PADDING * 2)
#define SPLASH_TICKS 180
#define DESKTOP_ANIMATION_TICKS 18
#define TIMER_BOOT_FALLBACK_LOOPS 8000
#define TIMER_FALLBACK_IDLE_PAUSES 20000
#define DESKTOP_MAX_CLIENTS 8
#define DESKTOP_MAX_WORKSPACES 10
#define DESKTOP_CLIENT_ADDRESS_BASE 0x100000u

#define COLOR_BG_TOP MAKE_COLOR(10, 14, 24)
#define COLOR_BG_BOTTOM MAKE_COLOR(22, 28, 42)
#define COLOR_PANEL MAKE_COLOR(18, 23, 34)
#define COLOR_PANEL_EDGE MAKE_COLOR(52, 68, 94)
#define COLOR_PANEL_ACCENT MAKE_COLOR(84, 158, 255)
#define COLOR_TEXT_PRIMARY MAKE_COLOR(240, 244, 252)
#define COLOR_TEXT_SECONDARY MAKE_COLOR(156, 171, 196)
#define COLOR_TEXT_MUTED MAKE_COLOR(104, 119, 144)
#define COLOR_CURSOR_BORDER MAKE_COLOR(12, 16, 28)

int ui_scale = 1;

static terminal_t *main_terminal = NULL;
static int shell_x = 0;
static int shell_y = 0;
static int term_x = 0;
static int term_y = 0;
static int mouse_x = 0;
static int mouse_y = 0;
static int prev_buttons = 0;
static int needs_redraw = 1;
static int desktop_mode_enabled = 0;
static int desktop_terminal_visible = 1;
static int desktop_launcher_visible = 0;
static orizon_desktop_session_t desktop_session = {
    "graphite", "aurora", "dwindle", 1, 1, 1, 0};
static orizon_desktop_settings_t desktop_settings = {
    1, 6, 12, 2, 8, 1, 1, 1, 18, 18, 0, 0, "orizon-terminal",
    "builtin", "top", "us", "flat", "orizon-pop", "balanced"};
static int desktop_workspace_count = DESKTOP_MAX_WORKSPACES;
static int desktop_active_workspace = 1;
static int desktop_previous_workspace = 1;
static int desktop_terminal_workspace = 1;
static int desktop_layout_mode = 0; /* 0=dwindle, 1=master, 2=monocle */
static int desktop_split_mode = 0; /* 0=auto, 1=vertical, 2=horizontal */
static int desktop_split_ratio_percent = 50;
static int desktop_master_ratio_percent = 58;
static int desktop_master_count = 1;
static char desktop_loaded_layout_default[32] = "";
static char desktop_submap[32] = "default";
static int desktop_last_key = 0;
static uint64_t desktop_key_serial = 0;
static uint64_t desktop_pointer_focus_changes = 0;
static int desktop_animation_ticks_remaining = 0;
static int desktop_transition_from_workspace = 1;
static int desktop_transition_to_workspace = 1;
static const char *desktop_transition_reason = "initial";
static uint64_t desktop_render_serial = 1;
static uint64_t desktop_workspace_serial = 1;
static uint64_t desktop_layout_serial = 1;
typedef struct {
  int id;
  int workspace;
  int last_workspace;
  int visible;
  int mapped;
  int hidden;
  int terminal_backed;
  int fullscreen;
  int pseudo;
  int pinned;
  int focus_history_id;
  uint64_t mapped_generation;
  uint64_t focus_generation;
  char title[48];
  char app_id[32];
  int rule_match_count;
  int rule_apply_count;
  char rule_actions[80];
} desktop_client_t;

typedef struct {
  int used;
  int layout_initialized;
  int layout_mode;
  int split_mode;
  int split_ratio_percent;
  int master_ratio_percent;
  int master_count;
  int last_focused_client_id;
  uint64_t focus_generation;
  uint64_t visit_generation;
  uint64_t layout_generation;
  char name[16];
} desktop_workspace_t;

static desktop_client_t desktop_clients[DESKTOP_MAX_CLIENTS];
static desktop_workspace_t desktop_workspaces[DESKTOP_MAX_WORKSPACES];
static int desktop_next_client_id = 1;
static int desktop_focused_client_id = 0;
static int desktop_focus_history[DESKTOP_MAX_CLIENTS];
static uint64_t desktop_client_serial = 1;
static uint64_t desktop_focus_serial = 1;
static int splash_ticks_remaining = SPLASH_TICKS;
static int timer_irq_seen = 0;
static int timer_fallback_polling = 0;
static uint64_t gui_loop_count = 0;
static int core_services_done = 0;
static int ps2_ready = 0;
static int usb_ready = 0;
static int net_ready = 0;
static int wifi_ready = 0;
static int i2c_hid_ready = 0;
static int i2c_hid_deferred_probe = 0;
static const char *boot_stage_hint = "Starting Orizon shell";

static int desktop_submap_is_default(void);
static void desktop_apply_spawn_rules_to_client(int idx);
static void desktop_load_layout_state_for_workspace(int workspace);
static void desktop_save_active_layout_state(void);

static void draw_circle(int cx, int cy, int radius, color_t color) {
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        fb_put_pixel(cx + x, cy + y, color);
      }
    }
  }
}

static void draw_shadow_panel(int x, int y, int width, int height) {
  int range = desktop_mode_enabled ? desktop_settings.shadow_range : 18;
  int passes;

  if (range <= 0) {
    return;
  }
  if (range > 32) {
    range = 32;
  }
  passes = range / 3;
  if (passes < 3) {
    passes = 3;
  }
  if (passes > 10) {
    passes = 10;
  }
  for (int i = 1; i <= passes; i++) {
    int alpha = 16 + range - i * 2;
    if (alpha < 4) {
      alpha = 4;
    }
    if (alpha > 44) {
      alpha = 44;
    }
    color_t shadow = MAKE_ARGB(alpha, 0, 0, 0);
    fb_fill_rect_alpha(x + i, y + height + i - 2, width, 3, shadow);
    fb_fill_rect_alpha(x + width + i - 2, y + i, 3, height, shadow);
  }
}

static int desktop_animation_tick_budget(void) {
  int ticks = desktop_settings.animation_ticks;

  if (ticks < 4) {
    return 4;
  }
  if (ticks > 60) {
    return 60;
  }
  return ticks;
}

static color_t desktop_theme_accent(void) {
  if (desktop_mode_enabled) {
    if (strcmp(desktop_session.theme, "moss") == 0) {
      return MAKE_COLOR(88, 190, 132);
    }
    if (strcmp(desktop_session.theme, "ember") == 0) {
      return MAKE_COLOR(242, 132, 74);
    }
    if (strcmp(desktop_session.theme, "frost") == 0) {
      return MAKE_COLOR(98, 210, 232);
    }
  }
  return COLOR_PANEL_ACCENT;
}

static void draw_centered_string(int y, const char *text, color_t color) {
  int x = ((int)screen_width - font_string_width(text)) / 2;
  font_draw_string(x, y, text, color);
}

static void draw_background(void) {
  color_t top = COLOR_BG_TOP;
  color_t bottom = COLOR_BG_BOTTOM;
  color_t accent = desktop_theme_accent();

  if (desktop_mode_enabled) {
    if (strcmp(desktop_session.wallpaper, "dawn") == 0) {
      top = MAKE_COLOR(35, 27, 24);
      bottom = MAKE_COLOR(74, 54, 45);
    } else if (strcmp(desktop_session.wallpaper, "noir") == 0) {
      top = MAKE_COLOR(4, 7, 12);
      bottom = MAKE_COLOR(14, 18, 26);
    } else if (strcmp(desktop_session.wallpaper, "moss") == 0) {
      top = MAKE_COLOR(10, 24, 20);
      bottom = MAKE_COLOR(22, 48, 38);
    }
  }

  fb_fill_gradient_v(0, 0, (int)screen_width, (int)screen_height, top,
                     bottom);

  /* Two large soft accents keep the shell feeling intentional without
     adding extra UI machinery. */
  fb_fill_rect_alpha((int)screen_width - 260, 70, 220, 220,
                     MAKE_ARGB(20, 76, 130, 220));
  fb_fill_rect_alpha(48, (int)screen_height - 220, 180, 140,
                     MAKE_ARGB(16, 42, 92, 168));
  fb_fill_rect_alpha(0, TOP_BAR_HEIGHT, (int)screen_width, 1,
                     MAKE_ARGB(60, (accent >> 16) & 0xff,
                               (accent >> 8) & 0xff, accent & 0xff));
}

static void draw_top_bar(void) {
  char resolution[64];

  fb_fill_rect_alpha(0, 0, (int)screen_width, TOP_BAR_HEIGHT,
                     MAKE_ARGB(212, 8, 12, 20));
  font_draw_string(18, 8, "Orizon OS", COLOR_TEXT_PRIMARY);
  if (desktop_mode_enabled) {
    char title[128];
    snprintf(title, sizeof(title), "Hyprland-style Desktop | %s/%s",
             desktop_session.theme, desktop_session.wallpaper);
    font_draw_string(110, 8, title, COLOR_TEXT_SECONDARY);
  } else {
    font_draw_string(110, 8, "Core Development Base", COLOR_TEXT_SECONDARY);
  }

  snprintf(resolution, sizeof(resolution), "%lux%lu x86_64",
           (unsigned long)screen_width, (unsigned long)screen_height);
  font_draw_string((int)screen_width - font_string_width(resolution) - 18, 8,
                   resolution, COLOR_TEXT_MUTED);
}

static void draw_footer(void) {
  const char *hint =
      timer_fallback_polling
          ? "Timer IRQ fallback active. Boot continues in polling mode; APIC timer support is next."
          : "Core development profile active. Console, workspace and low-level tools are ready.";
  int y = (int)screen_height - FOOTER_HEIGHT;

  if (i2c_hid_deferred_probe == 1) {
    hint = "Lenovo I2C-HID probe selected. Boot UI is visible first; driver probe runs after startup.";
  } else if (desktop_mode_enabled && core_services_done) {
    if (!desktop_submap_is_default()) {
      hint = "Desktop submap active. F9 resize, F10 move, F11 launch, F12/Esc default.";
    } else {
      hint = desktop_terminal_visible
                 ? "Desktop active. F2 kill, F4 full, F5 pseudo, F6 focus, F9/F10/F11 submaps."
                 : "Desktop active. Press F1, t or Enter to spawn a tiled terminal.";
    }
  } else if (boot_stage_hint && boot_stage_hint[0]) {
    hint = boot_stage_hint;
  } else if (boot_cmdline_has("orizon.safe=1")) {
    hint = "Safe laptop boot active. Risky hardware probes are disabled for this boot.";
  }

  fb_fill_rect_alpha(0, y, (int)screen_width, FOOTER_HEIGHT,
                     MAKE_ARGB(200, 8, 12, 20));
  font_draw_string(18, y + 6, hint, COLOR_TEXT_SECONDARY);
}

static int desktop_clamp_workspace(int workspace) {
  if (workspace < 1) {
    return 1;
  }
  if (workspace > desktop_workspace_count) {
    return desktop_workspace_count;
  }
  return workspace;
}

static int desktop_wrap_workspace(int workspace) {
  while (workspace < 1) {
    workspace += desktop_workspace_count;
  }
  while (workspace > desktop_workspace_count) {
    workspace -= desktop_workspace_count;
  }
  return workspace;
}

static void desktop_workspace_mark_used(int workspace) {
  int idx = workspace - 1;

  if (workspace < 1 || workspace > DESKTOP_MAX_WORKSPACES) {
    return;
  }
  desktop_workspaces[idx].used = 1;
  if (!desktop_workspaces[idx].name[0]) {
    snprintf(desktop_workspaces[idx].name,
             sizeof(desktop_workspaces[idx].name), "%d", workspace);
  }
}

static void desktop_workspace_mark_visited(int workspace) {
  int idx = workspace - 1;

  if (workspace < 1 || workspace > DESKTOP_MAX_WORKSPACES) {
    return;
  }
  desktop_workspace_mark_used(workspace);
  desktop_workspaces[idx].visit_generation = desktop_workspace_serial++;
}

static const char *desktop_workspace_name(int workspace) {
  static char fallback[16];
  int idx = workspace - 1;

  if (workspace < 1 || workspace > DESKTOP_MAX_WORKSPACES) {
    return "unknown";
  }
  if (desktop_workspaces[idx].name[0]) {
    return desktop_workspaces[idx].name;
  }
  snprintf(fallback, sizeof(fallback), "%d", workspace);
  return fallback;
}

static int desktop_workspace_name_char_safe(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return 1;
  }
  return c == '-' || c == '_' || c == '.';
}

static int desktop_workspace_copy_safe_name(char *out, size_t out_size,
                                            const char *value) {
  size_t len = 0;
  size_t token_len;
  const char *v = value ? value : "";

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  while (*v == ' ') {
    v++;
  }
  while (v[len] && v[len] != ' ') {
    if (!desktop_workspace_name_char_safe(v[len]) || len + 1 >= out_size) {
      return -1;
    }
    len++;
  }
  token_len = len;
  while (v[len] == ' ') {
    len++;
  }
  if (token_len == 0 || v[len] != '\0') {
    return -1;
  }
  for (size_t i = 0; i < token_len; i++) {
    out[i] = v[i];
  }
  out[token_len] = '\0';
  return 0;
}

static int desktop_workspace_find_named(const char *name) {
  if (!name || !name[0]) {
    return -1;
  }
  for (int i = 1; i <= desktop_workspace_count; i++) {
    if (strcmp(desktop_workspace_name(i), name) == 0) {
      return i;
    }
  }
  return -1;
}

static int desktop_workspace_rename(int workspace, const char *name,
                                    char *reason, size_t reason_size) {
  char safe[sizeof(desktop_workspaces[0].name)];
  int existing;

  if (workspace < 1 || workspace > desktop_workspace_count) {
    if (reason && reason_size) {
      snprintf(reason, reason_size, "workspace must be 1-%d",
               desktop_workspace_count);
    }
    return -1;
  }
  if (desktop_workspace_copy_safe_name(safe, sizeof(safe), name) < 0) {
    if (reason && reason_size) {
      snprintf(reason, reason_size,
               "name must be 1-%u chars using A-Z, 0-9, '.', '-' or '_'",
               (unsigned)(sizeof(safe) - 1));
    }
    return -1;
  }
  existing = desktop_workspace_find_named(safe);
  if (existing >= 1 && existing != workspace) {
    if (reason && reason_size) {
      snprintf(reason, reason_size, "name \"%s\" already belongs to workspace %d",
               safe, existing);
    }
    return -1;
  }
  desktop_workspace_mark_used(workspace);
  snprintf(desktop_workspaces[workspace - 1].name,
           sizeof(desktop_workspaces[workspace - 1].name), "%s", safe);
  desktop_workspaces[workspace - 1].layout_generation =
      desktop_layout_serial++;
  if (reason && reason_size) {
    reason[0] = '\0';
  }
  return 0;
}

static int desktop_layout_mode_from_name(const char *name) {
  if (name && strcmp(name, "master") == 0) {
    return 1;
  }
  if (name && strcmp(name, "monocle") == 0) {
    return 2;
  }
  return 0;
}

static const char *desktop_layout_mode_name(int mode) {
  if (mode == 1) {
    return "master";
  }
  if (mode == 2) {
    return "monocle";
  }
  return "dwindle";
}

static const char *desktop_split_mode_name_for_value(int mode) {
  if (mode == 1) {
    return "vertical";
  }
  if (mode == 2) {
    return "horizontal";
  }
  return "auto";
}

static int desktop_workspace_state_index(int workspace) {
  if (workspace < 1 || workspace > DESKTOP_MAX_WORKSPACES) {
    return -1;
  }
  return workspace - 1;
}

static void desktop_workspace_init_layout_state(int workspace) {
  int idx = desktop_workspace_state_index(workspace);

  if (idx < 0 || desktop_workspaces[idx].layout_initialized) {
    return;
  }
  desktop_workspaces[idx].layout_initialized = 1;
  desktop_workspaces[idx].layout_mode =
      desktop_layout_mode_from_name(desktop_session.layout);
  desktop_workspaces[idx].split_mode = 0;
  desktop_workspaces[idx].split_ratio_percent = 50;
  desktop_workspaces[idx].master_ratio_percent = 58;
  desktop_workspaces[idx].master_count = 1;
  desktop_workspaces[idx].layout_generation = desktop_layout_serial++;
}

static void desktop_load_layout_state_for_workspace(int workspace) {
  int idx = desktop_workspace_state_index(workspace);

  if (idx < 0) {
    return;
  }
  desktop_workspace_init_layout_state(workspace);
  desktop_layout_mode = desktop_workspaces[idx].layout_mode;
  desktop_split_mode = desktop_workspaces[idx].split_mode;
  desktop_split_ratio_percent = desktop_workspaces[idx].split_ratio_percent;
  desktop_master_ratio_percent = desktop_workspaces[idx].master_ratio_percent;
  desktop_master_count = desktop_workspaces[idx].master_count;
}

static void desktop_save_active_layout_state(void) {
  int idx = desktop_workspace_state_index(desktop_active_workspace);

  if (idx < 0) {
    return;
  }
  desktop_workspace_init_layout_state(desktop_active_workspace);
  desktop_workspaces[idx].layout_mode = desktop_layout_mode;
  desktop_workspaces[idx].split_mode = desktop_split_mode;
  desktop_workspaces[idx].split_ratio_percent = desktop_split_ratio_percent;
  desktop_workspaces[idx].master_ratio_percent = desktop_master_ratio_percent;
  desktop_workspaces[idx].master_count = desktop_master_count;
  desktop_workspaces[idx].layout_generation = desktop_layout_serial++;
}

static const char *desktop_layout_engine_for_workspace(int workspace) {
  int idx = desktop_workspace_state_index(workspace);

  if (idx < 0) {
    return "dwindle";
  }
  desktop_workspace_init_layout_state(workspace);
  return desktop_layout_mode_name(desktop_workspaces[idx].layout_mode);
}

static int desktop_split_mode_for_workspace(int workspace) {
  int idx = desktop_workspace_state_index(workspace);

  if (idx < 0) {
    return 0;
  }
  desktop_workspace_init_layout_state(workspace);
  return desktop_workspaces[idx].split_mode;
}

static int desktop_split_ratio_for_workspace(int workspace) {
  int idx = desktop_workspace_state_index(workspace);

  if (idx < 0) {
    return 50;
  }
  desktop_workspace_init_layout_state(workspace);
  return desktop_workspaces[idx].split_ratio_percent;
}

static int desktop_master_ratio_for_workspace(int workspace) {
  int idx = desktop_workspace_state_index(workspace);

  if (idx < 0) {
    return 58;
  }
  desktop_workspace_init_layout_state(workspace);
  return desktop_workspaces[idx].master_ratio_percent;
}

static int desktop_master_count_for_workspace(int workspace) {
  int idx = desktop_workspace_state_index(workspace);

  if (idx < 0) {
    return 1;
  }
  desktop_workspace_init_layout_state(workspace);
  return desktop_workspaces[idx].master_count;
}

static int desktop_parse_int_arg(const char *value, int *out) {
  int sign = 1;
  int n = 0;
  int seen = 0;

  if (!value || !out) {
    return -1;
  }
  while (*value == ' ') {
    value++;
  }
  if (*value == '+') {
    value++;
  } else if (*value == '-') {
    sign = -1;
    value++;
  }
  while (*value >= '0' && *value <= '9') {
    seen = 1;
    n = n * 10 + (*value - '0');
    value++;
  }
  if (!seen) {
    return -1;
  }
  *out = n * sign;
  return 0;
}

static int desktop_ascii_lower(int c) {
  if (c >= 'A' && c <= 'Z') {
    return c + ('a' - 'A');
  }
  return c;
}

static int desktop_starts_with_icase(const char *value, const char *prefix) {
  if (!value || !prefix) {
    return 0;
  }
  while (*prefix) {
    if (desktop_ascii_lower((unsigned char)*value) !=
        desktop_ascii_lower((unsigned char)*prefix)) {
      return 0;
    }
    value++;
    prefix++;
  }
  return 1;
}

static int desktop_contains_icase(const char *value, const char *needle) {
  size_t needle_len;

  if (!value || !needle || !needle[0]) {
    return 0;
  }
  needle_len = strlen(needle);
  for (const char *p = value; *p; p++) {
    size_t i = 0;
    while (i < needle_len && p[i] &&
           desktop_ascii_lower((unsigned char)p[i]) ==
               desktop_ascii_lower((unsigned char)needle[i])) {
      i++;
    }
    if (i == needle_len) {
      return 1;
    }
  }
  return 0;
}

static int desktop_parse_bool_state_arg(const char *value, int current,
                                        int *out) {
  char token[24];
  size_t len = 0;

  if (!out) {
    return -1;
  }
  while (value && *value == ' ') {
    value++;
  }
  if (!value || !value[0]) {
    *out = current ? 0 : 1;
    return 0;
  }
  while (value[len] && value[len] != ' ' && len + 1 < sizeof(token)) {
    token[len] = (char)desktop_ascii_lower((unsigned char)value[len]);
    len++;
  }
  token[len] = '\0';
  while (value[len] == ' ') {
    len++;
  }
  if (value[len] != '\0' && strcmp(token, "0") != 0 &&
      strcmp(token, "1") != 0) {
    return -1;
  }
  if (strcmp(token, "toggle") == 0 || strcmp(token, "togglestate") == 0 ||
      strcmp(token, "switch") == 0) {
    *out = current ? 0 : 1;
    return 0;
  }
  if (strcmp(token, "on") == 0 || strcmp(token, "yes") == 0 ||
      strcmp(token, "true") == 0 || strcmp(token, "enable") == 0 ||
      strcmp(token, "enabled") == 0 || strcmp(token, "1") == 0) {
    *out = 1;
    return 0;
  }
  if (strcmp(token, "off") == 0 || strcmp(token, "no") == 0 ||
      strcmp(token, "false") == 0 || strcmp(token, "disable") == 0 ||
      strcmp(token, "disabled") == 0 || strcmp(token, "0") == 0) {
    *out = 0;
    return 0;
  }
  return -1;
}

static int desktop_parse_positive_token(const char *value, int *out) {
  int n = 0;
  int seen = 0;

  if (!value || !out) {
    return -1;
  }
  while (*value == ' ') {
    value++;
  }
  while (*value >= '0' && *value <= '9') {
    seen = 1;
    n = n * 10 + (*value - '0');
    value++;
  }
  while (*value == ' ') {
    value++;
  }
  if (!seen || *value != '\0') {
    return -1;
  }
  *out = n;
  return 0;
}

static int desktop_hex_digit_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + c - 'a';
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + c - 'A';
  }
  return -1;
}

static int desktop_parse_address_token(const char *value, uint32_t *address) {
  uint32_t parsed = 0;
  int seen = 0;

  if (!value || !address) {
    return -1;
  }
  while (*value == ' ') {
    value++;
  }
  if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    value += 2;
  }
  while (*value) {
    int digit = desktop_hex_digit_value(*value);
    if (digit < 0) {
      break;
    }
    seen = 1;
    parsed = (parsed << 4) | (uint32_t)digit;
    value++;
  }
  while (*value == ' ') {
    value++;
  }
  if (!seen || *value != '\0') {
    return -1;
  }
  *address = parsed;
  return 0;
}

static int desktop_workspace_local_client_count(int workspace) {
  int count = 0;

  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible && !desktop_clients[i].pinned &&
        desktop_clients[i].workspace == workspace) {
      count++;
    }
  }
  return count;
}

static int desktop_find_empty_workspace(void) {
  for (int offset = 1; offset <= desktop_workspace_count; offset++) {
    int candidate =
        desktop_wrap_workspace(desktop_active_workspace + offset);
    if (desktop_workspace_local_client_count(candidate) == 0) {
      return candidate;
    }
  }
  return desktop_active_workspace;
}

static void desktop_start_transition(const char *reason, int from_workspace,
                                     int to_workspace) {
  if (!reason || !reason[0]) {
    reason = "redraw";
  }
  if (from_workspace <= 0) {
    from_workspace = desktop_active_workspace;
  }
  if (to_workspace <= 0) {
    to_workspace = desktop_active_workspace;
  }
  desktop_transition_from_workspace = desktop_clamp_workspace(from_workspace);
  desktop_transition_to_workspace = desktop_clamp_workspace(to_workspace);
  desktop_transition_reason = reason;
  desktop_render_serial++;
  desktop_animation_ticks_remaining =
      desktop_settings.animations_enabled ? desktop_animation_tick_budget() : 0;
  needs_redraw = 1;
}

static int desktop_transition_progress_percent(void) {
  int budget = desktop_animation_tick_budget();

  if (!desktop_settings.animations_enabled ||
      desktop_animation_ticks_remaining <= 0) {
    return 100;
  }
  if (desktop_animation_ticks_remaining > budget) {
    return 0;
  }
  return ((budget - desktop_animation_ticks_remaining) * 100) / budget;
}

static const char *desktop_layout_engine(void) {
  return desktop_layout_mode_name(desktop_layout_mode);
}

static const char *desktop_split_mode_name(void) {
  return desktop_split_mode_name_for_value(desktop_split_mode);
}

static int desktop_set_layout_mode(int mode) {
  if (mode < 0 || mode > 2) {
    return -1;
  }
  desktop_layout_mode = mode;
  desktop_save_active_layout_state();
  desktop_start_transition("layout", desktop_active_workspace,
                           desktop_active_workspace);
  return 0;
}

static int desktop_cycle_split_mode(int delta) {
  desktop_split_mode = (desktop_split_mode + delta + 3) % 3;
  desktop_save_active_layout_state();
  desktop_start_transition("layout", desktop_active_workspace,
                           desktop_active_workspace);
  return desktop_split_mode;
}

static int desktop_set_split_mode(int mode) {
  if (mode < 0 || mode > 2) {
    return -1;
  }
  desktop_split_mode = mode;
  desktop_save_active_layout_state();
  desktop_start_transition("layout", desktop_active_workspace,
                           desktop_active_workspace);
  return 0;
}

static int desktop_set_split_ratio(int ratio) {
  if (ratio < 10 || ratio > 90) {
    return -1;
  }
  desktop_split_ratio_percent = ratio;
  desktop_save_active_layout_state();
  desktop_start_transition("layout", desktop_active_workspace,
                           desktop_active_workspace);
  return 0;
}

static int desktop_set_master_ratio(int ratio) {
  if (ratio < 10 || ratio > 90) {
    return -1;
  }
  desktop_master_ratio_percent = ratio;
  desktop_save_active_layout_state();
  desktop_start_transition("layout", desktop_active_workspace,
                           desktop_active_workspace);
  return 0;
}

static int desktop_set_master_count(int count) {
  if (count < 1 || count > DESKTOP_MAX_CLIENTS) {
    return -1;
  }
  desktop_master_count = count;
  desktop_save_active_layout_state();
  desktop_start_transition("layout", desktop_active_workspace,
                           desktop_active_workspace);
  return 0;
}

static int desktop_adjust_split_ratio(int delta) {
  int next = desktop_split_ratio_percent + delta;
  if (next < 10) {
    next = 10;
  }
  if (next > 90) {
    next = 90;
  }
  return desktop_set_split_ratio(next);
}

static int desktop_adjust_master_ratio(int delta) {
  int next = desktop_master_ratio_percent + delta;
  if (next < 10) {
    next = 10;
  }
  if (next > 90) {
    next = 90;
  }
  return desktop_set_master_ratio(next);
}

static int desktop_adjust_master_count(int delta) {
  int next = desktop_master_count + delta;
  if (next < 1) {
    next = 1;
  }
  if (next > DESKTOP_MAX_CLIENTS) {
    next = DESKTOP_MAX_CLIENTS;
  }
  return desktop_set_master_count(next);
}

static int desktop_parse_ratio_arg(const char *value, int current, int *out) {
  int exact = 0;
  int parsed = 0;

  if (!out) {
    return -1;
  }
  value = value ? value : "";
  while (*value == ' ') {
    value++;
  }
  if (strncmp(value, "exact ", 6) == 0) {
    exact = 1;
    value += 6;
    while (*value == ' ') {
      value++;
    }
  }
  if (!exact && (*value == '+' || *value == '-')) {
    if (desktop_parse_int_arg(value, &parsed) < 0) {
      return -1;
    }
    *out = current + parsed;
    return 0;
  }
  if (desktop_parse_int_arg(value, &parsed) < 0) {
    return -1;
  }
  *out = parsed;
  return 0;
}

static int desktop_set_submap(const char *name) {
  size_t len = 0;

  if (!name || !name[0]) {
    return -1;
  }
  while (name[len] && name[len] != ' ' && name[len] != '\t' &&
         name[len] != '\r' && name[len] != '\n') {
    char c = name[len];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
      return -1;
    }
    len++;
  }
  if (len == 0 || len >= sizeof(desktop_submap)) {
    return -1;
  }
  memcpy(desktop_submap, name, len);
  desktop_submap[len] = '\0';
  needs_redraw = 1;
  return 0;
}

static int desktop_submap_is_default(void) {
  return strcmp(desktop_submap, "default") == 0 ||
         strcmp(desktop_submap, "reset") == 0;
}

static const char *desktop_key_name(int key, char *buf, size_t buf_size) {
  if (!buf || buf_size == 0) {
    return "";
  }
  switch (key) {
  case KEY_UP:
    snprintf(buf, buf_size, "Up");
    break;
  case KEY_DOWN:
    snprintf(buf, buf_size, "Down");
    break;
  case KEY_LEFT:
    snprintf(buf, buf_size, "Left");
    break;
  case KEY_RIGHT:
    snprintf(buf, buf_size, "Right");
    break;
  case KEY_ESC:
    snprintf(buf, buf_size, "Esc");
    break;
  case KEY_F1:
    snprintf(buf, buf_size, "F1");
    break;
  case KEY_F2:
    snprintf(buf, buf_size, "F2");
    break;
  case KEY_F3:
    snprintf(buf, buf_size, "F3");
    break;
  case KEY_F4:
    snprintf(buf, buf_size, "F4");
    break;
  case KEY_F5:
    snprintf(buf, buf_size, "F5");
    break;
  case KEY_F6:
    snprintf(buf, buf_size, "F6");
    break;
  case KEY_F7:
    snprintf(buf, buf_size, "F7");
    break;
  case KEY_F8:
    snprintf(buf, buf_size, "F8");
    break;
  case KEY_F9:
    snprintf(buf, buf_size, "F9");
    break;
  case KEY_F10:
    snprintf(buf, buf_size, "F10");
    break;
  case KEY_F11:
    snprintf(buf, buf_size, "F11");
    break;
  case KEY_F12:
    snprintf(buf, buf_size, "F12");
    break;
  default:
    if (key >= 32 && key < 127) {
      snprintf(buf, buf_size, "%c", key);
    } else {
      snprintf(buf, buf_size, "0x%x", key);
    }
    break;
  }
  return buf;
}

static int desktop_parse_workspace_arg(const char *value, int *workspace) {
  int n = 0;
  const char *v = value ? value : "";

  if (!workspace) {
    return -1;
  }
  while (*v == ' ') {
    v++;
  }
  if (strncmp(v, "name:", 5) == 0) {
    char name[sizeof(desktop_workspaces[0].name)];
    int named;

    if (desktop_workspace_copy_safe_name(name, sizeof(name), v + 5) < 0) {
      return -1;
    }
    named = desktop_workspace_find_named(name);
    if (named < 1) {
      return -1;
    }
    *workspace = named;
    return 0;
  }
  if (strcmp(v, "active") == 0 || strcmp(v, "current") == 0) {
    *workspace = desktop_clamp_workspace(desktop_active_workspace);
    return 0;
  }
  if (strcmp(v, "previous") == 0 || strcmp(v, "prev") == 0) {
    *workspace = desktop_clamp_workspace(desktop_previous_workspace);
    return 0;
  }
  if (strcmp(v, "next") == 0) {
    *workspace = desktop_wrap_workspace(desktop_active_workspace + 1);
    return 0;
  }
  if (strcmp(v, "empty") == 0 || strcmp(v, "emptynext") == 0 ||
      strcmp(v, "e") == 0) {
    *workspace = desktop_find_empty_workspace();
    return 0;
  }
  if (strcmp(v, "last") == 0) {
    *workspace = desktop_clamp_workspace(desktop_previous_workspace);
    return 0;
  }
  if (v[0] == 'e' && (v[1] == '+' || v[1] == '-')) {
    v++;
  }
  if (v[0] == '+' || v[0] == '-') {
    if (desktop_parse_int_arg(v, &n) < 0) {
      return -1;
    }
    *workspace = desktop_wrap_workspace(desktop_active_workspace + n);
    return 0;
  }
  if (desktop_parse_int_arg(v, &n) < 0) {
    return -1;
  }
  if (n < 1 || n > desktop_workspace_count) {
    return -1;
  }
  *workspace = n;
  return 0;
}

static int desktop_parse_workspace_rename_arg(const char *value, int *workspace,
                                              char *name, size_t name_size) {
  char target_buf[32];
  size_t target_len = 0;
  const char *v = value ? value : "";

  if (!workspace || !name || name_size == 0) {
    return -1;
  }
  name[0] = '\0';
  while (*v == ' ') {
    v++;
  }
  while (v[target_len] && v[target_len] != ' ') {
    if (target_len + 1 >= sizeof(target_buf)) {
      return -1;
    }
    target_buf[target_len] = v[target_len];
    target_len++;
  }
  if (target_len == 0) {
    return -1;
  }
  target_buf[target_len] = '\0';
  v += target_len;
  while (*v == ' ') {
    v++;
  }
  if (*v == '\0') {
    return -1;
  }
  if (desktop_parse_workspace_arg(target_buf, workspace) < 0) {
    return -1;
  }
  return desktop_workspace_copy_safe_name(name, name_size, v);
}

static int desktop_parse_direction_arg(const char *value, int *dx, int *dy,
                                       int *fallback_delta) {
  const char *v = value ? value : "";

  while (*v == ' ') {
    v++;
  }
  if (!v[0] || strcmp(v, "next") == 0 || strcmp(v, "forward") == 0 ||
      strcmp(v, "n") == 0) {
    *dx = 1;
    *dy = 0;
    *fallback_delta = 1;
    return 0;
  }
  if (strcmp(v, "prev") == 0 || strcmp(v, "previous") == 0 ||
      strcmp(v, "-1") == 0) {
    *dx = -1;
    *dy = 0;
    *fallback_delta = -1;
    return 0;
  }
  if (strcmp(v, "l") == 0 || strcmp(v, "left") == 0) {
    *dx = -1;
    *dy = 0;
    *fallback_delta = -1;
    return 0;
  }
  if (strcmp(v, "r") == 0 || strcmp(v, "right") == 0) {
    *dx = 1;
    *dy = 0;
    *fallback_delta = 1;
    return 0;
  }
  if (strcmp(v, "u") == 0 || strcmp(v, "up") == 0) {
    *dx = 0;
    *dy = -1;
    *fallback_delta = -1;
    return 0;
  }
  if (strcmp(v, "d") == 0 || strcmp(v, "down") == 0) {
    *dx = 0;
    *dy = 1;
    *fallback_delta = 1;
    return 0;
  }
  return -1;
}

static int desktop_client_on_workspace(const desktop_client_t *client,
                                       int workspace) {
  if (!client || !client->visible) {
    return 0;
  }
  return client->pinned || client->workspace == workspace;
}

static int desktop_client_local_to_workspace(const desktop_client_t *client,
                                             int workspace) {
  if (!client || !client->visible || client->pinned) {
    return 0;
  }
  return client->workspace == workspace;
}

static int desktop_client_index_by_id(int id) {
  if (id <= 0) {
    return -1;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible && desktop_clients[i].id == id) {
      return i;
    }
  }
  return -1;
}

static uint32_t desktop_client_address(const desktop_client_t *client) {
  if (!client || client->id <= 0) {
    return 0;
  }
  return DESKTOP_CLIENT_ADDRESS_BASE + ((uint32_t)client->id * 0x100u);
}

static void desktop_workspace_record_focus_index(int idx) {
  int workspace;

  if (idx < 0 || idx >= DESKTOP_MAX_CLIENTS || !desktop_clients[idx].visible ||
      desktop_clients[idx].id <= 0) {
    return;
  }
  workspace = desktop_clients[idx].pinned ? desktop_active_workspace
                                          : desktop_clients[idx].workspace;
  if (workspace < 1 || workspace > DESKTOP_MAX_WORKSPACES) {
    return;
  }
  desktop_workspace_mark_used(workspace);
  desktop_workspaces[workspace - 1].last_focused_client_id =
      desktop_clients[idx].id;
  desktop_workspaces[workspace - 1].focus_generation =
      desktop_clients[idx].focus_generation;
}

static void desktop_workspace_forget_focus_id(int id) {
  if (id <= 0) {
    return;
  }
  for (int i = 0; i < DESKTOP_MAX_WORKSPACES; i++) {
    if (desktop_workspaces[i].last_focused_client_id == id) {
      desktop_workspaces[i].last_focused_client_id = 0;
      desktop_workspaces[i].focus_generation = 0;
    }
  }
}

static void desktop_refresh_focus_history_ids(void) {
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible) {
      desktop_clients[i].focus_history_id = -1;
    }
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    int idx = desktop_client_index_by_id(desktop_focus_history[i]);
    if (idx >= 0) {
      desktop_clients[idx].focus_history_id = i;
    }
  }
}

static int desktop_history_seen(const int *history, int count, int id) {
  for (int i = 0; i < count; i++) {
    if (history[i] == id) {
      return 1;
    }
  }
  return 0;
}

static void desktop_focus_history_compact(void) {
  int compacted[DESKTOP_MAX_CLIENTS];
  int used = 0;

  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    compacted[i] = 0;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    int id = desktop_focus_history[i];
    if (id <= 0 || desktop_client_index_by_id(id) < 0 ||
        desktop_history_seen(compacted, used, id)) {
      continue;
    }
    compacted[used++] = id;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    desktop_focus_history[i] = compacted[i];
  }
  desktop_refresh_focus_history_ids();
}

static void desktop_focus_history_remove(int id) {
  int compacted[DESKTOP_MAX_CLIENTS];
  int used = 0;

  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    compacted[i] = 0;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_focus_history[i] > 0 && desktop_focus_history[i] != id &&
        desktop_client_index_by_id(desktop_focus_history[i]) >= 0 &&
        !desktop_history_seen(compacted, used, desktop_focus_history[i])) {
      compacted[used++] = desktop_focus_history[i];
    }
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    desktop_focus_history[i] = compacted[i];
  }
  desktop_refresh_focus_history_ids();
}

static void desktop_focus_history_touch(int id) {
  int compacted[DESKTOP_MAX_CLIENTS];
  int used = 1;

  if (desktop_client_index_by_id(id) < 0) {
    return;
  }
  compacted[0] = id;
  for (int i = 1; i < DESKTOP_MAX_CLIENTS; i++) {
    compacted[i] = 0;
  }
  desktop_focus_history_compact();
  for (int i = 0; i < DESKTOP_MAX_CLIENTS && used < DESKTOP_MAX_CLIENTS; i++) {
    int old_id = desktop_focus_history[i];
    if (old_id > 0 && old_id != id && desktop_client_index_by_id(old_id) >= 0 &&
        !desktop_history_seen(compacted, used, old_id)) {
      compacted[used++] = old_id;
    }
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    desktop_focus_history[i] = compacted[i];
  }
  desktop_refresh_focus_history_ids();
}

static int desktop_last_focused_index_on_workspace_scope(int workspace,
                                                         int local_only) {
  int remembered = 0;

  if (workspace < 1 || workspace > DESKTOP_MAX_WORKSPACES) {
    return -1;
  }
  remembered = desktop_workspaces[workspace - 1].last_focused_client_id;
  if (remembered > 0) {
    int idx = desktop_client_index_by_id(remembered);
    if (idx >= 0 &&
        (local_only ? desktop_client_local_to_workspace(&desktop_clients[idx],
                                                        workspace)
                    : desktop_client_on_workspace(&desktop_clients[idx],
                                                  workspace))) {
      return idx;
    }
  }
  desktop_focus_history_compact();
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    int idx = desktop_client_index_by_id(desktop_focus_history[i]);
    if (idx >= 0 &&
        (local_only ? desktop_client_local_to_workspace(&desktop_clients[idx],
                                                        workspace)
                    : desktop_client_on_workspace(&desktop_clients[idx],
                                                  workspace))) {
      return idx;
    }
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (local_only ? desktop_client_local_to_workspace(&desktop_clients[i],
                                                       workspace)
                   : desktop_client_on_workspace(&desktop_clients[i],
                                                 workspace)) {
      return i;
    }
  }
  return -1;
}

static void desktop_set_focused_client_index(int idx) {
  int previous = desktop_focused_client_id;
  int id;
  int was_top;

  if (idx < 0 || idx >= DESKTOP_MAX_CLIENTS || !desktop_clients[idx].visible) {
    desktop_focused_client_id = 0;
    desktop_focus_history_compact();
    return;
  }
  id = desktop_clients[idx].id;
  was_top = desktop_focus_history[0] == id && desktop_focused_client_id == id;
  desktop_focused_client_id = id;
  if (!was_top || desktop_clients[idx].focus_generation == 0) {
    desktop_clients[idx].focus_generation = desktop_focus_serial++;
  }
  desktop_focus_history_touch(id);
  desktop_workspace_record_focus_index(idx);
  if (previous != desktop_focused_client_id) {
    desktop_start_transition("focus", desktop_active_workspace,
                             desktop_active_workspace);
  }
}

static int desktop_restore_focus_for_workspace(int workspace) {
  int idx = desktop_last_focused_index_on_workspace_scope(workspace, 1);

  if (idx < 0) {
    idx = desktop_last_focused_index_on_workspace_scope(workspace, 0);
  }
  if (idx < 0) {
    desktop_set_focused_client_index(-1);
    return -1;
  }
  desktop_set_focused_client_index(idx);
  return 0;
}

static int desktop_focused_client_index(void) {
  int idx = desktop_client_index_by_id(desktop_focused_client_id);
  if (idx >= 0 &&
      desktop_client_on_workspace(&desktop_clients[idx],
                                  desktop_active_workspace)) {
    return idx;
  }
  return desktop_restore_focus_for_workspace(desktop_active_workspace) == 0
             ? desktop_client_index_by_id(desktop_focused_client_id)
             : -1;
}

static int desktop_focused_client_is_terminal(void) {
  int idx = desktop_focused_client_index();
  return idx >= 0 && desktop_clients[idx].terminal_backed;
}

static int desktop_client_count_on_workspace(int workspace) {
  int count = 0;
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_client_on_workspace(&desktop_clients[i], workspace)) {
      count++;
    }
  }
  return count;
}

static int desktop_nth_client_on_workspace(int workspace, int nth) {
  int seen = 0;
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (!desktop_client_on_workspace(&desktop_clients[i], workspace)) {
      continue;
    }
    if (seen == nth) {
      return i;
    }
    seen++;
  }
  return -1;
}

static int desktop_spawn_client(const char *title, const char *app_id,
                                int terminal_backed) {
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (!desktop_clients[i].visible) {
      desktop_clients[i].id = desktop_next_client_id++;
      desktop_clients[i].workspace = desktop_active_workspace;
      desktop_clients[i].last_workspace = desktop_active_workspace;
      desktop_workspace_mark_used(desktop_active_workspace);
      desktop_clients[i].visible = 1;
      desktop_clients[i].mapped = 1;
      desktop_clients[i].hidden = 0;
      desktop_clients[i].terminal_backed = terminal_backed ? 1 : 0;
      desktop_clients[i].fullscreen = 0;
      desktop_clients[i].pseudo = 0;
      desktop_clients[i].pinned = 0;
      desktop_clients[i].focus_history_id = -1;
      desktop_clients[i].mapped_generation = desktop_client_serial++;
      desktop_clients[i].focus_generation = 0;
      desktop_clients[i].rule_match_count = 0;
      desktop_clients[i].rule_apply_count = 0;
      desktop_clients[i].rule_actions[0] = '\0';
      snprintf(desktop_clients[i].title, sizeof(desktop_clients[i].title),
               "%s", title ? title : "client");
      snprintf(desktop_clients[i].app_id, sizeof(desktop_clients[i].app_id),
               "%s", app_id ? app_id : "orizon-client");
      desktop_apply_spawn_rules_to_client(i);
      if (terminal_backed) {
        desktop_terminal_visible = 1;
        desktop_terminal_workspace = desktop_clients[i].workspace;
      }
      if (desktop_client_on_workspace(&desktop_clients[i],
                                      desktop_active_workspace)) {
        desktop_set_focused_client_index(i);
      } else {
        desktop_focused_client_index();
      }
      desktop_launcher_visible = 0;
      needs_redraw = 1;
      return desktop_clients[i].id;
    }
  }
  return -1;
}

static int desktop_known_app_spec(const char *app, char *title,
                                  size_t title_size, char *app_id,
                                  size_t app_id_size,
                                  int *terminal_backed) {
  const char *resolved_title = NULL;
  const char *resolved_app = NULL;
  int resolved_terminal = 0;

  if (!app || !app[0] || !title || !app_id || !terminal_backed) {
    return -1;
  }
  if (strcmp(app, "terminal") == 0 ||
      strcmp(app, "orizon-terminal") == 0 || strcmp(app, "kitty") == 0) {
    resolved_title = "Terminal";
    resolved_app = "orizon-terminal";
    resolved_terminal = 1;
  } else if (strcmp(app, "settings") == 0 ||
             strcmp(app, "desktop-settings") == 0 ||
             strcmp(app, "orizon-settings") == 0) {
    resolved_title = "Settings";
    resolved_app = "orizon-settings";
  } else if (strcmp(app, "logs") == 0 || strcmp(app, "log-viewer") == 0 ||
             strcmp(app, "logs-viewer") == 0 ||
             strcmp(app, "orizon-logs") == 0) {
    resolved_title = "Logs";
    resolved_app = "orizon-logs";
  } else if (strcmp(app, "packages") == 0 || strcmp(app, "pkg") == 0 ||
             strcmp(app, "package-viewer") == 0 ||
             strcmp(app, "orizon-packages") == 0) {
    resolved_title = "Packages";
    resolved_app = "orizon-packages";
  } else if (strcmp(app, "update") == 0 || strcmp(app, "updater") == 0 ||
             strcmp(app, "update-viewer") == 0 ||
             strcmp(app, "orizon-update-viewer") == 0) {
    resolved_title = "Update";
    resolved_app = "orizon-update-viewer";
  } else if (strcmp(app, "launcher") == 0 ||
             strcmp(app, "orizon-launcher") == 0) {
    resolved_title = "Launcher";
    resolved_app = "orizon-launcher";
  } else {
    return -1;
  }

  snprintf(title, title_size, "%s", resolved_title);
  snprintf(app_id, app_id_size, "%s", resolved_app);
  *terminal_backed = resolved_terminal;
  return 0;
}

static const char *desktop_client_backend_name(const desktop_client_t *client) {
  if (!client) {
    return "unknown";
  }
  if (client->terminal_backed) {
    return "terminal";
  }
  if (strcmp(client->app_id, "orizon-settings") == 0 ||
      strcmp(client->app_id, "orizon-logs") == 0 ||
      strcmp(client->app_id, "orizon-packages") == 0 ||
      strcmp(client->app_id, "orizon-update-viewer") == 0) {
    return "native-app";
  }
  return "prepared";
}

static int desktop_spawn_known_app_client(const char *app, char *out,
                                          size_t out_size) {
  char title[48];
  char app_id[32];
  int terminal_backed = 0;
  int id;

  if (desktop_known_app_spec(app, title, sizeof(title), app_id,
                             sizeof(app_id), &terminal_backed) < 0) {
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: exec supports terminal, settings, logs, "
               "packages, update, launcher\n");
    }
    return -1;
  }
  if (strcmp(app_id, "orizon-launcher") == 0) {
    gui_desktop_toggle_launcher();
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: exec orizon-launcher overlay toggled\n");
    }
    return 0;
  }
  id = desktop_spawn_client(title, app_id, terminal_backed);
  if (id < 0) {
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: client limit reached\n");
    }
    return -1;
  }
  if (out && out_size) {
    snprintf(out, out_size,
             "desktop dispatch: exec %s client spawned id=%d tiled=yes\n",
             app_id, id);
  }
  return 0;
}

static void desktop_ensure_terminal_client(void) {
  int has_terminal = 0;

  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible && desktop_clients[i].terminal_backed) {
      has_terminal = 1;
      if (desktop_terminal_visible) {
        desktop_terminal_workspace = desktop_clients[i].workspace;
      }
      break;
    }
  }
  if (!has_terminal && desktop_terminal_visible) {
    desktop_spawn_client("Terminal", "orizon-terminal", 1);
  }
}

static void desktop_sync_terminal_compat(void) {
  desktop_terminal_visible = 0;
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible && desktop_clients[i].terminal_backed) {
      desktop_terminal_visible = 1;
      desktop_terminal_workspace = desktop_clients[i].workspace;
      if (desktop_focused_client_id == desktop_clients[i].id) {
        desktop_terminal_workspace = desktop_clients[i].workspace;
        return;
      }
    }
  }
}

static int desktop_focus_relative(int delta) {
  int count = desktop_client_count_on_workspace(desktop_active_workspace);
  int focused_idx = desktop_focused_client_index();
  int focused_pos = 0;

  if (count <= 0) {
    return -1;
  }
  if (focused_idx >= 0) {
    for (int pos = 0; pos < count; pos++) {
      if (desktop_nth_client_on_workspace(desktop_active_workspace, pos) ==
          focused_idx) {
        focused_pos = pos;
        break;
      }
    }
  }
  focused_pos = (focused_pos + delta + count) % count;
  focused_idx =
      desktop_nth_client_on_workspace(desktop_active_workspace, focused_pos);
  if (focused_idx < 0) {
    return -1;
  }
  desktop_set_focused_client_index(focused_idx);
  desktop_sync_terminal_compat();
  needs_redraw = 1;
  return 0;
}

static int desktop_client_matches_focus_query(const desktop_client_t *client,
                                             const char *query) {
  const char *q = query ? query : "";
  int parsed_id = 0;
  uint32_t parsed_address = 0;

  if (!client || !client->visible) {
    return 0;
  }
  while (*q == ' ') {
    q++;
  }
  if (!q[0]) {
    return 0;
  }
  if (desktop_starts_with_icase(q, "id:")) {
    return desktop_parse_positive_token(q + 3, &parsed_id) == 0 &&
           client->id == parsed_id;
  }
  if (desktop_starts_with_icase(q, "address:")) {
    return desktop_parse_address_token(q + 8, &parsed_address) == 0 &&
           desktop_client_address(client) == parsed_address;
  }
  if (desktop_starts_with_icase(q, "addr:")) {
    return desktop_parse_address_token(q + 5, &parsed_address) == 0 &&
           desktop_client_address(client) == parsed_address;
  }
  if (desktop_starts_with_icase(q, "0x")) {
    return desktop_parse_address_token(q, &parsed_address) == 0 &&
           desktop_client_address(client) == parsed_address;
  }
  if (desktop_starts_with_icase(q, "class:")) {
    return desktop_contains_icase(client->app_id, q + 6);
  }
  if (desktop_starts_with_icase(q, "app:")) {
    return desktop_contains_icase(client->app_id, q + 4);
  }
  if (desktop_starts_with_icase(q, "title:")) {
    return desktop_contains_icase(client->title, q + 6);
  }
  if (desktop_starts_with_icase(q, "active")) {
    return client->id == desktop_focused_client_id;
  }
  if (desktop_parse_positive_token(q, &parsed_id) == 0) {
    return client->id == parsed_id;
  }
  return desktop_contains_icase(client->app_id, q) ||
         desktop_contains_icase(client->title, q);
}

static int desktop_find_client_by_focus_query(const char *query) {
  desktop_focus_history_compact();
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    int idx = desktop_client_index_by_id(desktop_focus_history[i]);
    if (idx >= 0 &&
        desktop_client_matches_focus_query(&desktop_clients[idx], query)) {
      return idx;
    }
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_client_matches_focus_query(&desktop_clients[i], query)) {
      return i;
    }
  }
  return -1;
}

static int desktop_focus_window_query(const char *query, char *out,
                                      size_t out_size) {
  const char *q = query ? query : "";
  int previous_workspace = desktop_active_workspace;
  int switched = 0;
  int idx;

  while (*q == ' ') {
    q++;
  }
  if (!q[0]) {
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: focuswindow expects <id|0xaddress|class:app|app:id|title:text>\n");
    }
    return -1;
  }
  idx = desktop_find_client_by_focus_query(q);
  if (idx < 0) {
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: focuswindow no-match target=\"%s\"\n", q);
    }
    return -1;
  }
  if (!desktop_clients[idx].pinned &&
      desktop_clients[idx].workspace != desktop_active_workspace) {
    desktop_save_active_layout_state();
    desktop_previous_workspace = desktop_active_workspace;
    desktop_active_workspace =
        desktop_clamp_workspace(desktop_clients[idx].workspace);
    desktop_workspace_mark_used(desktop_active_workspace);
    desktop_workspace_mark_visited(desktop_active_workspace);
    desktop_load_layout_state_for_workspace(desktop_active_workspace);
    switched = 1;
  }
  desktop_launcher_visible = 0;
  desktop_set_focused_client_index(idx);
  desktop_sync_terminal_compat();
  if (switched) {
    desktop_start_transition("focuswindow", previous_workspace,
                             desktop_active_workspace);
  } else {
    needs_redraw = 1;
  }
  if (out && out_size) {
    snprintf(out, out_size,
             "desktop dispatch: focuswindow ok target=\"%s\" address=0x%x id=%d class=%s title=\"%s\" workspace=%d switched=%s tiled=yes\n",
             q, desktop_client_address(&desktop_clients[idx]),
             desktop_clients[idx].id, desktop_clients[idx].app_id,
             desktop_clients[idx].title, desktop_clients[idx].workspace,
             switched ? "yes" : "no");
  }
  return 0;
}

static int desktop_swap_relative(int delta) {
  int count = desktop_client_count_on_workspace(desktop_active_workspace);
  int focused_idx = desktop_focused_client_index();
  int focused_pos = -1;
  int other_idx;
  desktop_client_t tmp;

  if (count <= 1 || focused_idx < 0) {
    return -1;
  }
  for (int pos = 0; pos < count; pos++) {
    if (desktop_nth_client_on_workspace(desktop_active_workspace, pos) ==
        focused_idx) {
      focused_pos = pos;
      break;
    }
  }
  if (focused_pos < 0) {
    return -1;
  }
  other_idx = desktop_nth_client_on_workspace(
      desktop_active_workspace, (focused_pos + delta + count) % count);
  if (other_idx < 0 || other_idx == focused_idx) {
    return -1;
  }
  tmp = desktop_clients[focused_idx];
  desktop_clients[focused_idx] = desktop_clients[other_idx];
  desktop_clients[other_idx] = tmp;
  desktop_set_focused_client_index(other_idx);
  desktop_sync_terminal_compat();
  needs_redraw = 1;
  return 0;
}

static int desktop_swap_client_indices(int focused_idx, int other_idx) {
  desktop_client_t tmp;

  if (focused_idx < 0 || other_idx < 0 || focused_idx == other_idx) {
    return -1;
  }
  tmp = desktop_clients[focused_idx];
  desktop_clients[focused_idx] = desktop_clients[other_idx];
  desktop_clients[other_idx] = tmp;
  desktop_set_focused_client_index(other_idx);
  desktop_sync_terminal_compat();
  needs_redraw = 1;
  return 0;
}

static int desktop_focus_master_client(void) {
  int idx = desktop_nth_client_on_workspace(desktop_active_workspace, 0);

  if (idx < 0) {
    return -1;
  }
  desktop_set_focused_client_index(idx);
  desktop_sync_terminal_compat();
  needs_redraw = 1;
  return 0;
}

static int desktop_swap_with_master(void) {
  int focused_idx = desktop_focused_client_index();
  int master_idx = desktop_nth_client_on_workspace(desktop_active_workspace, 0);

  if (focused_idx < 0 || master_idx < 0 || focused_idx == master_idx) {
    return -1;
  }
  return desktop_swap_client_indices(focused_idx, master_idx);
}

static int desktop_toggle_active_fullscreen(void) {
  int desired;
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desired = desktop_clients[idx].fullscreen ? 0 : 1;
  desktop_clients[idx].fullscreen = desired;
  needs_redraw = 1;
  return desktop_clients[idx].fullscreen;
}

static int desktop_set_active_fullscreen(int desired) {
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].fullscreen = desired ? 1 : 0;
  needs_redraw = 1;
  return desktop_clients[idx].fullscreen;
}

static int desktop_toggle_active_pseudo(void) {
  int desired;
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desired = desktop_clients[idx].pseudo ? 0 : 1;
  desktop_clients[idx].pseudo = desired;
  needs_redraw = 1;
  return desktop_clients[idx].pseudo;
}

static int desktop_set_active_pseudo(int desired) {
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].pseudo = desired ? 1 : 0;
  needs_redraw = 1;
  return desktop_clients[idx].pseudo;
}

static int desktop_toggle_active_pin(void) {
  int desired;
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desired = desktop_clients[idx].pinned ? 0 : 1;
  desktop_clients[idx].pinned = desired;
  needs_redraw = 1;
  return desktop_clients[idx].pinned;
}

static int desktop_set_active_pin(int desired) {
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].pinned = desired ? 1 : 0;
  needs_redraw = 1;
  return desktop_clients[idx].pinned;
}

static void draw_shell_frame(void) {
  int title_y = shell_y + 8;

  draw_shadow_panel(shell_x, shell_y, SHELL_WIDTH, SHELL_HEIGHT);
  fb_fill_rect(shell_x, shell_y, SHELL_WIDTH, SHELL_HEIGHT, COLOR_PANEL);
  fb_draw_rect(shell_x, shell_y, SHELL_WIDTH, SHELL_HEIGHT, COLOR_PANEL_EDGE);

  fb_fill_rect(shell_x, shell_y, SHELL_WIDTH, PANEL_TITLE_HEIGHT,
               MAKE_COLOR(12, 17, 27));
  fb_fill_rect(shell_x, shell_y + PANEL_TITLE_HEIGHT - 1, SHELL_WIDTH, 1,
               COLOR_PANEL_EDGE);

  draw_circle(shell_x + 18, shell_y + (PANEL_TITLE_HEIGHT / 2), 5,
              COLOR_PANEL_EDGE);
  draw_circle(shell_x + 38, shell_y + (PANEL_TITLE_HEIGHT / 2), 5,
              COLOR_TEXT_MUTED);
  draw_circle(shell_x + 58, shell_y + (PANEL_TITLE_HEIGHT / 2), 5,
              COLOR_PANEL_ACCENT);

  font_draw_string(shell_x + 86, title_y, "Orizon OS Console",
                   COLOR_TEXT_PRIMARY);
  font_draw_string(shell_x + SHELL_WIDTH - 236, title_y,
                   "stable personal base", COLOR_TEXT_MUTED);
}

static void draw_console_scene(void) {
  draw_background();
  draw_top_bar();

  draw_centered_string(72, "Orizon OS", COLOR_TEXT_PRIMARY);
  draw_centered_string(96, "A personal, stable, stripped-down base for iterative development.",
                       COLOR_TEXT_SECONDARY);

  draw_shell_frame();

  if (main_terminal) {
    term_render(main_terminal);
  } else {
    font_draw_string(shell_x + PANEL_PADDING, shell_y + PANEL_TITLE_HEIGHT + 14,
                     "Terminal initialization failed.", COLOR_RED);
  }

  draw_footer();
}

static void draw_desktop_status_bar(void) {
  char line[160];
  int y = TOP_BAR_HEIGHT + 8;

  if (!desktop_session.bar_enabled) {
    return;
  }
  fb_fill_rect_alpha(36, y, (int)screen_width - 72, 28,
                     MAKE_ARGB(168, 8, 12, 20));
  snprintf(line, sizeof(line),
           "WS %d/%d  layout=%s split=%s/%d master=%d nmaster=%d  gaps=%d/%d border=%d  F1 term F9 resize F10 move sub=%s",
           desktop_active_workspace, desktop_workspace_count,
           desktop_layout_engine(), desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           desktop_master_count, desktop_settings.gaps_in, desktop_settings.gaps_out,
           desktop_settings.border_size, desktop_submap);
  font_draw_string(52, y + 7, line, COLOR_TEXT_SECONDARY);
}

static void draw_desktop_launcher(void) {
  int width = 520;
  int height = 252;
  int x = ((int)screen_width - width) / 2;
  int y = TOP_BAR_HEIGHT + 96;

  if (!desktop_launcher_visible) {
    return;
  }
  draw_shadow_panel(x, y, width, height);
  fb_fill_rect_alpha(x, y, width, height, MAKE_ARGB(238, 10, 15, 24));
  fb_draw_rect(x, y, width, height, COLOR_PANEL_EDGE);
  fb_fill_rect(x, y, 5, height, COLOR_PANEL_ACCENT);
  font_draw_string(x + 24, y + 22, "Orizon Launcher", COLOR_TEXT_PRIMARY);
  font_draw_string(x + 24, y + 52,
                   "1 Terminal  2 Settings  3 Logs  4 Packages  5 Update",
                   COLOR_TEXT_SECONDARY);
  font_draw_string(x + 24, y + 70,
                   "Overlay only: no taskbar, no start menu, no free-drag",
                   COLOR_TEXT_MUTED);
  font_draw_string(x + 24, y + 88,
                   "Terminal      desktop launch terminal",
                   COLOR_TEXT_PRIMARY);
  font_draw_string(x + 24, y + 116,
                   "Settings      desktop launch settings",
                   COLOR_TEXT_SECONDARY);
  font_draw_string(x + 24, y + 144,
                   "Logs          desktop launch logs",
                   COLOR_TEXT_SECONDARY);
  font_draw_string(x + 24, y + 164,
                   "Packages      desktop launch packages",
                   COLOR_TEXT_SECONDARY);
  font_draw_string(x + 24, y + 180,
                   "Update        desktop launch update",
                   COLOR_TEXT_MUTED);
  font_draw_string(x + 24, y + 208,
                   "Details: desktop app <id> | Close: F3/Esc",
                   COLOR_TEXT_MUTED);
}

static void desktop_dwindle_rect_with_state(int target, int count, int x,
                                            int y, int width, int height,
                                            int split_mode,
                                            int split_ratio_percent, int *rx,
                                            int *ry, int *rw, int *rh) {
  int cur_x = x;
  int cur_y = y;
  int cur_w = width;
  int cur_h = height;

  if (count <= 1) {
    *rx = x;
    *ry = y;
    *rw = width;
    *rh = height;
    return;
  }
  for (int i = 0; i < count; i++) {
    int split_vertical = split_mode == 1 ||
                         (split_mode == 0 && cur_w >= cur_h);
    if (i == count - 1) {
      *rx = cur_x;
      *ry = cur_y;
      *rw = cur_w;
      *rh = cur_h;
      return;
    }
    if (split_vertical) {
      int first_w = (cur_w * split_ratio_percent) / 100;
      if (first_w < 48) {
        first_w = cur_w / 2;
      }
      if (i == target) {
        *rx = cur_x;
        *ry = cur_y;
        *rw = first_w;
        *rh = cur_h;
        return;
      }
      cur_x += first_w;
      cur_w -= first_w;
    } else {
      int first_h = (cur_h * split_ratio_percent) / 100;
      if (first_h < 40) {
        first_h = cur_h / 2;
      }
      if (i == target) {
        *rx = cur_x;
        *ry = cur_y;
        *rw = cur_w;
        *rh = first_h;
        return;
      }
      cur_y += first_h;
      cur_h -= first_h;
    }
  }
  *rx = cur_x;
  *ry = cur_y;
  *rw = cur_w;
  *rh = cur_h;
}

static void desktop_dwindle_rect(int target, int count, int x, int y, int width,
                                 int height, int *rx, int *ry, int *rw,
                                 int *rh) {
  desktop_dwindle_rect_with_state(target, count, x, y, width, height,
                                  desktop_split_mode,
                                  desktop_split_ratio_percent, rx, ry, rw,
                                  rh);
}

static void desktop_master_rect_with_state(int target, int count, int x, int y,
                                           int width, int height,
                                           int master_ratio_percent,
                                           int master_count, int *rx, int *ry,
                                           int *rw, int *rh) {
  int master_w;
  int effective_master_count;
  int stack_count;
  int stack_h;
  int master_h;

  if (master_count < 1) {
    master_count = 1;
  }
  if (master_count > DESKTOP_MAX_CLIENTS) {
    master_count = DESKTOP_MAX_CLIENTS;
  }
  effective_master_count = master_count < count ? master_count : count;

  if (count <= 1) {
    *rx = x;
    *ry = y;
    *rw = width;
    *rh = height;
    return;
  }

  if (target < effective_master_count) {
    master_w = count <= effective_master_count
                   ? width
                   : (width * master_ratio_percent) / 100;
    master_h = height / effective_master_count;
    *rx = x;
    *ry = y + target * master_h;
    *rw = master_w;
    *rh = target == effective_master_count - 1
              ? height - target * master_h
              : master_h;
    return;
  }

  master_w = (width * master_ratio_percent) / 100;
  stack_count = count - effective_master_count;
  stack_h = height / stack_count;
  *rx = x + master_w;
  *ry = y + (target - effective_master_count) * stack_h;
  *rw = width - master_w;
  *rh = target == count - 1
            ? height - (target - effective_master_count) * stack_h
            : stack_h;
}

static void desktop_master_rect(int target, int count, int x, int y, int width,
                                int height, int *rx, int *ry, int *rw,
                                int *rh) {
  desktop_master_rect_with_state(target, count, x, y, width, height,
                                 desktop_master_ratio_percent,
                                 desktop_master_count, rx, ry, rw, rh);
}

static int desktop_client_position_on_workspace(int idx, int workspace) {
  int pos = 0;

  if (idx < 0 || idx >= DESKTOP_MAX_CLIENTS) {
    return -1;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (!desktop_client_on_workspace(&desktop_clients[i], workspace)) {
      continue;
    }
    if (i == idx) {
      return pos;
    }
    pos++;
  }
  return -1;
}

static const char *desktop_client_role_on_workspace(int workspace, int idx,
                                                    int pos, int count) {
  int layout_mode;

  if (idx < 0 || idx >= DESKTOP_MAX_CLIENTS ||
      !desktop_clients[idx].visible) {
    return "missing";
  }
  if (desktop_clients[idx].fullscreen) {
    return "fullscreen";
  }
  layout_mode = desktop_layout_mode_from_name(
      desktop_layout_engine_for_workspace(workspace));
  if (layout_mode == 2) {
    return "monocle";
  }
  if (layout_mode == 1) {
    int master_count = desktop_master_count_for_workspace(workspace);
    if (master_count < 1) {
      master_count = 1;
    }
    return pos < master_count ? "master" : "stack";
  }
  if (count <= 1) {
    return "single";
  }
  return pos == 0 ? "dwindle-root" : "dwindle-leaf";
}

static void desktop_client_rect_for_workspace(int idx, int workspace, int *rx,
                                              int *ry, int *rw, int *rh) {
  int outer_gap = desktop_settings.gaps_out;
  int inner_gap = desktop_settings.gaps_in;
  int area_x = 44 + outer_gap;
  int area_y = TOP_BAR_HEIGHT + 92;
  int area_w = (int)screen_width - 88 - outer_gap * 2;
  int area_h = (int)screen_height - area_y - FOOTER_HEIGHT - 18 - outer_gap;
  int count;
  int pos;
  int layout_mode;
  int split_mode;
  int split_ratio_percent;
  int master_ratio_percent;
  int master_count;

  if (!rx || !ry || !rw || !rh) {
    return;
  }
  *rx = 0;
  *ry = 0;
  *rw = 0;
  *rh = 0;
  if (idx < 0 || idx >= DESKTOP_MAX_CLIENTS ||
      !desktop_clients[idx].visible) {
    return;
  }
  if (!desktop_client_on_workspace(&desktop_clients[idx], workspace)) {
    return;
  }
  if (area_w < 120) {
    area_w = 120;
  }
  if (area_h < 80) {
    area_h = 80;
  }
  count = desktop_client_count_on_workspace(workspace);
  pos = desktop_client_position_on_workspace(idx, workspace);
  layout_mode = desktop_layout_mode_from_name(desktop_layout_engine_for_workspace(workspace));
  split_mode = desktop_split_mode_for_workspace(workspace);
  split_ratio_percent = desktop_split_ratio_for_workspace(workspace);
  master_ratio_percent = desktop_master_ratio_for_workspace(workspace);
  master_count = desktop_master_count_for_workspace(workspace);
  if (count <= 0 || pos < 0 || desktop_clients[idx].fullscreen ||
      layout_mode == 2) {
    *rx = area_x;
    *ry = area_y;
    *rw = area_w;
    *rh = area_h;
  } else if (layout_mode == 1) {
    desktop_master_rect_with_state(pos, count, area_x, area_y, area_w, area_h,
                                   master_ratio_percent, master_count, rx, ry,
                                   rw, rh);
  } else {
    desktop_dwindle_rect_with_state(pos, count, area_x, area_y, area_w, area_h,
                                    split_mode, split_ratio_percent, rx, ry,
                                    rw, rh);
  }
  *rx += inner_gap;
  *ry += inner_gap;
  *rw -= inner_gap * 2;
  *rh -= inner_gap * 2;
  if (*rw < 0) {
    *rw = 0;
  }
  if (*rh < 0) {
    *rh = 0;
  }
}

static void desktop_client_rect(int idx, int *rx, int *ry, int *rw, int *rh) {
  int workspace;

  if (idx < 0 || idx >= DESKTOP_MAX_CLIENTS ||
      !desktop_clients[idx].visible) {
    if (rx) {
      *rx = 0;
    }
    if (ry) {
      *ry = 0;
    }
    if (rw) {
      *rw = 0;
    }
    if (rh) {
      *rh = 0;
    }
    return;
  }
  workspace = desktop_clients[idx].pinned ? desktop_active_workspace
                                          : desktop_clients[idx].workspace;
  desktop_client_rect_for_workspace(idx, workspace, rx, ry, rw, rh);
}

static int desktop_directional_neighbor_index(int focused_idx, int dx, int dy) {
  int fx;
  int fy;
  int fw;
  int fh;
  int fcx;
  int fcy;
  int best_idx = -1;
  int best_primary = 0;
  int best_secondary = 0;

  if (focused_idx < 0 || focused_idx >= DESKTOP_MAX_CLIENTS ||
      (dx == 0 && dy == 0)) {
    return -1;
  }
  desktop_client_rect(focused_idx, &fx, &fy, &fw, &fh);
  if (fw <= 0 || fh <= 0) {
    return -1;
  }
  fcx = fx + fw / 2;
  fcy = fy + fh / 2;
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    int rx;
    int ry;
    int rw;
    int rh;
    int cx;
    int cy;
    int primary;
    int secondary;

    if (i == focused_idx ||
        !desktop_client_on_workspace(&desktop_clients[i],
                                     desktop_active_workspace)) {
      continue;
    }
    desktop_client_rect(i, &rx, &ry, &rw, &rh);
    if (rw <= 0 || rh <= 0) {
      continue;
    }
    cx = rx + rw / 2;
    cy = ry + rh / 2;
    if (dx < 0) {
      if (cx >= fcx) {
        continue;
      }
      primary = fcx - cx;
      secondary = cy > fcy ? cy - fcy : fcy - cy;
    } else if (dx > 0) {
      if (cx <= fcx) {
        continue;
      }
      primary = cx - fcx;
      secondary = cy > fcy ? cy - fcy : fcy - cy;
    } else if (dy < 0) {
      if (cy >= fcy) {
        continue;
      }
      primary = fcy - cy;
      secondary = cx > fcx ? cx - fcx : fcx - cx;
    } else {
      if (cy <= fcy) {
        continue;
      }
      primary = cy - fcy;
      secondary = cx > fcx ? cx - fcx : fcx - cx;
    }
    if (best_idx < 0 || primary < best_primary ||
        (primary == best_primary && secondary < best_secondary)) {
      best_idx = i;
      best_primary = primary;
      best_secondary = secondary;
    }
  }
  return best_idx;
}

static int desktop_focus_directional(int dx, int dy, int fallback_delta,
                                     int *used_fallback) {
  int focused_idx = desktop_focused_client_index();
  int target_idx;

  if (used_fallback) {
    *used_fallback = 0;
  }
  if (focused_idx < 0) {
    return -1;
  }
  target_idx = desktop_directional_neighbor_index(focused_idx, dx, dy);
  if (target_idx >= 0) {
    desktop_set_focused_client_index(target_idx);
    desktop_sync_terminal_compat();
    needs_redraw = 1;
    return 0;
  }
  if (used_fallback) {
    *used_fallback = 1;
  }
  return desktop_focus_relative(fallback_delta);
}

static int desktop_swap_directional(int dx, int dy, int fallback_delta,
                                    int *used_fallback) {
  int focused_idx = desktop_focused_client_index();
  int target_idx;

  if (used_fallback) {
    *used_fallback = 0;
  }
  if (focused_idx < 0) {
    return -1;
  }
  target_idx = desktop_directional_neighbor_index(focused_idx, dx, dy);
  if (target_idx >= 0) {
    return desktop_swap_client_indices(focused_idx, target_idx);
  }
  if (used_fallback) {
    *used_fallback = 1;
  }
  return desktop_swap_relative(fallback_delta);
}

static int desktop_focus_client_at(int x, int y) {
  for (int i = DESKTOP_MAX_CLIENTS - 1; i >= 0; i--) {
    int rx;
    int ry;
    int rw;
    int rh;

    if (!desktop_client_on_workspace(&desktop_clients[i],
                                     desktop_active_workspace)) {
      continue;
    }
    desktop_client_rect(i, &rx, &ry, &rw, &rh);
    if (x >= rx && y >= ry && x < rx + rw && y < ry + rh) {
      if (desktop_clients[i].id != desktop_focused_client_id) {
        desktop_set_focused_client_index(i);
        desktop_sync_terminal_compat();
        desktop_pointer_focus_changes++;
        needs_redraw = 1;
      }
      return 0;
    }
  }
  return -1;
}

static void draw_desktop_focus_ring(int x, int y, int width, int height,
                                    color_t accent) {
  int r = (accent >> 16) & 0xff;
  int g = (accent >> 8) & 0xff;
  int b = accent & 0xff;
  int rounding = desktop_settings.rounding;

  if (!desktop_settings.focus_ring_enabled || width < 24 || height < 24) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    int alpha = 70 - i * 18;
    color_t glow = MAKE_ARGB(alpha, r, g, b);
    int inset = i + 1;
    fb_fill_rect_alpha(x - inset, y - inset, width + inset * 2, 2, glow);
    fb_fill_rect_alpha(x - inset, y + height + inset - 2,
                       width + inset * 2, 2, glow);
    fb_fill_rect_alpha(x - inset, y - inset, 2, height + inset * 2, glow);
    fb_fill_rect_alpha(x + width + inset - 2, y - inset, 2,
                       height + inset * 2, glow);
  }
  if (rounding > 0) {
    int hint = rounding > 12 ? 12 : rounding;
    color_t corner = MAKE_ARGB(110, r, g, b);
    fb_fill_rect_alpha(x - 2, y - 2, hint, 2, corner);
    fb_fill_rect_alpha(x - 2, y - 2, 2, hint, corner);
    fb_fill_rect_alpha(x + width - hint + 2, y - 2, hint, 2, corner);
    fb_fill_rect_alpha(x + width, y - 2, 2, hint, corner);
    fb_fill_rect_alpha(x - 2, y + height, hint, 2, corner);
    fb_fill_rect_alpha(x - 2, y + height - hint + 2, 2, hint, corner);
    fb_fill_rect_alpha(x + width - hint + 2, y + height, hint, 2, corner);
    fb_fill_rect_alpha(x + width, y + height - hint + 2, 2, hint, corner);
  }
}

static void draw_desktop_transition_overlay(void) {
  char line[128];
  color_t accent = desktop_theme_accent();
  int r = (accent >> 16) & 0xff;
  int g = (accent >> 8) & 0xff;
  int b = accent & 0xff;
  int progress;
  int width = 370;
  int height = 30;
  int x = (int)screen_width - width - 44;
  int y = TOP_BAR_HEIGHT + 46;

  if (!desktop_settings.animations_enabled ||
      desktop_animation_ticks_remaining <= 0) {
    return;
  }
  if (x < 48) {
    x = 48;
  }
  progress = desktop_transition_progress_percent();
  fb_fill_rect_alpha(x, y, width, height, MAKE_ARGB(210, 8, 13, 22));
  fb_draw_rect(x, y, width, height, MAKE_ARGB(160, r, g, b));
  fb_fill_rect_alpha(x + 2, y + height - 4,
                     ((width - 4) * progress) / 100, 2,
                     MAKE_ARGB(190, r, g, b));
  snprintf(line, sizeof(line), "anim %s %s ws %d -> %d progress=%d%%",
           desktop_settings.animation_curve,
           desktop_transition_reason, desktop_transition_from_workspace,
           desktop_transition_to_workspace, progress);
  font_draw_string(x + 12, y + 9, line, COLOR_TEXT_SECONDARY);
}

static void draw_desktop_native_app(const desktop_client_t *client, int x,
                                    int y, int width, int height) {
  int line = y + 8;
  int step = 20;

  if (!client || width < 48 || height < 48) {
    return;
  }
  font_draw_string(x, line, "native Orizon tiling client",
                   COLOR_TEXT_PRIMARY);
  line += step;
  font_draw_string(x, line, "surface=tiled-client floating=no manual-drag=no",
                   COLOR_TEXT_MUTED);
  line += step;
  if (strcmp(client->app_id, "orizon-settings") == 0) {
    font_draw_string(x, line, "Settings", COLOR_TEXT_PRIMARY);
    line += step;
    font_draw_string(x, line, "theme, gaps, borders, input, autostart",
                     COLOR_TEXT_SECONDARY);
    line += step;
    font_draw_string(x, line, "commands: desktop settings | desktop keymap",
                     COLOR_TEXT_MUTED);
    line += step;
    font_draw_string(x, line, "edit policy: desktop keyword <key> <value>",
                     COLOR_TEXT_MUTED);
  } else if (strcmp(client->app_id, "orizon-logs") == 0) {
    font_draw_string(x, line, "Logs Viewer", COLOR_TEXT_PRIMARY);
    line += step;
    font_draw_string(x, line, "/logs/desktop.log and session rollinglog",
                     COLOR_TEXT_SECONDARY);
    line += step;
    font_draw_string(x, line, "commands: desktop logs | desktop rollinglog",
                     COLOR_TEXT_MUTED);
    line += step;
    font_draw_string(x, line, "SSH-safe: cat/head/tail still expose files",
                     COLOR_TEXT_MUTED);
  } else if (strcmp(client->app_id, "orizon-packages") == 0) {
    font_draw_string(x, line, "Package Viewer", COLOR_TEXT_PRIMARY);
    line += step;
    font_draw_string(x, line, "orizon-desktop-hypr and optional modules",
                     COLOR_TEXT_SECONDARY);
    line += step;
    font_draw_string(x, line, "commands: pkg search desktop | pkg info",
                     COLOR_TEXT_MUTED);
    line += step;
    font_draw_string(x, line, "next split: terminal/settings/launcher pkgs",
                     COLOR_TEXT_MUTED);
  } else if (strcmp(client->app_id, "orizon-update-viewer") == 0) {
    font_draw_string(x, line, "Update Viewer", COLOR_TEXT_PRIMARY);
    line += step;
    font_draw_string(x, line, "manifest, signature, bootguard, rollback",
                     COLOR_TEXT_SECONDARY);
    line += step;
    font_draw_string(x, line, "commands: update status | rollback-status",
                     COLOR_TEXT_MUTED);
    line += step;
    font_draw_string(x, line, "VM facade only; no hardware validation here",
                     COLOR_TEXT_MUTED);
  } else {
    font_draw_string(x, line, "prepared surface", COLOR_TEXT_PRIMARY);
    line += step;
    font_draw_string(x, line, "future native Orizon desktop app",
                     COLOR_TEXT_SECONDARY);
  }
}

static void draw_desktop_client_tile(const desktop_client_t *client, int x,
                                     int y, int width, int height,
                                     int focused) {
  char title[128];
  color_t accent = desktop_theme_accent();
  color_t border = focused ? accent : COLOR_PANEL_EDGE;
  int inner_x = x + 8;
  int inner_y = y + 30;
  int inner_w = width - 16;
  int inner_h = height - 38;
  int render_x;
  int render_y;
  int render_w;
  int render_h;
  int border_count = desktop_settings.border_size;

  if (!client || width < 80 || height < 64) {
    return;
  }
  if (desktop_settings.shadows_enabled) {
    draw_shadow_panel(x, y, width, height);
  }
  if (focused && desktop_settings.focus_ring_enabled) {
    draw_desktop_focus_ring(x, y, width, height, accent);
  }
  fb_fill_rect_alpha(x, y, width, height, MAKE_ARGB(226, 10, 15, 24));
  if (border_count < 1) {
    border_count = 1;
  }
  for (int i = 0; i < border_count && width - i * 2 > 4 &&
                  height - i * 2 > 4;
       i++) {
    fb_draw_rect(x + i, y + i, width - i * 2, height - i * 2, border);
  }
  snprintf(title, sizeof(title), "%s  id=%d  app=%s  ws=%d%s%s%s%s",
           client->title, client->id, client->app_id, client->workspace,
           client->fullscreen ? "  fullscreen" : "",
           client->pseudo ? "  pseudo" : "",
           client->pinned ? "  pinned" : "",
           focused ? "  focused" : "");
  font_draw_string(x + 10, y + 10, title,
                   focused ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY);

  render_x = inner_x;
  render_y = inner_y;
  render_w = inner_w;
  render_h = inner_h;
  if (client->pseudo && !client->fullscreen && inner_w > 120 &&
      inner_h > 96) {
    render_x += inner_w / 10;
    render_y += inner_h / 10;
    render_w = (inner_w * 8) / 10;
    render_h = (inner_h * 8) / 10;
    fb_draw_rect(render_x - 4, render_y - 4, render_w + 8, render_h + 8,
                 COLOR_TEXT_MUTED);
    font_draw_string(inner_x, inner_y + inner_h - 14,
                     "pseudo tiled surface: tile kept, content constrained",
                     COLOR_TEXT_MUTED);
  }

  if (client->terminal_backed && focused && main_terminal && render_w > 48 &&
      render_h > 48) {
    term_render_in_rect(main_terminal, render_x, render_y, render_w, render_h);
  } else if (client->terminal_backed) {
    font_draw_string(render_x, render_y + 8, "terminal surface",
                     COLOR_TEXT_PRIMARY);
    font_draw_string(render_x, render_y + 32,
                     "shared backend; focus with dispatch movefocus",
                     COLOR_TEXT_SECONDARY);
  } else {
    draw_desktop_native_app(client, render_x, render_y, render_w, render_h);
  }
}

static void draw_desktop_scene(void) {
  char session_line[128];
  char workspace_line[128];
  int outer_gap = desktop_settings.gaps_out;
  int inner_gap = desktop_settings.gaps_in;
  int area_x = 44 + outer_gap;
  int area_y = TOP_BAR_HEIGHT + 92;
  int area_w = (int)screen_width - 88 - outer_gap * 2;
  int area_h = (int)screen_height - area_y - FOOTER_HEIGHT - 18 - outer_gap;
  int client_count;
  int focused_idx;

  if (area_w < 120) {
    area_w = 120;
  }
  if (area_h < 80) {
    area_h = 80;
  }

  desktop_ensure_terminal_client();
  draw_background();
  draw_top_bar();
  draw_desktop_status_bar();
  font_draw_string(48, TOP_BAR_HEIGHT + 24, "Orizon Desktop",
                   COLOR_TEXT_PRIMARY);
  snprintf(session_line, sizeof(session_line),
           "Hyprland-style profile | theme=%s wallpaper=%s layout=%s submap=%s",
           desktop_session.theme, desktop_session.wallpaper,
           desktop_layout_engine(), desktop_submap);
  font_draw_string(48, TOP_BAR_HEIGHT + 48, session_line,
                   COLOR_TEXT_SECONDARY);
  snprintf(workspace_line, sizeof(workspace_line),
           "workspace %d/%d | clients=%d | layout=%s split=%s/%d master=%d nmaster=%d | settings=%s",
           desktop_active_workspace, desktop_workspace_count,
           desktop_client_count_on_workspace(desktop_active_workspace),
           desktop_layout_engine(), desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           desktop_master_count, ORIZON_DESKTOP_SETTINGS_PATH);
  font_draw_string(48, TOP_BAR_HEIGHT + 68, workspace_line,
                   COLOR_TEXT_MUTED);

  client_count = desktop_client_count_on_workspace(desktop_active_workspace);
  focused_idx = desktop_focused_client_index();
  if (client_count <= 0) {
    draw_centered_string((int)screen_height / 2 + 56,
                         "Workspace is empty",
                         COLOR_TEXT_PRIMARY);
    draw_centered_string((int)screen_height / 2 + 80,
                         "Use 'desktop dispatch exec terminal' or press F1.",
                         COLOR_TEXT_SECONDARY);
  } else if (focused_idx >= 0 && desktop_clients[focused_idx].fullscreen) {
    draw_desktop_client_tile(&desktop_clients[focused_idx], area_x, area_y,
                             area_w, area_h, 1);
  } else if (desktop_layout_mode == 2) {
    if (focused_idx >= 0) {
      draw_desktop_client_tile(&desktop_clients[focused_idx], area_x, area_y,
                               area_w, area_h, 1);
    }
  } else {
    int pos = 0;
    for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
      int rx;
      int ry;
      int rw;
      int rh;
      if (!desktop_client_on_workspace(&desktop_clients[i],
                                       desktop_active_workspace)) {
        continue;
      }
      if (desktop_layout_mode == 1) {
        desktop_master_rect(pos, client_count, area_x, area_y, area_w, area_h,
                            &rx, &ry, &rw, &rh);
      } else {
        desktop_dwindle_rect(pos, client_count, area_x, area_y, area_w, area_h,
                             &rx, &ry, &rw, &rh);
      }
      draw_desktop_client_tile(&desktop_clients[i], rx + inner_gap,
                               ry + inner_gap, rw - inner_gap * 2,
                               rh - inner_gap * 2,
                               desktop_clients[i].id ==
                                   desktop_focused_client_id);
      pos++;
    }
  }

  draw_desktop_launcher();
  draw_desktop_transition_overlay();
  draw_footer();
}

static void draw_cursor(int x, int y) {
  /* Small arrow cursor with dark outline for legibility. */
  for (int py = 0; py < 14; py++) {
    int row_width = py < 8 ? py + 1 : 4;
    for (int px = 0; px < row_width; px++) {
      fb_put_pixel(x + px, y + py, COLOR_WHITE);
      if (px == row_width - 1 || py == 0) {
        fb_put_pixel(x + px + 1, y + py, COLOR_CURSOR_BORDER);
      }
    }
  }

  for (int i = 0; i < 11; i++) {
    fb_put_pixel(x + i, y + i, COLOR_CURSOR_BORDER);
  }
}

static void draw_splash(void) {
  fb_fill_gradient_v(0, 0, (int)screen_width, (int)screen_height,
                     MAKE_COLOR(7, 10, 18), MAKE_COLOR(16, 24, 38));

  {
    int card_w = 460;
    int card_h = 164;
    int card_x = ((int)screen_width - card_w) / 2;
    int card_y = ((int)screen_height - card_h) / 2;

    draw_shadow_panel(card_x, card_y, card_w, card_h);
    fb_fill_rect(card_x, card_y, card_w, card_h, COLOR_PANEL);
    fb_draw_rect(card_x, card_y, card_w, card_h, COLOR_PANEL_EDGE);
    fb_fill_rect(card_x, card_y, 6, card_h, COLOR_PANEL_ACCENT);

    font_draw_string(card_x + 28, card_y + 32, "Orizon OS",
                     COLOR_TEXT_PRIMARY);
    font_draw_string(card_x + 28, card_y + 62,
                     "Booting your personal core environment",
                     COLOR_TEXT_SECONDARY);
    font_draw_string(card_x + 28, card_y + 92,
                     "Stable personal development base ready.",
                     COLOR_TEXT_MUTED);
    font_draw_string(card_x + 28, card_y + 122,
                     "Preparing core workspace...",
                     COLOR_PANEL_ACCENT);
    if (timer_fallback_polling) {
      font_draw_string(card_x + 28, card_y + 144,
                       "Timer IRQ fallback: continuing without hlt sleep.",
                       COLOR_TEXT_MUTED);
    } else if (boot_stage_hint && boot_stage_hint[0]) {
      font_draw_string(card_x + 28, card_y + 144, boot_stage_hint,
                       COLOR_TEXT_MUTED);
    } else if (!timer_irq_seen) {
      font_draw_string(card_x + 28, card_y + 144,
                       "Waiting for firmware timer IRQ...",
                       COLOR_TEXT_MUTED);
    }
  }
}

static void layout_console(void) {
  shell_x = ((int)screen_width - SHELL_WIDTH) / 2;
  shell_y = ((int)screen_height - SHELL_HEIGHT) / 2 + 10;

  if (shell_x < 24) {
    shell_x = 24;
  }
  if (shell_y < TOP_BAR_HEIGHT + 24) {
    shell_y = TOP_BAR_HEIGHT + 24;
  }

  term_x = shell_x + PANEL_PADDING;
  term_y = shell_y + PANEL_TITLE_HEIGHT + PANEL_PADDING;
}

static int desktop_handle_submap_key(int key) {
  if (desktop_submap_is_default()) {
    return 0;
  }
  if (key == KEY_ESC || key == KEY_F12) {
    desktop_set_submap("default");
    return 1;
  }
  if (strcmp(desktop_submap, "resize") == 0) {
    if (key == KEY_LEFT || key == 'h' || key == 'H') {
      desktop_adjust_split_ratio(-5);
      return 1;
    }
    if (key == KEY_RIGHT || key == 'l' || key == 'L') {
      desktop_adjust_split_ratio(5);
      return 1;
    }
    if (key == KEY_UP || key == 'k' || key == 'K') {
      desktop_adjust_master_ratio(5);
      return 1;
    }
    if (key == KEY_DOWN || key == 'j' || key == 'J') {
      desktop_adjust_master_ratio(-5);
      return 1;
    }
    if (key == 'r' || key == 'R') {
      desktop_set_split_ratio(50);
      desktop_set_master_ratio(58);
      return 1;
    }
    if (key == 's' || key == 'S') {
      desktop_cycle_split_mode(1);
      return 1;
    }
    return 1;
  }
  if (strcmp(desktop_submap, "move") == 0) {
    if (key == KEY_LEFT || key == KEY_UP || key == 'h' || key == 'H' ||
        key == 'k' || key == 'K') {
      gui_desktop_focus_prev_client();
      return 1;
    }
    if (key == KEY_RIGHT || key == KEY_DOWN || key == 'l' || key == 'L' ||
        key == 'j' || key == 'J') {
      gui_desktop_focus_next_client();
      return 1;
    }
    if (key >= '1' && key <= '3') {
      gui_desktop_move_terminal_to_workspace(key - '0');
      return 1;
    }
    if (key == 'p' || key == 'P') {
      desktop_toggle_active_pin();
      return 1;
    }
    return 1;
  }
  if (strcmp(desktop_submap, "launch") == 0) {
    if (key == 't' || key == 'T' || key == '\n' || key == '\r') {
      gui_desktop_spawn_terminal_client();
      desktop_set_submap("default");
      return 1;
    }
    if (key == 's' || key == 'S') {
      desktop_spawn_known_app_client("settings", NULL, 0);
      desktop_set_submap("default");
      return 1;
    }
    if (key == 'l' || key == 'L') {
      desktop_spawn_known_app_client("logs", NULL, 0);
      desktop_set_submap("default");
      return 1;
    }
    if (key == 'p' || key == 'P') {
      desktop_spawn_known_app_client("packages", NULL, 0);
      desktop_set_submap("default");
      return 1;
    }
    if (key == 'u' || key == 'U') {
      desktop_spawn_known_app_client("update", NULL, 0);
      desktop_set_submap("default");
      return 1;
    }
    if (key == 'd' || key == 'D' || key == ' ') {
      gui_desktop_toggle_launcher();
      desktop_set_submap("default");
      return 1;
    }
    if (key == 'q' || key == 'Q') {
      gui_desktop_close_active_client();
      desktop_set_submap("default");
      return 1;
    }
    return 1;
  }
  return 0;
}

static void keyboard_callback(int key) {
  desktop_last_key = key;
  desktop_key_serial++;

  if (splash_ticks_remaining > 0 &&
      (key == ' ' || key == '\n' || key == '\r' || key == KEY_ESC)) {
    splash_ticks_remaining = 0;
    needs_redraw = 1;
    return;
  }

  if (desktop_mode_enabled) {
    if (key == KEY_F9) {
      desktop_set_submap("resize");
      return;
    }
    if (key == KEY_F10) {
      desktop_set_submap("move");
      return;
    }
    if (key == KEY_F11) {
      desktop_set_submap("launch");
      return;
    }
    if (desktop_handle_submap_key(key)) {
      return;
    }
    if (key == KEY_F3) {
      gui_desktop_toggle_launcher();
      return;
    }
    if (desktop_launcher_visible) {
      if (key == KEY_ESC) {
        gui_desktop_hide_launcher();
      } else if (key == '\n' || key == '\r' || key == ' ' || key == '1') {
        gui_desktop_spawn_terminal_client();
        gui_desktop_hide_launcher();
      } else if (key == '2' || key == 's' || key == 'S') {
        desktop_spawn_known_app_client("settings", NULL, 0);
        gui_desktop_hide_launcher();
      } else if (key == '3' || key == 'l' || key == 'L') {
        desktop_spawn_known_app_client("logs", NULL, 0);
        gui_desktop_hide_launcher();
      } else if (key == '4' || key == 'p' || key == 'P') {
        desktop_spawn_known_app_client("packages", NULL, 0);
        gui_desktop_hide_launcher();
      } else if (key == '5' || key == 'u' || key == 'U') {
        desktop_spawn_known_app_client("update", NULL, 0);
        gui_desktop_hide_launcher();
      }
      return;
    }
    if (key == KEY_F1 || key == 't' || key == 'T') {
      gui_desktop_spawn_terminal_client();
      return;
    }
    if (key == KEY_F2) {
      gui_desktop_close_active_client();
      return;
    }
    if (key == KEY_F4) {
      desktop_toggle_active_fullscreen();
      return;
    }
    if (key == KEY_F5) {
      desktop_toggle_active_pseudo();
      return;
    }
    if (key == KEY_F6) {
      gui_desktop_focus_next_client();
      return;
    }
    if (key == KEY_F7) {
      gui_desktop_switch_workspace(
          desktop_clamp_workspace(desktop_active_workspace + 1));
      return;
    }
    if (key == KEY_F8) {
      gui_desktop_switch_workspace(
          desktop_clamp_workspace(desktop_active_workspace - 1));
      return;
    }
    if (!desktop_focused_client_is_terminal()) {
      if (key >= '1' && key <= '3') {
        gui_desktop_switch_workspace(key - '0');
        return;
      }
      if (key == '\n' || key == '\r' || key == ' ') {
        gui_desktop_spawn_terminal_client();
      }
      return;
    }
  }

  if (main_terminal) {
    term_handle_key(main_terminal, key);
    needs_redraw = 1;
  }
}

static void poll_input_state(void) {
  int new_x = ps2_get_mouse_x();
  int new_y = ps2_get_mouse_y();
  int new_buttons = ps2_get_mouse_buttons();
  int wheel = ps2_consume_mouse_wheel();
  int left_click = (new_buttons & 1) && !(prev_buttons & 1);

  if (new_x != mouse_x || new_y != mouse_y || new_buttons != prev_buttons) {
    mouse_x = new_x;
    mouse_y = new_y;
    if (desktop_mode_enabled && desktop_session.focus_follows_mouse &&
        !desktop_launcher_visible && splash_ticks_remaining <= 0) {
      desktop_focus_client_at(mouse_x, mouse_y);
    }
    needs_redraw = 1;
  }

  if (left_click && splash_ticks_remaining > 0) {
    splash_ticks_remaining = 0;
    needs_redraw = 1;
  } else if (left_click && splash_ticks_remaining <= 0 &&
             desktop_mode_enabled && desktop_launcher_visible) {
    gui_desktop_hide_launcher();
  }

  if (wheel != 0 && splash_ticks_remaining <= 0 && main_terminal &&
      (!desktop_mode_enabled || desktop_focused_client_is_terminal())) {
    term_scroll_view(main_terminal, -wheel * 3);
    needs_redraw = 1;
  }

  prev_buttons = new_buttons;
}

void gui_init(void) {
  serial_puts("GUI: initializing minimal Orizon OS shell\n");

  ui_scale = (screen_width >= 2400 || screen_height >= 1440) ? 2 : 1;
  font_init();
  vfs_init();
  vfs_seed_content();
  layout_console();

  main_terminal = term_create(term_x, term_y);
  term_set_active(main_terminal);

  ps2_set_screen_bounds((int)screen_width, (int)screen_height);
  ps2_set_mouse_scale(1);
  ps2_set_keyboard_callback(keyboard_callback);
  usb_set_keyboard_callback(keyboard_callback);
  i2c_hid_deferred_probe = boot_cmdline_has("orizon.i2chid=1") ? 1 : 0;
  if (i2c_hid_deferred_probe) {
    serial_puts("GUI: Lenovo I2C-HID probe deferred until after first render\n");
  } else {
    serial_puts("GUI: Lenovo I2C-HID probe disabled for safe boot\n");
  }
  boot_stage_hint = "First screen ready. Core services will start after render.";

  mouse_x = ps2_get_mouse_x();
  mouse_y = ps2_get_mouse_y();
  prev_buttons = ps2_get_mouse_buttons();
  needs_redraw = 1;
}

int gui_timer_irq_active(void) {
  return timer_irq_seen || timer_ticks() > 0;
}

int gui_timer_fallback_active(void) {
  return timer_fallback_polling;
}

void gui_compose(void) {
  poll_input_state();

  if (!needs_redraw) {
    return;
  }

  if (splash_ticks_remaining > 0) {
    draw_splash();
  } else if (desktop_mode_enabled) {
    draw_desktop_scene();
    draw_cursor(mouse_x, mouse_y);
  } else {
    draw_console_scene();
    draw_cursor(mouse_x, mouse_y);
  }

  fb_swap_buffers();
  needs_redraw = 0;
}

void gui_desktop_set_enabled(int enabled) {
  desktop_mode_enabled = enabled ? 1 : 0;
  gui_desktop_reload_session();
  if (desktop_mode_enabled) {
    desktop_active_workspace = desktop_clamp_workspace(desktop_active_workspace);
    desktop_workspace_mark_visited(desktop_active_workspace);
    desktop_load_layout_state_for_workspace(desktop_active_workspace);
    desktop_terminal_workspace =
        desktop_clamp_workspace(desktop_terminal_workspace);
    desktop_terminal_visible = desktop_session.autostart_terminal ? 1 : 0;
    desktop_ensure_terminal_client();
  } else {
    desktop_launcher_visible = 0;
  }
  needs_redraw = 1;
}

int gui_desktop_enabled(void) {
  return desktop_mode_enabled;
}

void gui_desktop_open_terminal(void) {
  int found = -1;

  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible && desktop_clients[i].terminal_backed) {
      found = i;
      break;
    }
  }
  if (found < 0) {
    gui_desktop_spawn_terminal_client();
    return;
  }
  desktop_clients[found].last_workspace = desktop_clients[found].workspace;
  desktop_clients[found].workspace = desktop_active_workspace;
  desktop_set_focused_client_index(found);
  desktop_sync_terminal_compat();
  desktop_launcher_visible = 0;
  needs_redraw = 1;
}

void gui_desktop_close_terminal(void) {
  gui_desktop_close_active_client();
}

void gui_desktop_toggle_terminal(void) {
  if (desktop_client_count_on_workspace(desktop_active_workspace) > 0) {
    gui_desktop_close_active_client();
  } else {
    gui_desktop_spawn_terminal_client();
  }
}

int gui_desktop_terminal_visible(void) {
  return desktop_terminal_visible;
}

void gui_desktop_show_launcher(void) {
  if (!desktop_mode_enabled || !desktop_session.launcher_enabled) {
    return;
  }
  desktop_launcher_visible = 1;
  needs_redraw = 1;
}

void gui_desktop_hide_launcher(void) {
  desktop_launcher_visible = 0;
  needs_redraw = 1;
}

void gui_desktop_toggle_launcher(void) {
  if (!desktop_mode_enabled || !desktop_session.launcher_enabled) {
    return;
  }
  desktop_launcher_visible = desktop_launcher_visible ? 0 : 1;
  needs_redraw = 1;
}

int gui_desktop_launcher_visible(void) {
  return desktop_launcher_visible;
}

int gui_desktop_switch_workspace(int workspace) {
  int previous_workspace = desktop_active_workspace;

  if (workspace < 1 || workspace > desktop_workspace_count) {
    return -1;
  }
  desktop_save_active_layout_state();
  desktop_workspace_mark_used(workspace);
  if (workspace != desktop_active_workspace) {
    desktop_previous_workspace = desktop_active_workspace;
  }
  desktop_active_workspace = workspace;
  desktop_workspace_mark_visited(workspace);
  desktop_load_layout_state_for_workspace(workspace);
  desktop_launcher_visible = 0;
  desktop_restore_focus_for_workspace(workspace);
  desktop_sync_terminal_compat();
  if (workspace != previous_workspace) {
    desktop_start_transition("workspace", previous_workspace, workspace);
  } else {
    needs_redraw = 1;
  }
  return 0;
}

static int desktop_move_active_client_to_workspace(int workspace, int follow) {
  int idx = desktop_focused_client_index();
  int source_workspace = desktop_active_workspace;
  int moved_id;

  if (workspace < 1 || workspace > desktop_workspace_count) {
    return -1;
  }
  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].last_workspace = desktop_clients[idx].workspace;
  desktop_clients[idx].workspace = workspace;
  moved_id = desktop_clients[idx].id;
  desktop_workspace_mark_used(workspace);
  desktop_workspaces[workspace - 1].last_focused_client_id = moved_id;
  desktop_workspaces[workspace - 1].focus_generation =
      desktop_clients[idx].focus_generation;
  if (follow) {
    gui_desktop_switch_workspace(workspace);
    idx = desktop_client_index_by_id(moved_id);
    if (idx >= 0) {
      desktop_set_focused_client_index(idx);
    }
    desktop_start_transition("movetoworkspace", source_workspace, workspace);
  } else if (!desktop_client_on_workspace(&desktop_clients[idx],
                                          source_workspace)) {
    desktop_restore_focus_for_workspace(source_workspace);
    desktop_start_transition("movetoworkspacesilent", source_workspace,
                             workspace);
  } else {
    desktop_set_focused_client_index(idx);
    desktop_start_transition("movetoworkspace", source_workspace, workspace);
  }
  desktop_sync_terminal_compat();
  return 0;
}

int gui_desktop_move_terminal_to_workspace(int workspace) {
  return desktop_move_active_client_to_workspace(workspace, 0);
}

int gui_desktop_spawn_terminal_client(void) {
  return desktop_spawn_client("Terminal", "orizon-terminal", 1) > 0 ? 0 : -1;
}

int gui_desktop_spawn_app_client(const char *app, char *out, size_t out_size) {
  return desktop_spawn_known_app_client(app, out, out_size);
}

int gui_desktop_close_active_client(void) {
  int idx;

  if (!desktop_mode_enabled) {
    return -1;
  }
  idx = desktop_focused_client_index();
  if (idx < 0) {
    return -1;
  }
  desktop_workspace_forget_focus_id(desktop_clients[idx].id);
  desktop_focus_history_remove(desktop_clients[idx].id);
  desktop_clients[idx].visible = 0;
  desktop_clients[idx].id = 0;
  desktop_clients[idx].workspace = 0;
  desktop_clients[idx].last_workspace = 0;
  desktop_clients[idx].mapped = 0;
  desktop_clients[idx].hidden = 0;
  desktop_clients[idx].terminal_backed = 0;
  desktop_clients[idx].fullscreen = 0;
  desktop_clients[idx].pseudo = 0;
  desktop_clients[idx].pinned = 0;
  desktop_clients[idx].focus_history_id = -1;
  desktop_clients[idx].mapped_generation = 0;
  desktop_clients[idx].focus_generation = 0;
  desktop_clients[idx].rule_match_count = 0;
  desktop_clients[idx].rule_apply_count = 0;
  desktop_clients[idx].rule_actions[0] = '\0';
  desktop_clients[idx].title[0] = '\0';
  desktop_clients[idx].app_id[0] = '\0';
  desktop_set_focused_client_index(-1);
  desktop_focused_client_index();
  desktop_sync_terminal_compat();
  desktop_start_transition("client", desktop_active_workspace,
                           desktop_active_workspace);
  return 0;
}

int gui_desktop_focus_next_client(void) {
  return desktop_focus_relative(1);
}

int gui_desktop_focus_prev_client(void) {
  return desktop_focus_relative(-1);
}

int gui_desktop_dispatch(const char *dispatcher, const char *args, char *out,
                         size_t out_size) {
  uint32_t workspace = 0;
  const char *a = args ? args : "";

  if (out && out_size) {
    out[0] = '\0';
  }
  while (*a == ' ') {
    a++;
  }
  if (!dispatcher || !dispatcher[0]) {
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: usage exec <terminal|settings|logs|packages|update|launcher> | killactive | "
               "workspace <n|name:name|next|empty|+1|-1|previous> | renameworkspace <target> <name> | movetoworkspace <target> | movetoworkspacesilent <target> | "
               "movefocus <l|r|u|d|next|prev> | focuswindow <target> | swapwindow <l|r|u|d|next|prev> | fullscreen/fullscreenstate | pseudo/pseudotile | pin | swapnext | "
               "focusmaster | swapwithmaster | togglesplit | layoutmsg <msg> | "
               "resizeactive <x> <y> | submap <name>\n");
    }
    return -1;
  }
  if (strcmp(dispatcher, "exec") == 0) {
    return desktop_spawn_known_app_client(a, out, out_size);
  }
  if (strcmp(dispatcher, "killactive") == 0 || strcmp(dispatcher, "close") == 0) {
    if (gui_desktop_close_active_client() == 0) {
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: killed active client\n");
      }
      return 0;
    }
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: no active client\n");
    }
    return -1;
  }
  if (strcmp(dispatcher, "workspace") == 0) {
    int target = 0;
    if (desktop_parse_workspace_arg(a, &target) == 0 &&
        gui_desktop_switch_workspace(target) == 0) {
      workspace = (uint32_t)target;
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: workspace %u name=\"%s\"\n",
                 (unsigned)workspace, desktop_workspace_name(target));
      }
      return 0;
    }
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: workspace expects 1-%d, name:<name>, next, empty, +/-n, active or previous\n",
               desktop_workspace_count);
    }
    return -1;
  }
  if (strcmp(dispatcher, "renameworkspace") == 0) {
    char name[sizeof(desktop_workspaces[0].name)];
    char reason[96];
    int target = 0;

    reason[0] = '\0';
    if (desktop_parse_workspace_rename_arg(a, &target, name, sizeof(name)) <
            0 ||
        desktop_workspace_rename(target, name, reason, sizeof(reason)) < 0) {
      if (out && out_size) {
        if (reason[0]) {
          snprintf(out, out_size,
                   "desktop dispatch: renameworkspace failed: %s\n", reason);
        } else {
          snprintf(out, out_size,
                   "desktop dispatch: renameworkspace expects <1-%d|active|current|name:name> <safe-name>\n",
                   desktop_workspace_count);
        }
      }
      return -1;
    }
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: renameworkspace %d name=\"%s\" target=name:%s\n",
               target, desktop_workspace_name(target), desktop_workspace_name(target));
    }
    return 0;
  }
  if (strcmp(dispatcher, "movetoworkspace") == 0 ||
      strcmp(dispatcher, "movetoworkspacesilent") == 0) {
    int silent = strcmp(dispatcher, "movetoworkspacesilent") == 0;
    int target = 0;
    if (desktop_parse_workspace_arg(a, &target) == 0 &&
        desktop_move_active_client_to_workspace(target, silent ? 0 : 1) == 0) {
      workspace = (uint32_t)target;
      if (out && out_size) {
        snprintf(out, out_size,
                 silent
                     ? "desktop dispatch: silently moved active to workspace %u name=\"%s\" follow=no active=%d\n"
                     : "desktop dispatch: moved active to workspace %u name=\"%s\" follow=yes active=%d\n",
                 (unsigned)workspace, desktop_workspace_name(target),
                 desktop_active_workspace);
      }
      return 0;
    }
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: movetoworkspace expects active client and 1-%d, name:<name>, empty or +/-n\n",
               desktop_workspace_count);
    }
    return -1;
  }
  if (strcmp(dispatcher, "movefocus") == 0) {
    int dx = 0;
    int dy = 0;
    int delta = 1;
    int fallback = 0;
    int rc;
    if (desktop_parse_direction_arg(a, &dx, &dy, &delta) < 0) {
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: movefocus expects l/r/u/d/next/prev\n");
      }
      return -1;
    }
    rc = desktop_focus_directional(dx, dy, delta, &fallback);
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: movefocus %s%s\n",
               rc == 0 ? "ok" : "no-client",
               fallback && rc == 0 ? " fallback=cycle" : "");
    }
    return rc;
  }
  if (strcmp(dispatcher, "focuswindow") == 0 ||
      strcmp(dispatcher, "focus-window") == 0) {
    return desktop_focus_window_query(a, out, out_size);
  }
  if (strcmp(dispatcher, "cyclenext") == 0) {
    int rc = (strcmp(a, "prev") == 0 || strcmp(a, "previous") == 0 ||
              strcmp(a, "-1") == 0)
                 ? gui_desktop_focus_prev_client()
                 : gui_desktop_focus_next_client();
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: cyclenext %s\n",
               rc == 0 ? "ok" : "no-client");
    }
    return rc;
  }
  if (strcmp(dispatcher, "swapnext") == 0) {
    int rc = (strcmp(a, "prev") == 0 || strcmp(a, "previous") == 0 ||
              strcmp(a, "-1") == 0)
                 ? desktop_swap_relative(-1)
                 : desktop_swap_relative(1);
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: swapnext %s\n",
               rc == 0 ? "ok" : "needs-two-clients");
    }
    return rc;
  }
  if (strcmp(dispatcher, "swapwindow") == 0) {
    int dx = 0;
    int dy = 0;
    int delta = 1;
    int fallback = 0;
    int rc;
    if (desktop_parse_direction_arg(a, &dx, &dy, &delta) < 0) {
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: swapwindow expects l/r/u/d/next/prev\n");
      }
      return -1;
    }
    rc = desktop_swap_directional(dx, dy, delta, &fallback);
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: swapwindow %s%s\n",
               rc == 0 ? "ok" : "needs-two-clients",
               fallback && rc == 0 ? " fallback=cycle" : "");
    }
    return rc;
  }
  if (strcmp(dispatcher, "focusmaster") == 0) {
    int rc = desktop_focus_master_client();
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: focusmaster %s\n",
               rc == 0 ? "ok" : "no-client");
    }
    return rc;
  }
  if (strcmp(dispatcher, "swapwithmaster") == 0) {
    int rc = desktop_swap_with_master();
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: swapwithmaster %s\n",
               rc == 0 ? "ok" : "needs-two-clients");
    }
    return rc;
  }
  if (strcmp(dispatcher, "togglesplit") == 0) {
    desktop_cycle_split_mode(1);
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: togglesplit split=%s ratio=%d master=%d nmaster=%d\n",
               desktop_split_mode_name(), desktop_split_ratio_percent,
               desktop_master_ratio_percent, desktop_master_count);
    }
    return 0;
  }
  if (strcmp(dispatcher, "resizeactive") == 0 ||
      strcmp(dispatcher, "resizewindowpixel") == 0) {
    const char *next_arg = a;
    int dx = 0;
    int dy = 0;

    if (desktop_parse_int_arg(next_arg, &dx) < 0) {
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: resizeactive expects <x-delta> <y-delta>\n");
      }
      return -1;
    }
    while (*next_arg && *next_arg != ' ') {
      next_arg++;
    }
    while (*next_arg == ' ') {
      next_arg++;
    }
    if (*next_arg) {
      desktop_parse_int_arg(next_arg, &dy);
    }
    if (dx == 0 && dy == 0) {
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: resizeactive no-op split=%d master=%d nmaster=%d\n",
                 desktop_split_ratio_percent, desktop_master_ratio_percent,
                 desktop_master_count);
      }
      return 0;
    }
    if (dx != 0 && (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
      desktop_adjust_split_ratio(dx > 0 ? 5 : -5);
    } else {
      desktop_adjust_master_ratio(dy > 0 ? 5 : -5);
    }
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: resizeactive split=%s ratio=%d master=%d nmaster=%d note=tiling-ratio-only\n",
               desktop_split_mode_name(), desktop_split_ratio_percent,
               desktop_master_ratio_percent, desktop_master_count);
    }
    return 0;
  }
  if (strcmp(dispatcher, "layoutmsg") == 0) {
    int value = 0;
    const char *layout_arg = NULL;
    if (strncmp(a, "layout", 6) == 0) {
      layout_arg = a + 6;
    } else if (strncmp(a, "setlayout", 9) == 0) {
      layout_arg = a + 9;
    }
    if (layout_arg) {
      while (*layout_arg == ' ') {
        layout_arg++;
      }
      if (strcmp(layout_arg, "dwindle") == 0 ||
          strcmp(layout_arg, "master") == 0 ||
          strcmp(layout_arg, "monocle") == 0) {
        desktop_set_layout_mode(desktop_layout_mode_from_name(layout_arg));
        if (out && out_size) {
          snprintf(out, out_size,
                   "desktop dispatch: layoutmsg layout %s workspace=%d split=%s ratio=%d master=%d nmaster=%d\n",
                   desktop_layout_engine(), desktop_active_workspace,
                   desktop_split_mode_name(), desktop_split_ratio_percent,
                   desktop_master_ratio_percent, desktop_master_count);
        }
        return 0;
      }
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg layout expects dwindle, master or monocle\n");
      }
      return -1;
    }
    if (strcmp(a, "togglesplit") == 0 ||
        strcmp(a, "orientationnext") == 0 ||
        strcmp(a, "orientationcycle") == 0) {
      desktop_cycle_split_mode(1);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d nmaster=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent, desktop_master_count);
      }
      return 0;
    }
    if (strcmp(a, "orientationauto") == 0 ||
        strcmp(a, "orientationcenter") == 0) {
      desktop_set_split_mode(0);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d nmaster=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent, desktop_master_count);
      }
      return 0;
    }
    if (strcmp(a, "orientationleft") == 0 ||
        strcmp(a, "orientationright") == 0 ||
        strcmp(a, "orientationvertical") == 0) {
      desktop_set_split_mode(1);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d nmaster=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent, desktop_master_count);
      }
      return 0;
    }
    if (strcmp(a, "orientationtop") == 0 ||
        strcmp(a, "orientationbottom") == 0 ||
        strcmp(a, "orientationhorizontal") == 0) {
      desktop_set_split_mode(2);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d nmaster=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent, desktop_master_count);
      }
      return 0;
    }
    if (strcmp(a, "orientationprev") == 0) {
      desktop_cycle_split_mode(-1);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d nmaster=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent, desktop_master_count);
      }
      return 0;
    }
    if (strncmp(a, "splitratio", 10) == 0) {
      const char *ratio_arg = a + 10;
      while (*ratio_arg == ' ') {
        ratio_arg++;
      }
      if (desktop_parse_ratio_arg(ratio_arg, desktop_split_ratio_percent,
                                  &value) == 0 &&
          desktop_set_split_ratio(value) == 0) {
        if (out && out_size) {
          snprintf(out, out_size,
                   "desktop dispatch: layoutmsg splitratio %d split=%s master=%d nmaster=%d\n",
                   desktop_split_ratio_percent, desktop_split_mode_name(),
                   desktop_master_ratio_percent, desktop_master_count);
        }
        return 0;
      }
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg splitratio expects 10-90 or +/-delta\n");
      }
      return -1;
    }
    if (strncmp(a, "masterratio", 11) == 0 || strncmp(a, "mfact", 5) == 0) {
      const char *ratio_arg =
          strncmp(a, "masterratio", 11) == 0 ? a + 11 : a + 5;
      while (*ratio_arg == ' ') {
        ratio_arg++;
      }
      if (desktop_parse_ratio_arg(ratio_arg, desktop_master_ratio_percent,
                                  &value) == 0 &&
          desktop_set_master_ratio(value) == 0) {
        if (out && out_size) {
          snprintf(out, out_size,
                   "desktop dispatch: layoutmsg masterratio %d split=%s ratio=%d\n",
                   desktop_master_ratio_percent, desktop_split_mode_name(),
                   desktop_split_ratio_percent);
        }
        return 0;
      }
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg masterratio expects 10-90 or +/-delta\n");
      }
      return -1;
    }
    if (strncmp(a, "nmaster", 7) == 0 ||
        strncmp(a, "mastercount", 11) == 0 ||
        strncmp(a, "masters", 7) == 0) {
      const char *count_arg = strncmp(a, "mastercount", 11) == 0
                                  ? a + 11
                                  : a + 7;
      while (*count_arg == ' ') {
        count_arg++;
      }
      if ((count_arg[0] == '+' || count_arg[0] == '-') &&
          desktop_parse_int_arg(count_arg, &value) == 0 &&
          desktop_adjust_master_count(value) == 0) {
        if (out && out_size) {
          snprintf(out, out_size,
                   "desktop dispatch: layoutmsg nmaster %d master=%d split=%s ratio=%d\n",
                   desktop_master_count, desktop_master_ratio_percent,
                   desktop_split_mode_name(), desktop_split_ratio_percent);
        }
        return 0;
      }
      if (desktop_parse_int_arg(count_arg, &value) == 0 &&
          desktop_set_master_count(value) == 0) {
        if (out && out_size) {
          snprintf(out, out_size,
                   "desktop dispatch: layoutmsg nmaster %d master=%d split=%s ratio=%d\n",
                   desktop_master_count, desktop_master_ratio_percent,
                   desktop_split_mode_name(), desktop_split_ratio_percent);
        }
        return 0;
      }
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg nmaster expects 1-%d or +/-delta\n",
                 DESKTOP_MAX_CLIENTS);
      }
      return -1;
    }
    if (strncmp(a, "addmaster", 9) == 0 ||
        strncmp(a, "removemaster", 12) == 0) {
      const int remove = strncmp(a, "removemaster", 12) == 0;
      const char *count_arg = a + (remove ? 12 : 9);
      int delta = remove ? -1 : 1;
      while (*count_arg == ' ') {
        count_arg++;
      }
      if (*count_arg) {
        if (desktop_parse_int_arg(count_arg, &value) < 0) {
          if (out && out_size) {
            snprintf(out, out_size,
                     "desktop dispatch: layoutmsg %s expects an optional positive count\n",
                     remove ? "removemaster" : "addmaster");
          }
          return -1;
        }
        if (value < 0) {
          value = -value;
        }
        delta = remove ? -value : value;
      }
      if (desktop_adjust_master_count(delta) == 0) {
        if (out && out_size) {
          snprintf(out, out_size,
                   "desktop dispatch: layoutmsg %s nmaster=%d master=%d split=%s ratio=%d\n",
                   remove ? "removemaster" : "addmaster",
                   desktop_master_count, desktop_master_ratio_percent,
                   desktop_split_mode_name(), desktop_split_ratio_percent);
        }
        return 0;
      }
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg %s keeps nmaster within 1-%d\n",
                 remove ? "removemaster" : "addmaster",
                 DESKTOP_MAX_CLIENTS);
      }
      return -1;
    }
    if (strcmp(a, "focusmaster") == 0) {
      int rc = desktop_focus_master_client();
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: layoutmsg focusmaster %s\n",
                 rc == 0 ? "ok" : "no-client");
      }
      return rc;
    }
    if (strcmp(a, "swapwithmaster") == 0) {
      int rc = desktop_swap_with_master();
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg swapwithmaster %s\n",
                 rc == 0 ? "ok" : "needs-two-clients");
      }
      return rc;
    }
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: layoutmsg supports togglesplit, "
               "orientationnext, orientationprev, orientationleft/right/top/bottom, "
               "layout <dwindle|master|monocle>, "
               "splitratio <10-90|+/-n>, masterratio <10-90|+/-n>, "
               "nmaster <1-8|+/-n>, addmaster, removemaster, "
               "focusmaster, swapwithmaster\n");
    }
    return -1;
  }
  if (strcmp(dispatcher, "submap") == 0) {
    const char *name = *a ? a : "default";
    if (strcmp(name, "reset") == 0) {
      name = "default";
    }
    if (desktop_set_submap(name) == 0) {
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: submap %s\n",
                 desktop_submap);
      }
      return 0;
    }
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: submap expects a safe name or reset\n");
    }
    return -1;
  }
  if (strcmp(dispatcher, "fullscreen") == 0 ||
      strcmp(dispatcher, "fullscreenstate") == 0) {
    int desired = 0;
    int idx = desktop_focused_client_index();
    int state;
    if (idx < 0) {
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: %s no-client\n",
                 dispatcher);
      }
      return -1;
    }
    if (desktop_parse_bool_state_arg(a, desktop_clients[idx].fullscreen,
                                     &desired) < 0) {
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: %s expects on/off/toggle/1/0\n",
                 dispatcher);
      }
      return -1;
    }
    state = desktop_set_active_fullscreen(desired);
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: %s %s\n", dispatcher,
               state ? "on" : "off");
    }
    return 0;
  }
  if (strcmp(dispatcher, "pseudo") == 0 || strcmp(dispatcher, "pseudotile") == 0) {
    int desired = 0;
    int idx = desktop_focused_client_index();
    int state;
    if (idx < 0) {
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: %s no-client\n",
                 dispatcher);
      }
      return -1;
    }
    if (desktop_parse_bool_state_arg(a, desktop_clients[idx].pseudo,
                                     &desired) < 0) {
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: %s expects on/off/toggle/1/0\n",
                 dispatcher);
      }
      return -1;
    }
    state = desktop_set_active_pseudo(desired);
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: %s %s\n", dispatcher,
               state ? "on" : "off");
    }
    return 0;
  }
  if (strcmp(dispatcher, "pin") == 0) {
    int desired = 0;
    int idx = desktop_focused_client_index();
    int state;
    if (idx < 0) {
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: pin no-client\n");
      }
      return -1;
    }
    if (desktop_parse_bool_state_arg(a, desktop_clients[idx].pinned,
                                     &desired) < 0) {
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: pin expects on/off/toggle/1/0\n");
      }
      return -1;
    }
    state = desktop_set_active_pin(desired);
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: pin %s\n",
               state ? "on" : "off");
    }
    return 0;
  }
  if (out && out_size) {
    snprintf(out, out_size, "desktop dispatch: unknown '%s'\n", dispatcher);
  }
  return -1;
}

static int desktop_last_focused_index_on_workspace(int workspace) {
  return desktop_last_focused_index_on_workspace_scope(workspace, 0);
}

static int desktop_focus_rank_for_id(int id) {
  desktop_focus_history_compact();
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_focus_history[i] == id) {
      return i;
    }
  }
  return -1;
}

typedef struct {
  int used;
  int line_number;
  char kind[16];
  char action[48];
  char selectors[128];
  char raw[176];
} desktop_rule_match_t;

static int desktop_rule_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *desktop_rule_find_char(const char *text, char needle) {
  if (!text) {
    return NULL;
  }
  while (*text) {
    if (*text == needle) {
      return text;
    }
    text++;
  }
  return NULL;
}

static void desktop_rule_append_text(char *out, size_t out_size, size_t *used,
                                     const char *text) {
  size_t len;

  if (!out || !used || !text || *used >= out_size) {
    return;
  }
  len = strlen(text);
  if (*used + len >= out_size) {
    len = out_size - *used - 1;
  }
  memcpy(out + *used, text, len);
  *used += len;
  out[*used] = '\0';
}

static void desktop_rule_copy_trim(char *out, size_t out_size,
                                   const char *start, size_t len) {
  size_t first = 0;
  size_t last = len;
  size_t copy_len;

  if (!out || out_size == 0) {
    return;
  }
  if (!start) {
    out[0] = '\0';
    return;
  }
  while (first < len && desktop_rule_is_space(start[first])) {
    first++;
  }
  while (last > first && desktop_rule_is_space(start[last - 1])) {
    last--;
  }
  copy_len = last - first;
  if (copy_len >= out_size) {
    copy_len = out_size - 1;
  }
  memcpy(out, start + first, copy_len);
  out[copy_len] = '\0';
}

static int desktop_rule_kind_matches(const char *line, const char *kind) {
  size_t len;

  if (!line || !kind) {
    return 0;
  }
  len = strlen(kind);
  if (strncmp(line, kind, len) != 0) {
    return 0;
  }
  return line[len] == '\0' || desktop_rule_is_space(line[len]) ||
         line[len] == '=';
}

static int desktop_rule_parse_line(const char *line, int line_number,
                                   desktop_rule_match_t *rule) {
  const char *eq;
  const char *comma;
  char key[32];
  size_t key_len;

  if (!line || !rule) {
    return 0;
  }
  memset(rule, 0, sizeof(*rule));
  rule->line_number = line_number;
  snprintf(rule->raw, sizeof(rule->raw), "%s", line);
  if (!line[0] || line[0] == '#') {
    return 0;
  }
  if (!desktop_rule_kind_matches(line, "windowrulev2") &&
      !desktop_rule_kind_matches(line, "windowrule")) {
    return 0;
  }
  eq = desktop_rule_find_char(line, '=');
  if (!eq) {
    return 0;
  }
  key_len = (size_t)(eq - line);
  desktop_rule_copy_trim(key, sizeof(key), line, key_len);
  snprintf(rule->kind, sizeof(rule->kind), "%s", key);
  comma = desktop_rule_find_char(eq + 1, ',');
  if (comma) {
    desktop_rule_copy_trim(rule->action, sizeof(rule->action), eq + 1,
                           (size_t)(comma - (eq + 1)));
    desktop_rule_copy_trim(rule->selectors, sizeof(rule->selectors), comma + 1,
                           strlen(comma + 1));
  } else {
    desktop_rule_copy_trim(rule->action, sizeof(rule->action), eq + 1,
                           strlen(eq + 1));
    rule->selectors[0] = '\0';
  }
  rule->used = 1;
  return 1;
}

static void desktop_rule_simplify_pattern(char *out, size_t out_size,
                                          const char *pattern) {
  const char *start = pattern;
  size_t len;

  if (!out || out_size == 0) {
    return;
  }
  if (!pattern) {
    out[0] = '\0';
    return;
  }
  len = strlen(pattern);
  while (len > 0 && desktop_rule_is_space(start[0])) {
    start++;
    len--;
  }
  while (len > 0 && desktop_rule_is_space(start[len - 1])) {
    len--;
  }
  while (len > 0 && (start[0] == '^' || start[0] == '(')) {
    start++;
    len--;
  }
  while (len > 0 && (start[len - 1] == '$' || start[len - 1] == ')')) {
    len--;
  }
  if (len >= out_size) {
    len = out_size - 1;
  }
  memcpy(out, start, len);
  out[len] = '\0';
}

static int desktop_rule_pattern_matches(const char *pattern, const char *value,
                                        char *style, size_t style_size) {
  char clean[96];
  char chunk[96];
  char *wild;
  size_t chunk_len;

  if (style && style_size) {
    snprintf(style, style_size, "%s", "miss");
  }
  if (!pattern || !value || !value[0]) {
    return 0;
  }
  desktop_rule_simplify_pattern(clean, sizeof(clean), pattern);
  if (!clean[0] || strcmp(clean, "*") == 0 || strcmp(clean, ".*") == 0) {
    if (style && style_size) {
      snprintf(style, style_size, "%s", "wildcard");
    }
    return 1;
  }
  wild = strstr(clean, ".*");
  if (wild) {
    chunk_len = (size_t)(wild - clean);
    if (chunk_len > 0) {
      if (chunk_len >= sizeof(chunk)) {
        chunk_len = sizeof(chunk) - 1;
      }
      memcpy(chunk, clean, chunk_len);
      chunk[chunk_len] = '\0';
      if (strncmp(value, chunk, chunk_len) == 0 || strstr(value, chunk)) {
        if (style && style_size) {
          snprintf(style, style_size, "%s", "prefix");
        }
        return 1;
      }
    }
    if (wild[2]) {
      snprintf(chunk, sizeof(chunk), "%s", wild + 2);
      if (strstr(value, chunk)) {
        if (style && style_size) {
          snprintf(style, style_size, "%s", "contains");
        }
        return 1;
      }
    }
    return 0;
  }
  if (strcmp(value, clean) == 0) {
    if (style && style_size) {
      snprintf(style, style_size, "%s", "exact");
    }
    return 1;
  }
  if (strstr(value, clean)) {
    if (style && style_size) {
      snprintf(style, style_size, "%s", "contains");
    }
    return 1;
  }
  return 0;
}

static int desktop_rule_key_is_class(const char *key) {
  return key && (strstr(key, "class") || strstr(key, "Class"));
}

static int desktop_rule_key_is_title(const char *key) {
  return key && (strstr(key, "title") || strstr(key, "Title"));
}

static int desktop_rule_key_is_app(const char *key) {
  return key && (strstr(key, "app") || strstr(key, "App"));
}

static int desktop_rule_match_token(const char *token,
                                    const desktop_client_t *client,
                                    char *reason, size_t reason_size) {
  char local[128];
  char key[32];
  char pattern[96];
  char style[24];
  const char *sep;
  const char *target = NULL;
  const char *target_name = NULL;

  if (!token || !client) {
    return -1;
  }
  desktop_rule_copy_trim(local, sizeof(local), token, strlen(token));
  sep = desktop_rule_find_char(local, ':');
  if (!sep) {
    sep = desktop_rule_find_char(local, '=');
  }
  if (!sep) {
    return -1;
  }
  desktop_rule_copy_trim(key, sizeof(key), local, (size_t)(sep - local));
  desktop_rule_copy_trim(pattern, sizeof(pattern), sep + 1, strlen(sep + 1));
  if (desktop_rule_key_is_class(key)) {
    target = client->app_id;
    target_name = "class";
  } else if (desktop_rule_key_is_title(key)) {
    target = client->title;
    target_name = "title";
  } else if (desktop_rule_key_is_app(key)) {
    target = client->app_id;
    target_name = "app";
  } else {
    return -1;
  }
  if (desktop_rule_pattern_matches(pattern, target, style, sizeof(style))) {
    if (reason && reason_size) {
      snprintf(reason, reason_size, "%s-%s", target_name, style);
    }
    return 1;
  }
  if (reason && reason_size) {
    snprintf(reason, reason_size, "%s-miss", target_name);
  }
  return 0;
}

static int desktop_rule_matches_client(const desktop_rule_match_t *rule,
                                       const desktop_client_t *client,
                                       char *reason, size_t reason_size) {
  const char *p;
  int selector_count = 0;

  if (reason && reason_size) {
    snprintf(reason, reason_size, "%s", "selector-miss");
  }
  if (!rule || !client || !rule->used || !rule->selectors[0]) {
    if (reason && reason_size) {
      snprintf(reason, reason_size, "%s", "selector-missing");
    }
    return 0;
  }
  p = rule->selectors;
  while (*p) {
    const char *comma = desktop_rule_find_char(p, ',');
    char token[128];
    int rc;

    if (comma) {
      desktop_rule_copy_trim(token, sizeof(token), p, (size_t)(comma - p));
    } else {
      desktop_rule_copy_trim(token, sizeof(token), p, strlen(p));
    }
    rc = desktop_rule_match_token(token, client, reason, reason_size);
    if (rc >= 0) {
      selector_count++;
    }
    if (rc > 0) {
      return 1;
    }
    if (!comma) {
      break;
    }
    p = comma + 1;
  }
  if (selector_count == 0) {
    if (strstr(rule->selectors, client->app_id) ||
        strstr(rule->selectors, client->title)) {
      if (reason && reason_size) {
        snprintf(reason, reason_size, "%s", "literal-fallback");
      }
      return 1;
    }
    if (reason && reason_size) {
      snprintf(reason, reason_size, "%s", "unsupported-selector");
    }
  }
  return 0;
}

static int desktop_rule_load_runtime(char *cfg, size_t cfg_size) {
  file_t *f;
  ssize_t n;

  if (!cfg || cfg_size == 0) {
    return -1;
  }
  cfg[0] = '\0';
  f = vfs_open(ORIZON_DESKTOP_RULES_PATH, O_RDONLY);
  if (f) {
    n = vfs_read(f, cfg, cfg_size - 1);
    vfs_close(f);
    if (n > 0) {
      cfg[n] = '\0';
      return (int)n;
    }
  }
  snprintf(cfg, cfg_size,
           "# fallback when runtime rules are not mounted yet\n"
           "windowrulev2 = tile,class:^(orizon-.*)$\n");
  return (int)strlen(cfg);
}

static int desktop_rule_action_is(const char *action, const char *name) {
  size_t len;

  if (!action || !name) {
    return 0;
  }
  len = strlen(name);
  if (strncmp(action, name, len) != 0) {
    return 0;
  }
  return action[len] == '\0' || desktop_rule_is_space(action[len]) ||
         action[len] == ':' || action[len] == '=';
}

static int desktop_rule_parse_workspace_target(const char *action) {
  const char *p;
  int workspace = 0;
  int saw_digit = 0;

  if (!action || !desktop_rule_action_is(action, "workspace")) {
    return -1;
  }
  p = action + 9;
  while (*p && (desktop_rule_is_space(*p) || *p == ':' || *p == '=')) {
    p++;
  }
  while (*p >= '0' && *p <= '9') {
    saw_digit = 1;
    workspace = workspace * 10 + (*p - '0');
    p++;
  }
  if (!saw_digit || workspace < 1 || workspace > desktop_workspace_count) {
    return -1;
  }
  return workspace;
}

static void desktop_rule_record_client_action(desktop_client_t *client,
                                              const char *action,
                                              int applied) {
  char label[56];
  size_t used;
  size_t label_len;

  if (!client || !action || !action[0]) {
    return;
  }
  snprintf(label, sizeof(label), "%s%s", applied ? "" : "ignored:", action);
  for (size_t i = 0; label[i]; i++) {
    if (desktop_rule_is_space(label[i])) {
      label[i] = ':';
    }
  }
  used = strlen(client->rule_actions);
  if (used + 1 >= sizeof(client->rule_actions)) {
    return;
  }
  if (used > 0) {
    client->rule_actions[used++] = ',';
    client->rule_actions[used] = '\0';
  }
  label_len = strlen(label);
  if (used + label_len >= sizeof(client->rule_actions)) {
    label_len = sizeof(client->rule_actions) - used - 1;
  }
  memcpy(client->rule_actions + used, label, label_len);
  client->rule_actions[used + label_len] = '\0';
}

static int desktop_rule_apply_action_to_client(
    const desktop_rule_match_t *rule, desktop_client_t *client) {
  int workspace;

  if (!rule || !client || !rule->action[0]) {
    return 0;
  }
  if (desktop_rule_action_is(rule->action, "tile")) {
    desktop_rule_record_client_action(client, "tile", 1);
    return 1;
  }
  if (desktop_rule_action_is(rule->action, "fullscreen")) {
    client->fullscreen = 1;
    desktop_rule_record_client_action(client, "fullscreen", 1);
    return 1;
  }
  if (desktop_rule_action_is(rule->action, "pseudo") ||
      desktop_rule_action_is(rule->action, "pseudotile")) {
    client->pseudo = 1;
    desktop_rule_record_client_action(client, "pseudo", 1);
    return 1;
  }
  if (desktop_rule_action_is(rule->action, "pin")) {
    client->pinned = 1;
    desktop_rule_record_client_action(client, "pin", 1);
    return 1;
  }
  workspace = desktop_rule_parse_workspace_target(rule->action);
  if (workspace > 0) {
    if (client->workspace != workspace) {
      client->last_workspace = client->workspace;
      client->workspace = workspace;
      desktop_workspace_mark_used(workspace);
    }
    desktop_rule_record_client_action(client, rule->action, 1);
    return 1;
  }
  if (desktop_rule_action_is(rule->action, "float") ||
      desktop_rule_action_is(rule->action, "floating") ||
      desktop_rule_action_is(rule->action, "move") ||
      desktop_rule_action_is(rule->action, "center")) {
    desktop_rule_record_client_action(client, rule->action, 0);
    return 0;
  }
  desktop_rule_record_client_action(client, rule->action, 0);
  return 0;
}

static int desktop_rule_action_is_safe(const char *action) {
  if (!action || !action[0]) {
    return 0;
  }
  return desktop_rule_action_is(action, "tile") ||
         desktop_rule_action_is(action, "fullscreen") ||
         desktop_rule_action_is(action, "pseudo") ||
         desktop_rule_action_is(action, "pseudotile") ||
         desktop_rule_action_is(action, "pin") ||
         desktop_rule_parse_workspace_target(action) > 0;
}

static void desktop_apply_spawn_rules_to_client(int idx) {
  char cfg[1024];
  char line[192];
  const char *p;
  int line_number = 1;
  desktop_client_t *client;

  if (idx < 0 || idx >= DESKTOP_MAX_CLIENTS || !desktop_clients[idx].visible) {
    return;
  }
  client = &desktop_clients[idx];
  desktop_rule_load_runtime(cfg, sizeof(cfg));
  p = cfg;
  while (*p) {
    const char *start = p;
    size_t len;
    desktop_rule_match_t rule;
    char reason[40];

    while (*p && *p != '\n') {
      p++;
    }
    len = (size_t)(p - start);
    if (*p == '\n') {
      p++;
    }
    desktop_rule_copy_trim(line, sizeof(line), start, len);
    if (desktop_rule_parse_line(line, line_number, &rule) &&
        desktop_rule_matches_client(&rule, client, reason, sizeof(reason))) {
      client->rule_match_count++;
      if (desktop_rule_apply_action_to_client(&rule, client)) {
        client->rule_apply_count++;
      }
    }
    line_number++;
  }
}

static int desktop_workspace_is_used(int workspace) {
  int idx = workspace - 1;

  if (workspace == desktop_active_workspace ||
      workspace == desktop_previous_workspace ||
      desktop_workspace_local_client_count(workspace) > 0) {
    return 1;
  }
  if (workspace >= 1 && workspace <= DESKTOP_MAX_WORKSPACES &&
      desktop_workspaces[idx].used) {
    return 1;
  }
  return 0;
}

void gui_desktop_format_workspaces(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop workspaces\n"
                   "active: %d\n"
                   "previous: %d\n"
                   "count: %d\n"
                   "max: %d\n"
                   "model: dynamic workspaces with %s tiling\n"
                   "submap: %s\n",
                   desktop_active_workspace, desktop_previous_workspace,
                   desktop_workspace_count, DESKTOP_MAX_WORKSPACES,
                   desktop_layout_engine(), desktop_submap);
  for (int i = 1; i <= desktop_workspace_count && used < out_size; i++) {
    int count = desktop_client_count_on_workspace(i);
    int local_count = desktop_workspace_local_client_count(i);
    int last_idx = desktop_last_focused_index_on_workspace(i);
    int state_idx = i - 1;
    int ws_used = desktop_workspace_is_used(i);
    int ws_split = desktop_split_mode_for_workspace(i);
    int ws_split_ratio = desktop_split_ratio_for_workspace(i);
    int ws_master_ratio = desktop_master_ratio_for_workspace(i);
    int ws_master_count = desktop_master_count_for_workspace(i);
    const char *state = i == desktop_active_workspace
                            ? "active"
                            : (i == desktop_previous_workspace ? "previous"
                                                               : (ws_used ? "used" : "empty"));
    used += snprintf(
        out + used, out_size - used,
        "workspace %d: name=\"%s\" state=%s clients=%d local=%d "
        "lastwindow=0x%x lasttitle=\"%s\" rememberedFocus=0x%x focusSeq=%llu visitSeq=%llu layout=%s "
        "split=%s ratio=%d master=%d nmaster=%d layoutSeq=%llu "
        "dynamic=%s pinned-aware=yes\n",
        i, desktop_workspace_name(i), state, count, local_count,
        last_idx >= 0 ? desktop_client_address(&desktop_clients[last_idx]) : 0,
        last_idx >= 0 ? desktop_clients[last_idx].title : "none",
        desktop_workspaces[state_idx].last_focused_client_id > 0
            ? DESKTOP_CLIENT_ADDRESS_BASE +
                  ((uint32_t)desktop_workspaces[state_idx].last_focused_client_id *
                   0x100u)
            : 0,
        (unsigned long long)desktop_workspaces[state_idx].focus_generation,
        (unsigned long long)desktop_workspaces[state_idx].visit_generation,
        desktop_layout_engine_for_workspace(i),
        desktop_split_mode_name_for_value(ws_split), ws_split_ratio,
        ws_master_ratio, ws_master_count,
        (unsigned long long)desktop_workspaces[state_idx].layout_generation,
        ws_used ? "yes" : "available");
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: desktop dispatch workspace <1-%d|name:name|next|empty|+1|-1|previous> | "
             "desktop dispatch renameworkspace <target> <name> | "
             "desktop dispatch movetoworkspace <1-%d|name:name|empty|+1|-1> | "
             "desktop dispatch movetoworkspacesilent <target> | "
             "desktop dispatch submap <name|reset>\n",
             desktop_workspace_count, desktop_workspace_count);
  }
}

void gui_desktop_format_windows(char *out, size_t out_size) {
  size_t used = 0;
  int total = 0;
  int focused_idx;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  focused_idx = desktop_focused_client_index();
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible) {
      total++;
    }
  }
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop windows\n"
                   "layout-engine: %s dynamic tiling\n"
                   "configured-layout: %s\n"
                   "known-windows: %d\n"
                   "active-workspace: %d\n"
                   "focused-client: %d\n"
                   "launcher: %s overlay=yes workspace=global\n"
                   "bar: %s layer=top\n"
                   "split-mode: %s\n"
                   "split-ratio: %d\n"
                   "master-ratio: %d\n"
                   "master-count: %d\n"
                   "submap: %s\n",
                   desktop_layout_engine(),
                   desktop_session.layout, total, desktop_active_workspace,
                   focused_idx >= 0 ? desktop_clients[focused_idx].id : 0,
                   desktop_launcher_visible ? "open" : "closed",
                   desktop_session.bar_enabled ? "visible" : "hidden",
                   desktop_split_mode_name(), desktop_split_ratio_percent,
                   desktop_master_ratio_percent, desktop_master_count,
                   desktop_submap);
  for (int i = 0; i < DESKTOP_MAX_CLIENTS && used < out_size; i++) {
    int rx;
    int ry;
    int rw;
    int rh;
    if (!desktop_clients[i].visible) {
      continue;
    }
    desktop_client_rect(i, &rx, &ry, &rw, &rh);
    used += snprintf(
        out + used, out_size - used,
        "client address=0x%x id=%d mapped=%s hidden=%s at=%d,%d size=%dx%d "
        "workspace=%d workspaceName=\"%s\" title=\"%s\" class=%s app=%s tiled=yes floating=no "
        "fullscreen=%s pseudo=%s pinned=%s focused=%s focusHistoryID=%d "
        "rulesMatched=%d rulesApplied=%d ruleActions=\"%s\" "
        "lastWorkspace=%d mappedSeq=%llu focusSeq=%llu backend=%s\n",
        desktop_client_address(&desktop_clients[i]), desktop_clients[i].id,
        desktop_clients[i].mapped ? "true" : "false",
        desktop_clients[i].hidden ? "true" : "false", rx, ry, rw, rh,
        desktop_clients[i].workspace,
        desktop_workspace_name(desktop_clients[i].workspace),
        desktop_clients[i].title, desktop_clients[i].app_id,
        desktop_clients[i].app_id,
        desktop_clients[i].fullscreen ? "yes" : "no",
        desktop_clients[i].pseudo ? "yes" : "no",
        desktop_clients[i].pinned ? "yes" : "no",
        desktop_clients[i].id == desktop_focused_client_id ? "yes" : "no",
        desktop_clients[i].focus_history_id, desktop_clients[i].rule_match_count,
        desktop_clients[i].rule_apply_count,
        desktop_clients[i].rule_actions[0] ? desktop_clients[i].rule_actions
                                           : "none",
        desktop_clients[i].last_workspace,
        (unsigned long long)desktop_clients[i].mapped_generation,
        (unsigned long long)desktop_clients[i].focus_generation,
        desktop_client_backend_name(&desktop_clients[i]));
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: exec terminal|settings|logs|packages|update | "
             "killactive | movefocus l|r|u|d|next|prev | "
             "focuswindow <id|0xaddr|class:app|title:text> | "
             "cyclenext | swapnext | swapwindow l|r|u|d | focusmaster | swapwithmaster | "
             "fullscreen [on|off|toggle] | fullscreenstate <on|off|1|0> | pseudo|pseudotile [on|off|toggle] | pin [on|off|toggle] | "
             "workspace <n|name:name|next|empty|+1|-1|previous> | "
             "renameworkspace <target> <name> | "
             "movetoworkspace <n|name:name|empty|+1|-1> | "
             "togglesplit | layoutmsg <msg> | resizeactive <x> <y> | "
             "submap <name>\n"
             "rules: class/title/app selectors applied-on-spawn via %s\n"
             "focus-history: desktop focus-history | desktop hyprctl focushistory\n"
             "limits: no mouse-drag window moving; true Wayland clients are future work\n",
             ORIZON_DESKTOP_RULES_PATH);
  }
}

void gui_desktop_format_client_model(char *out, size_t out_size) {
  size_t used = 0;
  int total = 0;
  int mapped = 0;
  int hidden = 0;
  int pinned = 0;
  int fullscreen = 0;
  int pseudo = 0;
  int active_idx;
  int used_workspaces = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_focus_history_compact();
  active_idx = desktop_focused_client_index();
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (!desktop_clients[i].visible) {
      continue;
    }
    total++;
    if (desktop_clients[i].mapped) {
      mapped++;
    }
    if (desktop_clients[i].hidden) {
      hidden++;
    }
    if (desktop_clients[i].pinned) {
      pinned++;
    }
    if (desktop_clients[i].fullscreen) {
      fullscreen++;
    }
    if (desktop_clients[i].pseudo) {
      pseudo++;
    }
  }
  for (int ws = 1; ws <= desktop_workspace_count; ws++) {
    if (desktop_workspace_is_used(ws)) {
      used_workspaces++;
    }
  }

  used += snprintf(
      out + used, out_size - used,
      "Orizon desktop client model\n"
      "version: " ORIZON_DESKTOP_PACKAGE_VERSION "\n"
      "model: Hyprland-style internal tiled clients; manual-drag=no floating=no taskbar=no\n"
      "backend: framebuffer-vm protocol=orizon-desktop-ipc-v0 wayland=no wlroots=no\n"
      "layout: engine=%s configured=%s split=%s ratio=%d master=%d nmaster=%d submap=%s\n"
      "active: workspace=%d name=\"%s\" client=0x%x focusHistoryID=%d focusSeq=%llu\n"
      "summary: clients=%d mapped=%d hidden=%d pinned=%d fullscreen=%d pseudo=%d workspaces-used=%d/%d\n"
      "rules: runtime=%s selectors=class/title/app spawn-apply=tile/fullscreen/pseudo/pin/workspace\n",
      desktop_layout_engine(), desktop_session.layout, desktop_split_mode_name(),
      desktop_split_ratio_percent, desktop_master_ratio_percent,
      desktop_master_count, desktop_submap,
      desktop_active_workspace, desktop_workspace_name(desktop_active_workspace),
      active_idx >= 0 ? desktop_client_address(&desktop_clients[active_idx]) : 0,
      active_idx >= 0 ? desktop_clients[active_idx].focus_history_id : 0,
      active_idx >= 0
          ? (unsigned long long)desktop_clients[active_idx].focus_generation
          : 0ull,
      total, mapped, hidden, pinned, fullscreen, pseudo, used_workspaces,
      desktop_workspace_count, ORIZON_DESKTOP_RULES_PATH);

  if (used < out_size) {
    used += snprintf(out + used, out_size - used, "\n== workspaces ==\n");
  }
  for (int ws = 1; ws <= desktop_workspace_count && used < out_size; ws++) {
    int last_idx = desktop_last_focused_index_on_workspace(ws);
    const char *state = ws == desktop_active_workspace
                            ? "active"
                            : (ws == desktop_previous_workspace ? "previous"
                                                                 : (desktop_workspace_is_used(ws) ? "used" : "empty"));
    used += snprintf(
        out + used, out_size - used,
        "ws %d \"%s\": state=%s layout=%s split=%s ratio=%d master=%d nmaster=%d visible=%d local=%d last=0x%x lastTitle=\"%s\" visitSeq=%llu\n",
        ws, desktop_workspace_name(ws), state,
        desktop_layout_engine_for_workspace(ws),
        desktop_split_mode_name_for_value(desktop_split_mode_for_workspace(ws)),
        desktop_split_ratio_for_workspace(ws),
        desktop_master_ratio_for_workspace(ws),
        desktop_master_count_for_workspace(ws),
        desktop_client_count_on_workspace(ws),
        desktop_workspace_local_client_count(ws),
        last_idx >= 0 ? desktop_client_address(&desktop_clients[last_idx]) : 0,
        last_idx >= 0 ? desktop_clients[last_idx].title : "none",
        (unsigned long long)desktop_workspaces[ws - 1].visit_generation);
  }

  if (used < out_size) {
    used += snprintf(out + used, out_size - used, "\n== clients ==\n");
  }
  if (total == 0 && used < out_size) {
    used += snprintf(out + used, out_size - used,
                     "empty: dispatch exec terminal/settings/logs/packages/update to create a tiled client\n");
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS && used < out_size; i++) {
    int rx;
    int ry;
    int rw;
    int rh;
    int rank;

    if (!desktop_clients[i].visible) {
      continue;
    }
    desktop_client_rect(i, &rx, &ry, &rw, &rh);
    rank = desktop_focus_rank_for_id(desktop_clients[i].id);
    used += snprintf(
        out + used, out_size - used,
        "client 0x%x: id=%d title=\"%s\" class=%s app=%s workspace=%d current=%s mapped=%s hidden=%s pinned=%s fullscreen=%s pseudo=%s focused=%s focusHistoryID=%d focusRank=%d rulesMatched=%d rulesApplied=%d ruleActions=\"%s\" geom=%d,%d %dx%d backend=%s\n",
        desktop_client_address(&desktop_clients[i]), desktop_clients[i].id,
        desktop_clients[i].title, desktop_clients[i].app_id,
        desktop_clients[i].app_id, desktop_clients[i].workspace,
        desktop_client_on_workspace(&desktop_clients[i], desktop_active_workspace)
            ? "yes"
            : "no",
        desktop_clients[i].mapped ? "yes" : "no",
        desktop_clients[i].hidden ? "yes" : "no",
        desktop_clients[i].pinned ? "yes" : "no",
        desktop_clients[i].fullscreen ? "yes" : "no",
        desktop_clients[i].pseudo ? "yes" : "no",
        desktop_clients[i].id == desktop_focused_client_id ? "yes" : "no",
        desktop_clients[i].focus_history_id, rank,
        desktop_clients[i].rule_match_count,
        desktop_clients[i].rule_apply_count,
        desktop_clients[i].rule_actions[0] ? desktop_clients[i].rule_actions
                                           : "none",
        rx, ry, rw, rh,
        desktop_client_backend_name(&desktop_clients[i]));
  }

  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "\ncommands: desktop clients | desktop workspaces | desktop focus-history | desktop workspace-stack | desktop rule-matches | desktop layout-tree\n"
             "hyprctl: desktop hyprctl clientmodel | desktop hyprctl workspacestack | desktop hyprctl rulematches | desktop hyprctl clients | desktop hyprctl workspaces\n"
             "limits: VM-safe diagnostic only; no free-drag, no floating scene graph, no upstream Hyprland/wlroots yet\n");
  }
}

void gui_desktop_format_rule_matches(char *out, size_t out_size) {
  char cfg[1024];
  char line[192];
  char rendered[320];
  const char *p;
  size_t used = 0;
  int line_number = 1;
  int rules = 0;
  int clients = 0;
  int comparisons = 0;
  int matches = 0;
  int runtime_bytes;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  runtime_bytes = desktop_rule_load_runtime(cfg, sizeof(cfg));
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible) {
      clients++;
    }
  }

  snprintf(rendered, sizeof(rendered),
           "Orizon desktop rule matches\n"
           "version: " ORIZON_DESKTOP_PACKAGE_VERSION "\n"
           "runtime: %s bytes=%d\n"
           "model: Hyprland-style windowrule diagnostics; manual-drag=no floating=no taskbar=no\n"
           "selectors: class/title/app simplified-regex=yes diagnostic=yes\n"
           "spawn-apply: safe-actions=tile/fullscreen/pseudo/pin/workspace ignored=floating/move/center\n",
           ORIZON_DESKTOP_RULES_PATH, runtime_bytes);
  desktop_rule_append_text(out, out_size, &used, rendered);

  p = cfg;
  while (*p && used < out_size) {
    const char *start = p;
    size_t len;
    desktop_rule_match_t rule;

    while (*p && *p != '\n') {
      p++;
    }
    len = (size_t)(p - start);
    if (*p == '\n') {
      p++;
    }
    desktop_rule_copy_trim(line, sizeof(line), start, len);
    if (desktop_rule_parse_line(line, line_number, &rule)) {
      int matched_for_rule = 0;

      rules++;
      snprintf(rendered, sizeof(rendered),
               "\nrule %d line=%d kind=%s action=\"%s\" safeAction=%s selectors=\"%s\"\n",
               rules, rule.line_number, rule.kind, rule.action,
               desktop_rule_action_is_safe(rule.action) ? "yes" : "no",
               rule.selectors[0] ? rule.selectors : "none");
      desktop_rule_append_text(out, out_size, &used, rendered);

      if (clients == 0) {
        desktop_rule_append_text(
            out, out_size, &used,
            "  clients: none-visible; dispatch exec terminal/settings/logs/packages/update to test\n");
      }
      for (int i = 0; i < DESKTOP_MAX_CLIENTS && used < out_size; i++) {
        char reason[40];
        int matched;

        if (!desktop_clients[i].visible) {
          continue;
        }
        comparisons++;
        matched = desktop_rule_matches_client(&rule, &desktop_clients[i],
                                              reason, sizeof(reason));
        if (matched) {
          matches++;
          matched_for_rule++;
        }
        snprintf(rendered, sizeof(rendered),
                 "  client 0x%x: class=%s app=%s title=\"%s\" workspace=%d match=%s reason=%s spawnRulesMatched=%d spawnRulesApplied=%d spawnActions=\"%s\"\n",
                 desktop_client_address(&desktop_clients[i]),
                 desktop_clients[i].app_id, desktop_clients[i].app_id,
                 desktop_clients[i].title, desktop_clients[i].workspace,
                 matched ? "yes" : "no", reason,
                 desktop_clients[i].rule_match_count,
                 desktop_clients[i].rule_apply_count,
                 desktop_clients[i].rule_actions[0]
                     ? desktop_clients[i].rule_actions
                     : "none");
        desktop_rule_append_text(out, out_size, &used, rendered);
      }
      snprintf(rendered, sizeof(rendered),
               "  result: matches=%d clients=%d spawn-action=%s action-safe=%s\n",
               matched_for_rule, clients, rule.action[0] ? rule.action : "none",
               desktop_rule_action_is_safe(rule.action) ? "yes" : "no");
      desktop_rule_append_text(out, out_size, &used, rendered);
    }
    line_number++;
  }

  if (rules == 0) {
    desktop_rule_append_text(
        out, out_size, &used,
        "\nrules: none parsed; add with desktop keyword windowrulev2 <rule>\n");
  }
  snprintf(rendered, sizeof(rendered),
           "\nsummary: rules=%d clients=%d comparisons=%d matches=%d\n"
           "commands: desktop rules | desktop clients | desktop client-model | desktop hyprctl rulematches\n"
           "limits: simplified class/title/app matching only; no upstream Hyprland regex engine, no wlroots/Wayland scene graph, no free-drag/floating behavior\n",
           rules, clients, comparisons, matches);
  desktop_rule_append_text(out, out_size, &used, rendered);
}

void gui_desktop_format_activewindow(char *out, size_t out_size) {
  int idx;
  int rx;
  int ry;
  int rw;
  int rh;

  if (!out || out_size == 0) {
    return;
  }
  idx = desktop_focused_client_index();
  if (idx < 0) {
    snprintf(out, out_size,
             "activewindow:\n"
             "  address: 0x0\n"
             "  mapped: false\n"
             "  reason: no focused tiled client\n");
    return;
  }
  desktop_client_rect(idx, &rx, &ry, &rw, &rh);
  snprintf(out, out_size,
           "activewindow:\n"
           "  address: 0x%x\n"
           "  mapped: %s\n"
           "  hidden: %s\n"
           "  title: %s\n"
           "  class: %s\n"
           "  initialClass: %s\n"
           "  initialTitle: %s\n"
           "  workspace: %d\n"
           "  at: %d,%d\n"
           "  size: %d,%d\n"
           "  floating: false\n"
           "  fullscreen: %s\n"
           "  pseudo: %s\n"
           "  pinned: %s\n"
           "  rulesMatched: %d\n"
           "  rulesApplied: %d\n"
           "  ruleActions: %s\n"
           "  focusHistoryID: %d\n"
           "  mappedSeq: %llu\n"
           "  focusSeq: %llu\n"
           "  xwayland: false\n",
           desktop_client_address(&desktop_clients[idx]),
           desktop_clients[idx].mapped ? "true" : "false",
           desktop_clients[idx].hidden ? "true" : "false",
           desktop_clients[idx].title, desktop_clients[idx].app_id,
           desktop_clients[idx].app_id, desktop_clients[idx].title,
           desktop_clients[idx].workspace, rx, ry, rw, rh,
           desktop_clients[idx].fullscreen ? "true" : "false",
           desktop_clients[idx].pseudo ? "true" : "false",
           desktop_clients[idx].pinned ? "true" : "false",
           desktop_clients[idx].rule_match_count,
           desktop_clients[idx].rule_apply_count,
           desktop_clients[idx].rule_actions[0] ? desktop_clients[idx].rule_actions
                                                : "none",
           desktop_clients[idx].focus_history_id,
           (unsigned long long)desktop_clients[idx].mapped_generation,
           (unsigned long long)desktop_clients[idx].focus_generation);
}

void gui_desktop_format_activeworkspace(char *out, size_t out_size) {
  int clients;
  int local_clients;
  int last_idx;
  int state_idx = desktop_active_workspace - 1;

  if (!out || out_size == 0) {
    return;
  }
  clients = desktop_client_count_on_workspace(desktop_active_workspace);
  local_clients =
      desktop_workspace_local_client_count(desktop_active_workspace);
  last_idx = desktop_last_focused_index_on_workspace(desktop_active_workspace);
  snprintf(out, out_size,
           "active workspace:\n"
           "  id: %d\n"
           "  name: %s\n"
           "  dynamic: true\n"
           "  previous: %d\n"
           "  monitor: Orizon framebuffer\n"
           "  windows: %d\n"
           "  local-windows: %d\n"
           "  pinned-aware: true\n"
           "  layout: %s\n"
           "  split: %s ratio=%d master=%d nmaster=%d\n"
           "  submap: %s\n"
           "  visitSeq: %llu\n"
           "  rememberedFocus: 0x%x\n"
           "  focusSeq: %llu\n"
           "  lastwindow: 0x%x\n"
           "  lastwindowtitle: %s\n",
           desktop_active_workspace,
           desktop_workspace_name(desktop_active_workspace),
           desktop_previous_workspace, clients, local_clients,
           desktop_layout_engine(), desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           desktop_master_count, desktop_submap,
           (unsigned long long)desktop_workspaces[state_idx].visit_generation,
           desktop_workspaces[state_idx].last_focused_client_id > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_workspaces[state_idx]
                          .last_focused_client_id *
                      0x100u)
               : 0,
           (unsigned long long)desktop_workspaces[state_idx].focus_generation,
           last_idx >= 0 ? desktop_client_address(&desktop_clients[last_idx])
                          : 0,
           last_idx >= 0 ? desktop_clients[last_idx].title : "none");
}

void gui_desktop_format_focus_history(char *out, size_t out_size) {
  size_t used = 0;
  int active_idx;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_focus_history_compact();
  active_idx = desktop_client_index_by_id(desktop_focused_client_id);
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop focus history\n"
                   "model: most-recent-first, Hyprland-style focusHistoryID\n"
                   "active-client: 0x%x\n"
                   "active-workspace: %d\n",
                   active_idx >= 0
                       ? desktop_client_address(&desktop_clients[active_idx])
                       : 0,
                   desktop_active_workspace);
  for (int i = 0; i < DESKTOP_MAX_CLIENTS && used < out_size; i++) {
    int idx = desktop_client_index_by_id(desktop_focus_history[i]);
    if (idx < 0) {
      continue;
    }
    used += snprintf(
        out + used, out_size - used,
        "%d: address=0x%x id=%d workspace=%d title=\"%s\" class=%s "
        "mapped=%s hidden=%s pinned=%s fullscreen=%s focusSeq=%llu\n",
        i, desktop_client_address(&desktop_clients[idx]),
        desktop_clients[idx].id, desktop_clients[idx].workspace,
        desktop_clients[idx].title, desktop_clients[idx].app_id,
        desktop_clients[idx].mapped ? "true" : "false",
        desktop_clients[idx].hidden ? "true" : "false",
        desktop_clients[idx].pinned ? "true" : "false",
        desktop_clients[idx].fullscreen ? "true" : "false",
        (unsigned long long)desktop_clients[idx].focus_generation);
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: desktop dispatch movefocus l|r|u|d|next|prev | "
             "desktop dispatch focuswindow <id|0xaddr|class:app|title:text> | "
             "desktop dispatch cyclenext [prev] | "
             "desktop dispatch swapwindow l|r|u|d | "
             "desktop dispatch focusmaster\n");
  }
}

void gui_desktop_format_workspace_stack(char *out, size_t out_size) {
  size_t used = 0;
  int active_idx;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_focus_history_compact();
  active_idx = desktop_client_index_by_id(desktop_focused_client_id);
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop workspace stack\n"
                   "model: per-workspace master/stack/focus tiled order, manual-drag=no floating=no\n"
                   "active-workspace: %d name=\"%s\"\n"
                   "active-client: 0x%x\n"
                   "focus-history: most-recent-first focusHistoryID\n",
                   desktop_active_workspace,
                   desktop_workspace_name(desktop_active_workspace),
                   active_idx >= 0
                       ? desktop_client_address(&desktop_clients[active_idx])
                       : 0);
  for (int ws = 1; ws <= desktop_workspace_count && used < out_size; ws++) {
    int count = desktop_client_count_on_workspace(ws);
    int local_count = desktop_workspace_local_client_count(ws);
    int master_idx = desktop_nth_client_on_workspace(ws, 0);
    int focus_idx = desktop_last_focused_index_on_workspace(ws);
    const char *state = ws == desktop_active_workspace
                            ? "active"
                            : (ws == desktop_previous_workspace ? "previous"
                                                                 : (desktop_workspace_is_used(ws) ? "used" : "empty"));

    used += snprintf(
        out + used, out_size - used,
        "workspace %d \"%s\": state=%s layout=%s clients=%d local=%d "
        "master=0x%x focus=0x%x remembered=0x%x focusSeq=%llu pinned-aware=yes\n",
        ws, desktop_workspace_name(ws), state,
        desktop_layout_engine_for_workspace(ws), count, local_count,
        master_idx >= 0 ? desktop_client_address(&desktop_clients[master_idx])
                        : 0,
        focus_idx >= 0 ? desktop_client_address(&desktop_clients[focus_idx])
                       : 0,
        desktop_workspaces[ws - 1].last_focused_client_id > 0
            ? DESKTOP_CLIENT_ADDRESS_BASE +
                  ((uint32_t)desktop_workspaces[ws - 1].last_focused_client_id *
                   0x100u)
            : 0,
        (unsigned long long)desktop_workspaces[ws - 1].focus_generation);
    if (count <= 0 && used < out_size) {
      used += snprintf(out + used, out_size - used,
                       "  empty: dispatch exec terminal/settings/logs/packages/update to add a tiled client\n");
    }
    for (int pos = 0; pos < count && used < out_size; pos++) {
      int idx = desktop_nth_client_on_workspace(ws, pos);
      int rx;
      int ry;
      int rw;
      int rh;
      int rank;
      const char *scope;

      if (idx < 0) {
        continue;
      }
      desktop_client_rect_for_workspace(idx, ws, &rx, &ry, &rw, &rh);
      rank = desktop_focus_rank_for_id(desktop_clients[idx].id);
      scope = desktop_clients[idx].pinned ? "pinned"
                                          : (desktop_clients[idx].workspace == ws
                                                 ? "local"
                                                 : "foreign");
      used += snprintf(
          out + used, out_size - used,
          "  %d: role=%s address=0x%x id=%d title=\"%s\" class=%s scope=%s "
          "focused=%s focusRank=%d focusHistoryID=%d rect=%d,%d %dx%d "
          "fullscreen=%s pseudo=%s pinned=%s\n",
          pos, desktop_client_role_on_workspace(ws, idx, pos, count),
          desktop_client_address(&desktop_clients[idx]), desktop_clients[idx].id,
          desktop_clients[idx].title, desktop_clients[idx].app_id, scope,
          idx == focus_idx ? "yes" : "no", rank,
          desktop_clients[idx].focus_history_id, rx, ry, rw, rh,
          desktop_clients[idx].fullscreen ? "yes" : "no",
          desktop_clients[idx].pseudo ? "yes" : "no",
          desktop_clients[idx].pinned ? "yes" : "no");
    }
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: desktop dispatch focusmaster | desktop dispatch swapwithmaster | desktop dispatch swapwindow l|r|u|d | desktop dispatch movetoworkspace <target> | desktop dispatch renameworkspace <target> <name>\n"
             "hyprctl: desktop hyprctl workspacestack\n"
             "limits: diagnostic stack only; no free-drag, no floating scene graph, no upstream Hyprland/wlroots yet\n");
  }
}

void gui_desktop_format_monitors(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Monitor 0 (Orizon framebuffer):\n"
           "\t%lux%lu@60.00 at 0x0\n"
           "\tdescription: Orizon VM framebuffer\n"
           "\tmake: Orizon\n"
           "\tmodel: framebuffer\n"
           "\tactive workspace: %d\n"
           "\tscale: %d\n"
           "\treserved: 0 %d 0 %d\n",
           (unsigned long)screen_width, (unsigned long)screen_height,
           desktop_active_workspace, ui_scale, TOP_BAR_HEIGHT, FOOTER_HEIGHT);
}

void gui_desktop_format_layers(char *out, size_t out_size) {
  int total_clients = 0;

  if (!out || out_size == 0) {
    return;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible) {
      total_clients++;
    }
  }
  snprintf(out, out_size,
           "Orizon desktop layers\n"
           "monitor: Orizon framebuffer\n"
           "layer namespace=background z=0 visible=yes role=wallpaper\n"
           "layer namespace=bar z=10 visible=%s position=%s reserved-top=%d\n"
           "layer namespace=launcher z=90 visible=%s overlay=yes\n"
           "layer namespace=tiled-clients z=50 visible=%s clients=%d workspace=%d\n"
           "layer namespace=render-transition z=95 visible=%s reason=%s progress=%d%%\n"
           "layer namespace=cursor z=100 visible=yes x=%d y=%d\n"
           "rules-runtime: %s\n"
           "layerrules-runtime: %s\n"
           "limits: layer-shell protocol is not implemented yet; this is the Orizon compositor layer model\n",
           desktop_session.bar_enabled ? "yes" : "no",
           desktop_settings.bar_position,
           desktop_session.bar_enabled ? TOP_BAR_HEIGHT : 0,
           desktop_launcher_visible ? "yes" : "no",
           total_clients > 0 ? "yes" : "empty", total_clients,
           desktop_active_workspace,
           desktop_animation_ticks_remaining > 0 ? "yes" : "no",
           desktop_transition_reason, desktop_transition_progress_percent(),
           mouse_x, mouse_y, ORIZON_DESKTOP_RULES_PATH,
           ORIZON_DESKTOP_LAYERS_PATH);
}

void gui_desktop_format_binds(char *out, size_t out_size) {
  char cfg[2048];
  size_t used = 0;
  file_t *f;
  ssize_t n;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop binds\n"
                   "style: Hyprland-like bind/dispatcher model\n"
                   "runtime: %s\n",
                   ORIZON_DESKTOP_BINDS_PATH);

  cfg[0] = '\0';
  f = vfs_open(ORIZON_DESKTOP_BINDS_PATH, O_RDONLY);
  if (f) {
    n = vfs_read(f, cfg, sizeof(cfg) - 1);
    vfs_close(f);
    if (n > 0) {
      cfg[n] = '\0';
      used += snprintf(out + used, out_size - used, "\n== configured ==\n%s",
                       cfg);
    }
  }
  if (cfg[0] == '\0' && used < out_size) {
    used += snprintf(out + used, out_size - used,
                     "\n== built-in fallback ==\n"
                     "$mod=SUPER\n"
                     "bind $mod, RETURN, exec terminal\n"
                     "submap launch: t terminal, s settings, l logs, p packages, u update\n"
                     "bind $mod, Q, killactive\n"
                     "bind $mod, D, launcher toggle\n"
                     "bind $mod, M, fullscreen\n"
                     "bind $mod, P, pseudo\n"
                     "bind $mod, J, togglesplit\n"
                     "bind $mod, S, layoutmsg swapwithmaster\n"
                     "bind $mod, 1/2/3, workspace 1/2/3\n"
                     "bind $mod SHIFT, 1/2/3, movetoworkspace 1/2/3\n");
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "\ndispatch: desktop dispatch <dispatcher> [args]\n"
             "supported: exec terminal/settings/logs/packages/update, killactive, workspace, renameworkspace, movetoworkspace, movetoworkspacesilent, movefocus, "
             "focuswindow, cyclenext, swapnext, swapwindow, focusmaster, swapwithmaster, fullscreen/fullscreenstate, pseudo/pseudotile, pin, togglesplit, "
             "layoutmsg, resizeactive, submap\n"
             "no-drag: windows are tiled by layout dispatchers, not manually moved\n");
  }
}

void gui_desktop_format_layouts(char *out, size_t out_size) {
  int total = 0;

  if (!out || out_size == 0) {
    return;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible) {
      total++;
    }
  }
  snprintf(out, out_size,
           "Orizon desktop layouts\n"
           "current: %s\n"
           "default: %s\n"
           "available:\n"
           "  dwindle enabled=yes description=dynamic split tiling\n"
           "  master enabled=yes description=main-area plus stack\n"
           "  monocle enabled=yes description=single focused client\n"
           "split-mode: %s\n"
           "split-ratio: %d\n"
           "master-ratio: %d\n"
           "master-count: %d\n"
           "submap: %s\n"
           "clients: total=%d workspace=%d focused=0x%x focus-history=%s\n"
           "set: desktop layout <dwindle|master|monocle>\n"
           "workspace-state: per-workspace layout/split/master ratios and nmaster\n"
           "dispatch: desktop dispatch layoutmsg layout <dwindle|master|monocle> | desktop dispatch togglesplit | desktop dispatch focusmaster | desktop dispatch swapwithmaster | desktop dispatch swapwindow l|r|u|d\n"
           "dispatch: desktop dispatch layoutmsg splitratio <10-90|+/-n> | desktop dispatch layoutmsg masterratio <10-90|+/-n> | desktop dispatch layoutmsg nmaster <1-8|+/-n>\n"
           "state: desktop layout-state | desktop hyprctl layoutstate\n"
           "tree: desktop layout-tree | desktop hyprctl layouttree\n"
           "hyprctl: desktop hyprctl layouts\n"
           "limits: layout plugins and upstream Hyprland/wlroots scene graph are not implemented yet\n",
           desktop_layout_engine(), desktop_session.layout,
           desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           desktop_master_count, desktop_submap, total,
           desktop_active_workspace,
           desktop_focused_client_id > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_focused_client_id * 0x100u)
               : 0,
           desktop_focus_history[0] > 0 ? "ready" : "empty");
}

void gui_desktop_format_layout_state(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop layout state\n"
                   "model: per-workspace tiling state, Hyprland-style facade\n"
                   "active-workspace: %d\n"
                   "active-layout: %s\n"
                   "default-layout: %s\n"
                   "manual-window-drag: no\n",
                   desktop_active_workspace, desktop_layout_engine(),
                   desktop_session.layout);
  for (int ws = 1; ws <= desktop_workspace_count && used < out_size; ws++) {
    int idx = ws - 1;
    int count = desktop_client_count_on_workspace(ws);
    int local_count = desktop_workspace_local_client_count(ws);
    int last_idx = desktop_last_focused_index_on_workspace(ws);
    const char *state = ws == desktop_active_workspace
                            ? "active"
                            : (ws == desktop_previous_workspace ? "previous"
                                                                 : (desktop_workspace_is_used(ws) ? "used" : "empty"));
    desktop_workspace_init_layout_state(ws);
    used += snprintf(
        out + used, out_size - used,
        "workspace %d: state=%s layout=%s split=%s ratio=%d master=%d nmaster=%d "
        "clients=%d local=%d lastwindow=0x%x layoutSeq=%llu visitSeq=%llu\n",
        ws, state, desktop_layout_engine_for_workspace(ws),
        desktop_split_mode_name_for_value(desktop_workspaces[idx].split_mode),
        desktop_workspaces[idx].split_ratio_percent,
        desktop_workspaces[idx].master_ratio_percent,
        desktop_workspaces[idx].master_count, count, local_count,
        last_idx >= 0 ? desktop_client_address(&desktop_clients[last_idx]) : 0,
        (unsigned long long)desktop_workspaces[idx].layout_generation,
        (unsigned long long)desktop_workspaces[idx].visit_generation);
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: desktop dispatch layoutmsg layout <dwindle|master|monocle> | "
             "desktop dispatch layoutmsg splitratio <10-90|+/-n> | "
             "desktop dispatch layoutmsg masterratio <10-90|+/-n> | "
             "desktop dispatch layoutmsg nmaster <1-8|+/-n>\n"
             "hyprctl: desktop hyprctl layoutstate\n"
             "limits: per-workspace state is VM framebuffer compositor state, not upstream Hyprland/wlroots yet\n");
  }
}

void gui_desktop_format_layout_tree(char *out, size_t out_size) {
  size_t used = 0;
  int count;
  int local_count;
  int active_idx;
  int outer_gap = desktop_settings.gaps_out;
  int inner_gap = desktop_settings.gaps_in;
  int area_x = 44 + outer_gap;
  int area_y = TOP_BAR_HEIGHT + 92;
  int area_w = (int)screen_width - 88 - outer_gap * 2;
  int area_h = (int)screen_height - area_y - FOOTER_HEIGHT - 18 - outer_gap;
  const char *engine;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (area_w < 120) {
    area_w = 120;
  }
  if (area_h < 80) {
    area_h = 80;
  }
  engine = desktop_layout_engine();
  count = desktop_client_count_on_workspace(desktop_active_workspace);
  local_count = desktop_workspace_local_client_count(desktop_active_workspace);
  active_idx = desktop_focused_client_index();
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop layout tree\n"
                   "workspace: %d name=\"%s\" engine=%s clients=%d local=%d pinned-aware=yes\n"
                   "root: area=%d,%d size=%dx%d gaps-in=%d gaps-out=%d split=%s ratio=%d master=%d nmaster=%d\n"
                   "model: tiling-only manual-drag=no floating-tree=no backend=framebuffer-vm\n",
                   desktop_active_workspace,
                   desktop_workspace_name(desktop_active_workspace), engine,
                   count, local_count, area_x, area_y, area_w, area_h,
                   inner_gap, outer_gap, desktop_split_mode_name(),
                   desktop_split_ratio_percent, desktop_master_ratio_percent,
                   desktop_master_count);
  if (count <= 0 && used < out_size) {
    used += snprintf(out + used, out_size - used,
                     "tree: empty workspace; dispatch exec terminal to create the first tiled client\n");
  }
  for (int pos = 0; pos < count && used < out_size; pos++) {
    int idx = desktop_nth_client_on_workspace(desktop_active_workspace, pos);
    int rx;
    int ry;
    int rw;
    int rh;

    if (idx < 0) {
      continue;
    }
    desktop_client_rect(idx, &rx, &ry, &rw, &rh);
    used += snprintf(
        out + used, out_size - used,
        "node %d: role=%s address=0x%x id=%d title=\"%s\" class=%s rect=%d,%d %dx%d focused=%s fullscreen=%s pseudo=%s pinned=%s focusHistoryID=%d\n",
        pos,
        desktop_client_role_on_workspace(desktop_active_workspace, idx, pos,
                                         count),
        desktop_client_address(&desktop_clients[idx]),
        desktop_clients[idx].id, desktop_clients[idx].title,
        desktop_clients[idx].app_id, rx, ry, rw, rh,
        idx == active_idx ? "yes" : "no",
        desktop_clients[idx].fullscreen ? "yes" : "no",
        desktop_clients[idx].pseudo ? "yes" : "no",
        desktop_clients[idx].pinned ? "yes" : "no",
        desktop_clients[idx].focus_history_id);
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: layoutmsg splitratio/masterratio/nmaster/togglesplit | focusmaster | swapwithmaster | movefocus l|r|u|d | focuswindow <target>\n"
             "hyprctl: desktop hyprctl layouttree\n"
             "limits: diagnostic tree only; no mouse free-drag, no floating scene graph, no Wayland scene graph yet\n");
  }
}

void gui_desktop_format_animations(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop animations\n"
           "enabled: %s\n"
           "source: %s\n"
           "runtime: focus-ring=%s workspace-transition=yes layout-transition=yes tick-budget=%d curve=%s profile=%s\n"
           "transition: reason=%s from=%d to=%d ticks-remaining=%d progress=%d%% render-serial=%llu\n"
           "curves:\n"
           "  orizon-pop prepared=yes bezier=0.16,1,0.3,1\n"
           "  orizon-slide prepared=yes bezier=0.2,0.8,0.2,1\n"
           "rules:\n"
           "  windows enabled=yes style=focus-glow+shadow ticked-software\n"
           "  workspaces enabled=yes style=slide-indicator ticked-software\n"
           "  layers enabled=yes style=fade-indicator ticked-software\n"
           "truth: software framebuffer animation hints, not wlroots animation graph\n"
           "set: desktop keyword animations:enabled <true|false> | desktop keyword animations:tick_budget <4-60>\n",
           desktop_settings.animations_enabled ? "true" : "false",
           ORIZON_DESKTOP_SETTINGS_PATH,
           desktop_settings.focus_ring_enabled ? "yes" : "no",
           desktop_animation_tick_budget(), desktop_settings.animation_curve,
           desktop_settings.render_profile,
           desktop_transition_reason, desktop_transition_from_workspace,
           desktop_transition_to_workspace, desktop_animation_ticks_remaining,
           desktop_transition_progress_percent(),
           (unsigned long long)desktop_render_serial);
}

void gui_desktop_format_decorations(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop decorations\n"
           "border: size=%d active-color=accent inactive-color=edge\n"
           "focus-ring: enabled=%s renderer=software-glow follows=activewindow\n"
           "rounding: configured=%d renderer=corner-hints true-rounded=no\n"
           "shadows: enabled=%s range=%d renderer=software\n"
           "blur: enabled=no prepared=no\n"
           "drop-shadow: %s\n"
           "window-moving: manual-drag=no tiled-dispatch=yes\n"
           "set: desktop keyword decoration:rounding <n> | desktop keyword decoration:shadow:range <0-32>\n",
           desktop_settings.border_size,
           desktop_settings.focus_ring_enabled ? "true" : "false",
           desktop_settings.rounding,
           desktop_settings.shadows_enabled ? "true" : "false",
           desktop_settings.shadow_range,
           desktop_settings.shadows_enabled ? "enabled" : "disabled");
}

void gui_desktop_format_render(char *out, size_t out_size) {
  color_t accent = desktop_theme_accent();

  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop render\n"
           "backend: framebuffer\n"
           "renderer: software\n"
           "scale: %d\n"
           "theme: %s wallpaper=%s accent=#%06x\n"
           "render-profile: %s\n"
           "focus-ring: enabled=%s activewindow=0x%x style=hyprland-like-glow\n"
           "shadows: enabled=%s range=%d renderer=software\n"
           "rounding: configured=%d renderer=corner-hints true-rounded=no\n"
           "transitions: enabled=%s curve=%s budget=%d reason=%s from=%d to=%d ticks=%d progress=%d%%\n"
           "render-serial: %llu\n"
           "window-moving: manual-drag=no tiled-dispatch=yes\n"
           "backend-map: " ORIZON_DESKTOP_BACKEND_PATH "\n"
           "protocol-map: " ORIZON_DESKTOP_PROTOCOL_PATH "\n"
           "protocols: wayland=no wlroots=no xdg-shell=no layer-shell=prepared-only\n"
           "truth: VM-safe Hyprland-style facade over Orizon framebuffer compositor\n",
           ui_scale, desktop_session.theme, desktop_session.wallpaper,
           accent & 0xffffff,
           desktop_settings.render_profile,
           desktop_settings.focus_ring_enabled ? "true" : "false",
           desktop_focused_client_id > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_focused_client_id * 0x100u)
               : 0,
           desktop_settings.shadows_enabled ? "true" : "false",
           desktop_settings.shadow_range, desktop_settings.rounding,
           desktop_settings.animations_enabled ? "true" : "false",
           desktop_settings.animation_curve, desktop_animation_tick_budget(),
           desktop_transition_reason, desktop_transition_from_workspace,
           desktop_transition_to_workspace, desktop_animation_ticks_remaining,
           desktop_transition_progress_percent(),
           (unsigned long long)desktop_render_serial);
}

void gui_desktop_format_descriptions(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop hyprctl descriptions\n"
           "commands: version, systeminfo, clients, clientmodel, rulematches, workspaces, activeworkspace, activewindow\n"
           "commands: backend, protocol, monitors, binds, layers, layouts, layoutstate, layouttree, animations, decorations, render, devices\n"
           "commands: cursorpos, splash, configerrors, configtrace, rollinglog, instances, submap, focushistory, workspacestack\n"
           "commands: getoption <key>, keyword <key> <value>, dispatch <dispatcher> [args], reload\n"
           "dispatchers: exec, killactive, workspace, renameworkspace, movetoworkspace, movetoworkspacesilent, movefocus, focuswindow, cyclenext, swapnext, swapwindow\n"
           "dispatchers: focusmaster, swapwithmaster, fullscreen [on|off|toggle], fullscreenstate <on|off|1|0>, pseudo|pseudotile [on|off|toggle], pin [on|off|toggle], togglesplit, layoutmsg, resizeactive, submap\n"
           "layoutmsg: layout <dwindle|master|monocle>, togglesplit, orientationnext, orientationprev, orientationleft/right/top/bottom\n"
           "layoutmsg: splitratio <10-90|+/-n>, masterratio|mfact <10-90|+/-n>, focusmaster, swapwithmaster\n"
           "truth: Hyprland-style facade for Orizon's framebuffer compositor\n");
}

void gui_desktop_format_instances(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop instances\n"
           "instance 0:\n"
           "  signature: orizon-framebuffer-main\n"
           "  pid: kernel\n"
           "  wl_socket: none\n"
           "  running: %s\n"
           "  active-workspace: %d\n"
           "  layout: %s\n"
           "  submap: %s\n"
           "limits: no multi-seat or external Wayland instance yet\n",
           desktop_mode_enabled ? "true" : "false", desktop_active_workspace,
           desktop_layout_engine(), desktop_submap);
}

void gui_desktop_format_submap(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "submap: %s\n"
           "available: default, resize, move, launch\n"
           "set: desktop dispatch submap <name|reset>\n"
           "keys: F9 resize, F10 move, F11 launch, F12/Esc default\n"
           "resize: arrows/HJKL adjust tiling ratios; move: arrows/HJKL focus and 1/2/3 move clients\n"
           "bind-note: config parser records submap-style binds; runtime submaps are active in VM keyboard input\n",
           desktop_submap);
}

void gui_desktop_reload_session(void) {
  int layout_default_changed;

  if (orizon_desktop_load_session(&desktop_session) < 0) {
    snprintf(desktop_session.theme, sizeof(desktop_session.theme), "%s",
             "graphite");
    snprintf(desktop_session.wallpaper, sizeof(desktop_session.wallpaper),
             "%s", "aurora");
    snprintf(desktop_session.layout, sizeof(desktop_session.layout), "%s",
             "dwindle");
    desktop_session.bar_enabled = 1;
    desktop_session.launcher_enabled = 1;
    desktop_session.autostart_terminal = 1;
    desktop_session.focus_follows_mouse = 0;
  }
  if (orizon_desktop_load_settings(&desktop_settings) < 0) {
    desktop_settings.scale = 1;
    desktop_settings.gaps_in = 6;
    desktop_settings.gaps_out = 12;
    desktop_settings.border_size = 2;
    desktop_settings.rounding = 8;
    desktop_settings.animations_enabled = 1;
    desktop_settings.shadows_enabled = 1;
    desktop_settings.focus_ring_enabled = 1;
    desktop_settings.shadow_range = 18;
    desktop_settings.animation_ticks = DESKTOP_ANIMATION_TICKS;
    desktop_settings.idle_timeout_seconds = 0;
    desktop_settings.lock_on_idle = 0;
    snprintf(desktop_settings.default_terminal,
             sizeof(desktop_settings.default_terminal), "%s",
             "orizon-terminal");
    snprintf(desktop_settings.launcher_provider,
             sizeof(desktop_settings.launcher_provider), "%s", "builtin");
    snprintf(desktop_settings.bar_position,
             sizeof(desktop_settings.bar_position), "%s", "top");
    snprintf(desktop_settings.keyboard_layout,
             sizeof(desktop_settings.keyboard_layout), "%s", "us");
    snprintf(desktop_settings.pointer_profile,
             sizeof(desktop_settings.pointer_profile), "%s", "flat");
    snprintf(desktop_settings.animation_curve,
             sizeof(desktop_settings.animation_curve), "%s", "orizon-pop");
    snprintf(desktop_settings.render_profile,
             sizeof(desktop_settings.render_profile), "%s", "balanced");
  }
  layout_default_changed =
      strcmp(desktop_loaded_layout_default, desktop_session.layout) != 0;
  if (layout_default_changed) {
    snprintf(desktop_loaded_layout_default,
             sizeof(desktop_loaded_layout_default), "%s",
             desktop_session.layout);
    desktop_layout_mode = desktop_layout_mode_from_name(desktop_session.layout);
    desktop_save_active_layout_state();
  } else {
    desktop_load_layout_state_for_workspace(desktop_active_workspace);
  }
  if (!desktop_session.launcher_enabled) {
    desktop_launcher_visible = 0;
  }
  needs_redraw = 1;
}

void gui_desktop_format_status(char *out, size_t out_size) {
  char base[1400];
  const char *session;
  int client_count;

  if (!out || out_size == 0) {
    return;
  }
  orizon_desktop_format_status(base, sizeof(base));
  session = desktop_mode_enabled ? "active" : "inactive";
  client_count = desktop_client_count_on_workspace(desktop_active_workspace);
  snprintf(out, out_size,
           "%scompositor-session: %s\nterminal-window: %s\n"
           "launcher-window: %s\nbar: %s\nruntime-theme: %s\n"
           "runtime-wallpaper: %s\nruntime-layout: %s\n"
           "runtime-focus-follows-mouse: %s\n"
           "runtime-settings: %s\n"
           "runtime-gaps: in=%d out=%d border=%d rounding=%d animations=%s ticks=%d curve=%s shadows=%s shadow-range=%d focus-ring=%s render=%s\n"
           "tiling-engine: %s\n"
           "tiling-split: mode=%s ratio=%d master-ratio=%d master-count=%d\n"
           "runtime-submap: %s\n"
           "manual-window-drag: no\n"
           "workspace-active: %d\nworkspace-count: %d\n"
           "workspace-clients: %d\nfocused-client: 0x%x\n"
           "focus-history-front: 0x%x\n",
           base, session, desktop_terminal_visible ? "open" : "closed",
           desktop_launcher_visible ? "open" : "closed",
           desktop_session.bar_enabled ? "visible" : "hidden",
           desktop_session.theme, desktop_session.wallpaper,
           desktop_layout_engine(),
           desktop_session.focus_follows_mouse ? "yes" : "no",
           ORIZON_DESKTOP_SETTINGS_PATH, desktop_settings.gaps_in,
           desktop_settings.gaps_out, desktop_settings.border_size,
           desktop_settings.rounding,
           desktop_settings.animations_enabled ? "yes" : "no",
           desktop_settings.animation_ticks, desktop_settings.animation_curve,
           desktop_settings.shadows_enabled ? "yes" : "no",
           desktop_settings.shadow_range,
           desktop_settings.focus_ring_enabled ? "yes" : "no",
           desktop_settings.render_profile,
           desktop_layout_engine(),
           desktop_split_mode_name(), desktop_split_ratio_percent,
           desktop_master_ratio_percent, desktop_master_count, desktop_submap,
           desktop_active_workspace,
           desktop_workspace_count, client_count,
           desktop_focused_client_id > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_focused_client_id * 0x100u)
               : 0,
           desktop_focus_history[0] > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_focus_history[0] * 0x100u)
               : 0);
}

void gui_desktop_format_pointer(char *out, size_t out_size) {
  char ps2[256];
  char usb[320];
  char i2c[256];

  if (!out || out_size == 0) {
    return;
  }

  ps2_format_status(ps2, sizeof(ps2));
  usb_format_status(usb, sizeof(usb));
  i2c_hid_format_status(i2c, sizeof(i2c));
  snprintf(out, out_size,
           "Orizon desktop pointer\n"
           "cursor: x=%d y=%d buttons=%d profile=%s focus-follows-mouse=%s focus-changes=%llu\n"
           "window-moving: keyboard-dispatch-only manual-drag=no\n"
           "ps2: %s\n"
           "usb-hid: %s\n"
           "i2c-hid: %s\n"
           "vm-note: QEMU/libvirt usb-tablet and boot mouse reports are routed "
           "to the compositor pointer when their HID endpoint is selected.\n",
           mouse_x, mouse_y, prev_buttons, desktop_settings.pointer_profile,
           desktop_session.focus_follows_mouse ? "yes" : "no",
           (unsigned long long)desktop_pointer_focus_changes, ps2, usb, i2c);
}

void gui_desktop_format_devices(char *out, size_t out_size) {
  char ps2[256];
  char usb[320];
  char i2c[256];

  if (!out || out_size == 0) {
    return;
  }
  ps2_format_status(ps2, sizeof(ps2));
  usb_format_status(usb, sizeof(usb));
  i2c_hid_format_status(i2c, sizeof(i2c));
  snprintf(out, out_size,
           "Orizon desktop devices\n"
           "keyboard:\n"
           "  name: Orizon keyboard\n"
           "  layout: %s\n"
           "  backend: framebuffer-console\n"
           "pointer:\n"
           "  name: Orizon compositor pointer\n"
           "  position: %d,%d\n"
           "  buttons: %d\n"
           "  profile: %s\n"
           "  manual-window-drag: no\n"
           "backends:\n"
           "  ps2: %s\n"
           "  usb-hid: %s\n"
           "  i2c-hid: %s\n"
           "limits: this is the VM-safe Orizon input model, not libinput/Wayland yet\n",
           desktop_settings.keyboard_layout, mouse_x, mouse_y, prev_buttons,
           desktop_settings.pointer_profile, ps2, usb, i2c);
}

void gui_desktop_format_keymap(char *out, size_t out_size) {
  char key_name[24];

  if (!out || out_size == 0) {
    return;
  }
  desktop_key_name(desktop_last_key, key_name, sizeof(key_name));
  snprintf(out, out_size,
           "Orizon desktop keymap\n"
           "model: Hyprland-style dispatcher/submap facade over VM framebuffer input\n"
           "keyboard-layout: %s\n"
           "active-submap: %s\n"
           "last-key: %s serial=%llu\n"
           "focus-follows-mouse: %s pointer-focus-changes=%llu\n"
           "direct-keys:\n"
           "  F1 exec terminal | F2 killactive | F3 launcher toggle\n"
           "  F4 fullscreen | F5 pseudo | F6 cyclenext | F7/F8 workspace +/-1\n"
           "submaps:\n"
           "  F9 resize: Left/H split -5, Right/L split +5, Up/K master +5, Down/J master -5, R reset, S togglesplit, Esc/F12 default\n"
           "  F10 move: arrows/HJKL focus, 1/2/3 movetoworkspace, P pin, Esc/F12 default\n"
           "  F11 launch: T/Enter terminal, D/Space launcher, Q killactive, Esc/F12 default\n"
           "config-binds: %s\n"
           "mouse-policy: focus follows mouse is optional; manual-window-drag=no\n"
           "dispatch: desktop dispatch submap <default|resize|move|launch> | desktop dispatch resizeactive <x> <y>\n",
           desktop_settings.keyboard_layout, desktop_submap, key_name,
           (unsigned long long)desktop_key_serial,
           desktop_session.focus_follows_mouse ? "yes" : "no",
           (unsigned long long)desktop_pointer_focus_changes,
           ORIZON_DESKTOP_BINDS_PATH);
}

void gui_desktop_format_systeminfo(char *out, size_t out_size) {
  int total = 0;

  if (!out || out_size == 0) {
    return;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_clients[i].visible) {
      total++;
    }
  }
  snprintf(out, out_size,
           "Orizon desktop systeminfo\n"
           "version: %s " ORIZON_DESKTOP_PACKAGE_VERSION "\n"
           "compositor: Orizon framebuffer compositor\n"
           "backend: framebuffer\n"
           "renderer: software\n"
           "monitor: %lux%lu scale=%d reserved-top=%d reserved-bottom=%d\n"
           "session: enabled=%s theme=%s wallpaper=%s layout=%s default-layout=%s bar=%s launcher=%s\n"
           "clients: total=%d active-workspace=%d focused=0x%x focus-history=%s\n"
           "layout-state: layout=%s split=%s ratio=%d master=%d nmaster=%d submap=%s\n"
           "settings: gaps=%d/%d border=%d rounding=%d animations=%s ticks=%d curve=%s shadows=%s shadow-range=%d focus-ring=%s render=%s keyboard=%s pointer=%s\n"
           "render-state: serial=%llu focus-ring=%s transition=%s ticks=%d progress=%d%%\n"
           "protocols: wayland=no wlroots=no xwayland=no layer-shell=prepared\n"
           "architecture: backend-map=" ORIZON_DESKTOP_BACKEND_PATH " protocol-map=" ORIZON_DESKTOP_PROTOCOL_PATH "\n"
           "truth: Hyprland-style Orizon profile, not upstream Hyprland\n",
           ORIZON_DESKTOP_PACKAGE, (unsigned long)screen_width,
           (unsigned long)screen_height, ui_scale, TOP_BAR_HEIGHT,
           FOOTER_HEIGHT, desktop_mode_enabled ? "yes" : "no",
           desktop_session.theme, desktop_session.wallpaper,
           desktop_layout_engine(), desktop_session.layout,
           desktop_session.bar_enabled ? "yes" : "no",
           desktop_launcher_visible ? "open" : "closed", total,
           desktop_active_workspace,
           desktop_focused_client_id > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_focused_client_id * 0x100u)
               : 0,
           desktop_focus_history[0] > 0 ? "ready" : "empty",
           desktop_layout_engine(), desktop_split_mode_name(), desktop_split_ratio_percent,
           desktop_master_ratio_percent, desktop_master_count, desktop_submap,
           desktop_settings.gaps_in, desktop_settings.gaps_out,
           desktop_settings.border_size, desktop_settings.rounding,
           desktop_settings.animations_enabled ? "true" : "false",
           desktop_settings.animation_ticks, desktop_settings.animation_curve,
           desktop_settings.shadows_enabled ? "true" : "false",
           desktop_settings.shadow_range,
           desktop_settings.focus_ring_enabled ? "true" : "false",
           desktop_settings.render_profile, desktop_settings.keyboard_layout,
           desktop_settings.pointer_profile,
           (unsigned long long)desktop_render_serial,
           desktop_settings.focus_ring_enabled ? "yes" : "no",
           desktop_transition_reason, desktop_animation_ticks_remaining,
           desktop_transition_progress_percent());
}

void gui_desktop_format_hyprctl_version(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop hyprctl version\n"
           "facade: Hyprland-style compatibility commands\n"
           "desktop-package: %s " ORIZON_DESKTOP_PACKAGE_VERSION "\n"
           "compositor: Orizon framebuffer compositor\n"
           "backend: framebuffer-vm\n"
           "protocol: orizon-desktop-ipc-v0\n"
           "wayland: not-implemented\n"
           "wlroots: not-embedded\n"
           "layouts: dwindle, master, monocle\n"
           "truth: inspired by Hyprland, not upstream Hyprland\n",
           ORIZON_DESKTOP_PACKAGE);
}

void gui_desktop_format_cursorpos(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "cursorpos: %d %d\n"
           "buttons: %d\n"
           "profile: %s\n",
           mouse_x, mouse_y, prev_buttons, desktop_settings.pointer_profile);
}

void gui_desktop_format_splash(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop splash\n"
           "Welcome to the Hyprland-style Orizon profile.\n"
           "Tiny tiling gremlin status: awake, polite, no mouse-dragging.\n");
}

static void gui_show_boot_stage(const char *stage) {
  boot_stage_hint = stage;
  serial_puts("[boot] ");
  serial_puts(stage ? stage : "(null)");
  serial_puts("\n");
  needs_redraw = 1;
  gui_compose();
}

static void gui_run_deferred_core_services(void) {
  if (core_services_done) {
    return;
  }
  core_services_done = 1;

  if (!boot_cmdline_has("orizon.notimer=1") &&
      !boot_cmdline_has("orizon.minimal=1")) {
    gui_show_boot_stage("Initializing ACPI tables...");
    if (boot_cmdline_has("orizon.noacpi=1")) {
      acpi_init(NULL);
    } else if (boot_rsdp_address()) {
      acpi_init(boot_rsdp_address());
    } else {
      acpi_init(NULL);
    }
    if (boot_cmdline_has("orizon.safe=1") ||
        boot_cmdline_has("orizon.pit=1") ||
        boot_cmdline_has("orizon.nolapic=1")) {
      gui_show_boot_stage("Initializing safe PIT timer...");
      timer_init_pit_only();
    } else {
      gui_show_boot_stage("Initializing LAPIC timer hardware...");
      timer_init();
    }
  } else {
    gui_show_boot_stage("Minimal boot: ACPI/timer initialization skipped.");
  }

  if (!boot_cmdline_has("orizon.nohw=1") &&
      !boot_cmdline_has("orizon.minimal=1")) {
    if (boot_cmdline_has("orizon.safe=1")) {
      gui_show_boot_stage("Safe boot: persistent workspace auto-load skipped.");
    } else {
      gui_show_boot_stage("Loading persistent workspace from disk...");
      vfs_persist_load();
    }
    gui_desktop_set_enabled(orizon_desktop_is_enabled());
    gui_show_boot_stage("Running installed/live init tasks...");
    orizon_system_run_boot_tasks(NULL, 0);
    gui_show_boot_stage("Preparing package and keyboard state...");
    orizon_pkg_init();
    input_load_keyboard_layout_from_vfs();
    klog_persist_boot_if_installed();
    gui_show_boot_stage("Validating pending update boot state...");
    orizon_update_boot_guard_check();
    gui_show_boot_stage("Initializing Ethernet drivers...");
    net_init();
    net_ready = 1;
    gui_show_boot_stage("Detecting Wi-Fi hardware...");
    wifi_init();
    wifi_ready = 1;
  } else {
    gui_show_boot_stage("Minimal boot: disk/network initialization skipped.");
  }

  if (!boot_cmdline_has("orizon.noinput=1") &&
      !boot_cmdline_has("orizon.minimal=1")) {
    gui_show_boot_stage("Initializing PS/2 keyboard and pointer...");
    ps2_init();
    ps2_ready = 1;
    gui_show_boot_stage("Initializing USB keyboard support...");
    usb_init();
    usb_ready = 1;
  } else {
    gui_show_boot_stage("Minimal boot: input hardware initialization skipped.");
  }

  if (i2c_hid_deferred_probe == 1 && !boot_cmdline_has("orizon.minimal=1")) {
    gui_show_boot_stage("Probing Lenovo I2C-HID touchpad/Wacom...");
    i2c_hid_init();
    i2c_hid_ready = 1;
    i2c_hid_deferred_probe = 2;
  }

  gui_show_boot_stage("Confirming update boot readiness...");
  orizon_update_boot_guard_shell_ready();
  boot_stage_hint = "Boot complete. Console, diagnostics and installer are ready.";
  splash_ticks_remaining = 0;
  needs_redraw = 1;
}

void gui_main_loop(void) {
  uint64_t last_tick = timer_ticks();

  while (1) {
    gui_loop_count++;
    sched_enter_process("gui-shell");
    if (ps2_ready) {
      ps2_poll();
    }
    if (usb_ready) {
      usb_poll();
    }
    if (i2c_hid_ready) {
      i2c_hid_poll();
    }
    if (net_ready) {
      net_poll();
      ssh_poll();
    }
    if (wifi_ready) {
      wifi_poll();
    }

    uint64_t now = timer_ticks();
    if (now != last_tick) {
      uint64_t elapsed = now - last_tick;
      last_tick = now;
      timer_irq_seen = 1;
      timer_fallback_polling = 0;
      if (splash_ticks_remaining > 0) {
        if ((uint64_t)splash_ticks_remaining > elapsed) {
          splash_ticks_remaining -= (int)elapsed;
        } else {
          splash_ticks_remaining = 0;
        }
        needs_redraw = 1;
      }
      if (desktop_mode_enabled && desktop_animation_ticks_remaining > 0) {
        if ((uint64_t)desktop_animation_ticks_remaining > elapsed) {
          desktop_animation_ticks_remaining -= (int)elapsed;
        } else {
          desktop_animation_ticks_remaining = 0;
        }
        needs_redraw = 1;
      }
    } else if (!timer_irq_seen && gui_loop_count > TIMER_BOOT_FALLBACK_LOOPS) {
      /*
       * Some real UEFI laptops do not deliver the legacy PIT/PIC timer IRQ
       * even though QEMU does. If we hlt before observing a tick, the splash
       * can become a permanent nap. Keep booting in polling mode instead.
       */
      timer_fallback_polling = 1;
      if (splash_ticks_remaining > 0) {
        splash_ticks_remaining = 0;
        needs_redraw = 1;
      }
    }

    gui_compose();
    if (!core_services_done) {
      gui_run_deferred_core_services();
    }

    power_poll();

    sched_enter_idle();
    if (timer_irq_seen) {
      __asm__ volatile("hlt");
    } else {
      for (int i = 0; i < TIMER_FALLBACK_IDLE_PAUSES; i++) {
        __asm__ volatile("pause");
      }
    }
  }
}
