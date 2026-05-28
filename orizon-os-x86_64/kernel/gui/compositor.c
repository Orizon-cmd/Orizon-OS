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

#define TOP_BAR_HEIGHT 30
#define FOOTER_HEIGHT 28
#define PANEL_PADDING 12
#define PANEL_TITLE_HEIGHT 30
#define TERM_CONTENT_WIDTH (TERM_COLS * TERM_CHAR_W + TERM_PADDING * 2)
#define TERM_CONTENT_HEIGHT (TERM_ROWS * TERM_CHAR_H + TERM_PADDING * 2)
#define SHELL_WIDTH (TERM_CONTENT_WIDTH + PANEL_PADDING * 2)
#define SHELL_HEIGHT (PANEL_TITLE_HEIGHT + TERM_CONTENT_HEIGHT + PANEL_PADDING * 2)
#define SPLASH_TICKS 180
#define TIMER_BOOT_FALLBACK_LOOPS 8000
#define TIMER_FALLBACK_IDLE_PAUSES 20000
#define DESKTOP_MAX_CLIENTS 8
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
    1, 6, 12, 2, 8, 1, 1, 0, 0, "orizon-terminal", "builtin", "top", "us",
    "flat"};
static int desktop_workspace_count = 3;
static int desktop_active_workspace = 1;
static int desktop_previous_workspace = 1;
static int desktop_terminal_workspace = 1;
static int desktop_split_mode = 0; /* 0=auto, 1=vertical, 2=horizontal */
static int desktop_split_ratio_percent = 50;
static int desktop_master_ratio_percent = 58;
static char desktop_submap[32] = "default";
static int desktop_last_key = 0;
static uint64_t desktop_key_serial = 0;
static uint64_t desktop_pointer_focus_changes = 0;
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
} desktop_client_t;

static desktop_client_t desktop_clients[DESKTOP_MAX_CLIENTS];
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
  for (int i = 1; i <= 8; i++) {
    color_t shadow = MAKE_ARGB(24 - i * 2, 0, 0, 0);
    fb_fill_rect_alpha(x + i, y + height + i - 2, width, 3, shadow);
    fb_fill_rect_alpha(x + width + i - 2, y + i, 3, height, shadow);
  }
}

static void draw_centered_string(int y, const char *text, color_t color) {
  int x = ((int)screen_width - font_string_width(text)) / 2;
  font_draw_string(x, y, text, color);
}

static void draw_background(void) {
  color_t top = COLOR_BG_TOP;
  color_t bottom = COLOR_BG_BOTTOM;
  color_t accent = COLOR_PANEL_ACCENT;

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
    if (strcmp(desktop_session.theme, "moss") == 0) {
      accent = MAKE_COLOR(88, 190, 132);
    } else if (strcmp(desktop_session.theme, "ember") == 0) {
      accent = MAKE_COLOR(242, 132, 74);
    } else if (strcmp(desktop_session.theme, "frost") == 0) {
      accent = MAKE_COLOR(98, 210, 232);
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

static const char *desktop_layout_engine(void) {
  if (strcmp(desktop_session.layout, "master") == 0) {
    return "master";
  }
  if (strcmp(desktop_session.layout, "monocle") == 0) {
    return "monocle";
  }
  return "dwindle";
}

static const char *desktop_split_mode_name(void) {
  if (desktop_split_mode == 1) {
    return "vertical";
  }
  if (desktop_split_mode == 2) {
    return "horizontal";
  }
  return "auto";
}

static int desktop_cycle_split_mode(int delta) {
  desktop_split_mode = (desktop_split_mode + delta + 3) % 3;
  needs_redraw = 1;
  return desktop_split_mode;
}

static int desktop_set_split_mode(int mode) {
  if (mode < 0 || mode > 2) {
    return -1;
  }
  desktop_split_mode = mode;
  needs_redraw = 1;
  return 0;
}

static int desktop_set_split_ratio(int ratio) {
  if (ratio < 10 || ratio > 90) {
    return -1;
  }
  desktop_split_ratio_percent = ratio;
  needs_redraw = 1;
  return 0;
}

static int desktop_set_master_ratio(int ratio) {
  if (ratio < 10 || ratio > 90) {
    return -1;
  }
  desktop_master_ratio_percent = ratio;
  needs_redraw = 1;
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
  if (strcmp(v, "previous") == 0 || strcmp(v, "prev") == 0) {
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
    *workspace = desktop_clamp_workspace(desktop_active_workspace + n);
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

static int desktop_client_on_workspace(const desktop_client_t *client,
                                       int workspace) {
  if (!client || !client->visible) {
    return 0;
  }
  return client->pinned || client->workspace == workspace;
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

static void desktop_set_focused_client_index(int idx) {
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
}

static int desktop_focused_client_index(void) {
  int idx = desktop_client_index_by_id(desktop_focused_client_id);
  if (idx >= 0 &&
      desktop_client_on_workspace(&desktop_clients[idx],
                                  desktop_active_workspace)) {
    return idx;
  }
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    if (desktop_client_on_workspace(&desktop_clients[i],
                                    desktop_active_workspace)) {
      desktop_set_focused_client_index(i);
      return i;
    }
  }
  desktop_set_focused_client_index(-1);
  return -1;
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
      snprintf(desktop_clients[i].title, sizeof(desktop_clients[i].title),
               "%s", title ? title : "client");
      snprintf(desktop_clients[i].app_id, sizeof(desktop_clients[i].app_id),
               "%s", app_id ? app_id : "orizon-client");
      desktop_set_focused_client_index(i);
      if (terminal_backed) {
        desktop_terminal_visible = 1;
        desktop_terminal_workspace = desktop_active_workspace;
      }
      desktop_launcher_visible = 0;
      needs_redraw = 1;
      return desktop_clients[i].id;
    }
  }
  return -1;
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
  desktop_client_t tmp;

  if (focused_idx < 0 || master_idx < 0 || focused_idx == master_idx) {
    return -1;
  }
  tmp = desktop_clients[focused_idx];
  desktop_clients[focused_idx] = desktop_clients[master_idx];
  desktop_clients[master_idx] = tmp;
  desktop_set_focused_client_index(master_idx);
  desktop_sync_terminal_compat();
  needs_redraw = 1;
  return 0;
}

static int desktop_toggle_active_fullscreen(void) {
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].fullscreen = desktop_clients[idx].fullscreen ? 0 : 1;
  needs_redraw = 1;
  return desktop_clients[idx].fullscreen;
}

static int desktop_toggle_active_pseudo(void) {
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].pseudo = desktop_clients[idx].pseudo ? 0 : 1;
  needs_redraw = 1;
  return desktop_clients[idx].pseudo;
}

static int desktop_toggle_active_pin(void) {
  int idx = desktop_focused_client_index();

  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].pinned = desktop_clients[idx].pinned ? 0 : 1;
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
           "WS %d/%d  layout=%s split=%s/%d master=%d  gaps=%d/%d border=%d  F1 term F9 resize F10 move sub=%s",
           desktop_active_workspace, desktop_workspace_count,
           desktop_session.layout, desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           desktop_settings.gaps_in, desktop_settings.gaps_out,
           desktop_settings.border_size, desktop_submap);
  font_draw_string(52, y + 7, line, COLOR_TEXT_SECONDARY);
}

static void draw_desktop_launcher(void) {
  int width = 520;
  int height = 232;
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
                   "1 / Enter: Terminal   Esc: close launcher",
                   COLOR_TEXT_SECONDARY);
  font_draw_string(x + 24, y + 88,
                   "Terminal      desktop launch terminal",
                   COLOR_TEXT_PRIMARY);
  font_draw_string(x + 24, y + 116,
                   "Settings      desktop session",
                   COLOR_TEXT_SECONDARY);
  font_draw_string(x + 24, y + 144,
                   "Packages      pkg search desktop",
                   COLOR_TEXT_SECONDARY);
  font_draw_string(x + 24, y + 180,
                   "Next: file manager, bar widgets and true tiling.",
                   COLOR_TEXT_MUTED);
}

static void desktop_dwindle_rect(int target, int count, int x, int y, int width,
                                 int height, int *rx, int *ry, int *rw,
                                 int *rh) {
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
    int split_vertical = desktop_split_mode == 1 ||
                         (desktop_split_mode == 0 && cur_w >= cur_h);
    if (i == count - 1) {
      *rx = cur_x;
      *ry = cur_y;
      *rw = cur_w;
      *rh = cur_h;
      return;
    }
    if (split_vertical) {
      int first_w = (cur_w * desktop_split_ratio_percent) / 100;
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
      int first_h = (cur_h * desktop_split_ratio_percent) / 100;
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

static void desktop_master_rect(int target, int count, int x, int y, int width,
                                int height, int *rx, int *ry, int *rw,
                                int *rh) {
  int master_w;
  int stack_count;
  int stack_h;

  if (count <= 1 || target == 0) {
    *rx = x;
    *ry = y;
    *rw = count <= 1 ? width : (width * 58) / 100;
    *rh = height;
    return;
  }

  master_w = (width * desktop_master_ratio_percent) / 100;
  stack_count = count - 1;
  stack_h = height / stack_count;
  *rx = x + master_w;
  *ry = y + (target - 1) * stack_h;
  *rw = width - master_w;
  *rh = target == count - 1 ? height - (target - 1) * stack_h : stack_h;
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

static void desktop_client_rect(int idx, int *rx, int *ry, int *rw, int *rh) {
  int outer_gap = desktop_settings.gaps_out;
  int inner_gap = desktop_settings.gaps_in;
  int area_x = 44 + outer_gap;
  int area_y = TOP_BAR_HEIGHT + 92;
  int area_w = (int)screen_width - 88 - outer_gap * 2;
  int area_h = (int)screen_height - area_y - FOOTER_HEIGHT - 18 - outer_gap;
  int count;
  int pos;
  int workspace;

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
  workspace = desktop_clients[idx].pinned ? desktop_active_workspace
                                          : desktop_clients[idx].workspace;
  if (area_w < 120) {
    area_w = 120;
  }
  if (area_h < 80) {
    area_h = 80;
  }
  count = desktop_client_count_on_workspace(workspace);
  pos = desktop_client_position_on_workspace(idx, workspace);
  if (count <= 0 || pos < 0 || desktop_clients[idx].fullscreen ||
      strcmp(desktop_session.layout, "monocle") == 0) {
    *rx = area_x;
    *ry = area_y;
    *rw = area_w;
    *rh = area_h;
  } else if (strcmp(desktop_session.layout, "master") == 0) {
    desktop_master_rect(pos, count, area_x, area_y, area_w, area_h, rx, ry, rw,
                        rh);
  } else {
    desktop_dwindle_rect(pos, count, area_x, area_y, area_w, area_h, rx, ry,
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

static void draw_desktop_client_tile(const desktop_client_t *client, int x,
                                     int y, int width, int height,
                                     int focused) {
  char title[128];
  color_t border = focused ? COLOR_PANEL_ACCENT : COLOR_PANEL_EDGE;
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
    font_draw_string(render_x, render_y + 8, "prepared surface",
                     COLOR_TEXT_PRIMARY);
    font_draw_string(render_x, render_y + 32,
                     "future native Orizon desktop app",
                     COLOR_TEXT_SECONDARY);
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
           desktop_session.layout, desktop_submap);
  font_draw_string(48, TOP_BAR_HEIGHT + 48, session_line,
                   COLOR_TEXT_SECONDARY);
  snprintf(workspace_line, sizeof(workspace_line),
           "workspace %d/%d | clients=%d | layout=%s split=%s/%d master=%d | settings=%s",
           desktop_active_workspace, desktop_workspace_count,
           desktop_client_count_on_workspace(desktop_active_workspace),
           desktop_session.layout, desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           ORIZON_DESKTOP_SETTINGS_PATH);
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
  } else if (strcmp(desktop_session.layout, "monocle") == 0) {
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
      if (strcmp(desktop_session.layout, "master") == 0) {
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
  if (workspace < 1 || workspace > desktop_workspace_count) {
    return -1;
  }
  if (workspace != desktop_active_workspace) {
    desktop_previous_workspace = desktop_active_workspace;
  }
  desktop_active_workspace = workspace;
  desktop_launcher_visible = 0;
  desktop_focused_client_index();
  desktop_sync_terminal_compat();
  needs_redraw = 1;
  return 0;
}

int gui_desktop_move_terminal_to_workspace(int workspace) {
  int idx = desktop_focused_client_index();

  if (workspace < 1 || workspace > desktop_workspace_count) {
    return -1;
  }
  if (idx < 0) {
    return -1;
  }
  desktop_clients[idx].last_workspace = desktop_clients[idx].workspace;
  desktop_clients[idx].workspace = workspace;
  desktop_set_focused_client_index(idx);
  desktop_sync_terminal_compat();
  needs_redraw = 1;
  return 0;
}

int gui_desktop_spawn_terminal_client(void) {
  return desktop_spawn_client("Terminal", "orizon-terminal", 1) > 0 ? 0 : -1;
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
  desktop_clients[idx].title[0] = '\0';
  desktop_clients[idx].app_id[0] = '\0';
  desktop_set_focused_client_index(-1);
  desktop_focused_client_index();
  desktop_sync_terminal_compat();
  needs_redraw = 1;
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
               "desktop dispatch: usage exec terminal | killactive | "
               "workspace <n|+1|-1|previous> | movetoworkspace <n> | "
               "movefocus next|prev | fullscreen | pseudo | pin | swapnext | "
               "focusmaster | swapwithmaster | togglesplit | layoutmsg <msg> | "
               "resizeactive <x> <y> | submap <name>\n");
    }
    return -1;
  }
  if (strcmp(dispatcher, "exec") == 0) {
    if (strcmp(a, "terminal") == 0 || strcmp(a, "orizon-terminal") == 0 ||
        strcmp(a, "kitty") == 0) {
      if (gui_desktop_spawn_terminal_client() == 0) {
        if (out && out_size) {
          snprintf(out, out_size,
                   "desktop dispatch: exec terminal client spawned\n");
        }
        return 0;
      }
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: client limit reached\n");
      }
      return -1;
    }
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: exec supports terminal today\n");
    }
    return -1;
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
        snprintf(out, out_size, "desktop dispatch: workspace %u\n",
                 (unsigned)workspace);
      }
      return 0;
    }
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: workspace expects 1-%d\n",
               desktop_workspace_count);
    }
    return -1;
  }
  if (strcmp(dispatcher, "movetoworkspace") == 0 ||
      strcmp(dispatcher, "movetoworkspacesilent") == 0) {
    int target = 0;
    if (desktop_parse_workspace_arg(a, &target) == 0 &&
        gui_desktop_move_terminal_to_workspace(target) == 0) {
      workspace = (uint32_t)target;
      if (out && out_size) {
        snprintf(out, out_size, "desktop dispatch: moved active to workspace %u\n",
                 (unsigned)workspace);
      }
      return 0;
    }
    if (out && out_size) {
      snprintf(out, out_size,
               "desktop dispatch: movetoworkspace expects active client and 1-%d\n",
               desktop_workspace_count);
    }
    return -1;
  }
  if (strcmp(dispatcher, "movefocus") == 0) {
    int rc = (strcmp(a, "prev") == 0 || strcmp(a, "l") == 0 ||
              strcmp(a, "u") == 0)
                 ? gui_desktop_focus_prev_client()
                 : gui_desktop_focus_next_client();
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: movefocus %s\n",
               rc == 0 ? "ok" : "no-client");
    }
    return rc;
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
               "desktop dispatch: togglesplit split=%s ratio=%d master=%d\n",
               desktop_split_mode_name(), desktop_split_ratio_percent,
               desktop_master_ratio_percent);
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
                 "desktop dispatch: resizeactive no-op split=%d master=%d\n",
                 desktop_split_ratio_percent, desktop_master_ratio_percent);
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
               "desktop dispatch: resizeactive split=%s ratio=%d master=%d note=tiling-ratio-only\n",
               desktop_split_mode_name(), desktop_split_ratio_percent,
               desktop_master_ratio_percent);
    }
    return 0;
  }
  if (strcmp(dispatcher, "layoutmsg") == 0) {
    int value = 0;
    if (strcmp(a, "togglesplit") == 0 ||
        strcmp(a, "orientationnext") == 0 ||
        strcmp(a, "orientationcycle") == 0) {
      desktop_cycle_split_mode(1);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent);
      }
      return 0;
    }
    if (strcmp(a, "orientationauto") == 0 ||
        strcmp(a, "orientationcenter") == 0) {
      desktop_set_split_mode(0);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent);
      }
      return 0;
    }
    if (strcmp(a, "orientationleft") == 0 ||
        strcmp(a, "orientationright") == 0 ||
        strcmp(a, "orientationvertical") == 0) {
      desktop_set_split_mode(1);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent);
      }
      return 0;
    }
    if (strcmp(a, "orientationtop") == 0 ||
        strcmp(a, "orientationbottom") == 0 ||
        strcmp(a, "orientationhorizontal") == 0) {
      desktop_set_split_mode(2);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent);
      }
      return 0;
    }
    if (strcmp(a, "orientationprev") == 0) {
      desktop_cycle_split_mode(-1);
      if (out && out_size) {
        snprintf(out, out_size,
                 "desktop dispatch: layoutmsg split=%s ratio=%d master=%d\n",
                 desktop_split_mode_name(), desktop_split_ratio_percent,
                 desktop_master_ratio_percent);
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
                   "desktop dispatch: layoutmsg splitratio %d split=%s master=%d\n",
                   desktop_split_ratio_percent, desktop_split_mode_name(),
                   desktop_master_ratio_percent);
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
               "splitratio <10-90|+/-n>, masterratio <10-90|+/-n>, "
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
  if (strcmp(dispatcher, "fullscreen") == 0) {
    int state = desktop_toggle_active_fullscreen();
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: fullscreen %s\n",
               state < 0 ? "no-client" : (state ? "on" : "off"));
    }
    return state < 0 ? -1 : 0;
  }
  if (strcmp(dispatcher, "pseudo") == 0 || strcmp(dispatcher, "pseudotile") == 0) {
    int state = desktop_toggle_active_pseudo();
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: pseudo %s\n",
               state < 0 ? "no-client" : (state ? "on" : "off"));
    }
    return state < 0 ? -1 : 0;
  }
  if (strcmp(dispatcher, "pin") == 0) {
    int state = desktop_toggle_active_pin();
    if (out && out_size) {
      snprintf(out, out_size, "desktop dispatch: pin %s\n",
               state < 0 ? "no-client" : (state ? "on" : "off"));
    }
    return state < 0 ? -1 : 0;
  }
  if (out && out_size) {
    snprintf(out, out_size, "desktop dispatch: unknown '%s'\n", dispatcher);
  }
  return -1;
}

static int desktop_last_focused_index_on_workspace(int workspace) {
  desktop_focus_history_compact();
  for (int i = 0; i < DESKTOP_MAX_CLIENTS; i++) {
    int idx = desktop_client_index_by_id(desktop_focus_history[i]);
    if (idx >= 0 &&
        desktop_client_on_workspace(&desktop_clients[idx], workspace)) {
      return idx;
    }
  }
  return -1;
}

void gui_desktop_format_workspaces(char *out, size_t out_size) {
  size_t used = 0;
  char label[96];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  used += snprintf(out + used, out_size - used,
                   "Orizon desktop workspaces\n"
                   "active: %d\n"
                   "count: %d\n"
                   "model: dynamic workspaces with %s tiling\n"
                   "submap: %s\n",
                   desktop_active_workspace, desktop_workspace_count,
  desktop_layout_engine(), desktop_submap);
  for (int i = 1; i <= desktop_workspace_count && used < out_size; i++) {
    int count = desktop_client_count_on_workspace(i);
    int last_idx = desktop_last_focused_index_on_workspace(i);
    snprintf(label, sizeof(label), "%d client%s", count,
             count == 1 ? "" : "s");
    used += snprintf(
        out + used, out_size - used,
        "workspace %d: %s%s lastwindow=0x%x lasttitle=\"%s\" pinned-aware=yes\n",
        i, count > 0 ? label : "empty",
        i == desktop_active_workspace ? " active" : "",
        last_idx >= 0 ? desktop_client_address(&desktop_clients[last_idx]) : 0,
        last_idx >= 0 ? desktop_clients[last_idx].title : "none");
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: desktop dispatch workspace <1-%d|+1|-1|previous> | "
             "desktop dispatch movetoworkspace <1-%d|+1|-1> | "
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
                   "submap: %s\n",
                   desktop_layout_engine(),
                   desktop_session.layout, total, desktop_active_workspace,
                   focused_idx >= 0 ? desktop_clients[focused_idx].id : 0,
                   desktop_launcher_visible ? "open" : "closed",
                   desktop_session.bar_enabled ? "visible" : "hidden",
                   desktop_split_mode_name(), desktop_split_ratio_percent,
                   desktop_master_ratio_percent, desktop_submap);
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
        "workspace=%d title=\"%s\" class=%s app=%s tiled=yes floating=no "
        "fullscreen=%s pseudo=%s pinned=%s focused=%s focusHistoryID=%d "
        "lastWorkspace=%d mappedSeq=%llu focusSeq=%llu backend=%s\n",
        desktop_client_address(&desktop_clients[i]), desktop_clients[i].id,
        desktop_clients[i].mapped ? "true" : "false",
        desktop_clients[i].hidden ? "true" : "false", rx, ry, rw, rh,
        desktop_clients[i].workspace, desktop_clients[i].title,
        desktop_clients[i].app_id, desktop_clients[i].app_id,
        desktop_clients[i].fullscreen ? "yes" : "no",
        desktop_clients[i].pseudo ? "yes" : "no",
        desktop_clients[i].pinned ? "yes" : "no",
        desktop_clients[i].id == desktop_focused_client_id ? "yes" : "no",
        desktop_clients[i].focus_history_id, desktop_clients[i].last_workspace,
        (unsigned long long)desktop_clients[i].mapped_generation,
        (unsigned long long)desktop_clients[i].focus_generation,
        desktop_clients[i].terminal_backed ? "terminal" : "prepared");
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "dispatch: exec terminal | killactive | movefocus next|prev | "
             "cyclenext | swapnext | focusmaster | swapwithmaster | "
             "fullscreen | pseudo | pin | "
             "workspace <n|+1|-1> | movetoworkspace <n|+1|-1> | "
             "togglesplit | layoutmsg <msg> | resizeactive <x> <y> | "
             "submap <name>\n"
             "rules: class/title/app selectors prepared via %s\n"
             "focus-history: desktop focus-history | desktop hyprctl focushistory\n"
             "limits: no mouse-drag window moving; true Wayland clients are future work\n",
             ORIZON_DESKTOP_RULES_PATH);
  }
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
           desktop_clients[idx].focus_history_id,
           (unsigned long long)desktop_clients[idx].mapped_generation,
           (unsigned long long)desktop_clients[idx].focus_generation);
}

void gui_desktop_format_activeworkspace(char *out, size_t out_size) {
  int clients;
  int last_idx;

  if (!out || out_size == 0) {
    return;
  }
  clients = desktop_client_count_on_workspace(desktop_active_workspace);
  last_idx = desktop_last_focused_index_on_workspace(desktop_active_workspace);
  snprintf(out, out_size,
           "active workspace:\n"
           "  id: %d\n"
           "  name: %d\n"
           "  monitor: Orizon framebuffer\n"
           "  windows: %d\n"
           "  layout: %s\n"
           "  split: %s ratio=%d master=%d\n"
           "  submap: %s\n"
           "  lastwindow: 0x%x\n"
           "  lastwindowtitle: %s\n",
           desktop_active_workspace, desktop_active_workspace, clients,
           desktop_layout_engine(), desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           desktop_submap,
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
             "dispatch: desktop dispatch movefocus next|prev | "
             "desktop dispatch cyclenext [prev] | "
             "desktop dispatch focusmaster\n");
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
           "layer namespace=cursor z=100 visible=yes x=%d y=%d\n"
           "rules-runtime: %s\n"
           "layerrules-runtime: %s\n"
           "limits: layer-shell protocol is not implemented yet; this is the Orizon compositor layer model\n",
           desktop_session.bar_enabled ? "yes" : "no",
           desktop_settings.bar_position,
           desktop_session.bar_enabled ? TOP_BAR_HEIGHT : 0,
           desktop_launcher_visible ? "yes" : "no",
           total_clients > 0 ? "yes" : "empty", total_clients,
           desktop_active_workspace, mouse_x, mouse_y,
           ORIZON_DESKTOP_RULES_PATH, ORIZON_DESKTOP_LAYERS_PATH);
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
             "supported: exec, killactive, workspace, movetoworkspace, movefocus, "
             "cyclenext, swapnext, focusmaster, swapwithmaster, fullscreen, pseudo, pin, togglesplit, "
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
           "available:\n"
           "  dwindle enabled=yes description=dynamic split tiling\n"
           "  master enabled=yes description=main-area plus stack\n"
           "  monocle enabled=yes description=single focused client\n"
           "split-mode: %s\n"
           "split-ratio: %d\n"
           "master-ratio: %d\n"
           "submap: %s\n"
           "clients: total=%d workspace=%d focused=0x%x focus-history=%s\n"
           "set: desktop layout <dwindle|master|monocle>\n"
           "dispatch: desktop dispatch togglesplit | desktop dispatch focusmaster | desktop dispatch swapwithmaster\n"
           "dispatch: desktop dispatch layoutmsg splitratio <10-90|+/-n> | desktop dispatch layoutmsg masterratio <10-90|+/-n>\n"
           "hyprctl: desktop hyprctl layouts\n"
           "limits: layout plugins and per-window layout rules are not implemented yet\n",
           desktop_session.layout, desktop_split_mode_name(),
           desktop_split_ratio_percent, desktop_master_ratio_percent,
           desktop_submap, total, desktop_active_workspace,
           desktop_focused_client_id > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_focused_client_id * 0x100u)
               : 0,
           desktop_focus_history[0] > 0 ? "ready" : "empty");
}

void gui_desktop_format_animations(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop animations\n"
           "enabled: %s\n"
           "source: %s\n"
           "curves:\n"
           "  orizon-pop prepared=yes bezier=0.16,1,0.3,1\n"
           "  orizon-slide prepared=yes bezier=0.2,0.8,0.2,1\n"
           "rules:\n"
           "  windows prepared=yes style=fade+scale\n"
           "  workspaces prepared=yes style=slide\n"
           "  layers prepared=yes style=fade\n"
           "runtime: compositor currently applies static redraws only\n"
           "set: desktop keyword animations:enabled <true|false>\n",
           desktop_settings.animations_enabled ? "true" : "false",
           ORIZON_DESKTOP_SETTINGS_PATH);
}

void gui_desktop_format_decorations(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop decorations\n"
           "border: size=%d active-color=accent inactive-color=edge\n"
           "rounding: %d\n"
           "shadows: enabled=%s renderer=software\n"
           "blur: enabled=no prepared=no\n"
           "drop-shadow: %s\n"
           "window-moving: manual-drag=no tiled-dispatch=yes\n"
           "set: desktop keyword decoration:rounding <n> | desktop settings set border-size <n>\n",
           desktop_settings.border_size, desktop_settings.rounding,
           desktop_settings.shadows_enabled ? "true" : "false",
           desktop_settings.shadows_enabled ? "enabled" : "disabled");
}

void gui_desktop_format_descriptions(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop hyprctl descriptions\n"
           "commands: version, systeminfo, clients, workspaces, activeworkspace, activewindow\n"
           "commands: monitors, binds, layers, layouts, animations, decorations, devices\n"
           "commands: cursorpos, splash, configerrors, rollinglog, instances, submap, focushistory\n"
           "commands: getoption <key>, keyword <key> <value>, dispatch <dispatcher> [args], reload\n"
           "dispatchers: exec, killactive, workspace, movetoworkspace, movefocus, cyclenext, swapnext\n"
           "dispatchers: focusmaster, swapwithmaster, fullscreen, pseudo, pin, togglesplit, layoutmsg, resizeactive, submap\n"
           "layoutmsg: togglesplit, orientationnext, orientationprev, orientationleft/right/top/bottom\n"
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
           "runtime-gaps: in=%d out=%d border=%d rounding=%d animations=%s shadows=%s\n"
           "tiling-engine: %s\n"
           "tiling-split: mode=%s ratio=%d master-ratio=%d\n"
           "runtime-submap: %s\n"
           "manual-window-drag: no\n"
           "workspace-active: %d\nworkspace-count: %d\n"
           "workspace-clients: %d\nfocused-client: 0x%x\n"
           "focus-history-front: 0x%x\n",
           base, session, desktop_terminal_visible ? "open" : "closed",
           desktop_launcher_visible ? "open" : "closed",
           desktop_session.bar_enabled ? "visible" : "hidden",
           desktop_session.theme, desktop_session.wallpaper,
           desktop_session.layout,
           desktop_session.focus_follows_mouse ? "yes" : "no",
           ORIZON_DESKTOP_SETTINGS_PATH, desktop_settings.gaps_in,
           desktop_settings.gaps_out, desktop_settings.border_size,
           desktop_settings.rounding,
           desktop_settings.animations_enabled ? "yes" : "no",
           desktop_settings.shadows_enabled ? "yes" : "no",
           strcmp(desktop_session.layout, "master") == 0
               ? "master"
               : (strcmp(desktop_session.layout, "monocle") == 0 ? "monocle"
                                                                  : "dwindle"),
           desktop_split_mode_name(), desktop_split_ratio_percent,
           desktop_master_ratio_percent, desktop_submap,
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
           "version: %s 0.20.0\n"
           "compositor: Orizon framebuffer compositor\n"
           "backend: framebuffer\n"
           "renderer: software\n"
           "monitor: %lux%lu scale=%d reserved-top=%d reserved-bottom=%d\n"
           "session: enabled=%s theme=%s wallpaper=%s layout=%s bar=%s launcher=%s\n"
           "clients: total=%d active-workspace=%d focused=0x%x focus-history=%s\n"
           "layout-state: split=%s ratio=%d master=%d submap=%s\n"
           "settings: gaps=%d/%d border=%d rounding=%d animations=%s shadows=%s keyboard=%s pointer=%s\n"
           "protocols: wayland=no wlroots=no xwayland=no layer-shell=prepared\n"
           "truth: Hyprland-style Orizon profile, not upstream Hyprland\n",
           ORIZON_DESKTOP_PACKAGE, (unsigned long)screen_width,
           (unsigned long)screen_height, ui_scale, TOP_BAR_HEIGHT,
           FOOTER_HEIGHT, desktop_mode_enabled ? "yes" : "no",
           desktop_session.theme, desktop_session.wallpaper,
           desktop_session.layout,
           desktop_session.bar_enabled ? "yes" : "no",
           desktop_launcher_visible ? "open" : "closed", total,
           desktop_active_workspace,
           desktop_focused_client_id > 0
               ? DESKTOP_CLIENT_ADDRESS_BASE +
                     ((uint32_t)desktop_focused_client_id * 0x100u)
               : 0,
           desktop_focus_history[0] > 0 ? "ready" : "empty",
           desktop_split_mode_name(), desktop_split_ratio_percent,
           desktop_master_ratio_percent, desktop_submap,
           desktop_settings.gaps_in, desktop_settings.gaps_out,
           desktop_settings.border_size, desktop_settings.rounding,
           desktop_settings.animations_enabled ? "true" : "false",
           desktop_settings.shadows_enabled ? "true" : "false",
           desktop_settings.keyboard_layout, desktop_settings.pointer_profile);
}

void gui_desktop_format_hyprctl_version(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop hyprctl version\n"
           "facade: Hyprland-style compatibility commands\n"
           "desktop-package: %s 0.20.0\n"
           "compositor: Orizon framebuffer compositor\n"
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
    gui_show_boot_stage("Loading persistent workspace from disk...");
    vfs_persist_load();
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
