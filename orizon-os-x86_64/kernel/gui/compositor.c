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
    "graphite", "aurora", "floating", 1, 1, 1};
static int desktop_workspace_count = 3;
static int desktop_active_workspace = 1;
static int desktop_terminal_workspace = 1;
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
    hint = desktop_terminal_visible
               ? "Desktop profile active. F2 closes the terminal; 'desktop status' shows session state."
               : "Desktop profile active. Press F1, t, Enter or click to open the terminal.";
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

static int desktop_terminal_on_active_workspace(void) {
  return desktop_terminal_visible &&
         desktop_terminal_workspace == desktop_active_workspace;
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

static void draw_desktop_card(int x, int y, int width, int height,
                              const char *title, const char *body,
                              const char *hint) {
  draw_shadow_panel(x, y, width, height);
  fb_fill_rect_alpha(x, y, width, height, MAKE_ARGB(228, 12, 17, 28));
  fb_draw_rect(x, y, width, height, COLOR_PANEL_EDGE);
  fb_fill_rect(x, y, 4, height, COLOR_PANEL_ACCENT);
  font_draw_string(x + 18, y + 18, title, COLOR_TEXT_PRIMARY);
  font_draw_string(x + 18, y + 46, body, COLOR_TEXT_SECONDARY);
  if (hint && hint[0]) {
    font_draw_string(x + 18, y + height - 28, hint, COLOR_TEXT_MUTED);
  }
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
           "WS %d/%d  layout=%s  F1 Terminal  F2 Close  F3 Launcher  terminal=ws%d",
           desktop_active_workspace, desktop_workspace_count,
           desktop_session.layout, desktop_terminal_workspace);
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

static void draw_desktop_scene(void) {
  int card_w = 360;
  int card_h = 112;
  int start_x = 48;
  int start_y = TOP_BAR_HEIGHT + 86;
  char session_line[128];
  char workspace_line[128];

  draw_background();
  draw_top_bar();
  draw_desktop_status_bar();
  font_draw_string(48, TOP_BAR_HEIGHT + 24, "Orizon Desktop",
                   COLOR_TEXT_PRIMARY);
  snprintf(session_line, sizeof(session_line),
           "Hyprland-style profile | theme=%s wallpaper=%s layout=%s",
           desktop_session.theme, desktop_session.wallpaper,
           desktop_session.layout);
  font_draw_string(48, TOP_BAR_HEIGHT + 48, session_line,
                   COLOR_TEXT_SECONDARY);
  snprintf(workspace_line, sizeof(workspace_line),
           "workspace %d/%d | terminal workspace %d",
           desktop_active_workspace, desktop_workspace_count,
           desktop_terminal_workspace);
  font_draw_string(48, TOP_BAR_HEIGHT + 68, workspace_line,
                   COLOR_TEXT_MUTED);

  draw_desktop_card(start_x, start_y, card_w, card_h, "Terminal",
                    desktop_terminal_on_active_workspace()
                        ? "Open here: Orizon console window"
                        : (desktop_terminal_visible
                               ? "Open on another workspace"
                               : "Closed: press F1, t or click"),
                    "F2 closes, F1 opens");
  draw_desktop_card(start_x + card_w + 24, start_y, card_w, card_h,
                    "Config",
                    "/system/desktop-session.conf",
                    "desktop session/layout/theme");
  draw_desktop_card(start_x, start_y + card_h + 22, card_w, card_h,
                    "Package",
                    ORIZON_DESKTOP_PACKAGE,
                    "desktop package writes installable .opkg");
  draw_desktop_card(start_x + card_w + 24, start_y + card_h + 22, card_w,
                    card_h, "Launcher",
                    desktop_launcher_visible ? "Open: app launcher"
                                             : "Closed: press F3",
                    "desktop apps lists entries");

  if (desktop_terminal_on_active_workspace()) {
    draw_shell_frame();
    if (main_terminal) {
      term_render(main_terminal);
    } else {
      font_draw_string(shell_x + PANEL_PADDING,
                       shell_y + PANEL_TITLE_HEIGHT + 14,
                       "Terminal initialization failed.", COLOR_RED);
    }
  } else {
    draw_centered_string((int)screen_height / 2 + 56,
                         desktop_terminal_visible ? "Workspace is empty"
                                                  : "Terminal is closed",
                         COLOR_TEXT_PRIMARY);
    draw_centered_string((int)screen_height / 2 + 80,
                         desktop_terminal_visible
                             ? "Use 'desktop workspace <n>' or 'desktop move terminal <n>'."
                             : "Press F1, t, Enter, Space or click the desktop to open it.",
                         COLOR_TEXT_SECONDARY);
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

static void keyboard_callback(int key) {
  if (splash_ticks_remaining > 0 &&
      (key == ' ' || key == '\n' || key == '\r' || key == KEY_ESC)) {
    splash_ticks_remaining = 0;
    needs_redraw = 1;
    return;
  }

  if (desktop_mode_enabled) {
    if (key == KEY_F3) {
      gui_desktop_toggle_launcher();
      return;
    }
    if (desktop_launcher_visible) {
      if (key == KEY_ESC) {
        gui_desktop_hide_launcher();
      } else if (key == '\n' || key == '\r' || key == ' ' || key == '1') {
        gui_desktop_open_terminal();
        gui_desktop_hide_launcher();
      }
      return;
    }
    if (key == KEY_F1 || key == 't' || key == 'T') {
      gui_desktop_open_terminal();
      return;
    }
    if (key == KEY_F2) {
      gui_desktop_close_terminal();
      return;
    }
    if (!desktop_terminal_on_active_workspace()) {
      if (key == '\n' || key == '\r' || key == ' ') {
        gui_desktop_open_terminal();
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
    needs_redraw = 1;
  }

  if (left_click && splash_ticks_remaining > 0) {
    splash_ticks_remaining = 0;
    needs_redraw = 1;
  } else if (left_click && splash_ticks_remaining <= 0 &&
             desktop_mode_enabled && desktop_launcher_visible) {
    gui_desktop_hide_launcher();
  } else if (left_click && splash_ticks_remaining <= 0 &&
             desktop_mode_enabled && !desktop_terminal_on_active_workspace()) {
    gui_desktop_open_terminal();
  }

  if (wheel != 0 && splash_ticks_remaining <= 0 && main_terminal &&
      (!desktop_mode_enabled || desktop_terminal_on_active_workspace())) {
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
  } else {
    desktop_launcher_visible = 0;
  }
  needs_redraw = 1;
}

int gui_desktop_enabled(void) {
  return desktop_mode_enabled;
}

void gui_desktop_open_terminal(void) {
  desktop_terminal_workspace = desktop_active_workspace;
  desktop_terminal_visible = 1;
  desktop_launcher_visible = 0;
  needs_redraw = 1;
}

void gui_desktop_close_terminal(void) {
  if (!desktop_mode_enabled) {
    return;
  }
  desktop_terminal_visible = 0;
  needs_redraw = 1;
}

void gui_desktop_toggle_terminal(void) {
  if (!desktop_mode_enabled) {
    return;
  }
  desktop_terminal_visible = desktop_terminal_visible ? 0 : 1;
  needs_redraw = 1;
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
  desktop_active_workspace = workspace;
  desktop_launcher_visible = 0;
  needs_redraw = 1;
  return 0;
}

int gui_desktop_move_terminal_to_workspace(int workspace) {
  if (workspace < 1 || workspace > desktop_workspace_count) {
    return -1;
  }
  desktop_terminal_workspace = workspace;
  needs_redraw = 1;
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
                   "count: %d\n"
                   "terminal: %s workspace=%d on-active=%s\n",
                   desktop_active_workspace, desktop_workspace_count,
                   desktop_terminal_visible ? "open" : "closed",
                   desktop_terminal_workspace,
                   desktop_terminal_on_active_workspace() ? "yes" : "no");
  for (int i = 1; i <= desktop_workspace_count && used < out_size; i++) {
    used += snprintf(out + used, out_size - used, "workspace %d: %s%s\n", i,
                     (desktop_terminal_visible && desktop_terminal_workspace == i)
                         ? "terminal"
                         : "empty",
                     i == desktop_active_workspace ? " active" : "");
  }
  if (used < out_size) {
    snprintf(out + used, out_size - used,
             "commands: desktop workspace <1-%d> | desktop move terminal <1-%d>\n",
             desktop_workspace_count, desktop_workspace_count);
  }
}

void gui_desktop_format_windows(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "Orizon desktop windows\n"
           "layout: %s\n"
           "known-windows: 1\n"
           "terminal: %s workspace=%d active-workspace=%d focused=%s\n"
           "launcher: %s overlay=yes workspace=global\n"
           "bar: %s layer=top\n"
           "commands: desktop launch terminal | desktop close terminal | "
           "desktop move terminal <1-%d> | desktop layout <floating|tiling|monocle>\n"
           "limits: one managed app today; true tiling and Wayland clients are future work\n",
           desktop_session.layout, desktop_terminal_visible ? "open" : "closed",
           desktop_terminal_workspace, desktop_active_workspace,
           desktop_terminal_on_active_workspace() ? "yes" : "no",
           desktop_launcher_visible ? "open" : "closed",
           desktop_session.bar_enabled ? "visible" : "hidden",
           desktop_workspace_count);
}

void gui_desktop_reload_session(void) {
  if (orizon_desktop_load_session(&desktop_session) < 0) {
    snprintf(desktop_session.theme, sizeof(desktop_session.theme), "%s",
             "graphite");
    snprintf(desktop_session.wallpaper, sizeof(desktop_session.wallpaper),
             "%s", "aurora");
    snprintf(desktop_session.layout, sizeof(desktop_session.layout), "%s",
             "floating");
    desktop_session.bar_enabled = 1;
    desktop_session.launcher_enabled = 1;
    desktop_session.autostart_terminal = 1;
  }
  if (!desktop_session.launcher_enabled) {
    desktop_launcher_visible = 0;
  }
  needs_redraw = 1;
}

void gui_desktop_format_status(char *out, size_t out_size) {
  char base[1400];
  const char *session;

  if (!out || out_size == 0) {
    return;
  }
  orizon_desktop_format_status(base, sizeof(base));
  session = desktop_mode_enabled ? "active" : "inactive";
  snprintf(out, out_size,
           "%scompositor-session: %s\nterminal-window: %s\n"
           "launcher-window: %s\nbar: %s\nruntime-theme: %s\n"
           "runtime-wallpaper: %s\nruntime-layout: %s\n"
           "workspace-active: %d\nworkspace-count: %d\n"
           "terminal-workspace: %d\nterminal-on-active-workspace: %s\n",
           base, session, desktop_terminal_visible ? "open" : "closed",
           desktop_launcher_visible ? "open" : "closed",
           desktop_session.bar_enabled ? "visible" : "hidden",
           desktop_session.theme, desktop_session.wallpaper,
           desktop_session.layout, desktop_active_workspace,
           desktop_workspace_count, desktop_terminal_workspace,
           desktop_terminal_on_active_workspace() ? "yes" : "no");
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
