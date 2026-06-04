/*
 * Orizon OS x86_64 - VT100 Terminal Emulator
 */

#include "../include/gui.h"
#include "../include/i2c_hid.h"
#include "../include/acpi.h"
#include "../include/bootinfo.h"
#include "../include/desktop.h"
#include "../include/input_layout.h"
#include "../include/klog.h"
#include "../include/kmalloc.h"
#include "../include/install.h"
#include "../include/net.h"
#include "../include/netstack.h"
#include "../include/packages.h"
#include "../include/pci.h"
#include "../include/power.h"
#include "../include/ps2.h"
#include "../include/report.h"
#include "../include/sched.h"
#include "../include/selftest.h"
#include "../include/ssh.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/system_state.h"
#include "../include/terminal.h"
#include "../include/timer.h"
#include "../include/update.h"
#include "../include/usb.h"
#include "../include/vfs.h"
#include "../include/wifi.h"

/* Terminal colors (ANSI) */
static const uint32_t term_colors[16] = {
    0x1E1E2E, /* 0 - Black */
    0xF38BA8, /* 1 - Red */
    0xA6E3A1, /* 2 - Green */
    0xF9E2AF, /* 3 - Yellow */
    0x89B4FA, /* 4 - Blue */
    0xCBA6F7, /* 5 - Magenta */
    0x94E2D5, /* 6 - Cyan */
    0xCDD6F4, /* 7 - White */
    0x585B70, /* 8 - Bright Black */
    0xF38BA8, /* 9 - Bright Red */
    0xA6E3A1, /* 10 - Bright Green */
    0xF9E2AF, /* 11 - Bright Yellow */
    0x89B4FA, /* 12 - Bright Blue */
    0xCBA6F7, /* 13 - Bright Magenta */
    0x94E2D5, /* 14 - Bright Cyan */
    0xFFFFFF, /* 15 - Bright White */
};

#define TERM_EDIT_MAX 2048
#define TERM_SCROLLBACK_LINES 256
#define TERM_KEY_SCROLL_LINES 3
#define TERM_HISTORY_MAX 32
#define TERM_PAGER_MAX 32768
#define TERM_PIPE_MAX TERM_PAGER_MAX
#define TERM_PAGER_PAGE_LINES (TERM_ROWS - 2)
#define TERM_HISTORY_PATH "/workspace/.orizon/history"
#define TERM_WIFI_LOG_PATH "/logs/wifi.log"
#define TERM_WIFI_LAST_PATH "/workspace/.orizon/wifi-validation"

/* Terminal state */
typedef struct terminal {
  char chars[TERM_ROWS * TERM_COLS];
  uint8_t fg_colors[TERM_ROWS * TERM_COLS];
  uint8_t bg_colors[TERM_ROWS * TERM_COLS];
  char scroll_chars[TERM_SCROLLBACK_LINES * TERM_COLS];
  uint8_t scroll_fg[TERM_SCROLLBACK_LINES * TERM_COLS];
  uint8_t scroll_bg[TERM_SCROLLBACK_LINES * TERM_COLS];
  int scroll_count;
  int scroll_offset;
  int cursor_x, cursor_y;
  int visible;
  int content_x, content_y;
  int width, height;
  uint8_t current_fg, current_bg;
  
  /* Escape sequence */
  int in_escape;
  char escape_buf[32];
  int escape_len;
  
  /* Input */
  char input_buf[256];
  int input_len;
  int input_cursor;
  int input_start_x, input_start_y;
  char cwd[256];

  /* Line editor */
  int edit_mode;
  char edit_path[MAX_PATH];
  char edit_buf[TERM_EDIT_MAX];
  size_t edit_len;

  /* Full-screen pager */
  int pager_mode;
  char pager_title[MAX_PATH];
  size_t pager_size;
  int pager_top_line;
  int pager_total_lines;
  int pager_truncated;

  /* Guided disk installer */
  int install_mode;
  int install_step;
  char install_language[16];
  char install_keyboard[24];
  char install_disk_mode[24];
  int install_disk_index;
  char install_disk_name[24];
  char install_disk_summary[128];
  int install_data_partition_index;
  char install_data_partition_name[24];
  char install_data_partition_summary[128];
  char install_hostname[64];
  char install_desktop[32];
  
  /* History */
  char history[TERM_HISTORY_MAX][256];
  int history_count;
  int history_pos;
} terminal_t;

static terminal_t *active_term = NULL;
static char term_diag_buf[32768];
static char term_pager_buf[TERM_PAGER_MAX + 1];
static char term_pipe_buf[TERM_PIPE_MAX + 1];
static char term_pipe_tmp[TERM_PIPE_MAX + 1];
static char *term_capture_buf = NULL;
static size_t term_capture_cap = 0;
static size_t term_capture_used = 0;
static int term_capture_truncated = 0;
static int term_capture_esc = 0;

static int term_install_already_complete(void);
static int term_read_text_file_silent(const char *path, char *buf, size_t cap);
static void term_execute_single(terminal_t *term, const char *cmd);
static void term_capture_append_char(char c);

/* External functions */
extern void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void fb_put_pixel(int x, int y, uint32_t color);
extern const uint8_t font_data[256][16];

static void path_pop_component(char *path) {
  int len = strlen(path);

  if (len <= 1) {
    strcpy(path, "/");
    return;
  }

  while (len > 1 && path[len - 1] != '/') {
    len--;
  }

  if (len <= 1) {
    strcpy(path, "/");
  } else {
    path[len - 1] = '\0';
  }
}

static int path_append_component(char *path, size_t size, const char *component,
                                 size_t component_len) {
  size_t path_len = strlen(path);

  if (component_len == 0 ||
      (component_len == 1 && component[0] == '.')) {
    return 0;
  }

  if (component_len == 2 && component[0] == '.' && component[1] == '.') {
    path_pop_component(path);
    return 0;
  }

  if (path_len > 1) {
    if (path_len + 1 >= size) {
      return -1;
    }
    path[path_len++] = '/';
    path[path_len] = '\0';
  }

  if (path_len + component_len >= size) {
    return -1;
  }

  for (size_t i = 0; i < component_len; i++) {
    path[path_len + i] = component[i];
  }
  path[path_len + component_len] = '\0';
  return 0;
}

static int resolve_path(const char *cwd, const char *input, char *out,
                        size_t out_size) {
  char raw[MAX_PATH];
  char trimmed[MAX_PATH];
  const char *p;
  size_t input_len;

  if (!input || out_size < 2) {
    return -1;
  }

  while (*input == ' ') {
    input++;
  }
  input_len = strlen(input);
  while (input_len > 0 && input[input_len - 1] == ' ') {
    input_len--;
  }
  if (input_len == 0 || input_len >= sizeof(trimmed)) {
    return -1;
  }
  memcpy(trimmed, input, input_len);
  trimmed[input_len] = '\0';
  input = trimmed;

  if (input[0] == '/') {
    snprintf(raw, sizeof(raw), "%s", input);
  } else if (cwd && cwd[0] && strcmp(cwd, "/") != 0) {
    snprintf(raw, sizeof(raw), "%s/%s", cwd, input);
  } else {
    snprintf(raw, sizeof(raw), "/%s", input);
  }

  out[0] = '/';
  out[1] = '\0';
  p = raw;

  while (*p) {
    const char *component;
    size_t component_len = 0;

    while (*p == '/') {
      p++;
    }
    component = p;
    while (*p && *p != '/') {
      component_len++;
      p++;
    }

    if (path_append_component(out, out_size, component, component_len) < 0) {
      return -1;
    }
  }

  return 0;
}

/* Draw character */
static void term_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
  fb_fill_rect(x, y, TERM_CHAR_W, TERM_CHAR_H, bg);
  if (c < 32 || c > 126) c = ' ';
  const uint8_t *glyph = font_data[(uint8_t)c];
  for (int row = 0; row < 16; row++) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; col++) {
      if (bits & (0x80 >> col)) {
        fb_put_pixel(x + col, y + row, fg);
      }
    }
  }
}

/* Clear line */
static void term_clear_line(terminal_t *term, int row) {
  for (int col = 0; col < TERM_COLS; col++) {
    int idx = row * TERM_COLS + col;
    term->chars[idx] = ' ';
    term->fg_colors[idx] = term->current_fg;
    term->bg_colors[idx] = term->current_bg;
  }
}

static void term_clear_screen(terminal_t *term) {
  if (!term) {
    return;
  }
  for (int row = 0; row < TERM_ROWS; row++) {
    term_clear_line(term, row);
  }
  term->cursor_x = 0;
  term->cursor_y = 0;
  term->scroll_offset = 0;
}

static void term_write_screen_line(terminal_t *term, int row,
                                   const char *text, size_t text_len,
                                   uint8_t fg, uint8_t bg) {
  size_t len = text_len;

  if (!term || row < 0 || row >= TERM_ROWS) {
    return;
  }
  if (len > TERM_COLS) {
    len = TERM_COLS;
  }
  for (int col = 0; col < TERM_COLS; col++) {
    int idx = row * TERM_COLS + col;
    char ch = ' ';
    if ((size_t)col < len && text) {
      ch = text[col];
      if (ch < 32 || ch > 126) {
        ch = (ch == '\t') ? ' ' : '.';
      }
    }
    term->chars[idx] = ch;
    term->fg_colors[idx] = fg;
    term->bg_colors[idx] = bg;
  }
}

static void term_push_scrollback_line(terminal_t *term, int row) {
  int dst = term->scroll_count;

  if (row < 0 || row >= TERM_ROWS) {
    return;
  }
  if (term->scroll_count >= TERM_SCROLLBACK_LINES) {
    memmove(term->scroll_chars, term->scroll_chars + TERM_COLS,
            (TERM_SCROLLBACK_LINES - 1) * TERM_COLS);
    memmove(term->scroll_fg, term->scroll_fg + TERM_COLS,
            (TERM_SCROLLBACK_LINES - 1) * TERM_COLS);
    memmove(term->scroll_bg, term->scroll_bg + TERM_COLS,
            (TERM_SCROLLBACK_LINES - 1) * TERM_COLS);
    dst = TERM_SCROLLBACK_LINES - 1;
  } else {
    term->scroll_count++;
  }

  memcpy(term->scroll_chars + dst * TERM_COLS, term->chars + row * TERM_COLS,
         TERM_COLS);
  memcpy(term->scroll_fg + dst * TERM_COLS, term->fg_colors + row * TERM_COLS,
         TERM_COLS);
  memcpy(term->scroll_bg + dst * TERM_COLS, term->bg_colors + row * TERM_COLS,
         TERM_COLS);
}

/* Scroll up */
static void term_scroll_up(terminal_t *term) {
  term_push_scrollback_line(term, 0);
  for (int row = 0; row < TERM_ROWS - 1; row++) {
    memcpy(&term->chars[row * TERM_COLS], &term->chars[(row + 1) * TERM_COLS], TERM_COLS);
    memcpy(&term->fg_colors[row * TERM_COLS], &term->fg_colors[(row + 1) * TERM_COLS], TERM_COLS);
    memcpy(&term->bg_colors[row * TERM_COLS], &term->bg_colors[(row + 1) * TERM_COLS], TERM_COLS);
  }
  term_clear_line(term, TERM_ROWS - 1);
}

void term_scroll_view(terminal_t *term, int lines) {
  if (!term || lines == 0) {
    return;
  }
  term->scroll_offset += lines;
  if (term->scroll_offset < 0) {
    term->scroll_offset = 0;
  }
  if (term->scroll_offset > term->scroll_count) {
    term->scroll_offset = term->scroll_count;
  }
}

static int term_handle_scrollback_key(terminal_t *term, int key) {
  if (!term || term->edit_mode || term->install_mode || term->input_len != 0) {
    return 0;
  }
  if (key == 'z' || key == 'Z') {
    term_scroll_view(term, key == 'Z' ? TERM_ROWS / 2 : TERM_KEY_SCROLL_LINES);
    return 1;
  }
  if ((key == 's' || key == 'S') && term->scroll_offset > 0) {
    term_scroll_view(term, key == 'S' ? -(TERM_ROWS / 2) : -TERM_KEY_SCROLL_LINES);
    return 1;
  }
  return 0;
}

/* Newline */
static void term_newline(terminal_t *term) {
  term->cursor_x = 0;
  term->cursor_y++;
  if (term->cursor_y >= TERM_ROWS) {
    term_scroll_up(term);
    term->cursor_y = TERM_ROWS - 1;
  }
}

/* Process escape sequence */
static void term_process_escape(terminal_t *term) {
  if (term->escape_len < 1) return;
  
  if (term->escape_buf[0] == '[') {
    char cmd = term->escape_buf[term->escape_len - 1];
    int params[8] = {0};
    int param_count = 0;
    int num = 0;
    int in_num = 0;
    
    for (int i = 1; i < term->escape_len - 1 && param_count < 8; i++) {
      char c = term->escape_buf[i];
      if (c >= '0' && c <= '9') {
        num = num * 10 + (c - '0');
        in_num = 1;
      } else if (c == ';') {
        if (in_num) params[param_count++] = num;
        num = 0;
        in_num = 0;
      }
    }
    if (in_num) params[param_count++] = num;
    
    switch (cmd) {
      case 'A': /* Cursor Up */
        term->cursor_y -= (params[0] > 0) ? params[0] : 1;
        if (term->cursor_y < 0) term->cursor_y = 0;
        break;
      case 'B': /* Cursor Down */
        term->cursor_y += (params[0] > 0) ? params[0] : 1;
        if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
        break;
      case 'C': /* Cursor Forward */
        term->cursor_x += (params[0] > 0) ? params[0] : 1;
        if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
        break;
      case 'D': /* Cursor Back */
        term->cursor_x -= (params[0] > 0) ? params[0] : 1;
        if (term->cursor_x < 0) term->cursor_x = 0;
        break;
      case 'H': case 'f': /* Cursor Position */
        term->cursor_y = (params[0] > 0) ? params[0] - 1 : 0;
        term->cursor_x = (param_count > 1 && params[1] > 0) ? params[1] - 1 : 0;
        if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
        if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
        break;
      case 'J': /* Erase Display */
        if (params[0] == 2) {
          for (int row = 0; row < TERM_ROWS; row++) term_clear_line(term, row);
          term->cursor_x = 0;
          term->cursor_y = 0;
        }
        break;
      case 'K': /* Erase Line */
        for (int col = term->cursor_x; col < TERM_COLS; col++) {
          term->chars[term->cursor_y * TERM_COLS + col] = ' ';
        }
        break;
      case 'm': /* SGR */
        for (int i = 0; i < param_count; i++) {
          int p = params[i];
          if (p == 0) { term->current_fg = 7; term->current_bg = 0; }
          else if (p >= 30 && p <= 37) term->current_fg = p - 30;
          else if (p >= 40 && p <= 47) term->current_bg = p - 40;
          else if (p >= 90 && p <= 97) term->current_fg = p - 90 + 8;
          else if (p >= 100 && p <= 107) term->current_bg = p - 100 + 8;
          else if (p == 1) term->current_fg |= 8; /* Bold = bright */
        }
        break;
    }
  }
  term->in_escape = 0;
  term->escape_len = 0;
}

/* Put character */
void term_putc(terminal_t *term, char c) {
  if (!term) return;

  if (term_capture_buf) {
    term_capture_append_char(c);
    return;
  }

  term->scroll_offset = 0;
  
  if (term->in_escape) {
    term->escape_buf[term->escape_len++] = c;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
      term_process_escape(term);
    } else if (term->escape_len >= 31) {
      term->in_escape = 0;
      term->escape_len = 0;
    }
    return;
  }
  
  switch (c) {
    case '\033': term->in_escape = 1; term->escape_len = 0; break;
    case '\n': term_newline(term); break;
    case '\r': term->cursor_x = 0; break;
    case '\b': if (term->cursor_x > 0) term->cursor_x--; break;
    case '\t': term->cursor_x = (term->cursor_x + 8) & ~7;
               if (term->cursor_x >= TERM_COLS) term_newline(term); break;
    default:
      if (c >= 32 && c < 127) {
        int idx = term->cursor_y * TERM_COLS + term->cursor_x;
        term->chars[idx] = c;
        term->fg_colors[idx] = term->current_fg;
        term->bg_colors[idx] = term->current_bg;
        term->cursor_x++;
        if (term->cursor_x >= TERM_COLS) term_newline(term);
      }
      break;
  }
}

/* Put string */
void term_puts_t(terminal_t *term, const char *str) {
  while (*str) term_putc(term, *str++);
}

static void term_prepare_input(terminal_t *term) {
  term->input_len = 0;
  term->input_cursor = 0;
  term->input_buf[0] = '\0';
  term->input_start_x = term->cursor_x;
  term->input_start_y = term->cursor_y;
  term->history_pos = term->history_count;
}

static int term_input_limit(terminal_t *term) {
  int limit = TERM_COLS - term->input_start_x - 1;
  if (limit < 1) {
    limit = 1;
  }
  if (limit > 255) {
    limit = 255;
  }
  return limit;
}

static void term_redraw_input(terminal_t *term) {
  int row = term->input_start_y;
  int start = term->input_start_x;

  if (row < 0 || row >= TERM_ROWS) {
    return;
  }

  term->cursor_x = start;
  term->cursor_y = row;
  for (int col = start; col < TERM_COLS; col++) {
    int idx = row * TERM_COLS + col;
    term->chars[idx] = ' ';
    term->fg_colors[idx] = term->current_fg;
    term->bg_colors[idx] = term->current_bg;
  }

  for (int i = 0; i < term->input_len; i++) {
    term_putc(term, term->input_buf[i]);
  }

  term->cursor_x = start + term->input_cursor;
  term->cursor_y = row;
  if (term->cursor_x >= TERM_COLS) {
    term->cursor_x = TERM_COLS - 1;
  }
}

static void term_insert_input_char(terminal_t *term, char c) {
  int limit = term_input_limit(term);
  if (term->input_len >= limit) {
    return;
  }

  for (int i = term->input_len; i > term->input_cursor; i--) {
    term->input_buf[i] = term->input_buf[i - 1];
  }
  term->input_buf[term->input_cursor++] = c;
  term->input_len++;
  term->input_buf[term->input_len] = '\0';
  term_redraw_input(term);
}

static void term_insert_input_text(terminal_t *term, const char *text) {
  while (text && *text) {
    int limit = term_input_limit(term);
    if (term->input_len >= limit) {
      break;
    }
    for (int i = term->input_len; i > term->input_cursor; i--) {
      term->input_buf[i] = term->input_buf[i - 1];
    }
    term->input_buf[term->input_cursor++] = *text++;
    term->input_len++;
    term->input_buf[term->input_len] = '\0';
  }
  term_redraw_input(term);
}

static void term_backspace_input(terminal_t *term) {
  if (term->input_cursor <= 0) {
    return;
  }

  for (int i = term->input_cursor - 1; i < term->input_len; i++) {
    term->input_buf[i] = term->input_buf[i + 1];
  }
  term->input_cursor--;
  term->input_len--;
  term_redraw_input(term);
}

static void term_save_history(terminal_t *term) {
  file_t *f;
  if (!term) {
    return;
  }
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  f = vfs_open(TERM_HISTORY_PATH, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return;
  }
  for (int i = 0; i < term->history_count; i++) {
    vfs_write(f, term->history[i], strlen(term->history[i]));
    vfs_write(f, "\n", 1);
  }
  vfs_close(f);
}

static void term_load_history(terminal_t *term) {
  file_t *f;
  char buf[4096];
  size_t used = 0;
  ssize_t n = 0;
  size_t pos = 0;

  if (!term) {
    return;
  }
  term->history_count = 0;
  term->history_pos = 0;
  f = vfs_open(TERM_HISTORY_PATH, O_RDONLY);
  if (!f) {
    return;
  }
  while (used < sizeof(buf) - 1 &&
         (n = vfs_read(f, buf + used, (sizeof(buf) - 1) - used)) > 0) {
    used += (size_t)n;
  }
  vfs_close(f);
  if (n < 0) {
    return;
  }
  buf[used] = '\0';
  while (pos < used) {
    size_t len = 0;
    while (pos + len < used && buf[pos + len] != '\n') {
      len++;
    }
    while (len > 0 && buf[pos + len - 1] == '\r') {
      len--;
    }
    if (len > 0) {
      if (term->history_count >= TERM_HISTORY_MAX) {
        for (int i = 1; i < TERM_HISTORY_MAX; i++) {
          strncpy(term->history[i - 1], term->history[i], 255);
          term->history[i - 1][255] = '\0';
        }
        term->history_count = TERM_HISTORY_MAX - 1;
      }
      size_t copy = len < 255 ? len : 255;
      memcpy(term->history[term->history_count], buf + pos, copy);
      term->history[term->history_count][copy] = '\0';
      term->history_count++;
    }
    pos += len;
    while (pos < used && buf[pos] != '\n') {
      pos++;
    }
    if (pos < used && buf[pos] == '\n') {
      pos++;
    }
  }
  term->history_pos = term->history_count;
}

static void term_add_history(terminal_t *term, const char *cmd) {
  if (!cmd || *cmd == '\0') {
    return;
  }
  if (term->history_count > 0 &&
      strcmp(term->history[term->history_count - 1], cmd) == 0) {
    term->history_pos = term->history_count;
    return;
  }

  if (term->history_count >= TERM_HISTORY_MAX) {
    for (int i = 1; i < TERM_HISTORY_MAX; i++) {
      strncpy(term->history[i - 1], term->history[i], 255);
      term->history[i - 1][255] = '\0';
    }
    term->history_count = TERM_HISTORY_MAX - 1;
  }

  strncpy(term->history[term->history_count], cmd, 255);
  term->history[term->history_count][255] = '\0';
  term->history_count++;
  term->history_pos = term->history_count;
  term_save_history(term);
}

static void term_set_input_text(terminal_t *term, const char *text) {
  strncpy(term->input_buf, text ? text : "", 255);
  term->input_buf[255] = '\0';
  term->input_len = strlen(term->input_buf);
  if (term->input_len > term_input_limit(term)) {
    term->input_len = term_input_limit(term);
    term->input_buf[term->input_len] = '\0';
  }
  term->input_cursor = term->input_len;
  term_redraw_input(term);
}

static int term_parse_uint(const char *s, int *out) {
  int value = 0;
  int seen = 0;
  while (*s >= '0' && *s <= '9') {
    value = value * 10 + (*s - '0');
    seen = 1;
    s++;
  }
  if (!seen || value <= 0) {
    return -1;
  }
  *out = value;
  return 0;
}

static int term_parse_uint64_allow_zero(const char *s, uint64_t *out) {
  uint64_t value = 0;
  int seen = 0;

  if (!s || !out) {
    return -1;
  }
  while (*s >= '0' && *s <= '9') {
    uint64_t digit = (uint64_t)(*s - '0');
    if (value > (0xffffffffffffffffULL - digit) / 10ULL) {
      return -1;
    }
    value = value * 10ULL + digit;
    seen = 1;
    s++;
  }
  if (!seen) {
    return -1;
  }
  *out = value;
  return 0;
}

static const char *term_skip_spaces(const char *s) {
  while (s && *s == ' ') {
    s++;
  }
  return s ? s : "";
}

static int term_command_is(const char *cmd, const char *name) {
  size_t len = strlen(name);
  return strncmp(cmd, name, len) == 0 && (cmd[len] == '\0' || cmd[len] == ' ');
}

static int term_starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

static char term_ascii_lower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static char *term_trim_mut(char *text) {
  size_t len;

  if (!text) {
    return text;
  }
  while (*text == ' ' || *text == '\t') {
    text++;
  }
  len = strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
    text[--len] = '\0';
  }
  return text;
}

static int term_command_is_interactive(const char *cmd) {
  return term_command_is(cmd, "less") || term_command_is(cmd, "more") ||
         term_command_is(cmd, "edit") || term_command_is(cmd, "install") ||
         term_command_is(cmd, "reboot") || term_command_is(cmd, "restart") ||
         term_command_is(cmd, "shutdown") || term_command_is(cmd, "poweroff") ||
         term_command_is(cmd, "clear");
}

static void term_capture_begin(char *buf, size_t cap) {
  term_capture_buf = buf;
  term_capture_cap = cap;
  term_capture_used = 0;
  term_capture_truncated = 0;
  term_capture_esc = 0;
  if (buf && cap > 0) {
    buf[0] = '\0';
  }
}

static void term_capture_end(void) {
  if (term_capture_buf && term_capture_cap > 0) {
    size_t end = term_capture_used < term_capture_cap
                     ? term_capture_used
                     : term_capture_cap - 1;
    term_capture_buf[end] = '\0';
  }
  term_capture_buf = NULL;
  term_capture_cap = 0;
  term_capture_used = 0;
  term_capture_esc = 0;
}

static void term_capture_append_char(char c) {
  if (!term_capture_buf || term_capture_cap == 0) {
    return;
  }
  if (term_capture_esc) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
      term_capture_esc = 0;
    }
    return;
  }
  if (c == '\033') {
    term_capture_esc = 1;
    return;
  }
  if (term_capture_used + 1 < term_capture_cap) {
    term_capture_buf[term_capture_used++] = c;
    term_capture_buf[term_capture_used] = '\0';
  } else {
    term_capture_truncated = 1;
  }
}

static void term_prompt_prefix(terminal_t *term) {
  const char *cwd = (term && term->cwd[0]) ? term->cwd : "/";
  term_puts_t(term, "\033[32morizon-os\033[0m:");
  term_puts_t(term, "\033[34m");
  term_puts_t(term, cwd);
  term_puts_t(term, "\033[0m$ ");
}

static void term_reprint_input_after_output(terminal_t *term) {
  term_prompt_prefix(term);
  term->input_start_x = term->cursor_x;
  term->input_start_y = term->cursor_y;
  term_redraw_input(term);
}

static void term_complete_command(terminal_t *term, const char *prefix,
                                  size_t prefix_len) {
  static const char *commands[] = {
      "about", "append", "boot-check", "bootguard", "cat", "cd", "clear", "cp", "date",
      "desktop", "disks", "disk", "dmesg", "dns", "dualboot-check", "edit", "echo", "find",
      "backup", "doctor", "firstboot", "free", "gpt", "grep", "head", "health", "help", "history", "hostname", "hw", "id", "init",
      "install", "install-plan", "install-status",
      "input", "journal", "keyboard", "less", "ls", "mkdir", "mounts", "mv", "persist",
      "neofetch", "net", "network-status", "logs", "pci", "ping", "pkg", "poweroff", "ps", "pwd", "reboot", "report", "rollback",
      "rollback-status", "repair-boot", "rescue", "rm", "security", "selftest", "services", "shutdown", "snapshot", "stat", "storage", "partitions", "sync",
      "sysinfo", "ssh", "tail", "touch", "tree", "route", "uname", "update", "uptime", "version", "wifi", "whoami",
      "write", "system"};
  const char *matches[16];
  int count = 0;

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (!term_install_already_complete() &&
        (strcmp(commands[i], "rollback") == 0 ||
         strcmp(commands[i], "rollback-status") == 0 ||
         strcmp(commands[i], "bootguard") == 0)) {
      continue;
    }
    if (term_install_already_complete() && strcmp(commands[i], "install") == 0) {
      continue;
    }
    if (strncmp(commands[i], prefix, prefix_len) == 0 && count < 16) {
      matches[count++] = commands[i];
    }
  }

  if (count == 1) {
    term_insert_input_text(term, matches[0] + prefix_len);
    term_insert_input_text(term, " ");
    return;
  }
  if (count > 1) {
    term_puts_t(term, "\n");
    for (int i = 0; i < count; i++) {
      term_puts_t(term, matches[i]);
      term_puts_t(term, i == count - 1 ? "\n" : "  ");
    }
    term_reprint_input_after_output(term);
  }
}

static void term_complete_path(terminal_t *term, const char *token,
                               size_t token_len) {
  char token_copy[MAX_PATH];
  char dir_arg[MAX_PATH];
  char dir_path[MAX_PATH];
  char prefix[MAX_NAME];
  const char *last_slash = NULL;
  dirent_t entries[32];
  const char *match_name = NULL;
  int match_is_dir = 0;
  int count = 0;

  if (token_len >= sizeof(token_copy)) {
    return;
  }
  memcpy(token_copy, token, token_len);
  token_copy[token_len] = '\0';

  for (size_t i = 0; i < token_len; i++) {
    if (token_copy[i] == '/') {
      last_slash = token_copy + i;
    }
  }

  if (last_slash) {
    size_t dir_len = (size_t)(last_slash - token_copy);
    size_t prefix_len = strlen(last_slash + 1);
    if (dir_len == 0) {
      strcpy(dir_arg, "/");
    } else {
      if (dir_len >= sizeof(dir_arg)) {
        return;
      }
      memcpy(dir_arg, token_copy, dir_len);
      dir_arg[dir_len] = '\0';
    }
    if (prefix_len >= sizeof(prefix)) {
      return;
    }
    strcpy(prefix, last_slash + 1);
  } else {
    strcpy(dir_arg, term->cwd[0] ? term->cwd : "/");
    if (strlen(token_copy) >= sizeof(prefix)) {
      return;
    }
    strcpy(prefix, token_copy);
  }

  if (resolve_path(term->cwd, dir_arg, dir_path, sizeof(dir_path)) < 0) {
    return;
  }

  int entry_count = vfs_readdir(dir_path, entries, 32);
  if (entry_count < 0) {
    return;
  }
  for (int i = 0; i < entry_count; i++) {
    if (term_starts_with(entries[i].name, prefix)) {
      count++;
      if (count == 1) {
        match_name = entries[i].name;
        match_is_dir = entries[i].type == 1;
      }
    }
  }

  if (count == 1 && match_name) {
    size_t prefix_len = strlen(prefix);
    term_insert_input_text(term, match_name + prefix_len);
    term_insert_input_text(term, match_is_dir ? "/" : " ");
    return;
  }
  if (count > 1) {
    term_puts_t(term, "\n");
    for (int i = 0; i < entry_count; i++) {
      if (term_starts_with(entries[i].name, prefix)) {
        term_puts_t(term, entries[i].name);
        if (entries[i].type == 1) {
          term_puts_t(term, "/");
        }
        term_puts_t(term, "  ");
      }
    }
    term_puts_t(term, "\n");
    term_reprint_input_after_output(term);
  }
}

static void term_autocomplete(terminal_t *term) {
  int start;
  int first_token = 1;

  if (!term || term->input_cursor != term->input_len) {
    return;
  }
  start = term->input_cursor;
  while (start > 0 && term->input_buf[start - 1] != ' ') {
    start--;
  }
  for (int i = 0; i < start; i++) {
    if (term->input_buf[i] == ' ') {
      first_token = 0;
      break;
    }
  }

  if (first_token) {
    term_complete_command(term, term->input_buf, (size_t)term->input_len);
  } else {
    term_complete_path(term, term->input_buf + start,
                       (size_t)(term->input_len - start));
  }
}

static int term_split_path_and_text(const char *args, char *path_arg,
                                    size_t path_size, const char **text) {
  size_t len = 0;

  args = term_skip_spaces(args);
  if (*args == '\0') {
    return -1;
  }

  while (args[len] && args[len] != ' ') {
    len++;
  }
  if (len == 0 || len >= path_size) {
    return -1;
  }

  for (size_t i = 0; i < len; i++) {
    path_arg[i] = args[i];
  }
  path_arg[len] = '\0';

  args = term_skip_spaces(args + len);
  if (*args == '\0') {
    return -1;
  }

  *text = args;
  return 0;
}

static const char *term_read_token(const char *args, char *out,
                                   size_t out_size) {
  size_t len = 0;

  args = term_skip_spaces(args);
  if (!args || *args == '\0' || !out || out_size == 0) {
    return NULL;
  }
  while (args[len] && args[len] != ' ') {
    len++;
  }
  if (len == 0 || len >= out_size) {
    return NULL;
  }
  memcpy(out, args, len);
  out[len] = '\0';
  return term_skip_spaces(args + len);
}

static int term_split_two_paths(const char *args, char *first, char *second,
                                size_t path_size) {
  size_t len = 0;

  args = term_skip_spaces(args);
  if (*args == '\0') {
    return -1;
  }

  while (args[len] && args[len] != ' ') {
    len++;
  }
  if (len == 0 || len >= path_size) {
    return -1;
  }
  for (size_t i = 0; i < len; i++) {
    first[i] = args[i];
  }
  first[len] = '\0';

  args = term_skip_spaces(args + len);
  len = 0;
  while (args[len] && args[len] != ' ') {
    len++;
  }
  if (len == 0 || len >= path_size) {
    return -1;
  }
  for (size_t i = 0; i < len; i++) {
    second[i] = args[i];
  }
  second[len] = '\0';

  return *term_skip_spaces(args + len) == '\0' ? 0 : -1;
}

static const char *term_basename(const char *path) {
  const char *name = path;
  while (*path) {
    if (*path == '/' && path[1]) {
      name = path + 1;
    }
    path++;
  }
  return name;
}

static int term_join_path(char *out, size_t out_size, const char *dir,
                          const char *name) {
  if (strcmp(dir, "/") == 0) {
    return snprintf(out, out_size, "/%s", name) < (int)out_size ? 0 : -1;
  }
  return snprintf(out, out_size, "%s/%s", dir, name) < (int)out_size ? 0 : -1;
}

static int term_path_is_inside(const char *path, const char *prefix) {
  size_t len = strlen(prefix);
  return strncmp(path, prefix, len) == 0 &&
         (path[len] == '\0' || path[len] == '/');
}

static void term_rewrite_cwd_after_move(terminal_t *term, const char *old_path,
                                        const char *new_path) {
  size_t old_len = strlen(old_path);
  char updated[MAX_PATH];

  if (!term_path_is_inside(term->cwd, old_path)) {
    return;
  }

  if (strcmp(term->cwd, old_path) == 0) {
    snprintf(term->cwd, sizeof(term->cwd), "%s", new_path);
    return;
  }

  if (snprintf(updated, sizeof(updated), "%s%s", new_path,
               term->cwd + old_len) < (int)sizeof(updated)) {
    snprintf(term->cwd, sizeof(term->cwd), "%s", updated);
  }
}

static int term_resolve_target_path(const char *cwd, const char *src_path,
                                    const char *dst_arg, char *dst_path,
                                    size_t dst_size) {
  int is_dir = 0;
  char resolved[MAX_PATH];

  if (resolve_path(cwd, dst_arg, resolved, sizeof(resolved)) < 0) {
    return -1;
  }

  if (vfs_stat(resolved, NULL, &is_dir) >= 0 && is_dir) {
    if (term_join_path(dst_path, dst_size, resolved,
                       term_basename(src_path)) < 0) {
      return -1;
    }
  } else {
    snprintf(dst_path, dst_size, "%s", resolved);
  }

  return 0;
}

static int term_copy_file(const char *src_path, const char *dst_path) {
  file_t *src = vfs_open(src_path, O_RDONLY);
  if (!src) {
    return -1;
  }

  file_t *dst = vfs_open(dst_path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!dst) {
    vfs_close(src);
    return -1;
  }

  char buf[256];
  ssize_t n;
  while ((n = vfs_read(src, buf, sizeof(buf))) > 0) {
    if (vfs_write(dst, buf, (size_t)n) != n) {
      vfs_close(src);
      vfs_close(dst);
      return -1;
    }
  }

  vfs_close(src);
  vfs_close(dst);
  return n < 0 ? -1 : 0;
}

static void term_editor_help(terminal_t *term) {
  term_puts_t(term, "Editor commands:\n");
  term_puts_t(term, "  .show                 Show numbered buffer\n");
  term_puts_t(term, "  .insert N text        Insert before line N\n");
  term_puts_t(term, "  .replace N text       Replace line N\n");
  term_puts_t(term, "  .del N                Delete line N\n");
  term_puts_t(term, "  .write                Save and keep editing\n");
  term_puts_t(term, "  .save                 Save and exit\n");
  term_puts_t(term, "  .q                    Exit without saving\n");
  term_puts_t(term, "  .clear                Empty the buffer\n");
}

static int term_editor_save_buffer(terminal_t *term) {
  file_t *f = vfs_open(term->edit_path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -1;
  }
  if (term->edit_len > 0 &&
      vfs_write(f, term->edit_buf, term->edit_len) !=
          (ssize_t)term->edit_len) {
    vfs_close(f);
    return -1;
  }
  vfs_close(f);
  return 0;
}

static int term_editor_line_bounds(terminal_t *term, int line_no, size_t *start,
                                   size_t *end) {
  size_t pos = 0;
  int current = 1;

  if (!term || line_no <= 0) {
    return -1;
  }

  while (pos < term->edit_len) {
    size_t line_start = pos;
    while (pos < term->edit_len && term->edit_buf[pos] != '\n') {
      pos++;
    }
    if (pos < term->edit_len && term->edit_buf[pos] == '\n') {
      pos++;
    }
    if (current == line_no) {
      *start = line_start;
      *end = pos;
      return 0;
    }
    current++;
  }
  return -1;
}

static size_t term_editor_insert_offset(terminal_t *term, int line_no) {
  size_t start = 0;
  size_t end = 0;

  if (line_no <= 1) {
    return 0;
  }
  if (term_editor_line_bounds(term, line_no, &start, &end) == 0) {
    return start;
  }
  return term->edit_len;
}

static int term_editor_insert_at(terminal_t *term, size_t offset,
                                 const char *text) {
  size_t len = strlen(text);

  if (offset > term->edit_len || term->edit_len + len + 1 >= TERM_EDIT_MAX) {
    return -1;
  }

  memmove(term->edit_buf + offset + len + 1, term->edit_buf + offset,
          term->edit_len - offset + 1);
  memcpy(term->edit_buf + offset, text, len);
  term->edit_buf[offset + len] = '\n';
  term->edit_len += len + 1;
  return 0;
}

static int term_editor_delete_line(terminal_t *term, int line_no) {
  size_t start = 0;
  size_t end = 0;

  if (term_editor_line_bounds(term, line_no, &start, &end) < 0) {
    return -1;
  }
  memmove(term->edit_buf + start, term->edit_buf + end,
          term->edit_len - end + 1);
  term->edit_len -= end - start;
  return 0;
}

static int term_editor_replace_line(terminal_t *term, int line_no,
                                    const char *text) {
  size_t start = 0;
  size_t end = 0;
  size_t len = strlen(text);

  if (term_editor_line_bounds(term, line_no, &start, &end) < 0) {
    return -1;
  }
  if (term->edit_len - (end - start) + len + 1 >= TERM_EDIT_MAX) {
    return -1;
  }
  memmove(term->edit_buf + start, term->edit_buf + end,
          term->edit_len - end + 1);
  term->edit_len -= end - start;
  return term_editor_insert_at(term, start, text);
}

static const char *term_editor_parse_line_arg(const char *s, int *line_no) {
  int value = 0;
  int seen = 0;

  s = term_skip_spaces(s);
  while (*s >= '0' && *s <= '9') {
    value = value * 10 + (*s - '0');
    seen = 1;
    s++;
  }
  if (!seen || value <= 0) {
    return NULL;
  }
  *line_no = value;
  return term_skip_spaces(s);
}

static void term_editor_show(terminal_t *term) {
  size_t pos = 0;
  int line_no = 1;

  if (term->edit_len == 0) {
    term_puts_t(term, "(empty buffer)\n");
    return;
  }

  while (pos < term->edit_len) {
    size_t start = pos;
    char prefix[16];
    char line[128];
    size_t len;

    while (pos < term->edit_len && term->edit_buf[pos] != '\n') {
      pos++;
    }
    len = pos - start;
    if (len >= sizeof(line)) {
      len = sizeof(line) - 1;
    }
    memcpy(line, term->edit_buf + start, len);
    line[len] = '\0';
    snprintf(prefix, sizeof(prefix), "%3d| ", line_no++);
    term_puts_t(term, prefix);
    term_puts_t(term, line);
    term_puts_t(term, "\n");
    if (pos < term->edit_len && term->edit_buf[pos] == '\n') {
      pos++;
    }
  }
}

static void term_editor_prompt(terminal_t *term) {
  term_puts_t(term, "\033[33medit>\033[0m ");
  term_prepare_input(term);
}

static void term_start_editor(terminal_t *term, const char *display,
                              const char *path) {
  size_t size = 0;
  int is_dir = 0;

  if (vfs_stat(path, &size, &is_dir) >= 0 && is_dir) {
    term_puts_t(term, "edit: ");
    term_puts_t(term, display);
    term_puts_t(term, ": Is a directory\n");
    return;
  }

  term->edit_len = 0;
  term->edit_buf[0] = '\0';
  snprintf(term->edit_path, sizeof(term->edit_path), "%s", path);

  file_t *f = vfs_open(path, O_RDONLY);
  if (f) {
    ssize_t n;
    while (term->edit_len < TERM_EDIT_MAX - 1 &&
           (n = vfs_read(f, term->edit_buf + term->edit_len,
                         (TERM_EDIT_MAX - 1) - term->edit_len)) > 0) {
      term->edit_len += (size_t)n;
    }
    vfs_close(f);
    term->edit_buf[term->edit_len] = '\0';
  }

  term->edit_mode = 1;
  term_puts_t(term, "Editing: ");
  term_puts_t(term, path);
  term_puts_t(term, "\n");
  term_puts_t(term, "Type text to append. Use .help for editor commands.\n");
  if (term->edit_len > 0) {
    term_puts_t(term, "Loaded existing file. Use .show to inspect it.\n");
  }
  term_editor_prompt(term);
}

static void term_editor_submit(terminal_t *term, const char *line) {
  term_puts_t(term, "\n");

  if (strcmp(line, ".help") == 0) {
    term_editor_help(term);
    term_editor_prompt(term);
    return;
  }

  if (strcmp(line, ".q") == 0) {
    term->edit_mode = 0;
    term_puts_t(term, "Editor closed without saving.\n");
    return;
  }

  if (strcmp(line, ".clear") == 0) {
    term->edit_len = 0;
    term->edit_buf[0] = '\0';
    term_puts_t(term, "Buffer cleared.\n");
    term_editor_prompt(term);
    return;
  }

  if (strcmp(line, ".show") == 0) {
    term_editor_show(term);
    term_editor_prompt(term);
    return;
  }

  if (strcmp(line, ".write") == 0) {
    if (term_editor_save_buffer(term) < 0) {
      term_puts_t(term, "edit: save failed\n");
      term_editor_prompt(term);
      return;
    }
    term_puts_t(term, "Saved. Continuing edit session.\n");
    term_editor_prompt(term);
    return;
  }

  if (strcmp(line, ".save") == 0 || strcmp(line, ".wq") == 0) {
    if (term_editor_save_buffer(term) < 0) {
      term_puts_t(term, "edit: save failed\n");
      term_editor_prompt(term);
      return;
    }
    term->edit_mode = 0;
    term_puts_t(term, "Saved: ");
    term_puts_t(term, term->edit_path);
    term_puts_t(term, "\n");
    return;
  }

  if (strncmp(line, ".del ", 5) == 0 ||
      strncmp(line, ".delete ", 8) == 0) {
    int line_no = 0;
    const char *arg = strncmp(line, ".delete ", 8) == 0 ? line + 8 : line + 5;
    if (!term_editor_parse_line_arg(arg, &line_no) ||
        term_editor_delete_line(term, line_no) < 0) {
      term_puts_t(term, "usage: .del N\n");
      term_editor_prompt(term);
      return;
    }
    term_puts_t(term, "Line deleted.\n");
    term_editor_prompt(term);
    return;
  }

  if (strncmp(line, ".insert ", 8) == 0) {
    int line_no = 0;
    const char *text = term_editor_parse_line_arg(line + 8, &line_no);
    if (!text || *text == '\0' ||
        term_editor_insert_at(term, term_editor_insert_offset(term, line_no),
                              text) < 0) {
      term_puts_t(term, "usage: .insert N text\n");
      term_editor_prompt(term);
      return;
    }
    term_puts_t(term, "Line inserted.\n");
    term_editor_prompt(term);
    return;
  }

  if (strncmp(line, ".replace ", 9) == 0) {
    int line_no = 0;
    const char *text = term_editor_parse_line_arg(line + 9, &line_no);
    if (!text || *text == '\0' ||
        term_editor_replace_line(term, line_no, text) < 0) {
      term_puts_t(term, "usage: .replace N text\n");
      term_editor_prompt(term);
      return;
    }
    term_puts_t(term, "Line replaced.\n");
    term_editor_prompt(term);
    return;
  }

  if (line[0] == '.') {
    term_puts_t(term, "edit: unknown command, use .help\n");
    term_editor_prompt(term);
    return;
  }

  if (term_editor_insert_at(term, term->edit_len, line) < 0) {
    term_puts_t(term, "edit: buffer full\n");
    term_editor_prompt(term);
    return;
  }
  term_editor_prompt(term);
}

static void term_print_version(terminal_t *term) {
  term_puts_t(term, "Orizon OS core-x86_64\n");
  term_puts_t(term, "Built: " __DATE__ " " __TIME__ "\n");
}

static void term_format_duration(uint64_t seconds, char *out, size_t size) {
  uint64_t days = seconds / 86400ULL;
  uint64_t hours = (seconds / 3600ULL) % 24ULL;
  uint64_t minutes = (seconds / 60ULL) % 60ULL;
  uint64_t secs = seconds % 60ULL;

  if (days > 0) {
    snprintf(out, size, "%lud %02lu:%02lu:%02lu", (unsigned long)days,
             (unsigned long)hours, (unsigned long)minutes, (unsigned long)secs);
  } else {
    snprintf(out, size, "%02lu:%02lu:%02lu", (unsigned long)hours,
             (unsigned long)minutes, (unsigned long)secs);
  }
}

static void term_print_about(terminal_t *term) {
  char line[128];
  char uptime[40];

  term_puts_t(term, "\033[1;36mOrizon OS\033[0m\n");
  term_puts_t(term, "Profile: Minimal development base\n");
  term_puts_t(term, "Kernel: core-x86_64\n");
  term_format_duration(timer_uptime_seconds(), uptime, sizeof(uptime));
  snprintf(line, sizeof(line), "Uptime: %s (%lu ticks @ %lu Hz)\n", uptime,
           (unsigned long)timer_ticks(), (unsigned long)timer_hz());
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Console: %dx%d\n", TERM_COLS, TERM_ROWS);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Display: %lux%lu\n",
           (unsigned long)screen_width, (unsigned long)screen_height);
  term_puts_t(term, line);
  kmalloc_stats_t stats;
  kmalloc_get_stats(&stats);
  snprintf(line, sizeof(line), "Heap used: %lu bytes\n",
           (unsigned long)stats.used);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Heap largest free: %lu bytes\n",
           (unsigned long)stats.largest_free);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Scheduler switches: %lu\n",
           (unsigned long)sched_context_switches());
  term_puts_t(term, line);
  term_puts_t(term, "Storage: ");
  term_puts_t(term, vfs_persist_status());
  term_puts_t(term, "\n");
  term_puts_t(term, "Built: " __DATE__ " " __TIME__ "\n");
}

static void term_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a,
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

static const char *term_pci_class_name(uint8_t cls, uint8_t sub) {
  switch (cls) {
    case 0x00: return "unclassified";
    case 0x01:
      if (sub == 0x01) return "storage-ide";
      if (sub == 0x06) return "storage-ahci";
      if (sub == 0x08) return "storage-nvme";
      return "storage";
    case 0x02:
      if (sub == 0x00) return "network-ethernet";
      if (sub == 0x80) return "network-other";
      return "network";
    case 0x03:
      if (sub == 0x00) return "display-vga";
      return "display";
    case 0x04: return "multimedia";
    case 0x05: return "memory";
    case 0x06:
      if (sub == 0x04) return "bridge-pci";
      if (sub == 0x01) return "bridge-isa";
      return "bridge";
    case 0x07: return "communication";
    case 0x08: return "system";
    case 0x09: return "input";
    case 0x0C:
      if (sub == 0x03) return "usb";
      if (sub == 0x05) return "smbus";
      if (sub == 0x80) return "serial-bus-other";
      return "serial-bus";
    case 0x11: return "signal-processing";
    default: return "device";
  }
}

static int term_pci_is_supported_intel_nic(uint16_t device_id) {
  switch (device_id) {
    case 0x100E:
    case 0x10D3:
    case 0x153A:
    case 0x15A2:
    case 0x15A3:
    case 0x15B7:
    case 0x15B8:
    case 0x15D6:
    case 0x15D7:
    case 0x15E3:
    case 0x15F2:
    case 0x15F3:
      return 1;
    default:
      return 0;
  }
}

static const char *term_pci_driver_hint(const pci_device_info_t *dev) {
  if (!dev) {
    return "driver=unknown";
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x08) {
    return "driver=nvme";
  }
  if (dev->class_code == 0x01 && dev->subclass == 0x06) {
    return "driver=ahci";
  }
  if (dev->class_code == 0x0C && dev->subclass == 0x03) {
    if (dev->prog_if == 0x30) {
      return "driver=xhci";
    }
    if (dev->prog_if == 0x20) {
      return "driver=ehci";
    }
    return "driver=usb-pending";
  }
  if (dev->class_code == 0x02) {
    if (dev->vendor_id == 0x8086 &&
        term_pci_is_supported_intel_nic(dev->device_id)) {
      return "driver=e1000";
    }
    if (dev->vendor_id == 0x10EC && dev->device_id == 0x8139) {
      return "driver=rtl8139";
    }
    if (dev->vendor_id == 0x1AF4 &&
        (dev->device_id == 0x1000 || dev->device_id == 0x1041)) {
      return "driver=virtio-net";
    }
    if (dev->subclass == 0x80) {
      return "driver=wifi-pending";
    }
    return "driver=ethernet-pending";
  }
  if (dev->class_code == 0x0C && dev->subclass == 0x80) {
    return "driver=i2c-lpss-pending";
  }
  if (dev->class_code == 0x0C && dev->subclass == 0x05) {
    return "driver=smbus-pending";
  }
  if (dev->class_code == 0x03) {
    return "driver=framebuffer";
  }
  return "driver=pending";
}

static int term_pci_is_input_bus_candidate(const pci_device_info_t *dev) {
  if (!dev) {
    return 0;
  }
  if (dev->class_code == 0x0C &&
      (dev->subclass == 0x80 || dev->subclass == 0x05)) {
    return 1;
  }
  if (dev->class_code == 0x0C && dev->subclass == 0x03) {
    return 1;
  }
  return 0;
}

static void term_print_pci_device_line(terminal_t *term,
                                       const pci_device_info_t *dev,
                                       int show_bars) {
  char line[256];
  snprintf(line, sizeof(line),
           "%02x:%02x.%u %04x:%04x class=%02x/%02x/%02x %-18s %s\n",
           dev->bus, dev->device, dev->function, dev->vendor_id,
           dev->device_id, dev->class_code, dev->subclass, dev->prog_if,
           term_pci_class_name(dev->class_code, dev->subclass),
           term_pci_driver_hint(dev));
  term_puts_t(term, line);
  if (show_bars) {
    snprintf(line, sizeof(line),
             "    BAR0=%08lx BAR1=%08lx BAR2=%08lx BAR3=%08lx BAR4=%08lx BAR5=%08lx\n",
             (unsigned long)dev->bar[0], (unsigned long)dev->bar[1],
             (unsigned long)dev->bar[2], (unsigned long)dev->bar[3],
             (unsigned long)dev->bar[4], (unsigned long)dev->bar[5]);
    term_puts_t(term, line);
  }
}

static void term_print_pci(terminal_t *term, const char *cmd) {
  pci_device_info_t devs[96];
  int total = pci_scan_all(devs, 96);
  int shown = total < 96 ? total : 96;
  int show_bars = strstr(cmd, "bars") != NULL;
  char line[128];

  term_puts_t(term, "\033[1;36mPCI devices\033[0m\n");
  snprintf(line, sizeof(line), "Detected: %d, showing: %d%s\n", total, shown,
           show_bars ? " with BARs" : "");
  term_puts_t(term, line);
  for (int i = 0; i < shown; i++) {
    term_print_pci_device_line(term, &devs[i], show_bars);
  }
  if (total > shown) {
    term_puts_t(term, "... list truncated; increase PCI buffer in kernel\n");
  }
  term_puts_t(term, "Tip: use 'pci bars' to include raw BAR registers.\n");
}

static void term_print_input_status(terminal_t *term) {
  char line[256];
  pci_device_info_t devs[96];
  int total;
  int candidates = 0;

  term_puts_t(term, "\033[1;36mInput diagnostics\033[0m\n");
  term_puts_t(term, "Keyboard layout: ");
  term_puts_t(term, input_keyboard_layout());
  term_puts_t(term, "\n");

  ps2_format_status(line, sizeof(line));
  term_puts_t(term, "PS/2: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  usb_format_status(line, sizeof(line));
  term_puts_t(term, "USB HID: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  i2c_hid_format_status(line, sizeof(line));
  term_puts_t(term, "I2C-HID: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");

  term_puts_t(term, "Pointer support:\n");
  term_puts_t(term, "  PS/2 mouse/touchpad: supported when firmware exposes i8042\n");
  term_puts_t(term, "  USB HID keyboard: supported\n");
  term_puts_t(term, "  USB HID mouse/tablet: boot mouse and QEMU usb-tablet reports supported\n");
  term_puts_t(term, "  Desktop pointer detail: use 'desktop pointer'\n");
  if (boot_cmdline_has("orizon.i2chid=1")) {
    term_puts_t(term, "  I2C-HID: Lenovo ELAN/Wacom probe selected; multitouch parser pending\n");
  } else {
    term_puts_t(term, "  I2C-HID: disabled in safe boot; select Lenovo hardware probe at boot\n");
  }

  total = pci_scan_all(devs, 96);
  term_puts_t(term, "Input bus candidates from PCI:\n");
  for (int i = 0; i < total && i < 96; i++) {
    if (!term_pci_is_input_bus_candidate(&devs[i])) {
      continue;
    }
    term_print_pci_device_line(term, &devs[i], 0);
    candidates++;
  }
  if (candidates == 0) {
    term_puts_t(term, "  none detected in the first PCI scan window\n");
  }
  term_puts_t(term, "Note: ACPI child HID names are not enumerated by Orizon yet.\n");
}

static void term_print_usb_status(terminal_t *term, const char *cmd) {
  char line[512];
  const char *args = term_skip_spaces(cmd + 3);

  if (term_command_is(args, "rescan") || term_command_is(args, "scan")) {
    term_puts_t(term, "usb: rescanning root ports...\n");
    usb_rescan();
  }

  term_puts_t(term, "\033[1;36mUSB diagnostics\033[0m\n");
  usb_format_status(line, sizeof(line));
  term_puts_t(term, "USB core: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  usb_format_port_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  usb_format_device_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  usb_format_net_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  if (usb_net_ready()) {
    term_puts_t(term,
                "Note: USB Ethernet raw packet path is ready; run 'net dhcp'.\n");
  } else if (usb_net_present()) {
    term_puts_t(term,
                "Note: USB Ethernet is detected; check the status field above "
                "for the blocked driver stage.\n");
  } else {
    term_puts_t(term,
                "Tip: plug the adapter, run 'usb rescan', and check whether a "
                "root port or hub appears.\n");
  }
}

static void term_print_sysinfo(terminal_t *term) {
  char line[256];
  char uptime[40];
  char capacity[64];
  char vendor[13];
  uint32_t a, b, c, d;
  kmalloc_stats_t stats;

  term_puts_t(term, "\033[1;36mOrizon sysinfo\033[0m\n");
  term_format_duration(timer_uptime_seconds(), uptime, sizeof(uptime));
  kmalloc_get_stats(&stats);
  storage_format_capacity(capacity, sizeof(capacity));

  term_cpuid(0, 0, &a, &b, &c, &d);
  memcpy(vendor + 0, &b, 4);
  memcpy(vendor + 4, &d, 4);
  memcpy(vendor + 8, &c, 4);
  vendor[12] = '\0';

  snprintf(line, sizeof(line), "os Orizon OS Core\n");
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "mode %s\n",
           term_install_already_complete() ? "installed" : "live-boot");
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "cmdline %s\n",
           boot_cmdline()[0] ? boot_cmdline() : "(none)");
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "kernel core-x86_64 built " __DATE__ " " __TIME__ "\n");
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "uptime %s ticks=%lu hz=%lu\n", uptime,
           (unsigned long)timer_ticks(), (unsigned long)timer_hz());
  term_puts_t(term, line);
  timer_format_status(line, sizeof(line));
  term_puts_t(term, "timer ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  acpi_format_status(line, sizeof(line));
  term_puts_t(term, "acpi ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  snprintf(line, sizeof(line), "timer irq=%s fallback=%s\n",
           gui_timer_irq_active() ? "active" : "not-seen",
           gui_timer_fallback_active() ? "polling" : "off");
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "cpu x86_64 vendor=%s\n", vendor);
  term_puts_t(term, line);
  snprintf(line, sizeof(line),
           "memory heap=%luKB used=%luKB free=%luKB largest=%luKB\n",
           (unsigned long)(stats.total / 1024),
           (unsigned long)(stats.used / 1024),
           (unsigned long)(stats.free / 1024),
           (unsigned long)(stats.largest_free / 1024));
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "display %lux%lu console=%dx%d\n",
           (unsigned long)screen_width, (unsigned long)screen_height,
           TERM_COLS, TERM_ROWS);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "storage %s capacity=%s data=%s\n",
           storage_available() ? storage_status() : "unavailable", capacity,
           vfs_persist_status());
  term_puts_t(term, line);
  net_format_status(line, sizeof(line));
  term_puts_t(term, "ethernet ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  wifi_format_status(line, sizeof(line));
  term_puts_t(term, "wifi ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  netstack_format_status(line, sizeof(line));
  term_puts_t(term, "ipv4 ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  snprintf(line, sizeof(line), "keyboard %s\n", input_keyboard_layout());
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "logs ring=%luB dropped=%lu boot-persisted=%s\n",
           (unsigned long)klog_size(), (unsigned long)klog_dropped_bytes(),
           klog_boot_persisted() ? "yes" : "no");
  term_puts_t(term, line);
}

static void term_print_mounts(terminal_t *term) {
  const char *mode = vfs_persist_available() ? "persistent" : "memory";

  term_puts_t(term, "\033[1;36mOrizon data roots\033[0m\n");
  term_puts_t(term, "/           kernel-vfs memory\n");
  term_puts_t(term, "/workspace  ");
  term_puts_t(term, mode);
  term_puts_t(term, " user workspace\n");
  term_puts_t(term, "/home       ");
  term_puts_t(term, mode);
  term_puts_t(term, " user data\n");
  term_puts_t(term, "/system     ");
  term_puts_t(term, mode);
  term_puts_t(term, " system state/config\n");
  term_puts_t(term, "/packages   ");
  term_puts_t(term, mode);
  term_puts_t(term, " package cache\n");
  term_puts_t(term, "/logs       ");
  term_puts_t(term, mode);
  term_puts_t(term, " boot/install/update/network logs\n");
  term_puts_t(term, "/tmp        memory scratch\n");
  term_puts_t(term, "status      ");
  term_puts_t(term, vfs_persist_status());
  term_puts_t(term, "\n");
}

static void term_print_persist(terminal_t *term, const char *cmd) {
  static char report[768];
  const char *args = term_skip_spaces(cmd + 7);

  if (*args == '\0' || term_command_is(args, "status")) {
    vfs_persist_format_status(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "slots")) {
    vfs_persist_format_slots(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "save") || term_command_is(args, "sync")) {
    if (vfs_persist_save() == 0) {
      term_puts_t(term, "persistence save: ok\n");
    } else {
      term_puts_t(term, "persistence save: failed or not installed\n");
    }
    vfs_persist_format_status(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "restore")) {
    const char *restore = term_skip_spaces(args + 7);
    if (term_command_is(restore, "previous") ||
        term_command_is(restore, "prev")) {
      vfs_persist_restore_previous(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(restore, "slot")) {
      uint64_t slot = 0;
      restore = term_skip_spaces(restore + 4);
      if (term_parse_uint64_allow_zero(restore, &slot) < 0 ||
          slot > 2147483647ULL) {
        term_puts_t(term, "usage: persist restore slot <0..n>\n");
        return;
      }
      vfs_persist_restore_slot((int)slot, report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    term_puts_t(term, "usage: persist restore previous | persist restore slot <n>\n");
    return;
  }
  if (term_command_is(args, "repair")) {
    vfs_persist_repair(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  term_puts_t(term,
              "usage: persist [status|slots|save|repair|restore previous|restore slot <n>]\n");
}

static void term_print_system_state(terminal_t *term, const char *cmd) {
  static char report[4096];
  const char *args = term_skip_spaces(cmd + 6);

  if (*args == '\0' || term_command_is(args, "status")) {
    orizon_system_format_status(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "repair")) {
    orizon_system_repair(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "init") || term_command_is(args, "boot")) {
    orizon_system_run_boot_tasks(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "services")) {
    orizon_system_format_services(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "health")) {
    orizon_system_format_health(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "snapshot")) {
    orizon_system_write_snapshot(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "backup")) {
    orizon_system_write_admin_backup(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "logs") || term_command_is(args, "journal") ||
      term_command_is(args, "bootlog")) {
    orizon_system_format_logs(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "doctor") || term_command_is(args, "check")) {
    orizon_system_format_doctor(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "rescue")) {
    orizon_system_format_rescue(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "firstboot")) {
    const char *sub = term_skip_spaces(args + strlen("firstboot"));
    if (term_command_is(sub, "done") || term_command_is(sub, "confirm")) {
      orizon_system_mark_firstboot_done(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    orizon_system_format_firstboot(report, sizeof(report));
    term_puts_t(term, report);
    term_puts_t(term, "usage: system firstboot done\n");
    return;
  }
  term_puts_t(term,
              "usage: system [status|health|snapshot|backup|init|services|logs|doctor|repair|rescue|firstboot done]\n");
}

static void term_print_hostname_command(terminal_t *term, const char *cmd) {
  static char status[160];
  char host[80];
  const char *args = term_skip_spaces(cmd + strlen("hostname"));

  if (*args == '\0') {
    orizon_system_hostname(host, sizeof(host));
    term_puts_t(term, host);
    term_puts_t(term, "\n");
    return;
  }
  if (term_command_is(args, "set")) {
    const char *name = term_skip_spaces(args + strlen("set"));
    orizon_system_set_hostname(name, status, sizeof(status));
    term_puts_t(term, status);
    return;
  }
  term_puts_t(term, "usage: hostname [set <name>]\n");
}

static void term_print_first_line_or(terminal_t *term, const char *label,
                                     const char *path,
                                     const char *fallback) {
  char buf[160];
  int n = term_read_text_file_silent(path, buf, sizeof(buf));
  term_puts_t(term, label);
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      if (buf[i] == '\n' || buf[i] == '\r') {
        buf[i] = '\0';
        break;
      }
    }
    term_puts_t(term, buf);
  } else {
    term_puts_t(term, fallback);
  }
  term_puts_t(term, "\n");
}

static void term_print_hw(terminal_t *term, const char *args) {
  char line[256];
  char uptime[40];
  char vendor[13];
  uint32_t a, b, c, d;
  kmalloc_stats_t stats;
  pci_device_info_t devs[24];

  if (term_command_is(term_skip_spaces(args), "next")) {
    static char next_report[2048];
    orizon_report_format_hardware_next(next_report, sizeof(next_report));
    term_puts_t(term, next_report);
    if (next_report[0] && next_report[strlen(next_report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }

  term_puts_t(term, "\033[1;36mOrizon Hardware Diagnostics\033[0m\n");
  snprintf(line, sizeof(line), "Boot cmdline: %s\n",
           boot_cmdline()[0] ? boot_cmdline() : "(none)");
  term_puts_t(term, line);

  term_cpuid(0, 0, &a, &b, &c, &d);
  memcpy(vendor + 0, &b, 4);
  memcpy(vendor + 4, &d, 4);
  memcpy(vendor + 8, &c, 4);
  vendor[12] = '\0';
  term_cpuid(1, 0, &a, &b, &c, &d);
  uint32_t stepping = a & 0x0FU;
  uint32_t model = (a >> 4) & 0x0FU;
  uint32_t family = (a >> 8) & 0x0FU;
  uint32_t ext_model = (a >> 16) & 0x0FU;
  uint32_t ext_family = (a >> 20) & 0xFFU;
  if (family == 0x0F) {
    family += ext_family;
  }
  if (family == 0x06 || family == 0x0F) {
    model += ext_model << 4;
  }
  snprintf(line, sizeof(line),
           "CPU: x86_64 vendor=%s family=%lu model=%lu stepping=%lu\n",
           vendor, (unsigned long)family, (unsigned long)model,
           (unsigned long)stepping);
  term_puts_t(term, line);

  term_format_duration(timer_uptime_seconds(), uptime, sizeof(uptime));
  snprintf(line, sizeof(line), "Uptime: %s, ticks=%lu, hz=%lu\n", uptime,
           (unsigned long)timer_ticks(), (unsigned long)timer_hz());
  term_puts_t(term, line);
  timer_format_status(line, sizeof(line));
  term_puts_t(term, "Timer: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  acpi_format_status(line, sizeof(line));
  term_puts_t(term, "ACPI: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");

  kmalloc_get_stats(&stats);
  snprintf(line, sizeof(line),
           "Heap: total=%lu KB used=%lu KB free=%lu KB largest=%lu KB\n",
           (unsigned long)(stats.total / 1024),
           (unsigned long)(stats.used / 1024),
           (unsigned long)(stats.free / 1024),
           (unsigned long)(stats.largest_free / 1024));
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Display: %lux%lu console=%dx%d\n",
           (unsigned long)screen_width, (unsigned long)screen_height,
           TERM_COLS, TERM_ROWS);
  term_puts_t(term, line);

  char capacity[64];
  storage_format_capacity(capacity, sizeof(capacity));
  snprintf(line, sizeof(line), "Disk: %s (%s)\n",
           storage_available() ? storage_status() : "unavailable", capacity);
  term_puts_t(term, line);
  term_puts_t(term, "Workspace: ");
  term_puts_t(term, vfs_persist_status());
  term_puts_t(term, "\n");

  net_format_status(line, sizeof(line));
  term_puts_t(term, "Network: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  wifi_format_status(line, sizeof(line));
  term_puts_t(term, "Wi-Fi: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  netstack_format_status(line, sizeof(line));
  term_puts_t(term, "IPv4: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");

  usb_format_status(line, sizeof(line));
  term_puts_t(term, "USB: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  i2c_hid_format_status(line, sizeof(line));
  term_puts_t(term, "I2C-HID: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  ps2_format_status(line, sizeof(line));
  term_puts_t(term, "PS/2: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  term_puts_t(term, "Keyboard layout: ");
  term_puts_t(term, input_keyboard_layout());
  term_puts_t(term, "\n");

  snprintf(line, sizeof(line), "Installed: %s\n",
           term_install_already_complete() ? "yes" : "no");
  term_puts_t(term, line);
  term_print_first_line_or(term, "Install state: ",
                           "/workspace/.orizon/install-state", "not installed");
  term_print_first_line_or(term, "Update state: ",
                           "/workspace/.orizon/update-state", "not run yet");

  int total = pci_scan_all(devs, 24);
  int shown = total < 24 ? total : 24;
  snprintf(line, sizeof(line), "PCI: %d device(s), showing %d\n", total,
           shown);
  term_puts_t(term, line);
  for (int i = 0; i < shown; i++) {
    term_puts_t(term, "  ");
    term_print_pci_device_line(term, &devs[i], 0);
  }
  if (total > shown) {
    term_puts_t(term, "  ... use 'pci' for the complete list\n");
  }
  term_puts_t(term, "Next capture plan: run 'hw next' or 'report next'.\n");
}

static void term_print_stat(terminal_t *term, const char *display,
                            const char *path) {
  size_t size = 0;
  int is_dir = 0;
  char line[128];

  if (vfs_stat(path, &size, &is_dir) < 0) {
    term_puts_t(term, "stat: cannot access ");
    term_puts_t(term, display);
    term_puts_t(term, "\n");
    return;
  }

  term_puts_t(term, "Path: ");
  term_puts_t(term, path);
  term_puts_t(term, "\n");
  term_puts_t(term, "Type: ");
  term_puts_t(term, is_dir ? "directory\n" : "file\n");
  snprintf(line, sizeof(line), "Size: %lu bytes\n", (unsigned long)size);
  term_puts_t(term, line);
}

static void term_print_tree_recursive(terminal_t *term, const char *path,
                                      int depth) {
  dirent_t entries[16];
  int count;

  if (depth > 5) {
    term_puts_t(term, "  ...\n");
    return;
  }

  count = vfs_readdir(path, entries, 16);
  if (count < 0) {
    return;
  }

  for (int i = 0; i < count; i++) {
    for (int d = 0; d < depth; d++) {
      term_puts_t(term, "  ");
    }
    term_puts_t(term, "|- ");
    term_puts_t(term, entries[i].name);
    if (entries[i].type == 1) {
      term_puts_t(term, "/");
    }
    term_puts_t(term, "\n");

    if (entries[i].type == 1) {
      char child[MAX_PATH];
      if (strcmp(path, "/") == 0) {
        snprintf(child, sizeof(child), "/%s", entries[i].name);
      } else {
        snprintf(child, sizeof(child), "%s/%s", path, entries[i].name);
      }
      term_print_tree_recursive(term, child, depth + 1);
    }
  }
}

static void term_print_tree(terminal_t *term, const char *display,
                            const char *path) {
  int is_dir = 0;

  if (vfs_stat(path, NULL, &is_dir) < 0) {
    term_puts_t(term, "tree: cannot access ");
    term_puts_t(term, display);
    term_puts_t(term, "\n");
    return;
  }
  if (!is_dir) {
    term_puts_t(term, "tree: ");
    term_puts_t(term, display);
    term_puts_t(term, ": Not a directory\n");
    return;
  }

  term_puts_t(term, path);
  term_puts_t(term, "\n");
  term_print_tree_recursive(term, path, 1);
}

static void term_print_wc(terminal_t *term, const char *display,
                          const char *path) {
  file_t *f;
  char buf[256];
  ssize_t n;
  size_t lines = 0;
  size_t words = 0;
  size_t bytes = 0;
  int in_word = 0;
  char last = '\0';
  int is_dir = 0;
  char line[128];

  if (vfs_stat(path, NULL, &is_dir) < 0) {
    term_puts_t(term, "wc: ");
    term_puts_t(term, display);
    term_puts_t(term, ": No such file\n");
    return;
  }
  if (is_dir) {
    term_puts_t(term, "wc: ");
    term_puts_t(term, display);
    term_puts_t(term, ": Is a directory\n");
    return;
  }
  f = vfs_open(path, O_RDONLY);
  if (!f) {
    term_puts_t(term, "wc: open failed\n");
    return;
  }
  while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
    for (ssize_t i = 0; i < n; i++) {
      char c = buf[i];
      bytes++;
      last = c;
      if (c == '\n') {
        lines++;
      }
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        in_word = 0;
      } else if (!in_word) {
        words++;
        in_word = 1;
      }
    }
  }
  vfs_close(f);
  if (n < 0) {
    term_puts_t(term, "wc: read error\n");
    return;
  }
  if (bytes > 0 && last != '\n') {
    lines++;
  }
  snprintf(line, sizeof(line), "%lu lines %lu words %lu bytes %s\n",
           (unsigned long)lines, (unsigned long)words,
           (unsigned long)bytes, display);
  term_puts_t(term, line);
}

static int term_read_regular_file(terminal_t *term, const char *display,
                                  const char *path, char *buf, size_t cap,
                                  const char *tool_name) {
  int is_dir = 0;
  if (vfs_stat(path, NULL, &is_dir) < 0) {
    term_puts_t(term, tool_name);
    term_puts_t(term, ": ");
    term_puts_t(term, display);
    term_puts_t(term, ": No such file\n");
    return -1;
  }
  if (is_dir) {
    term_puts_t(term, tool_name);
    term_puts_t(term, ": ");
    term_puts_t(term, display);
    term_puts_t(term, ": Is a directory\n");
    return -1;
  }

  file_t *f = vfs_open(path, O_RDONLY);
  if (!f) {
    term_puts_t(term, tool_name);
    term_puts_t(term, ": open failed\n");
    return -1;
  }

  size_t used = 0;
  ssize_t n;
  while (used < cap - 1 &&
         (n = vfs_read(f, buf + used, (cap - 1) - used)) > 0) {
    used += (size_t)n;
  }
  vfs_close(f);
  buf[used] = '\0';
  return n < 0 ? -1 : (int)used;
}

static int term_pager_count_visual_lines(size_t size) {
  const char *p = term_pager_buf;
  const char *end = term_pager_buf + size;
  int lines = 0;

  if (size == 0) {
    return 1;
  }
  while (p < end) {
    const char *line = p;
    size_t len = 0;
    while (line + len < end && line[len] != '\n') {
      len++;
    }
    if (len == 0) {
      lines++;
    } else {
      lines += (int)((len + TERM_COLS - 1) / TERM_COLS);
    }
    p = line + len;
    if (p < end && *p == '\n') {
      p++;
    }
  }
  return lines > 0 ? lines : 1;
}

static int term_pager_get_visual_line(terminal_t *term, int wanted,
                                      const char **start, size_t *len) {
  const char *p = term_pager_buf;
  const char *end;
  int visual = 0;

  if (!term || !start || !len || wanted < 0) {
    return -1;
  }
  end = term_pager_buf + term->pager_size;
  if (term->pager_size == 0) {
    *start = "";
    *len = 0;
    return wanted == 0 ? 0 : -1;
  }
  while (p < end) {
    const char *line = p;
    size_t line_len = 0;
    size_t off = 0;
    while (line + line_len < end && line[line_len] != '\n') {
      line_len++;
    }
    if (line_len == 0) {
      if (visual == wanted) {
        *start = line;
        *len = 0;
        return 0;
      }
      visual++;
    }
    while (off < line_len) {
      size_t chunk = line_len - off;
      if (chunk > TERM_COLS) {
        chunk = TERM_COLS;
      }
      if (visual == wanted) {
        *start = line + off;
        *len = chunk;
        return 0;
      }
      visual++;
      off += chunk;
    }
    p = line + line_len;
    if (p < end && *p == '\n') {
      p++;
    }
  }
  return -1;
}

static void term_pager_render(terminal_t *term) {
  char header[160];
  char footer[160];
  int max_top;
  int first;
  int last;

  if (!term || !term->pager_mode) {
    return;
  }
  if (term->pager_total_lines <= 0) {
    term->pager_total_lines = 1;
  }
  max_top = term->pager_total_lines - TERM_PAGER_PAGE_LINES;
  if (max_top < 0) {
    max_top = 0;
  }
  if (term->pager_top_line < 0) {
    term->pager_top_line = 0;
  }
  if (term->pager_top_line > max_top) {
    term->pager_top_line = max_top;
  }

  term_clear_screen(term);
  snprintf(header, sizeof(header), " less: %s", term->pager_title);
  term_write_screen_line(term, 0, header, strlen(header), 15, 4);

  for (int row = 0; row < TERM_PAGER_PAGE_LINES; row++) {
    const char *line = NULL;
    size_t len = 0;
    if (term_pager_get_visual_line(term, term->pager_top_line + row, &line,
                                   &len) == 0) {
      term_write_screen_line(term, row + 1, line, len, 7, 0);
    }
  }

  first = term->pager_top_line + 1;
  last = term->pager_top_line + TERM_PAGER_PAGE_LINES;
  if (last > term->pager_total_lines) {
    last = term->pager_total_lines;
  }
  snprintf(footer, sizeof(footer),
           " z/up back  s/down next  space page  g/G top/end  q quit  %d-%d/%d%s",
           first, last, term->pager_total_lines,
           term->pager_truncated ? " truncated" : "");
  term_write_screen_line(term, TERM_ROWS - 1, footer, strlen(footer), 0, 6);
  term->cursor_x = 0;
  term->cursor_y = TERM_ROWS - 1;
}

static void term_pager_close(terminal_t *term) {
  if (!term) {
    return;
  }
  term->pager_mode = 0;
  term_clear_screen(term);
  term_puts_t(term, "less: closed\n");
  term_prompt_prefix(term);
  term_prepare_input(term);
}

static int term_handle_pager_key(terminal_t *term, int key) {
  int page = TERM_PAGER_PAGE_LINES;
  int max_top;

  if (!term || !term->pager_mode) {
    return 0;
  }
  max_top = term->pager_total_lines - TERM_PAGER_PAGE_LINES;
  if (max_top < 0) {
    max_top = 0;
  }
  if (key == 'q' || key == 'Q' || key == KEY_ESC) {
    term_pager_close(term);
    return 1;
  }
  if (key == 'g') {
    term->pager_top_line = 0;
  } else if (key == 'G') {
    term->pager_top_line = max_top;
  } else if (key == 'z' || key == KEY_UP) {
    term->pager_top_line--;
  } else if (key == 'Z' || key == 'b' || key == 'B') {
    term->pager_top_line -= page;
  } else if (key == 's' || key == KEY_DOWN || key == '\n' || key == '\r') {
    term->pager_top_line++;
  } else if (key == 'S' || key == ' ') {
    term->pager_top_line += page;
  } else {
    return 1;
  }
  term_pager_render(term);
  return 1;
}

static void term_start_pager(terminal_t *term, const char *display,
                             const char *path) {
  size_t file_size = 0;
  int is_dir = 0;
  int n;

  if (vfs_stat(path, &file_size, &is_dir) < 0) {
    term_puts_t(term, "less: ");
    term_puts_t(term, display);
    term_puts_t(term, ": No such file\n");
    return;
  }
  if (is_dir) {
    term_puts_t(term, "less: ");
    term_puts_t(term, display);
    term_puts_t(term, ": Is a directory\n");
    return;
  }
  n = term_read_regular_file(term, display, path, term_pager_buf,
                             sizeof(term_pager_buf), "less");
  if (n < 0) {
    return;
  }
  term_pager_buf[n] = '\0';
  term->pager_mode = 1;
  term->pager_size = (size_t)n;
  term->pager_top_line = 0;
  term->pager_total_lines = term_pager_count_visual_lines(term->pager_size);
  term->pager_truncated = file_size > TERM_PAGER_MAX;
  snprintf(term->pager_title, sizeof(term->pager_title), "%s", display);
  term_pager_render(term);
}

static void term_start_pager_text(terminal_t *term, const char *title,
                                  const char *text, size_t size,
                                  int truncated) {
  size_t copy = size;

  if (!term || !text) {
    return;
  }
  if (copy > TERM_PAGER_MAX) {
    copy = TERM_PAGER_MAX;
    truncated = 1;
  }
  memcpy(term_pager_buf, text, copy);
  term_pager_buf[copy] = '\0';
  term->pager_mode = 1;
  term->pager_size = copy;
  term->pager_top_line = 0;
  term->pager_total_lines = term_pager_count_visual_lines(term->pager_size);
  term->pager_truncated = truncated;
  snprintf(term->pager_title, sizeof(term->pager_title), "%s",
           title ? title : "pipeline");
  term_pager_render(term);
}

static void term_print_head(terminal_t *term, const char *display,
                            const char *path, int max_lines) {
  char buf[2048];
  int n = term_read_regular_file(term, display, path, buf, sizeof(buf), "head");
  int lines = 0;

  if (n < 0) {
    return;
  }

  for (int i = 0; i < n && lines < max_lines; i++) {
    term_putc(term, buf[i]);
    if (buf[i] == '\n') {
      lines++;
    }
  }
  if (n > 0 && buf[n - 1] != '\n') {
    term_puts_t(term, "\n");
  }
}

static void term_print_tail(terminal_t *term, const char *display,
                            const char *path, int max_lines) {
  char buf[4096];
  int n = term_read_regular_file(term, display, path, buf, sizeof(buf), "tail");
  int lines = 0;
  int start;

  if (n < 0) {
    return;
  }
  start = n;
  while (start > 0 && lines <= max_lines) {
    start--;
    if (buf[start] == '\n') {
      lines++;
      if (lines > max_lines) {
        start++;
        break;
      }
    }
  }
  if (start < 0) {
    start = 0;
  }
  for (int i = start; i < n; i++) {
    term_putc(term, buf[i]);
  }
  if (n > 0 && buf[n - 1] != '\n') {
    term_puts_t(term, "\n");
  }
}

static void term_print_grep(terminal_t *term, const char *pattern,
                            const char *display, const char *path) {
  char buf[2048];
  char line[256];
  int line_len = 0;
  int matches = 0;
  int n = term_read_regular_file(term, display, path, buf, sizeof(buf), "grep");

  if (n < 0) {
    return;
  }

  for (int i = 0; i <= n; i++) {
    char c = (i < n) ? buf[i] : '\n';
    if (c == '\n' || line_len >= 255) {
      line[line_len] = '\0';
      if (strstr(line, pattern)) {
        term_puts_t(term, line);
        term_puts_t(term, "\n");
        matches++;
      }
      line_len = 0;
    } else {
      line[line_len++] = c;
    }
  }

  if (matches == 0) {
    term_puts_t(term, "grep: no matches\n");
  }
}

static void term_print_klog(terminal_t *term, size_t max_bytes) {
  char line[160];
  size_t cap = max_bytes;
  size_t n;

  if (cap == 0 || cap > sizeof(term_diag_buf)) {
    cap = sizeof(term_diag_buf);
  }
  n = klog_snapshot(term_diag_buf, cap);
  snprintf(line, sizeof(line),
           "dmesg: ring=%lu bytes dropped=%lu saved=%s\n",
           (unsigned long)klog_size(),
           (unsigned long)klog_dropped_bytes(),
           klog_boot_persisted() ? "yes" : "no");
  term_puts_t(term, line);
  if (n == 0) {
    term_puts_t(term, "dmesg: no kernel messages captured yet\n");
    return;
  }
  term_puts_t(term, term_diag_buf);
  if (term_diag_buf[n - 1] != '\n') {
    term_puts_t(term, "\n");
  }
}

static void term_print_file_tail(terminal_t *term, const char *label,
                                 const char *path, size_t max_bytes) {
  char line[160];
  size_t size = 0;
  size_t want;
  size_t used = 0;
  int is_dir = 0;
  file_t *f;
  ssize_t n = 0;

  if (vfs_stat(path, &size, &is_dir) < 0) {
    term_puts_t(term, label);
    term_puts_t(term, ": missing\n");
    return;
  }
  if (is_dir) {
    term_puts_t(term, label);
    term_puts_t(term, ": is a directory\n");
    return;
  }

  want = max_bytes;
  if (want == 0 || want > sizeof(term_diag_buf) - 1) {
    want = sizeof(term_diag_buf) - 1;
  }

  snprintf(line, sizeof(line), "== %s (%lu bytes) ==\n", label,
           (unsigned long)size);
  term_puts_t(term, line);

  f = vfs_open(path, O_RDONLY);
  if (!f) {
    term_puts_t(term, "logs: open failed\n");
    return;
  }
  if (size > want) {
    size_t start = size - want;
    snprintf(line, sizeof(line), "(last %lu bytes)\n", (unsigned long)want);
    term_puts_t(term, line);
    vfs_seek(f, (int)start, SEEK_SET);
  }

  while (used < want &&
         (n = vfs_read(f, term_diag_buf + used, want - used)) > 0) {
    used += (size_t)n;
  }
  vfs_close(f);
  if (n < 0) {
    term_puts_t(term, "logs: read error\n");
    return;
  }
  term_diag_buf[used] = '\0';
  if (used == 0) {
    term_puts_t(term, "(empty)\n");
    return;
  }
  term_puts_t(term, term_diag_buf);
  if (term_diag_buf[used - 1] != '\n') {
    term_puts_t(term, "\n");
  }
}

static void term_print_log_summary(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 4);
  int default_view = *args == '\0';

  if (term_install_already_complete()) {
    klog_persist_boot_if_installed();
  }

  if (default_view) {
    term_puts_t(term, "\033[1;36mRecent Orizon logs\033[0m\n");
    term_puts_t(term,
                "Use: logs boot | logs storage | logs pci | logs network | logs usb | logs wifi | logs ssh | logs security | logs update | logs install | logs all\n");
    if (vfs_exists(KLOG_BOOT_PATH)) {
      term_print_file_tail(term, KLOG_BOOT_PATH, KLOG_BOOT_PATH, 1024);
    } else {
      term_puts_t(term, "boot.log: not persisted yet, showing live dmesg tail\n");
      term_print_klog(term, 768);
    }
    if (vfs_exists("/workspace/.orizon/update.log")) {
      term_print_file_tail(term, "/workspace/.orizon/update.log",
                           "/workspace/.orizon/update.log", 1024);
    }
    if (vfs_exists(netstack_log_path())) {
      term_print_file_tail(term, netstack_log_path(), netstack_log_path(), 1024);
    }
    if (vfs_exists(usb_log_path())) {
      term_print_file_tail(term, usb_log_path(), usb_log_path(), 1024);
    }
    if (vfs_exists(TERM_WIFI_LOG_PATH)) {
      term_print_file_tail(term, TERM_WIFI_LOG_PATH, TERM_WIFI_LOG_PATH, 1024);
    }
    if (vfs_exists(ORIZON_SSH_LOG_PATH)) {
      term_print_file_tail(term, ORIZON_SSH_LOG_PATH, ORIZON_SSH_LOG_PATH, 1024);
    }
    if (vfs_exists(ORIZON_SECURITY_LOG_PATH)) {
      term_print_file_tail(term, ORIZON_SECURITY_LOG_PATH,
                           ORIZON_SECURITY_LOG_PATH, 1024);
    }
    return;
  }

  if (term_command_is(args, "boot")) {
    if (vfs_exists(KLOG_BOOT_PATH)) {
      term_print_file_tail(term, KLOG_BOOT_PATH, KLOG_BOOT_PATH, 8192);
    } else {
      term_print_klog(term, 8192);
    }
    return;
  }
  if (term_command_is(args, "update")) {
    term_print_file_tail(term, "/workspace/.orizon/update.log",
                         "/workspace/.orizon/update.log", 8192);
    return;
  }
  if (term_command_is(args, "storage")) {
    storage_format_log(term_diag_buf, sizeof(term_diag_buf));
    term_puts_t(term, term_diag_buf);
    if (term_diag_buf[0] && term_diag_buf[strlen(term_diag_buf) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }
  if (term_command_is(args, "pci")) {
    pci_format_diagnostics(term_diag_buf, sizeof(term_diag_buf));
    term_puts_t(term, term_diag_buf);
    if (term_diag_buf[0] && term_diag_buf[strlen(term_diag_buf) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }
  if (term_command_is(args, "network") || term_command_is(args, "net")) {
    term_print_file_tail(term, netstack_log_path(), netstack_log_path(), 8192);
    return;
  }
  if (term_command_is(args, "usb")) {
    term_print_file_tail(term, usb_log_path(), usb_log_path(), 8192);
    return;
  }
  if (term_command_is(args, "wifi")) {
    term_print_file_tail(term, TERM_WIFI_LOG_PATH, TERM_WIFI_LOG_PATH, 8192);
    term_print_file_tail(term, TERM_WIFI_LAST_PATH, TERM_WIFI_LAST_PATH, 2048);
    return;
  }
  if (term_command_is(args, "ssh")) {
    term_print_file_tail(term, ORIZON_SSH_LOG_PATH, ORIZON_SSH_LOG_PATH, 8192);
    return;
  }
  if (term_command_is(args, "security")) {
    term_print_file_tail(term, ORIZON_SECURITY_LOG_PATH,
                         ORIZON_SECURITY_LOG_PATH, 8192);
    return;
  }
  if (term_command_is(args, "install")) {
    term_print_file_tail(term, "/workspace/.orizon/install-log",
                         "/workspace/.orizon/install-log", 8192);
    return;
  }
  if (term_command_is(args, "all")) {
    if (vfs_exists(KLOG_BOOT_PATH)) {
      term_print_file_tail(term, KLOG_BOOT_PATH, KLOG_BOOT_PATH, 4096);
    } else {
      term_print_klog(term, 4096);
    }
    term_print_file_tail(term, "/workspace/.orizon/update.log",
                         "/workspace/.orizon/update.log", 4096);
    storage_format_log(term_diag_buf, sizeof(term_diag_buf));
    term_puts_t(term, term_diag_buf);
    pci_format_diagnostics(term_diag_buf, sizeof(term_diag_buf));
    term_puts_t(term, term_diag_buf);
    term_print_file_tail(term, netstack_log_path(), netstack_log_path(), 4096);
    term_print_file_tail(term, usb_log_path(), usb_log_path(), 4096);
    term_print_file_tail(term, TERM_WIFI_LOG_PATH, TERM_WIFI_LOG_PATH, 4096);
    term_print_file_tail(term, ORIZON_SSH_LOG_PATH, ORIZON_SSH_LOG_PATH, 4096);
    term_print_file_tail(term, ORIZON_SECURITY_LOG_PATH,
                         ORIZON_SECURITY_LOG_PATH, 4096);
    term_print_file_tail(term, "/workspace/.orizon/install-log",
                         "/workspace/.orizon/install-log", 4096);
    term_print_file_tail(term, "/workspace/.orizon/rollback-info",
                         "/workspace/.orizon/rollback-info", 4096);
    return;
  }

  term_puts_t(term,
              "usage: logs [boot|storage|pci|network|usb|wifi|ssh|security|update|install|all]\n");
}

static void term_print_diagnostic_hints(terminal_t *term) {
  int any = 0;

  if (!storage_available()) {
    if (!any) {
      term_puts_t(term, "Hints:\n");
      any = 1;
    }
    term_puts_t(term,
                "  - No writable disk is ready; run `storage diag` before install/update.\n");
  }
  if (!net_link_up()) {
    if (!any) {
      term_puts_t(term, "Hints:\n");
      any = 1;
    }
    term_puts_t(term,
                "  - Ethernet link is down; update needs wired network plus DHCP or static IPv4.\n");
  }
  if (!boot_payloads_ready()) {
    if (!any) {
      term_puts_t(term, "Hints:\n");
      any = 1;
    }
    term_puts_t(term,
                "  - Boot payload capture is missing; installer/update rollback may be blocked.\n");
  }
  if (!term_install_already_complete()) {
    if (!any) {
      term_puts_t(term, "Hints:\n");
      any = 1;
    }
    term_puts_t(term,
                "  - Live boot detected; install Orizon OS before using update/pkg install.\n");
  }
  if (!any) {
    term_puts_t(term, "Hints: no blocking issue detected by the compact checks.\n");
  }
}

static void term_print_report(terminal_t *term) {
  char line[256];
  char uptime[40];
  char capacity[64];
  kmalloc_stats_t stats;
  pci_device_info_t devs[16];
  int pci_total;

  term_puts_t(term, "\033[1;36mOrizon Health Report\033[0m\n");
  term_format_duration(timer_uptime_seconds(), uptime, sizeof(uptime));
  kmalloc_get_stats(&stats);
  storage_format_capacity(capacity, sizeof(capacity));

  snprintf(line, sizeof(line),
           "Boot: uptime=%s ticks=%lu hz=%lu timer=%s/%s log=%luB dropped=%lu saved=%s\n",
           uptime, (unsigned long)timer_ticks(), (unsigned long)timer_hz(),
           gui_timer_irq_active() ? "irq" : "no-irq",
           gui_timer_fallback_active() ? "poll" : "hlt",
           (unsigned long)klog_size(), (unsigned long)klog_dropped_bytes(),
           klog_boot_persisted() ? "yes" : "no");
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Cmdline: %s\n",
           boot_cmdline()[0] ? boot_cmdline() : "(none)");
  term_puts_t(term, line);
  timer_format_status(line, sizeof(line));
  term_puts_t(term, "Timer: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  acpi_format_status(line, sizeof(line));
  term_puts_t(term, "ACPI: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  snprintf(line, sizeof(line), "Install: %s, payloads=%s\n",
           term_install_already_complete() ? "installed" : "live",
           boot_payload_status());
  term_puts_t(term, line);
  snprintf(line, sizeof(line),
           "Memory: total=%luKB used=%luKB free=%luKB largest=%luKB\n",
           (unsigned long)(stats.total / 1024),
           (unsigned long)(stats.used / 1024),
           (unsigned long)(stats.free / 1024),
           (unsigned long)(stats.largest_free / 1024));
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Disk: %s (%s), data=%s\n",
           storage_available() ? storage_status() : "unavailable", capacity,
           vfs_persist_status());
  term_puts_t(term, line);
  net_format_status(line, sizeof(line));
  term_puts_t(term, "Network: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  wifi_format_status(line, sizeof(line));
  term_puts_t(term, "Wi-Fi: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  netstack_format_status(line, sizeof(line));
  term_puts_t(term, "IPv4: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  usb_format_status(line, sizeof(line));
  term_puts_t(term, "USB: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  i2c_hid_format_status(line, sizeof(line));
  term_puts_t(term, "I2C-HID: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  ps2_format_status(line, sizeof(line));
  term_puts_t(term, "PS/2: ");
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  term_print_first_line_or(term, "Update state: ",
                           "/workspace/.orizon/update-state", "not run yet");

  pci_total = pci_scan_all(devs, 16);
  snprintf(line, sizeof(line), "PCI: %d device(s) detected\n", pci_total);
  term_puts_t(term, line);
  term_print_diagnostic_hints(term);
  term_puts_t(term, "\nRecent kernel log tail:\n");
  term_print_klog(term, 256);
}

static void term_find_recursive(terminal_t *term, const char *path,
                                const char *pattern, int depth) {
  dirent_t entries[16];
  int count;

  if (depth > 6) {
    return;
  }

  count = vfs_readdir(path, entries, 16);
  if (count < 0) {
    return;
  }

  for (int i = 0; i < count; i++) {
    char child[MAX_PATH];
    if (strcmp(path, "/") == 0) {
      snprintf(child, sizeof(child), "/%s", entries[i].name);
    } else {
      snprintf(child, sizeof(child), "%s/%s", path, entries[i].name);
    }

    if (!pattern || *pattern == '\0' || strstr(entries[i].name, pattern)) {
      term_puts_t(term, child);
      if (entries[i].type == 1) {
        term_puts_t(term, "/");
      }
      term_puts_t(term, "\n");
    }

    if (entries[i].type == 1) {
      term_find_recursive(term, child, pattern, depth + 1);
    }
  }
}

static void term_update_progress(const char *line, void *ctx) {
  terminal_t *term = (terminal_t *)ctx;
  if (!term || !line) {
    return;
  }
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  term_render(term);
  fb_swap_buffers();
}

static void term_wifi_log_line(const char *line) {
  file_t *f;

  if (!line || !line[0]) {
    return;
  }
  vfs_mkdir("/logs");
  f = vfs_open(TERM_WIFI_LOG_PATH, O_CREAT | O_WRONLY | O_APPEND);
  if (!f) {
    return;
  }
  vfs_write(f, line, strlen(line));
  if (line[strlen(line) - 1] != '\n') {
    vfs_write(f, "\n", 1);
  }
  vfs_close(f);
}

static void term_wifi_write_last(const char *line) {
  file_t *f;

  if (!line || !line[0]) {
    return;
  }
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  f = vfs_open(TERM_WIFI_LAST_PATH, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return;
  }
  vfs_write(f, line, strlen(line));
  if (line[strlen(line) - 1] != '\n') {
    vfs_write(f, "\n", 1);
  }
  vfs_close(f);
}

static size_t term_wifi_append_text(char *dst, size_t size, size_t used,
                                    const char *text) {
  size_t len;
  size_t copy;

  if (!dst || size == 0 || !text || used >= size - 1) {
    return used;
  }
  len = strlen(text);
  copy = len;
  if (copy > size - used - 1) {
    copy = size - used - 1;
  }
  memcpy(dst + used, text, copy);
  used += copy;
  dst[used] = '\0';
  return used;
}

static const char *term_wifi_validation_hint(const char *stage) {
  if (!stage) {
    return "run wifi status, wifi wpa, wifi data and logs wifi";
  }
  if (strcmp(stage, "join") == 0) {
    return "check wifi bringup, wifi scan poll, selected AP security and WPA traces";
  }
  if (strcmp(stage, "ccmp-ready") == 0) {
    return "check wifi wpa, wifi key pairwise/gtk state, wifi data and protected RX";
  }
  if (strcmp(stage, "dhcp") == 0) {
    return "check AP DHCP server, CCMP data TX/RX, net status and logs network";
  }
  if (strcmp(stage, "dns") == 0) {
    return "check DHCP DNS option, route, gateway reachability and dns command output";
  }
  if (strcmp(stage, "tls") == 0) {
    return "check GitHub reachability, TLS/root trust status and update log";
  }
  if (strcmp(stage, "update") == 0) {
    return "install Orizon to disk before update, then rerun wifi update";
  }
  return "capture logs wifi, net status, wifi wpa and wifi data";
}

static void term_wifi_record_validation(const char *label, const char *ssid,
                                        const char *stage,
                                        const char *result,
                                        const char *detail) {
  char block[4096];
  char line[1024];
  size_t used = 0;
  const wifi_status_t *s = wifi_get_status();

  block[0] = '\0';
  snprintf(line, sizeof(line),
           "wifi-validation: label=\"%s\" result=%s stage=%s ssid=\"%s\"\n",
           label ? label : "wifi", result ? result : "unknown",
           stage ? stage : "unknown", ssid ? ssid : "");
  used = term_wifi_append_text(block, sizeof(block), used, line);

  if (detail && detail[0]) {
    snprintf(line, sizeof(line), "detail: %s\n", detail);
    used = term_wifi_append_text(block, sizeof(block), used, line);
  }

  snprintf(line, sizeof(line),
           "wifi: present=%s driver=%s chipset=%s firmware=%s valid=%s "
           "apm=%s boot=%s alive=%s queues=%s context=%s scheduler=%s "
           "rx=%s scan=%s connect=%s bind=%s txcmd=%s key=%s status=%s\n",
           s->present ? "yes" : "no", s->driver ? s->driver : "unknown",
           s->chipset ? s->chipset : "unknown",
           (s->firmware_present && s->firmware_name) ? s->firmware_name
                                                     : "missing",
           s->firmware_valid ? "yes" : "no",
           s->apm_ready ? "awake" : (s->apm_timeout ? "timeout" : "idle"),
           s->boot_ready ? "ready" : (s->boot_failed ? "failed" : "idle"),
           s->alive_seen ? "seen" : (s->alive_timeout ? "timeout" : "idle"),
           s->queues_ready ? (s->queues_armed ? "armed" : "staged")
                            : (s->queues_failed ? "failed" : "idle"),
           s->context_ready ? (s->context_armed ? "armed" : "staged")
                            : (s->context_failed ? "failed" : "idle"),
           s->scheduler_ready ? (s->scheduler_armed ? "armed" : "staged")
                              : (s->scheduler_failed ? "failed" : "idle"),
           s->rx_path_ready ? "ready"
                            : (s->rx_path_failed ? "failed" : "idle"),
           s->scan_complete_seen
               ? "complete"
               : (s->scan_inflight ? "inflight"
                                    : (s->scan_failed ? "failed" : "idle")),
           s->connect_assoc_confirmed
               ? (s->connect_data_ready ? "data-ready" : "associated")
               : (s->connect_ready
                      ? (s->connect_wpa ? "wpa-plan" : "open-plan")
                      : (s->connect_failed ? "failed" : "idle")),
           s->bind_sta_response_seen
               ? "acked"
               : (s->bind_sent
                      ? "sent"
                      : (s->bind_ready
                             ? "ready"
                             : (s->bind_failed ? "failed" : "idle"))),
           s->txcmd_response_seen
               ? "acked"
               : (s->txcmd_sent
                      ? "sent"
                      : (s->txcmd_failed ? "failed" : "idle")),
           (s->wpa_key_installed && s->wpa_gtk_key_installed)
               ? "installed"
               : (s->wpa_key_failed ? "failed" : "pending"),
           s->status ? s->status : "unknown");
  used = term_wifi_append_text(block, sizeof(block), used, line);

  snprintf(line, sizeof(line),
           "wpa: target=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x "
           "pmk=%s m1=%s m2=%s m3=%s m4=%s pairwise=%s gtk=%s "
           "eapol=%lu key-frames=%lu replay=0x%08x%08x\n",
           s->connect_ssid, s->connect_bssid[0], s->connect_bssid[1],
           s->connect_bssid[2], s->connect_bssid[3],
           s->connect_bssid[4], s->connect_bssid[5],
           s->connect_pmk_ready ? "ready" : "missing",
           s->wpa_m1_seen ? "seen" : "pending",
           s->wpa_m2_tx_acked ? "acked"
                              : (s->wpa_m2_data_ready ? "ready" : "pending"),
           s->wpa_m3_seen ? "seen" : "pending",
           s->wpa_m4_tx_acked ? "acked"
                              : (s->wpa_m4_data_ready ? "ready" : "pending"),
           s->wpa_key_installed ? "installed" : "pending",
           s->wpa_gtk_key_installed ? "installed" : "pending",
           s->wpa_eapol_packets, s->wpa_key_frames,
           s->wpa_replay_counter_hi, s->wpa_replay_counter_lo);
  used = term_wifi_append_text(block, sizeof(block), used, line);

  snprintf(line, sizeof(line),
           "ccmp: data=%s tx=%s rx-ready=%s rx-packets=%u "
           "rx-decrypt-failed=%s eth=0x%04x checksum=0x%08x\n",
           s->ccmp_data_ready ? "ready" : "pending",
           s->ccmp_tx_acked ? "acked" : "pending",
           s->ccmp_rx_ready ? "yes" : "no", s->ccmp_rx_packets,
           s->ccmp_rx_decrypt_failed ? "yes" : "no",
           s->ccmp_rx_eth_type, s->ccmp_rx_checksum);
  used = term_wifi_append_text(block, sizeof(block), used, line);

  netstack_format_status(line, sizeof(line));
  used = term_wifi_append_text(block, sizeof(block), used, "net: ");
  used = term_wifi_append_text(block, sizeof(block), used, line);
  used = term_wifi_append_text(block, sizeof(block), used, "\n");
  netstack_format_route(line, sizeof(line));
  used = term_wifi_append_text(block, sizeof(block), used, "route: ");
  used = term_wifi_append_text(block, sizeof(block), used, line);
  used = term_wifi_append_text(block, sizeof(block), used, "\n");
  netstack_format_dns(line, sizeof(line));
  used = term_wifi_append_text(block, sizeof(block), used, "dns: ");
  used = term_wifi_append_text(block, sizeof(block), used, line);
  used = term_wifi_append_text(block, sizeof(block), used, "\n");

  snprintf(line, sizeof(line), "hint: %s\n",
           term_wifi_validation_hint(stage));
  used = term_wifi_append_text(block, sizeof(block), used, line);

  term_wifi_log_line(block);
  term_wifi_write_last(block);
}

static void term_run_update(terminal_t *term, const char *cmd) {
  static char report[8192];
  const char *args =
      term_skip_spaces(cmd + (term_command_is(cmd, "orizon-update") ? 13 : 6));

  if (term_command_is(args, "status")) {
    orizon_update_format_status(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }

  if (!term_install_already_complete()) {
    term_puts_t(term,
                "update: unavailable in live boot. Install Orizon OS first. Use 'update status' for diagnostics.\n");
    return;
  }
  term_puts_t(term, "\033[1;36mStarting Orizon update...\033[0m\n");
  term_render(term);
  fb_swap_buffers();
  orizon_update_set_progress(term_update_progress, term);
  orizon_update_full_upgrade(report, sizeof(report));
  orizon_update_set_progress(NULL, NULL);
  if (report[0] == '\0') {
    term_puts_t(term, "update: no output produced\n");
  }
}

static void term_run_disk(terminal_t *term, const char *cmd) {
  static char report[4096];
  const char *args = term_skip_spaces(cmd + 4);

  if (*args == '\0' || term_command_is(args, "identify") ||
      term_command_is(args, "id")) {
    storage_format_identify(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }
  if (term_command_is(args, "read-test") || term_command_is(args, "read")) {
    const char *value = term_skip_spaces(args + (term_command_is(args, "read") ? 4 : 9));
    uint64_t lba = 0;
    if (term_command_is(value, "last") || term_command_is(value, "end")) {
      uint64_t sectors = storage_sector_count();
      lba = sectors > 0 ? sectors - 1 : 0;
    } else if (*value && term_parse_uint64_allow_zero(value, &lba) < 0) {
      term_puts_t(term, "usage: disk read-test [lba|last]\n");
      return;
    }
    storage_read_test(lba, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  term_puts_t(term, "usage: disk identify | disk read-test [lba|last]\n");
}

static void term_run_selftest(terminal_t *term, const char *cmd) {
  static char report[8192];
  const char *args = term_skip_spaces(cmd + 8);

  orizon_selftest_format(args, report, sizeof(report));
  term_puts_t(term, report);
  if (report[0] && report[strlen(report) - 1] != '\n') {
    term_puts_t(term, "\n");
  }
}

static void term_run_rollback(terminal_t *term) {
  static char report[4096];

  if (!term_install_already_complete()) {
    term_puts_t(term,
                "rollback: unavailable in live boot. Install Orizon OS first.\n");
    return;
  }
  orizon_update_rollback(report, sizeof(report));
  term_puts_t(term, report);
}

static void term_run_bootguard(terminal_t *term, const char *cmd) {
  static char report[4096];
  const char *args = term_skip_spaces(cmd + 9);

  if (!term_install_already_complete()) {
    term_puts_t(term,
                "bootguard: unavailable in live boot. Install Orizon OS first.\n");
    return;
  }
  if (term_command_is(args, "confirm") ||
      term_command_is(args, "validate")) {
    orizon_update_boot_guard_confirm(report, sizeof(report));
  } else if (term_command_is(args, "recover") ||
             term_command_is(args, "rollback")) {
    orizon_update_boot_guard_recover(report, sizeof(report));
  } else {
    orizon_update_boot_guard_status(report, sizeof(report));
  }
  term_puts_t(term, report);
  if (report[0] && report[strlen(report) - 1] != '\n') {
    term_puts_t(term, "\n");
  }
}

static void term_pkg_help(terminal_t *term) {
  term_puts_t(term, "\033[1;36mOrizon packages\033[0m\n");
  term_puts_t(term, "  pkg list          - List installed packages\n");
  term_puts_t(term, "  pkg status        - Show package manager state\n");
  term_puts_t(term, "  pkg audit         - Audit package db/cache consistency\n");
  term_puts_t(term, "  pkg doctor        - Diagnose package v5 safety state\n");
  term_puts_t(term, "  pkg cache         - Show package cache details\n");
  term_puts_t(term, "  pkg search <query> - Search builtin/installed/remote packages\n");
  term_puts_t(term, "  pkg remote        - Show cached signed remote package index\n");
  term_puts_t(term, "  pkg remote verify - Validate cached remote package index\n");
  term_puts_t(term, "  pkg upgrade plan  - Show signed package upgrade plan\n");
  term_puts_t(term, "  pkg info <name>   - Show package metadata/files\n");
  term_puts_t(term, "  pkg history       - Show package install/remove history\n");
  term_puts_t(term,
              "  pkg sample [desktop|desktop-module] - Create a sample .opkg package\n");
  term_puts_t(term, "  pkg hash <file>   - Print package payload sha256\n");
  term_puts_t(term, "  pkg verify <file> - Verify package hash/dependencies\n");
  term_puts_t(term, "  pkg simulate <file> - Dry-run install/upgrade without writes\n");
  if (term_install_already_complete()) {
    term_puts_t(term, "  pkg update        - Run signed update package refresh\n");
    term_puts_t(term, "  pkg upgrade       - Plan then run signed package refresh\n");
    term_puts_t(term,
                "  pkg install <file|desktop-package> - Install a verified package\n");
    term_puts_t(term, "  pkg remove <name> - Remove an installed package\n");
    term_puts_t(term, "  pkg rollback <name> - Restore last removed package snapshot\n");
  } else {
    term_puts_t(term,
                "  pkg update        - Available after disk install only\n");
    term_puts_t(term,
                "  pkg upgrade       - Available after disk install only\n");
    term_puts_t(term,
                "  pkg install <file|desktop-package> - Available after disk install only\n");
    term_puts_t(term,
                "  pkg remove <name> - Available after disk install only\n");
    term_puts_t(term,
                "  pkg rollback <name> - Available after disk install only\n");
  }
}

static int term_pkg_desktop_name(const char *name) {
  return term_command_is(name, ORIZON_DESKTOP_PACKAGE) ||
         term_command_is(name, "desktop") || term_command_is(name, "hypr") ||
         term_command_is(name, "hyprland");
}

static int term_pkg_desktop_module_name(const char *name) {
  return term_command_is(name, ORIZON_DESKTOP_PACKAGE_CORE) ||
         term_command_is(name, ORIZON_DESKTOP_PACKAGE_TERMINAL) ||
         term_command_is(name, ORIZON_DESKTOP_PACKAGE_SETTINGS) ||
         term_command_is(name, ORIZON_DESKTOP_PACKAGE_LAUNCHER) ||
         term_command_is(name, ORIZON_DESKTOP_PACKAGE_WAYBAR);
}

static void term_run_pkg(terminal_t *term, const char *cmd) {
  static char report[8192];
  const char *args = term_skip_spaces(cmd + 3);

  if (*args == '\0' || term_command_is(args, "help")) {
    term_pkg_help(term);
    return;
  }

  if (term_command_is(args, "list")) {
    if (orizon_pkg_list(report, sizeof(report)) == 0) {
      term_puts_t(term, report);
      if (report[0] && report[strlen(report) - 1] != '\n') {
        term_puts_t(term, "\n");
      }
    } else {
      term_puts_t(term, "pkg list: database unavailable\n");
    }
    return;
  }

  if (term_command_is(args, "status")) {
    orizon_pkg_status(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "audit")) {
    orizon_pkg_audit(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "doctor")) {
    orizon_pkg_doctor(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "cache")) {
    orizon_pkg_cache(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "search")) {
    const char *query = term_skip_spaces(args + 6);
    orizon_pkg_search(query, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "remote")) {
    const char *remote_args = term_skip_spaces(args + 6);
    if (term_command_is(remote_args, "verify") ||
        term_command_is(remote_args, "check")) {
      orizon_pkg_remote_verify(report, sizeof(report));
    } else {
      orizon_pkg_remote(report, sizeof(report));
    }
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "upgrade")) {
    const char *upgrade_args = term_skip_spaces(args + 7);
    if (term_command_is(upgrade_args, "plan") ||
        term_command_is(upgrade_args, "dry-run") ||
        term_command_is(upgrade_args, "check")) {
      orizon_pkg_upgrade_plan(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (!term_install_already_complete()) {
      term_puts_t(term,
                  "pkg upgrade: unavailable in live boot. Install Orizon OS first.\n");
      term_puts_t(term,
                  "hint: use 'pkg upgrade plan' to inspect the cached signed index.\n");
      return;
    }
    orizon_pkg_upgrade_plan(report, sizeof(report));
    term_puts_t(term, report);
    term_puts_t(term,
                "pkg upgrade: running signed system manifest/package refresh\n");
    orizon_update_full_upgrade(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "info")) {
    const char *name = term_skip_spaces(args + 4);
    if (*name == '\0') {
      term_puts_t(term, "usage: pkg info <name>\n");
      return;
    }
    orizon_pkg_info(name, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "history")) {
    orizon_pkg_history(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "sample")) {
    const char *sample_args = term_skip_spaces(args + 6);
    if (term_command_is(sample_args, "desktop") ||
        term_command_is(sample_args, "orizon-desktop-hypr")) {
      orizon_pkg_write_desktop_sample(report, sizeof(report));
    } else if (term_pkg_desktop_module_name(sample_args)) {
      orizon_pkg_write_desktop_module_sample(sample_args, report,
                                             sizeof(report));
    } else {
      orizon_pkg_write_sample(report, sizeof(report));
    }
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "hash")) {
    char path[MAX_PATH];
    const char *requested = term_skip_spaces(args + 4);
    if (*requested == '\0') {
      term_puts_t(term, "usage: pkg hash <file>\n");
      return;
    }
    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "pkg hash: invalid path\n");
      return;
    }
    orizon_pkg_hash_file(path, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "verify")) {
    char path[MAX_PATH];
    const char *requested = term_skip_spaces(args + 6);
    if (*requested == '\0') {
      term_puts_t(term, "usage: pkg verify <file>\n");
      return;
    }
    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "pkg verify: invalid path\n");
      return;
    }
    orizon_pkg_verify_file(path, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "simulate") ||
      term_command_is(args, "dry-run")) {
    char path[MAX_PATH];
    const char *requested =
        term_skip_spaces(args + (term_command_is(args, "dry-run") ? 7 : 8));
    if (*requested == '\0') {
      term_puts_t(term, "usage: pkg simulate <file>\n");
      return;
    }
    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "pkg simulate: invalid path\n");
      return;
    }
    orizon_pkg_simulate_file(path, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "update")) {
    if (!term_install_already_complete()) {
      term_puts_t(term,
                  "pkg update: unavailable in live boot. Install Orizon OS first.\n");
      return;
    }
    term_puts_t(term,
                "pkg update: using signed system manifest/package index via update\n");
    orizon_update_full_upgrade(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "install")) {
    char path[MAX_PATH];
    const char *requested = term_skip_spaces(args + 7);
    if (!term_install_already_complete()) {
      term_puts_t(term,
                  "pkg install: unavailable in live boot. Install Orizon OS first.\n");
      return;
    }
    if (*requested == '\0') {
      term_puts_t(term, "usage: pkg install <file|desktop-package>\n");
      return;
    }
    if (term_pkg_desktop_name(requested) ||
        term_pkg_desktop_module_name(requested)) {
      orizon_pkg_install_named(requested, report, sizeof(report));
      gui_desktop_set_enabled(orizon_desktop_is_enabled());
      term_puts_t(term, report);
      return;
    }
    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "pkg install: invalid path\n");
      return;
    }
    orizon_pkg_install_file(path, report, sizeof(report));
    gui_desktop_set_enabled(orizon_desktop_is_enabled());
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "remove")) {
    const char *name = term_skip_spaces(args + 6);
    if (!term_install_already_complete()) {
      term_puts_t(term,
                  "pkg remove: unavailable in live boot. Install Orizon OS first.\n");
      return;
    }
    if (*name == '\0') {
      term_puts_t(term, "usage: pkg remove <name>\n");
      return;
    }
    orizon_pkg_remove(term_pkg_desktop_name(name) ? ORIZON_DESKTOP_PACKAGE
                                                  : name,
                      report, sizeof(report));
    gui_desktop_set_enabled(orizon_desktop_is_enabled());
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "rollback")) {
    const char *name = term_skip_spaces(args + 8);
    if (!term_install_already_complete()) {
      term_puts_t(term,
                  "pkg rollback: unavailable in live boot. Install Orizon OS first.\n");
      return;
    }
    if (*name == '\0') {
      term_puts_t(term, "usage: pkg rollback <name>\n");
      return;
    }
    orizon_pkg_rollback(term_pkg_desktop_name(name) ? ORIZON_DESKTOP_PACKAGE
                                                    : name,
                        report, sizeof(report));
    gui_desktop_set_enabled(orizon_desktop_is_enabled());
    term_puts_t(term, report);
    return;
  }

  term_puts_t(term, "pkg: unknown command. Try 'pkg help'.\n");
}

static void term_run_desktop(terminal_t *term, const char *cmd) {
  static char report[4096];
  const char *args = term_skip_spaces(cmd + 7);

  if (*args == '\0' || term_command_is(args, "status")) {
    gui_desktop_format_status(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "help")) {
    term_puts_t(term, "\033[1;36mOrizon desktop\033[0m\n");
    term_puts_t(term, "  desktop status          - Show desktop/session state\n");
    term_puts_t(term, "  desktop config          - Show Hyprland-style config template\n");
    term_puts_t(term, "  desktop config doctor   - Validate Hyprland-style user config\n");
    term_puts_t(term, "  desktop config apply    - Apply supported config keys to session/settings\n");
    term_puts_t(term, "  desktop config trace    - Trace apply/prepare/ignore decisions per config line\n");
    term_puts_t(term, "  desktop start|stop      - Start/stop Hyprland-style session manager\n");
    term_puts_t(term, "  desktop restart|reload  - Restart/reload session manager/config\n");
    term_puts_t(term, "  desktop recover         - Repair/recover desktop session state\n");
    term_puts_t(term, "  desktop rescue          - Read-only session recovery checklist\n");
    term_puts_t(term, "  desktop state           - Show session manager state/log\n");
    term_puts_t(term, "  desktop enable          - Enable optional desktop profile\n");
    term_puts_t(term, "  desktop disable         - Disable desktop profile\n");
    term_puts_t(term, "  desktop doctor          - Check desktop install/config state\n");
    term_puts_t(term, "  desktop logs            - Show desktop events\n");
    term_puts_t(term, "  desktop shortcuts       - Show keys and commands\n");
    term_puts_t(term, "  desktop keymap          - Show VM keyboard/submap runtime\n");
    term_puts_t(term, "  desktop session         - Show session theme/wallpaper/layout\n");
    term_puts_t(term, "  desktop settings        - Show system-wide desktop settings\n");
    term_puts_t(term, "  desktop settings paths  - Show /system and ~/.config/hypr settings hub\n");
    term_puts_t(term, "  desktop settings export - Write Hyprland-style user config from /system\n");
    term_puts_t(term, "  desktop settings sync   - Export settings and refresh runtime hints\n");
    term_puts_t(term, "  desktop settings set <key> <value> - Update /system/desktop-settings.conf\n");
    term_puts_t(term, "  desktop settings preset <name> - Apply compact/cozy/performance settings\n");
    term_puts_t(term, "  desktop settings doctor - Validate system-wide desktop settings\n");
    term_puts_t(term, "  desktop input           - Show/set keyboard, pointer and focus policy\n");
    term_puts_t(term, "  desktop pointer         - Show cursor and HID mouse/tablet diagnostics\n");
    term_puts_t(term, "  desktop devices         - Show Hyprland-style input device summary\n");
    term_puts_t(term, "  desktop version         - Show desktop compatibility facade version\n");
    term_puts_t(term, "  desktop systeminfo      - Show compositor/backend/session summary\n");
    term_puts_t(term, "  desktop backend         - Show current framebuffer backend and future split\n");
    term_puts_t(term, "  desktop protocol        - Show internal client/compositor protocol map\n");
    term_puts_t(term, "  desktop layouts         - Show available tiling layouts\n");
    term_puts_t(term, "  desktop layout-tree     - Show active workspace tiling tree/rectangles\n");
    term_puts_t(term, "  desktop animations      - Show animation/runtime transition state\n");
    term_puts_t(term, "  desktop decorations     - Show border/shadow/rounding state\n");
    term_puts_t(term, "  desktop render          - Show render/focus/transition diagnostics\n");
    term_puts_t(term, "  desktop descriptions    - Show hyprctl/dispatcher command descriptions\n");
    term_puts_t(term, "  desktop instances       - Show compositor instance summary\n");
    term_puts_t(term, "  desktop submap          - Show active Hyprland-style submap\n");
    term_puts_t(term, "  desktop configerrors    - Show Hyprland-style config parser errors\n");
    term_puts_t(term, "  desktop rollinglog      - Show desktop event log as hyprctl rollinglog\n");
    term_puts_t(term, "  desktop focus-history   - Show Hyprland-style focusHistoryID order\n");
    term_puts_t(term, "  desktop workspace-stack - Show master/stack/focus order per workspace\n");
    term_puts_t(term, "  desktop client-model    - Show client/workspace/focus state graph\n");
    term_puts_t(term, "  desktop rule-matches    - Show windowrulev2 matches against tiled clients\n");
    term_puts_t(term, "  desktop modules         - Show prepared modular desktop packages\n");
    term_puts_t(term, "  desktop apps            - List compositor-managed desktop apps\n");
    term_puts_t(term, "  desktop app <id>        - Show app class/module/surface details\n");
    term_puts_t(term, "  desktop profiles        - List themes/wallpapers/layouts\n");
    term_puts_t(term, "  desktop preset <name>   - Apply graphite/moss/ember/frost/focus preset\n");
    term_puts_t(term, "  desktop binds           - Show Hyprland-style binds/dispatchers\n");
    term_puts_t(term, "  desktop rules           - Show Hyprland-style window rules runtime\n");
    term_puts_t(term, "  desktop monitors        - Show Hyprland-style monitor hints\n");
    term_puts_t(term, "  desktop runtime         - Show generated Hyprland-style runtime files\n");
    term_puts_t(term, "  desktop layers          - Show compositor layer model\n");
    term_puts_t(term, "  desktop layout-state    - Show per-workspace tiling layout state\n");
    term_puts_t(term, "  desktop keyword <k> <v> - Apply one Hyprland-style runtime keyword\n");
    term_puts_t(term, "  desktop dispatch <d>    - Run exec/workspace/layoutmsg/master/swapwindow/submap\n");
    term_puts_t(term, "  desktop hyprctl <cmd>   - Hyprland-like status/keyword/dispatch facade\n");
    term_puts_t(term, "  desktop autostart       - Show or configure startup apps\n");
    term_puts_t(term, "  desktop windows         - List compositor windows/layers\n");
    term_puts_t(term, "  desktop theme <name>    - Set session theme\n");
    term_puts_t(term, "  desktop wallpaper <name> - Set symbolic wallpaper\n");
    term_puts_t(term, "  desktop layout <name>   - Set dwindle/master/monocle layout\n");
    term_puts_t(term, "  desktop focus on|off|toggle - Configure focus-follows-mouse\n");
    term_puts_t(term, "  desktop bar on|off|toggle - Configure desktop bar\n");
    term_puts_t(term, "  desktop launcher [show|hide|toggle] - Control launcher\n");
    term_puts_t(term, "  desktop launch <app>    - Spawn terminal/settings/logs/packages/update/launcher\n");
    term_puts_t(term, "  desktop killactive      - Close focused tiled client\n");
    term_puts_t(term, "  desktop focus-window next|prev|<target> - Focus by id/address/class/title\n");
    term_puts_t(term, "  desktop workspace [n|name:name|next|empty|previous] - Show or switch workspace\n");
    term_puts_t(term, "  desktop dispatch renameworkspace <target> <name> - Rename a workspace\n");
    term_puts_t(term, "  desktop dispatch movetoworkspace <target>[,<window>] - Move focused/selected client and follow\n");
    term_puts_t(term, "  desktop dispatch movetoworkspacesilent <target>[,<window>] - Move focused/selected client without switching\n");
    term_puts_t(term, "  desktop dispatch fullscreen|fullscreenstate|pseudo|pseudotile|pin [state] - Hyprland-like client state\n");
    term_puts_t(term, "  desktop dispatch focuscurrentorlast|focusurgentorlast|markurgent|tagwindow - Focus history, urgent and tag diagnostics\n");
    term_puts_t(term, "  desktop dispatch cyclenext|swapnext|swapwindow|focusmaster|swapwithmaster - Hyprland-like actions\n");
    term_puts_t(term, "  desktop dispatch movefocus <l|r|u|d|next|prev> - Directional tiled focus\n");
    term_puts_t(term, "  desktop dispatch focuswindow <id|0xaddr|class:app|title:text|tag:name|activewindow> - Focus a matching client\n");
    term_puts_t(term, "  desktop dispatch layoutmsg <msg> - orientation/splitratio/masterratio/nmaster actions\n");
    term_puts_t(term, "  desktop dispatch resizeactive <x> <y> - Keyboard tiling ratio resize\n");
    term_puts_t(term, "  desktop dispatch submap <name|reset> - Set active submap\n");
    term_puts_t(term, "  desktop reset           - Disable and restore default policy\n");
    term_puts_t(term, "  desktop write-config    - Rewrite Hypr-style user config\n");
    term_puts_t(term, "  desktop open terminal   - Compat alias for dispatch exec terminal\n");
    term_puts_t(term, "  desktop close terminal  - Compat alias for killactive\n");
    term_puts_t(term, "  desktop package         - Write installable desktop .opkg\n");
    term_puts_t(term, "Shortcuts: F1 exec terminal, F2 killactive, F3 launcher, F9/F10/F11 submaps.\n");
    return;
  }
  if (term_command_is(args, "config")) {
    const char *config_args = term_skip_spaces(args + 6);
    if (*config_args == '\0' || term_command_is(config_args, "show") ||
        term_command_is(config_args, "template")) {
      orizon_desktop_format_config(report, sizeof(report));
    } else if (term_command_is(config_args, "doctor") ||
               term_command_is(config_args, "check") ||
               term_command_is(config_args, "validate")) {
      orizon_desktop_format_config_doctor(report, sizeof(report));
    } else if (term_command_is(config_args, "apply") ||
               term_command_is(config_args, "import") ||
               term_command_is(config_args, "reload")) {
      orizon_desktop_apply_hypr_config(report, sizeof(report));
      gui_desktop_reload_session();
    } else if (term_command_is(config_args, "trace") ||
               term_command_is(config_args, "explain") ||
               term_command_is(config_args, "why")) {
      orizon_desktop_format_config_trace(report, sizeof(report));
    } else {
      snprintf(report, sizeof(report),
               "usage: desktop config [show|doctor|apply|trace]\n");
    }
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "doctor") || term_command_is(args, "check")) {
    orizon_desktop_format_doctor(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "start") || term_command_is(args, "stop") ||
      term_command_is(args, "restart") || term_command_is(args, "reload") ||
      term_command_is(args, "recover")) {
    char action[16];
    size_t len = 0;
    while (args[len] && args[len] != ' ' && len + 1 < sizeof(action)) {
      action[len] = args[len];
      len++;
    }
    action[len] = '\0';
    orizon_desktop_session_manager(action, report, sizeof(report));
    if (term_command_is(args, "reload")) {
      gui_desktop_reload_session();
    } else {
      gui_desktop_set_enabled(orizon_desktop_is_enabled());
    }
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "rescue") ||
      term_command_is(args, "session-rescue")) {
    orizon_desktop_format_session_rescue(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "state") ||
      term_command_is(args, "session-state") ||
      term_command_is(args, "manager")) {
    orizon_desktop_format_session_state(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "logs") || term_command_is(args, "log")) {
    orizon_desktop_format_log(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "shortcuts") || term_command_is(args, "keys")) {
    orizon_desktop_format_shortcuts(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "keymap") || term_command_is(args, "inputmap") ||
      term_command_is(args, "ergonomics")) {
    gui_desktop_format_keymap(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "settings")) {
    const char *settings_args = term_skip_spaces(args + 8);
    if (*settings_args == '\0' || term_command_is(settings_args, "show") ||
        term_command_is(settings_args, "status")) {
      orizon_desktop_format_settings(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "repair") ||
        term_command_is(settings_args, "defaults") ||
        term_command_is(settings_args, "reset")) {
      orizon_desktop_repair_settings(report, sizeof(report));
      gui_desktop_reload_session();
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "presets") ||
        term_command_is(settings_args, "profiles")) {
      orizon_desktop_format_settings_presets(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "doctor") ||
        term_command_is(settings_args, "check")) {
      orizon_desktop_format_settings_doctor(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "paths") ||
        term_command_is(settings_args, "path") ||
        term_command_is(settings_args, "hub")) {
      orizon_desktop_format_settings_paths(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "export") ||
        term_command_is(settings_args, "write") ||
        term_command_is(settings_args, "generate")) {
      orizon_desktop_export_settings(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "sync") ||
        term_command_is(settings_args, "apply") ||
        term_command_is(settings_args, "reload")) {
      orizon_desktop_sync_settings(report, sizeof(report));
      gui_desktop_reload_session();
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "preset") ||
        term_command_is(settings_args, "profile")) {
      const char *preset = term_skip_spaces(
          settings_args + (term_command_is(settings_args, "preset") ? 6 : 7));
      if (*preset == '\0') {
        term_puts_t(term,
                    "usage: desktop settings preset <default|compact|cozy|performance|accessibility|locked>\n");
        return;
      }
      orizon_desktop_apply_settings_preset(preset, report, sizeof(report));
      gui_desktop_reload_session();
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(settings_args, "set")) {
      const char *key = term_skip_spaces(settings_args + 3);
      const char *value = key;
      char key_buf[48];
      size_t key_len;
      while (*value && *value != ' ') {
        value++;
      }
      key_len = (size_t)(value - key);
      if (*value == ' ') {
        value = term_skip_spaces(value);
      }
      if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
        term_puts_t(term,
                    "usage: desktop settings set <key> <value>\n");
        return;
      }
      memcpy(key_buf, key, key_len);
      key_buf[key_len] = '\0';
      orizon_desktop_set_setting(key_buf, value, report, sizeof(report));
      gui_desktop_reload_session();
      term_puts_t(term, report);
      return;
    }
    term_puts_t(term,
                "usage: desktop settings [show|paths|export|sync|doctor|presets|preset <name>|repair|set <key> <value>]\n");
    return;
  }
  if (term_command_is(args, "session")) {
    orizon_desktop_format_session(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "input")) {
    const char *input_args = term_skip_spaces(args + 5);
    const char *value = input_args;
    char key_buf[48];
    size_t key_len;
    if (*input_args == '\0' || term_command_is(input_args, "show") ||
        term_command_is(input_args, "status")) {
      orizon_desktop_format_input(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(input_args, "submap")) {
      value = term_skip_spaces(input_args + 6);
      if (*value == '\0') {
        gui_desktop_format_submap(report, sizeof(report));
      } else {
        gui_desktop_dispatch("submap", value, report, sizeof(report));
      }
      term_puts_t(term, report);
      return;
    }
    while (*value && *value != ' ') {
      value++;
    }
    key_len = (size_t)(value - input_args);
    if (*value == ' ') {
      value = term_skip_spaces(value);
    }
    if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
      term_puts_t(term,
                  "usage: desktop input [layout <fr|us>|pointer <flat|natural|precise|accelerated>|focus <on|off|toggle>|submap <name|reset>]\n");
      return;
    }
    memcpy(key_buf, input_args, key_len);
    key_buf[key_len] = '\0';
    orizon_desktop_set_input(key_buf, value, report, sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "pointer") || term_command_is(args, "cursor") ||
      term_command_is(args, "mouse")) {
    gui_desktop_format_pointer(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "devices") || term_command_is(args, "device")) {
    gui_desktop_format_devices(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "version") || term_command_is(args, "about")) {
    gui_desktop_format_hyprctl_version(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "systeminfo") ||
      term_command_is(args, "system-info")) {
    gui_desktop_format_systeminfo(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "backend") ||
      term_command_is(args, "backend-info")) {
    orizon_desktop_format_backend(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "protocol") ||
      term_command_is(args, "protocols")) {
    orizon_desktop_format_protocol(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "layouts")) {
    gui_desktop_format_layouts(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "layout-state") ||
      term_command_is(args, "layoutstate") ||
      term_command_is(args, "workspace-layouts")) {
    gui_desktop_format_layout_state(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "layout-tree") ||
      term_command_is(args, "layouttree") ||
      term_command_is(args, "tree")) {
    gui_desktop_format_layout_tree(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "animations")) {
    gui_desktop_format_animations(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "decorations") ||
      term_command_is(args, "decoration")) {
    gui_desktop_format_decorations(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "render") || term_command_is(args, "rendering") ||
      term_command_is(args, "renderdiag")) {
    gui_desktop_format_render(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "descriptions") ||
      term_command_is(args, "description")) {
    gui_desktop_format_descriptions(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "instances") ||
      term_command_is(args, "instance")) {
    gui_desktop_format_instances(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "submap")) {
    const char *value = term_skip_spaces(args + 6);
    if (*value == '\0' || term_command_is(value, "show") ||
        term_command_is(value, "status")) {
      gui_desktop_format_submap(report, sizeof(report));
    } else {
      gui_desktop_dispatch("submap", value, report, sizeof(report));
    }
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "configerrors") ||
      term_command_is(args, "config-errors")) {
    orizon_desktop_format_config_errors(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "rollinglog") ||
      term_command_is(args, "rolling-log")) {
    orizon_desktop_format_rolling_log(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "focus-history") ||
      term_command_is(args, "focushistory")) {
    gui_desktop_format_focus_history(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "workspace-stack") ||
      term_command_is(args, "workspacestack") ||
      term_command_is(args, "stack")) {
    gui_desktop_format_workspace_stack(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "apps") || term_command_is(args, "launcher apps")) {
    const char *app = term_command_is(args, "apps")
                          ? term_skip_spaces(args + 4)
                          : term_skip_spaces(args + strlen("launcher apps"));
    if (*app) {
      orizon_desktop_format_app_detail(app, report, sizeof(report));
    } else {
      orizon_desktop_format_apps(report, sizeof(report));
    }
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "app")) {
    const char *app = term_skip_spaces(args + 3);
    if (*app == '\0') {
      term_puts_t(term,
                  "usage: desktop app <terminal|settings|logs|packages|update|launcher>\n");
      return;
    }
    orizon_desktop_format_app_detail(app, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "modules") ||
      term_command_is(args, "packages") ||
      term_command_is(args, "package modules")) {
    orizon_desktop_format_modules(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "binds") || term_command_is(args, "bind")) {
    gui_desktop_format_binds(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "rules") || term_command_is(args, "rule")) {
    orizon_desktop_format_rules(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "monitors") || term_command_is(args, "monitor")) {
    orizon_desktop_format_monitor_hints(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "runtime") || term_command_is(args, "state")) {
    orizon_desktop_format_runtime(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "layers") || term_command_is(args, "layer")) {
    gui_desktop_format_layers(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "keyword")) {
    const char *key = term_skip_spaces(args + 7);
    const char *value = key;
    char key_buf[96];
    size_t key_len;
    while (*value && *value != ' ') {
      value++;
    }
    key_len = (size_t)(value - key);
    if (*value == ' ') {
      value = term_skip_spaces(value);
    }
    if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
      term_puts_t(term, "usage: desktop keyword <hypr-key> <value>\n");
      return;
    }
    memcpy(key_buf, key, key_len);
    key_buf[key_len] = '\0';
    orizon_desktop_apply_hypr_keyword(key_buf, value, report, sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "hyprctl")) {
    const char *hypr = term_skip_spaces(args + 7);
    if (*hypr == '\0' || term_command_is(hypr, "help")) {
      term_puts_t(term,
                  "usage: desktop hyprctl version|systeminfo|backend|protocol|clients|clientmodel|rulematches|workspaces|activeworkspace|activewindow|focushistory|workspacestack|monitors|binds|keymap|layers|layouts|layoutstate|layouttree|animations|decorations|render|descriptions|instances|submap|devices|cursorpos|splash|configerrors|configtrace|rollinglog|getoption <k>|keyword <k> <v>|dispatch <d> [args]|reload\n");
      return;
    }
    if (term_command_is(hypr, "version")) {
      gui_desktop_format_hyprctl_version(report, sizeof(report));
    } else if (term_command_is(hypr, "systeminfo")) {
      gui_desktop_format_systeminfo(report, sizeof(report));
    } else if (term_command_is(hypr, "backend") ||
               term_command_is(hypr, "backend-info")) {
      orizon_desktop_format_backend(report, sizeof(report));
    } else if (term_command_is(hypr, "protocol") ||
               term_command_is(hypr, "protocols")) {
      orizon_desktop_format_protocol(report, sizeof(report));
    } else if (term_command_is(hypr, "clients")) {
      gui_desktop_format_windows(report, sizeof(report));
    } else if (term_command_is(hypr, "clientmodel") ||
               term_command_is(hypr, "client-model") ||
               term_command_is(hypr, "clientmap") ||
               term_command_is(hypr, "client-map")) {
      gui_desktop_format_client_model(report, sizeof(report));
    } else if (term_command_is(hypr, "rulematches") ||
               term_command_is(hypr, "rule-matches") ||
               term_command_is(hypr, "windowrules") ||
               term_command_is(hypr, "window-rules")) {
      gui_desktop_format_rule_matches(report, sizeof(report));
    } else if (term_command_is(hypr, "workspaces")) {
      gui_desktop_format_workspaces(report, sizeof(report));
    } else if (term_command_is(hypr, "activeworkspace")) {
      gui_desktop_format_activeworkspace(report, sizeof(report));
    } else if (term_command_is(hypr, "activewindow")) {
      gui_desktop_format_activewindow(report, sizeof(report));
    } else if (term_command_is(hypr, "focushistory") ||
               term_command_is(hypr, "focus-history")) {
      gui_desktop_format_focus_history(report, sizeof(report));
    } else if (term_command_is(hypr, "workspacestack") ||
               term_command_is(hypr, "workspace-stack") ||
               term_command_is(hypr, "stack")) {
      gui_desktop_format_workspace_stack(report, sizeof(report));
    } else if (term_command_is(hypr, "monitors")) {
      gui_desktop_format_monitors(report, sizeof(report));
    } else if (term_command_is(hypr, "binds")) {
      gui_desktop_format_binds(report, sizeof(report));
    } else if (term_command_is(hypr, "keymap") ||
               term_command_is(hypr, "inputmap")) {
      gui_desktop_format_keymap(report, sizeof(report));
    } else if (term_command_is(hypr, "layers")) {
      gui_desktop_format_layers(report, sizeof(report));
    } else if (term_command_is(hypr, "layouts")) {
      gui_desktop_format_layouts(report, sizeof(report));
    } else if (term_command_is(hypr, "layoutstate") ||
               term_command_is(hypr, "layout-state") ||
               term_command_is(hypr, "workspacelayouts")) {
      gui_desktop_format_layout_state(report, sizeof(report));
    } else if (term_command_is(hypr, "layouttree") ||
               term_command_is(hypr, "layout-tree") ||
               term_command_is(hypr, "tree")) {
      gui_desktop_format_layout_tree(report, sizeof(report));
    } else if (term_command_is(hypr, "animations")) {
      gui_desktop_format_animations(report, sizeof(report));
    } else if (term_command_is(hypr, "decorations")) {
      gui_desktop_format_decorations(report, sizeof(report));
    } else if (term_command_is(hypr, "render") ||
               term_command_is(hypr, "rendering")) {
      gui_desktop_format_render(report, sizeof(report));
    } else if (term_command_is(hypr, "descriptions")) {
      gui_desktop_format_descriptions(report, sizeof(report));
    } else if (term_command_is(hypr, "instances")) {
      gui_desktop_format_instances(report, sizeof(report));
    } else if (term_command_is(hypr, "submap")) {
      const char *value = term_skip_spaces(hypr + 6);
      if (*value == '\0' || term_command_is(value, "show") ||
          term_command_is(value, "status")) {
        gui_desktop_format_submap(report, sizeof(report));
      } else {
        gui_desktop_dispatch("submap", value, report, sizeof(report));
      }
    } else if (term_command_is(hypr, "devices")) {
      gui_desktop_format_devices(report, sizeof(report));
    } else if (term_command_is(hypr, "cursorpos")) {
      gui_desktop_format_cursorpos(report, sizeof(report));
    } else if (term_command_is(hypr, "splash")) {
      gui_desktop_format_splash(report, sizeof(report));
    } else if (term_command_is(hypr, "configerrors")) {
      orizon_desktop_format_config_errors(report, sizeof(report));
    } else if (term_command_is(hypr, "configtrace") ||
               term_command_is(hypr, "config-trace")) {
      orizon_desktop_format_config_trace(report, sizeof(report));
    } else if (term_command_is(hypr, "rollinglog")) {
      orizon_desktop_format_rolling_log(report, sizeof(report));
    } else if (term_command_is(hypr, "getoption")) {
      const char *key = term_skip_spaces(hypr + 9);
      if (*key == '\0') {
        snprintf(report, sizeof(report),
                 "usage: desktop hyprctl getoption <hypr-key>\n");
      } else {
        orizon_desktop_format_hypr_option(key, report, sizeof(report));
      }
    } else if (term_command_is(hypr, "keyword")) {
      const char *key = term_skip_spaces(hypr + 7);
      const char *value = key;
      char key_buf[96];
      size_t key_len;
      while (*value && *value != ' ') {
        value++;
      }
      key_len = (size_t)(value - key);
      if (*value == ' ') {
        value = term_skip_spaces(value);
      }
      if (key_len == 0 || key_len >= sizeof(key_buf) || *value == '\0') {
        snprintf(report, sizeof(report),
                 "usage: desktop hyprctl keyword <hypr-key> <value>\n");
      } else {
        memcpy(key_buf, key, key_len);
        key_buf[key_len] = '\0';
        orizon_desktop_apply_hypr_keyword(key_buf, value, report,
                                          sizeof(report));
        gui_desktop_reload_session();
      }
    } else if (term_command_is(hypr, "reload")) {
      orizon_desktop_apply_hypr_config(report, sizeof(report));
      gui_desktop_reload_session();
    } else if (term_command_is(hypr, "dispatch")) {
      const char *dispatch = term_skip_spaces(hypr + 8);
      const char *dispatch_args = dispatch;
      while (*dispatch_args && *dispatch_args != ' ') {
        dispatch_args++;
      }
      if (*dispatch_args == ' ') {
        size_t dispatch_len = (size_t)(dispatch_args - dispatch);
        char name[32];
        if (dispatch_len >= sizeof(name)) {
          dispatch_len = sizeof(name) - 1;
        }
        memcpy(name, dispatch, dispatch_len);
        name[dispatch_len] = '\0';
        dispatch_args = term_skip_spaces(dispatch_args);
        gui_desktop_dispatch(name, dispatch_args, report, sizeof(report));
      } else {
        gui_desktop_dispatch(dispatch, "", report, sizeof(report));
      }
    } else {
      snprintf(report, sizeof(report), "hyprctl: unknown command\n");
    }
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "dispatch")) {
    const char *dispatch = term_skip_spaces(args + 8);
    const char *dispatch_args = dispatch;
    while (*dispatch_args && *dispatch_args != ' ') {
      dispatch_args++;
    }
    if (*dispatch_args == ' ') {
      size_t dispatch_len = (size_t)(dispatch_args - dispatch);
      char name[32];
      if (dispatch_len >= sizeof(name)) {
        dispatch_len = sizeof(name) - 1;
      }
      memcpy(name, dispatch, dispatch_len);
      name[dispatch_len] = '\0';
      dispatch_args = term_skip_spaces(dispatch_args);
      gui_desktop_dispatch(name, dispatch_args, report, sizeof(report));
    } else {
      gui_desktop_dispatch(dispatch, "", report, sizeof(report));
    }
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "profiles") || term_command_is(args, "themes")) {
    orizon_desktop_format_profiles(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "preset") || term_command_is(args, "profile")) {
    const char *value =
        term_skip_spaces(args + (term_command_is(args, "preset") ? 6 : 7));
    if (*value == '\0') {
      term_puts_t(term,
                  "usage: desktop preset <graphite|moss|ember|frost|focus>\n");
      return;
    }
    orizon_desktop_apply_preset(value, report, sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "autostart")) {
    const char *value = term_skip_spaces(args + 9);
    orizon_desktop_session_t session;
    if (*value == '\0') {
      orizon_desktop_format_autostart(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (!term_command_is(value, "terminal")) {
      term_puts_t(term,
                  "usage: desktop autostart terminal on|off|toggle\n");
      return;
    }
    value = term_skip_spaces(value + 8);
    if (term_command_is(value, "toggle")) {
      orizon_desktop_load_session(&session);
      value = session.autostart_terminal ? "off" : "on";
    }
    if (*value == '\0') {
      term_puts_t(term,
                  "usage: desktop autostart terminal on|off|toggle\n");
      return;
    }
    orizon_desktop_set_session_option("autostart-terminal", value, report,
                                      sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "windows") || term_command_is(args, "window") ||
      term_command_is(args, "clients") || term_command_is(args, "client")) {
    gui_desktop_format_windows(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "client-model") ||
      term_command_is(args, "clientmodel") ||
      term_command_is(args, "client-map") ||
      term_command_is(args, "clientmap") || term_command_is(args, "model")) {
    gui_desktop_format_client_model(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "rule-matches") ||
      term_command_is(args, "rulematches") ||
      term_command_is(args, "windowrules") ||
      term_command_is(args, "window-rules")) {
    gui_desktop_format_rule_matches(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "activewindow") ||
      term_command_is(args, "active-window")) {
    gui_desktop_format_activewindow(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "workspaces")) {
    gui_desktop_format_workspaces(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "workspace")) {
    const char *value = term_skip_spaces(args + 9);
    int workspace;
    if (*value == '\0') {
      gui_desktop_format_workspaces(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_parse_uint(value, &workspace) == 0 &&
        gui_desktop_switch_workspace(workspace) == 0) {
      snprintf(report, sizeof(report), "desktop: workspace %d active\n",
               workspace);
      term_puts_t(term, report);
      return;
    }
    if (gui_desktop_dispatch("workspace", value, report, sizeof(report)) == 0) {
      term_puts_t(term, report);
      return;
    }
    term_puts_t(term,
                "usage: desktop workspace <1-10|name:<name>|next|empty|+/-n|previous>\n");
    return;
  }
  if (term_command_is(args, "move")) {
    const char *target = term_skip_spaces(args + 4);
    int workspace;
    if (!term_command_is(target, "terminal")) {
      term_puts_t(term, "usage: desktop move terminal <1-10>\n");
      return;
    }
    target = term_skip_spaces(target + 8);
    if (term_parse_uint(target, &workspace) < 0 ||
        gui_desktop_move_terminal_to_workspace(workspace) < 0) {
      term_puts_t(term, "usage: desktop move terminal <1-10>\n");
      return;
    }
    snprintf(report, sizeof(report), "desktop: terminal moved to workspace %d\n",
             workspace);
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "focus-window")) {
    const char *value = term_skip_spaces(args + 12);
    int rc;
    if (*value == '\0' || term_command_is(value, "next") ||
        term_command_is(value, "right") || term_command_is(value, "down")) {
      rc = gui_desktop_focus_next_client();
    } else if (term_command_is(value, "prev") || term_command_is(value, "left") ||
               term_command_is(value, "up")) {
      rc = gui_desktop_focus_prev_client();
    } else {
      rc = gui_desktop_dispatch("focuswindow", value, report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    term_puts_t(term, rc == 0 ? "desktop: focus changed\n"
                              : "desktop: no client to focus\n");
    return;
  }
  if (term_command_is(args, "theme")) {
    const char *value = term_skip_spaces(args + 5);
    if (*value == '\0') {
      term_puts_t(term, "usage: desktop theme <graphite|moss|ember|frost>\n");
      return;
    }
    orizon_desktop_set_session_option("theme", value, report,
                                      sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "wallpaper")) {
    const char *value = term_skip_spaces(args + 9);
    if (*value == '\0') {
      term_puts_t(term, "usage: desktop wallpaper <aurora|dawn|noir|moss>\n");
      return;
    }
    orizon_desktop_set_session_option("wallpaper", value, report,
                                      sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "layout")) {
    const char *value = term_skip_spaces(args + 6);
    if (*value == '\0') {
      term_puts_t(term, "usage: desktop layout <dwindle|master|monocle>\n");
      return;
    }
    orizon_desktop_set_session_option("layout", value, report,
                                      sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "focus")) {
    const char *value = term_skip_spaces(args + 5);
    orizon_desktop_session_t session;
    if (term_command_is(value, "toggle")) {
      orizon_desktop_load_session(&session);
      value = session.focus_follows_mouse ? "off" : "on";
    }
    if (*value == '\0') {
      term_puts_t(term, "usage: desktop focus on|off|toggle\n");
      return;
    }
    orizon_desktop_set_session_option("focus", value, report, sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "bar")) {
    const char *value = term_skip_spaces(args + 3);
    orizon_desktop_session_t session;
    if (term_command_is(value, "toggle")) {
      orizon_desktop_load_session(&session);
      value = session.bar_enabled ? "off" : "on";
    }
    if (*value == '\0') {
      term_puts_t(term, "usage: desktop bar on|off|toggle\n");
      return;
    }
    orizon_desktop_set_session_option("bar", value, report, sizeof(report));
    gui_desktop_reload_session();
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "launcher")) {
    const char *value = term_skip_spaces(args + 8);
    if (*value == '\0' || term_command_is(value, "show") ||
        term_command_is(value, "open")) {
      gui_desktop_show_launcher();
      term_puts_t(term, "desktop: launcher open\n");
      return;
    }
    if (term_command_is(value, "hide") || term_command_is(value, "close")) {
      gui_desktop_hide_launcher();
      term_puts_t(term, "desktop: launcher closed\n");
      return;
    }
    if (term_command_is(value, "toggle")) {
      gui_desktop_toggle_launcher();
      term_puts_t(term, "desktop: launcher toggled\n");
      return;
    }
    term_puts_t(term, "usage: desktop launcher [show|hide|toggle]\n");
    return;
  }
  if (term_command_is(args, "launch")) {
    const char *app = term_skip_spaces(args + 6);
    if (*app == '\0') {
      term_puts_t(term,
                  "usage: desktop launch <terminal|settings|logs|packages|update|launcher>\n");
      return;
    }
    gui_desktop_spawn_app_client(app, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "spawn") || term_command_is(args, "exec")) {
    const char *app = term_skip_spaces(args + (term_command_is(args, "spawn") ? 5 : 4));
    if (*app == '\0') {
      term_puts_t(term,
                  "usage: desktop exec <terminal|settings|logs|packages|update|launcher>\n");
      return;
    }
    gui_desktop_spawn_app_client(app, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "apply")) {
    gui_desktop_reload_session();
    term_puts_t(term, "desktop: session reloaded\n");
    return;
  }
  if (term_command_is(args, "write-config") ||
      term_command_is(args, "regen-config")) {
    orizon_desktop_write_user_config(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "reset")) {
    orizon_desktop_reset(report, sizeof(report));
    gui_desktop_set_enabled(0);
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "enable") || term_command_is(args, "install")) {
    orizon_desktop_set_enabled(1, report, sizeof(report));
    gui_desktop_set_enabled(1);
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "disable") || term_command_is(args, "off")) {
    orizon_desktop_set_enabled(0, report, sizeof(report));
    gui_desktop_set_enabled(0);
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "open") ||
      term_command_is(args, "open terminal") ||
      term_command_is(args, "terminal")) {
    gui_desktop_open_terminal();
    term_puts_t(term, "desktop: terminal open\n");
    return;
  }
  if (term_command_is(args, "close") ||
      term_command_is(args, "close terminal")) {
    gui_desktop_close_active_client();
    term_puts_t(term, "desktop: active client closed\n");
    return;
  }
  if (term_command_is(args, "killactive")) {
    if (gui_desktop_close_active_client() == 0) {
      term_puts_t(term, "desktop: active client closed\n");
    } else {
      term_puts_t(term, "desktop: no active client\n");
    }
    return;
  }
  if (term_command_is(args, "toggle")) {
    gui_desktop_toggle_terminal();
    term_puts_t(term, "desktop: terminal toggled\n");
    return;
  }
  if (term_command_is(args, "package") || term_command_is(args, "sample")) {
    orizon_pkg_write_desktop_sample(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  term_puts_t(term, "desktop: unknown command. Try 'desktop help'.\n");
}

static void term_print_net_status(terminal_t *term) {
  char line[512];
  net_format_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  wifi_format_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  usb_format_net_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  netstack_format_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
}

static void term_run_ssh(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 3);
  char report[3072];

  if (*args == '\0' || term_command_is(args, "status")) {
    ssh_format_report(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    term_puts_t(term,
                "commands: ssh password <pass> | ssh password off | ssh start | ssh stop | ssh status | ssh audit | ssh auth | ssh auth max <n> | ssh auth lockout <s> | ssh hostkey | ssh hostkey reload | ssh hostkey reset | ssh reload | ssh lockout clear | ssh algorithms | ssh poll\n");
    return;
  }

  if (term_command_is(args, "password") || term_command_is(args, "passwd")) {
    const char *password =
        term_skip_spaces(args + (term_command_is(args, "passwd") ? 6 : 8));
    if (term_command_is(password, "off") ||
        term_command_is(password, "disable") ||
        term_command_is(password, "disabled")) {
      ssh_disable_password(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    ssh_set_password(password, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "start")) {
    term_puts_t(term, "ssh: starting TCP/22 listener...\n");
    if (ssh_start(report, sizeof(report)) == 0) {
      term_puts_t(term, report);
    } else {
      term_puts_t(term, report);
    }
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }

  if (term_command_is(args, "stop")) {
    ssh_stop(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "poll")) {
    int rc = ssh_poll();
    ssh_format_status(report, sizeof(report));
    term_puts_t(term, report);
    term_puts_t(term, "\n");
    snprintf(report, sizeof(report), "poll=%d\n", rc);
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "audit") || term_command_is(args, "sessions")) {
    ssh_format_audit(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }

  if (term_command_is(args, "auth") || term_command_is(args, "security")) {
    const char *auth_args =
        term_skip_spaces(args + (term_command_is(args, "auth") ? 4 : 8));
    const ssh_status_t *st = ssh_get_status();
    int value = 0;
    if (term_command_is(auth_args, "max")) {
      const char *value_arg = term_skip_spaces(auth_args + 3);
      if (term_parse_uint(value_arg, &value) < 0) {
        term_puts_t(term, "usage: ssh auth max <attempts>\n");
        return;
      }
      ssh_set_auth_policy((uint32_t)value, st->auth_lockout_seconds, report,
                          sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(auth_args, "lockout")) {
      const char *value_arg = term_skip_spaces(auth_args + 7);
      if (term_parse_uint(value_arg, &value) < 0) {
        term_puts_t(term, "usage: ssh auth lockout <seconds>\n");
        return;
      }
      ssh_set_auth_policy(st->max_auth_attempts, (uint32_t)value, report,
                          sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(auth_args, "default") ||
        term_command_is(auth_args, "defaults")) {
      ssh_reset_auth_policy(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    ssh_format_auth(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }

  if (term_command_is(args, "reload")) {
    ssh_reload_config(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "hostkey")) {
    const char *hostkey_args = term_skip_spaces(args + 7);
    if (term_command_is(hostkey_args, "reload")) {
      ssh_reload_hostkey(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(hostkey_args, "reset")) {
      ssh_reset_hostkey(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    ssh_format_hostkey(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }

  if (term_command_is(args, "lockout")) {
    const char *lock_args = term_skip_spaces(args + 7);
    if (term_command_is(lock_args, "clear") ||
        term_command_is(lock_args, "reset") ||
        term_command_is(lock_args, "unlock")) {
      ssh_clear_lockout(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    ssh_format_auth(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "algorithms") || term_command_is(args, "algo")) {
    ssh_format_algorithms(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    return;
  }

  term_puts_t(term,
              "usage: ssh password <pass> | ssh password off | ssh start | ssh stop | ssh status | ssh audit | ssh auth | ssh auth max <n> | ssh auth lockout <s> | ssh hostkey | ssh hostkey reload | ssh hostkey reset | ssh reload | ssh lockout clear | ssh algorithms | ssh poll\n");
}

static void term_run_security(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 8);
  static char report[4096];

  if (*args == '\0' || term_command_is(args, "status")) {
    ssh_format_security(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "policy")) {
    ssh_format_security_policy(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "audit") || term_command_is(args, "sessions")) {
    ssh_format_security_audit(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "keys") || term_command_is(args, "hostkey")) {
    ssh_format_security_keys(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "doctor") || term_command_is(args, "check")) {
    ssh_format_security_doctor(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }
  if (term_command_is(args, "rotate")) {
    const char *rotate = term_skip_spaces(args + 6);
    if (term_command_is(rotate, "ssh-hostkey") ||
        term_command_is(rotate, "hostkey")) {
      term_puts_t(term,
                  "security rotate: regenerating SSH host key; future clients "
                  "may need known_hosts cleanup.\n");
      ssh_reset_hostkey(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
  }
  term_puts_t(term,
              "usage: security [status|policy|audit|keys|doctor|rotate ssh-hostkey]\n");
}

static void term_run_net(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 3);
  static char report[16384];
  char line[512];
  size_t tls_len = 0;

  if (term_command_is(args, "check") || term_command_is(args, "doctor")) {
    netstack_format_check(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "renew")) {
    netstack_renew_ipv4(report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "tcp")) {
    const char *tcp_args = term_skip_spaces(args + 3);
    char host[96];
    char token[32];
    char attempts_text[16];
    int port = 443;
    int attempts = 0;

    tcp_args = term_read_token(tcp_args, host, sizeof(host));
    if (!tcp_args) {
      term_puts_t(term,
                  "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\n");
      return;
    }
    if (*tcp_args) {
      tcp_args = term_read_token(tcp_args, token, sizeof(token));
      if (!tcp_args) {
        term_puts_t(term,
                    "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\n");
        return;
      }
      if (strcmp(token, "attempts") == 0 || strcmp(token, "tries") == 0 ||
          strcmp(token, "retry") == 0) {
        tcp_args = term_read_token(tcp_args, attempts_text,
                                   sizeof(attempts_text));
        if (!tcp_args || term_parse_uint(attempts_text, &attempts) < 0 ||
            attempts <= 0 || attempts > 5 || *tcp_args) {
          term_puts_t(term,
                      "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\n");
          return;
        }
      } else if (term_parse_uint(token, &port) == 0 && port > 0 &&
                 port <= 65535) {
        if (*tcp_args) {
          tcp_args = term_read_token(tcp_args, token, sizeof(token));
          if (!tcp_args || (strcmp(token, "attempts") != 0 &&
                            strcmp(token, "tries") != 0 &&
                            strcmp(token, "retry") != 0)) {
            term_puts_t(term,
                        "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\n");
            return;
          }
          tcp_args = term_read_token(tcp_args, attempts_text,
                                     sizeof(attempts_text));
          if (!tcp_args || term_parse_uint(attempts_text, &attempts) < 0 ||
              attempts <= 0 || attempts > 5 || *tcp_args) {
            term_puts_t(term,
                        "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\n");
            return;
          }
        }
      } else {
        term_puts_t(term,
                    "usage: net tcp <host-or-ip> [port] [attempts <1-5>]\n");
        return;
      }
    }
    if (attempts > 0) {
      netstack_tcp_probe_retry(host, (uint16_t)port, (unsigned)attempts,
                               report, sizeof(report));
    } else {
      netstack_tcp_probe(host, (uint16_t)port, report, sizeof(report));
    }
    term_puts_t(term, report);
    return;
  }

  if (term_command_is(args, "diag") || term_command_is(args, "daily")) {
    int include_tls = term_command_is(args, "diag");
    term_puts_t(term, include_tls ? "net diag: daily VM network diagnostics\n"
                                  : "net daily: VM network diagnostics\n");
    netstack_format_daily(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    netstack_format_check(report, sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    netstack_tcp_probe("raw.githubusercontent.com", 443, report,
                       sizeof(report));
    term_puts_t(term, report);
    if (report[0] && report[strlen(report) - 1] != '\n') {
      term_puts_t(term, "\n");
    }
    if (!include_tls) {
      return;
    }
    term_puts_t(term, "net diag: TLS/root-trust probe\n");
    report[0] = '\0';
    if (netstack_github_tls_probe(report, sizeof(report), &tls_len) == 0) {
      snprintf(line, sizeof(line), "net tls: PASS bytes=%lu\n",
               (unsigned long)tls_len);
      term_puts_t(term, line);
    } else {
      snprintf(line, sizeof(line), "net tls: FAIL status=%s\n",
               netstack_get_status()->status);
      term_puts_t(term, line);
    }
    if (report[0]) {
      term_puts_t(term, report);
      if (report[strlen(report) - 1] != '\n') {
        term_puts_t(term, "\n");
      }
    }
    return;
  }

  if (term_command_is(args, "tls") || term_command_is(args, "https")) {
    term_puts_t(term, "net tls: probing raw.githubusercontent.com over TLS...\n");
    report[0] = '\0';
    if (netstack_github_tls_probe(report, sizeof(report), &tls_len) == 0) {
      snprintf(line, sizeof(line), "net tls: PASS bytes=%lu\n",
               (unsigned long)tls_len);
      term_puts_t(term, line);
    } else {
      snprintf(line, sizeof(line), "net tls: FAIL status=%s\n",
               netstack_get_status()->status);
      term_puts_t(term, line);
      term_puts_t(term,
                  "hint: run 'net tcp raw.githubusercontent.com 443' to "
                  "separate TCP reachability from TLS/root trust.\n");
    }
    if (report[0]) {
      term_puts_t(term, report);
      if (report[strlen(report) - 1] != '\n') {
        term_puts_t(term, "\n");
      }
    }
    return;
  }

  if (term_command_is(args, "dhcp")) {
    term_puts_t(term, "net: configuring IPv4 with DHCP...\n");
    netstack_reset();
    if (netstack_configure_ipv4_dhcp() == 0) {
      term_puts_t(term, "net: DHCP configured\n");
    } else {
      term_puts_t(term, "net: DHCP failed\n");
      if (usb_net_present()) {
        usb_format_net_status(line, sizeof(line));
        term_puts_t(term, line);
        term_puts_t(term, "\n");
        if (usb_net_ready()) {
          term_puts_t(term,
                      "net: USB Ethernet packet driver is active; check cable, "
                      "DHCP server, or adapter link.\n");
        } else {
          term_puts_t(term,
                      "net: USB Ethernet detected, but the packet path is not "
                      "ready yet; check the usb status field.\n");
        }
      }
    }
    netstack_format_status(line, sizeof(line));
    term_puts_t(term, line);
    term_puts_t(term, "\n");
    return;
  }

  if (term_command_is(args, "auto")) {
    term_puts_t(term, "net: auto config DHCP, then static fallback...\n");
    netstack_reset();
    if (netstack_configure_ipv4() == 0) {
      term_puts_t(term, "net: IPv4 configured\n");
    } else {
      term_puts_t(term, "net: IPv4 configuration failed\n");
    }
    netstack_format_status(line, sizeof(line));
    term_puts_t(term, line);
    term_puts_t(term, "\n");
    return;
  }

  if (term_command_is(args, "reset")) {
    netstack_reset();
    term_puts_t(term, "net: IPv4 state reset\n");
    return;
  }

  if (term_command_is(args, "status")) {
    term_print_net_status(term);
    netstack_format_route(line, sizeof(line));
    term_puts_t(term, line);
    term_puts_t(term, "\n");
    netstack_format_dns(line, sizeof(line));
    term_puts_t(term, line);
    term_puts_t(term, "\n");
    term_puts_t(term, "config: ");
    term_puts_t(term, netstack_config_path());
    term_puts_t(term, "\nlog: ");
    term_puts_t(term, netstack_log_path());
    term_puts_t(term, "\n");
    return;
  }

  if (term_command_is(args, "config")) {
    const char *cfg_args = term_skip_spaces(args + 6);
    char token[32];
    char ip_token[32];
    char key[32];
    char value[32];
    uint32_t ip = 0;
    uint32_t subnet = 0xffffff00U;
    uint32_t gateway = 0;
    uint32_t dns = 0;

    if (term_command_is(cfg_args, "show") || *cfg_args == '\0') {
      char cfg_text[512];
      int n = term_read_text_file_silent(netstack_config_path(), cfg_text,
                                         sizeof(cfg_text));
      if (n > 0) {
        cfg_text[n] = '\0';
        term_puts_t(term, cfg_text);
        if (cfg_text[n - 1] != '\n') {
          term_puts_t(term, "\n");
        }
      } else {
        term_puts_t(term, "net config: no config file yet\n");
      }
      return;
    }

    if (term_command_is(cfg_args, "dhcp")) {
      if (netstack_save_dhcp_config() == 0) {
        netstack_reset();
        vfs_persist_save();
        term_puts_t(term, "net config: saved DHCP mode\n");
      } else {
        term_puts_t(term, "net config: cannot save DHCP mode\n");
      }
      return;
    }

    cfg_args = term_read_token(cfg_args, token, sizeof(token));
    if (!cfg_args || strcmp(token, "ip") != 0) {
      term_puts_t(term,
                  "usage: net config ip <ip> gateway <gw> dns <dns> [subnet <mask>]\n");
      return;
    }
    cfg_args = term_read_token(cfg_args, ip_token, sizeof(ip_token));
    if (!cfg_args || netstack_parse_ipv4(ip_token, &ip) != 0) {
      term_puts_t(term, "net config: invalid ip\n");
      return;
    }

    while (*cfg_args) {
      cfg_args = term_read_token(cfg_args, key, sizeof(key));
      if (!cfg_args) {
        break;
      }
      cfg_args = term_read_token(cfg_args, value, sizeof(value));
      if (!cfg_args) {
        term_puts_t(term, "net config: missing value\n");
        return;
      }
      if (strcmp(key, "gateway") == 0) {
        if (netstack_parse_ipv4(value, &gateway) != 0) {
          term_puts_t(term, "net config: invalid gateway\n");
          return;
        }
      } else if (strcmp(key, "dns") == 0) {
        if (netstack_parse_ipv4(value, &dns) != 0) {
          term_puts_t(term, "net config: invalid dns\n");
          return;
        }
      } else if (strcmp(key, "subnet") == 0) {
        if (netstack_parse_ipv4(value, &subnet) != 0) {
          term_puts_t(term, "net config: invalid subnet\n");
          return;
        }
      } else {
        term_puts_t(term, "net config: unknown field\n");
        return;
      }
    }

    if (gateway == 0) {
      term_puts_t(term, "net config: gateway is required\n");
      return;
    }
    if (dns == 0) {
      dns = gateway;
    }
    if (netstack_save_static_config(ip, subnet, gateway, dns) != 0) {
      term_puts_t(term, "net config: save failed\n");
      return;
    }
    netstack_reset();
    if (netstack_configure_ipv4_static(ip, subnet, gateway, dns) == 0) {
      vfs_persist_save();
      term_puts_t(term, "net config: static IPv4 saved and applied\n");
    } else {
      term_puts_t(term, "net config: saved, but apply failed now\n");
    }
    return;
  }

  term_print_net_status(term);
}

static int term_wifi_online_probe(terminal_t *term, const char *label,
                                  const char *ssid, const char *password) {
  char line[8192];
  char ip_s[24];
  char evidence[512];
  const wifi_status_t *wifi_status;
  uint32_t ip = 0;
  size_t tls_len = 0;

  if (!term || !ssid || !ssid[0]) {
    return -1;
  }

  snprintf(evidence, sizeof(evidence),
           "%s: START ssid=\"%s\" stages=wpa2/ccmp,dhcp,dns,tls",
           label, ssid);
  term_wifi_log_line(evidence);
  term_wifi_write_last(evidence);
  term_wifi_record_validation(label, ssid, "start", "START",
                              "starting guarded WPA2/CCMP online validation");

  term_puts_t(term, label);
  term_puts_t(term, ": joining guarded WPA2/CCMP link...\n");
  if (wifi_join(ssid, password, line, sizeof(line)) != 0) {
    term_puts_t(term, line);
    term_wifi_log_line(line);
    snprintf(evidence, sizeof(evidence),
             "%s: FAIL stage=join ssid=\"%s\"", label, ssid);
    term_wifi_log_line(evidence);
    term_wifi_record_validation(label, ssid, "join", "FAIL",
                                "wifi_join did not complete; inspect WPA/AP stage details");
    vfs_persist_save();
    return -1;
  }
  term_puts_t(term, line);
  term_wifi_log_line(line);
  if (!wifi_data_link_ready()) {
    term_puts_t(term, label);
    term_puts_t(term,
                ": Wi-Fi link is associated but not CCMP-ready\n");
    snprintf(evidence, sizeof(evidence),
             "%s: FAIL stage=ccmp-ready ssid=\"%s\"", label, ssid);
    term_wifi_log_line(evidence);
    term_wifi_record_validation(label, ssid, "ccmp-ready", "FAIL",
                                "association exists but guarded CCMP data path is not ready");
    vfs_persist_save();
    return -1;
  }
  wifi_status = wifi_get_status();
  snprintf(evidence, sizeof(evidence),
           "%s: OK stage=ccmp-ready ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x rx-ccmp=%lu",
           label, ssid, wifi_status->connect_bssid[0],
           wifi_status->connect_bssid[1], wifi_status->connect_bssid[2],
           wifi_status->connect_bssid[3], wifi_status->connect_bssid[4],
           wifi_status->connect_bssid[5],
           (unsigned long)wifi_status->ccmp_rx_packets);
  term_wifi_log_line(evidence);

  term_puts_t(term, label);
  term_puts_t(term, ": requesting DHCP over CCMP...\n");
  netstack_reset();
  if (netstack_configure_ipv4_dhcp() != 0) {
    term_puts_t(term, label);
    term_puts_t(term, ": DHCP failed\n");
    netstack_format_status(line, sizeof(line));
    term_puts_t(term, line);
    term_puts_t(term, "\n");
    snprintf(evidence, sizeof(evidence),
             "%s: FAIL stage=dhcp ssid=\"%s\"", label, ssid);
    term_wifi_log_line(evidence);
    term_wifi_record_validation(label, ssid, "dhcp", "FAIL",
                                "DHCP over the protected Wi-Fi link failed");
    vfs_persist_save();
    return -1;
  }
  netstack_format_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  snprintf(evidence, sizeof(evidence), "%s: OK stage=dhcp %s", label, line);
  term_wifi_log_line(evidence);

  if (netstack_resolve_a("raw.githubusercontent.com", &ip) != 0) {
    term_puts_t(term, label);
    term_puts_t(term, ": DNS probe failed\n");
    snprintf(evidence, sizeof(evidence),
             "%s: FAIL stage=dns host=raw.githubusercontent.com", label);
    term_wifi_log_line(evidence);
    term_wifi_record_validation(label, ssid, "dns", "FAIL",
                                "DNS resolution failed after DHCP");
    vfs_persist_save();
    return -1;
  }
  netstack_format_ipv4(ip, ip_s, sizeof(ip_s));
  term_puts_t(term, label);
  term_puts_t(term, ": DNS raw.githubusercontent.com -> ");
  term_puts_t(term, ip_s);
  term_puts_t(term, "\n");
  snprintf(evidence, sizeof(evidence),
           "%s: OK stage=dns host=raw.githubusercontent.com ip=%s",
           label, ip_s);
  term_wifi_log_line(evidence);

  line[0] = '\0';
  if (netstack_github_tls_probe(line, sizeof(line), &tls_len) != 0) {
    term_puts_t(term, label);
    term_puts_t(term, ": GitHub TLS probe failed\n");
    if (line[0]) {
      term_puts_t(term, line);
      term_wifi_log_line(line);
    }
    snprintf(evidence, sizeof(evidence),
             "%s: FAIL stage=tls host=raw.githubusercontent.com", label);
    term_wifi_log_line(evidence);
    term_wifi_record_validation(label, ssid, "tls", "FAIL",
                                "GitHub TLS probe failed after DNS");
    vfs_persist_save();
    return -1;
  }
  snprintf(line, sizeof(line),
           "%s: GitHub TLS probe ok bytes=%lu\n",
           label, (unsigned long)tls_len);
  term_puts_t(term, line);
  snprintf(evidence, sizeof(evidence),
           "%s: PASS ssid=\"%s\" ccmp=yes dhcp=yes dns=yes tls=yes tls-bytes=%lu",
           label, ssid, (unsigned long)tls_len);
  term_wifi_log_line(evidence);
  term_wifi_record_validation(label, ssid, "tls", "PASS",
                              "GitHub TLS probe passed over protected Wi-Fi");
  vfs_persist_save();
  return 0;
}

static void term_run_wifi(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 4);
  char line[8192];
  char ssid[96];
  char password[96];
  const char *rest;

  if (*args == '\0' || term_command_is(args, "status")) {
    wifi_format_status(line, sizeof(line));
    term_puts_t(term, line);
    term_puts_t(term, "\n");
    term_puts_t(term,
                "Wi-Fi note: Intel CNVi support is staged only; scan/connect need firmware + WPA layer.\n");
    return;
  }

  if (term_command_is(args, "scan")) {
    const char *scan_args = term_skip_spaces(args + 4);
    if (term_command_is(scan_args, "poll") ||
        term_command_is(scan_args, "wait")) {
      wifi_scan_poll(line, sizeof(line));
      term_puts_t(term, line);
      return;
    }
    int arm_scan = term_command_is(scan_args, "arm") ||
                   term_command_is(scan_args, "go");
    wifi_scan(arm_scan, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "firmware")) {
    wifi_firmware_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "hw")) {
    wifi_hw_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "apm")) {
    wifi_apm_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "load")) {
    wifi_load_firmware(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "upload")) {
    const char *upload_args = term_skip_spaces(args + 6);
    if (term_command_is(upload_args, "all")) {
      const char *all_args = term_skip_spaces(upload_args + 3);
      int arm_all = term_command_is(all_args, "arm") ||
                    term_command_is(all_args, "go");
      wifi_upload_all_firmware(arm_all, line, sizeof(line));
    } else {
      int arm = term_command_is(upload_args, "arm") ||
                term_command_is(upload_args, "first");
      wifi_upload_firmware(arm, line, sizeof(line));
    }
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "boot")) {
    const char *boot_args = term_skip_spaces(args + 4);
    int arm_boot = term_command_is(boot_args, "arm") ||
                   term_command_is(boot_args, "go");
    wifi_boot_firmware(arm_boot, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "alive")) {
    wifi_alive_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "queues")) {
    const char *queue_args = term_skip_spaces(args + 6);
    int arm_queues = term_command_is(queue_args, "arm") ||
                     term_command_is(queue_args, "go");
    wifi_queue_probe(arm_queues, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "context")) {
    const char *context_args = term_skip_spaces(args + 7);
    int arm_context = term_command_is(context_args, "arm") ||
                      term_command_is(context_args, "go");
    wifi_context_probe(arm_context, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "scheduler")) {
    const char *scheduler_args = term_skip_spaces(args + 9);
    int arm_scheduler = term_command_is(scheduler_args, "arm") ||
                        term_command_is(scheduler_args, "go");
    wifi_scheduler_probe(arm_scheduler, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "rx")) {
    const char *rx_args = term_skip_spaces(args + 2);
    int poll_rx = term_command_is(rx_args, "poll") ||
                  term_command_is(rx_args, "wait");
    wifi_rx_probe(poll_rx, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "command")) {
    const char *command_args = term_skip_spaces(args + 7);
    int arm_command = term_command_is(command_args, "arm") ||
                      term_command_is(command_args, "go");
    wifi_command_probe(arm_command, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "nvm")) {
    const char *nvm_args = term_skip_spaces(args + 3);
    int arm_nvm = term_command_is(nvm_args, "arm") ||
                  term_command_is(nvm_args, "go");
    wifi_nvm_probe(arm_nvm, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "nvm-info")) {
    const char *nvm_info_args = term_skip_spaces(args + 8);
    int arm_nvm_info = term_command_is(nvm_info_args, "arm") ||
                       term_command_is(nvm_info_args, "go");
    wifi_nvm_info_probe(arm_nvm_info, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "bringup")) {
    wifi_bringup_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "crypto")) {
    wifi_crypto_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "wpa")) {
    wifi_wpa_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "key")) {
    const char *key_args = term_skip_spaces(args + 3);
    char target[16];
    char mode[16];
    int group_key = 0;
    int arm_key = 0;
    target[0] = '\0';
    mode[0] = '\0';
    if (*key_args) {
      const char *rest_key = term_read_token(key_args, target, sizeof(target));
      if (strcmp(target, "arm") == 0 || strcmp(target, "go") == 0) {
        arm_key = 1;
      } else if (strcmp(target, "gtk") == 0 ||
                 strcmp(target, "group") == 0) {
        group_key = 1;
        if (rest_key && *rest_key) {
          term_read_token(rest_key, mode, sizeof(mode));
          arm_key = strcmp(mode, "arm") == 0 || strcmp(mode, "go") == 0;
        }
      } else if (strcmp(target, "pairwise") == 0 ||
                 strcmp(target, "ptk") == 0) {
        group_key = 0;
        if (rest_key && *rest_key) {
          term_read_token(rest_key, mode, sizeof(mode));
          arm_key = strcmp(mode, "arm") == 0 || strcmp(mode, "go") == 0;
        }
      }
    }
    wifi_key_probe(group_key, arm_key, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "data")) {
    wifi_data_probe(line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "bind")) {
    const char *bind_args = term_skip_spaces(args + 4);
    int arm_bind = term_command_is(bind_args, "arm") ||
                   term_command_is(bind_args, "go");
    wifi_bind_probe(arm_bind, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "tx")) {
    const char *tx_args = term_skip_spaces(args + 2);
    char target[16];
    target[0] = '\0';
    if (*tx_args) {
      term_read_token(tx_args, target, sizeof(target));
    }
    wifi_tx_stage_probe(target, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "txcmd")) {
    const char *txcmd_args = term_skip_spaces(args + 5);
    char target[16];
    char mode[16];
    int arm_txcmd = 0;
    target[0] = '\0';
    mode[0] = '\0';
    if (*txcmd_args) {
      const char *rest_txcmd = term_read_token(txcmd_args, target, sizeof(target));
      if (strcmp(target, "arm") == 0 || strcmp(target, "go") == 0) {
        arm_txcmd = 1;
        target[0] = '\0';
      } else if (rest_txcmd) {
        rest_txcmd = term_skip_spaces(rest_txcmd);
        if (*rest_txcmd) {
          term_read_token(rest_txcmd, mode, sizeof(mode));
          arm_txcmd = strcmp(mode, "arm") == 0 || strcmp(mode, "go") == 0;
        }
      }
    }
    wifi_txcmd_probe(target, arm_txcmd, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "connect")) {
    rest = term_skip_spaces(args + 7);
    rest = term_read_token(rest, ssid, sizeof(ssid));
    if (!rest) {
      term_puts_t(term, "usage: wifi connect <ssid> [password]\n");
      return;
    }
    password[0] = '\0';
    if (*rest) {
      term_read_token(rest, password, sizeof(password));
    }
    wifi_connect(ssid, password, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "join")) {
    rest = term_skip_spaces(args + 4);
    rest = term_read_token(rest, ssid, sizeof(ssid));
    if (!rest) {
      term_puts_t(term, "usage: wifi join <ssid> [password]\n");
      return;
    }
    password[0] = '\0';
    if (*rest) {
      term_read_token(rest, password, sizeof(password));
    }
    wifi_join(ssid, password, line, sizeof(line));
    term_puts_t(term, line);
    return;
  }

  if (term_command_is(args, "online") || term_command_is(args, "validate")) {
    int validate = term_command_is(args, "validate");
    rest = term_skip_spaces(args + (validate ? 8 : 6));
    rest = term_read_token(rest, ssid, sizeof(ssid));
    if (!rest) {
      term_puts_t(term,
                  validate ? "usage: wifi validate <ssid> [password]\n"
                           : "usage: wifi online <ssid> [password]\n");
      return;
    }
    password[0] = '\0';
    if (*rest) {
      term_read_token(rest, password, sizeof(password));
    }

    if (term_wifi_online_probe(term,
                               validate ? "wifi validate" : "wifi online",
                               ssid, password) == 0) {
      term_puts_t(term,
                  validate
                      ? "wifi validate: AP/WPA2/DHCP/DNS/TLS path ready\n"
                      : "wifi online: update path ready; run `update` to use Wi-Fi\n");
    }
    return;
  }

  if (term_command_is(args, "update")) {
    rest = term_skip_spaces(args + 6);
    rest = term_read_token(rest, ssid, sizeof(ssid));
    if (!rest) {
      term_puts_t(term, "usage: wifi update <ssid> [password]\n");
      return;
    }
    password[0] = '\0';
    if (*rest) {
      term_read_token(rest, password, sizeof(password));
    }
    if (term_wifi_online_probe(term, "wifi update", ssid, password) != 0) {
      return;
    }
    if (!term_install_already_complete()) {
      term_puts_t(term,
                  "wifi update: network is ready, but update requires an installed Orizon disk\n");
      term_wifi_record_validation(
          "wifi update", ssid, "update", "BLOCKED",
          "network validation passed, but live boot cannot run installed update");
      vfs_persist_save();
      return;
    }
    term_wifi_record_validation("wifi update", ssid, "update", "START",
                                "GitHub reachable over Wi-Fi; launching updater");
    term_puts_t(term,
                "wifi update: GitHub reachable over Wi-Fi; launching update...\n");
    term_run_update(term, "update");
    return;
  }

  term_puts_t(term,
              "usage: wifi [status|hw|apm|firmware|load|upload [arm|all [arm]]|boot [arm]|alive|queues [arm]|context [arm]|scheduler [arm]|rx [poll]|command [arm]|nvm [arm]|nvm-info [arm]|bringup|crypto|wpa|key [pairwise|gtk] [arm]|data|bind [arm]|scan [arm|poll]|connect <ssid> [password]|join <ssid> [password]|online <ssid> [password]|validate <ssid> [password]|update <ssid> [password]|tx [auth|assoc|m2|m4|data|all]|txcmd [auth|assoc|m2|m4|data] [arm]]\n");
}

static void term_run_dns(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 3);
  char host[128];
  uint32_t ip = 0;
  char ip_s[24];

  if (!term_read_token(args, host, sizeof(host))) {
    term_puts_t(term, "usage: dns <hostname>\n");
    return;
  }
  if (netstack_resolve_a(host, &ip) != 0) {
    term_puts_t(term, "dns: resolve failed\n");
    return;
  }
  netstack_format_ipv4(ip, ip_s, sizeof(ip_s));
  term_puts_t(term, host);
  term_puts_t(term, " -> ");
  term_puts_t(term, ip_s);
  term_puts_t(term, "\n");
}

static void term_run_route(terminal_t *term) {
  char line[256];
  netstack_format_route(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
}

static void term_run_ping(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 4);
  char target[128];
  uint32_t ip = 0;
  char ip_s[24];

  if (!term_read_token(args, target, sizeof(target))) {
    term_puts_t(term, "usage: ping <ip-or-host>\n");
    return;
  }
  if (netstack_parse_ipv4(target, &ip) != 0) {
    term_puts_t(term, "ping: resolving host...\n");
    if (netstack_resolve_a(target, &ip) != 0) {
      term_puts_t(term, "ping: cannot resolve host\n");
      return;
    }
  }
  netstack_format_ipv4(ip, ip_s, sizeof(ip_s));
  for (int i = 0; i < 4; i++) {
    uint32_t ms = 0;
    if (netstack_ping(ip, &ms) == 0) {
      char line[96];
      snprintf(line, sizeof(line), "reply from %s time=%lums\n", ip_s,
               (unsigned long)ms);
      term_puts_t(term, line);
    } else {
      term_puts_t(term, "request timeout\n");
    }
  }
}

static int term_write_text_file(const char *path, const char *text) {
  file_t *f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -1;
  }
  if (text && vfs_write(f, text, strlen(text)) < 0) {
    vfs_close(f);
    return -1;
  }
  vfs_close(f);
  return 0;
}

static int term_read_text_file_silent(const char *path, char *buf, size_t cap) {
  file_t *f;
  size_t used = 0;
  ssize_t n = 0;

  if (!path || !buf || cap < 2) {
    return -1;
  }
  f = vfs_open(path, O_RDONLY);
  if (!f) {
    return -1;
  }
  while (used < cap - 1 &&
         (n = vfs_read(f, buf + used, (cap - 1) - used)) > 0) {
    used += (size_t)n;
  }
  vfs_close(f);
  buf[used] = '\0';
  return n < 0 ? -1 : (int)used;
}

static int term_install_already_complete(void) {
  return orizon_system_is_installed();
}

static void term_run_install_plan(terminal_t *term, const char *cmd) {
  static char report[4096];
  orizon_install_config_t config;
  storage_device_info_t disk;
  char disk_name[24];
  const char *args = term_skip_spaces(cmd + strlen("install-plan"));
  const char *mode = "manual-later";
  const char *desktop_profile = "none";
  int disk_index = -1;
  int data_partition = -1;
  int count;

  if (strstr(args, "desktop") || strstr(args, "hypr")) {
    desktop_profile = ORIZON_DESKTOP_PROFILE;
  }
  if (*args != '\0') {
    if (term_command_is(args, "manual") ||
        term_command_is(args, "manual-later") ||
        term_command_is(args, "desktop")) {
      mode = "manual-later";
    } else if (term_command_is(args, "dual-boot-esp")) {
      mode = "dual-boot-esp";
    } else if (term_command_is(args, "guided-full-disk") ||
               term_command_is(args, "full")) {
      mode = "guided-full-disk";
    } else if (term_command_is(args, "dual-boot-data")) {
      const char *part_arg =
          term_skip_spaces(args + strlen("dual-boot-data"));
      mode = "dual-boot-data";
      if (strncmp(part_arg, "part", 4) == 0) {
        part_arg += 4;
      }
      if (term_parse_uint(part_arg, &data_partition) < 0) {
        term_puts_t(term,
                    "usage: install-plan [manual|manual desktop|dual-boot-esp|dual-boot-data <part>|guided-full-disk]\n");
        return;
      }
    } else {
      term_puts_t(term,
                  "usage: install-plan [manual|manual desktop|dual-boot-esp|dual-boot-data <part>|guided-full-disk]\n");
      return;
    }
  }

  disk_name[0] = '\0';
  count = storage_device_count();
  if (count > 0) {
    disk_index = storage_selected_device();
    if (disk_index < 0) {
      disk_index = 0;
    }
    if (storage_get_device(disk_index, &disk) == 0) {
      snprintf(disk_name, sizeof(disk_name), "%s", disk.name);
    }
  }
  if (disk_name[0] == '\0') {
    snprintf(disk_name, sizeof(disk_name), "none");
  }

  config.language = "en_US";
  config.keyboard = input_keyboard_layout();
  config.disk_mode = mode;
  config.hostname = "orizon-vm";
  config.desktop_profile = desktop_profile;
  config.disk_index = disk_index;
  config.disk_name = disk_name;
  config.data_partition_index = data_partition;

  orizon_install_format_plan(&config, report, sizeof(report));
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  if (term_write_text_file("/workspace/.orizon/install-report.txt", report) <
      0) {
    term_puts_t(term, "install-plan: failed to write report\n");
    return;
  }
  term_puts_t(term, report);
  if (report[0] && report[strlen(report) - 1] != '\n') {
    term_puts_t(term, "\n");
  }
  term_puts_t(term,
              "install-plan: wrote /workspace/.orizon/install-report.txt\n"
              "read with: cat /workspace/.orizon/install-report.txt\n");
}

static int term_install_value_is(const char *value, const char *a,
                                 const char *b, const char *c) {
  return strcmp(value, a) == 0 || (b && strcmp(value, b) == 0) ||
         (c && strcmp(value, c) == 0);
}

static void term_print_disks(terminal_t *term) {
  int count = storage_device_count();
  char line[224];
  char capacity[64];

  if (count <= 0) {
    static char diag[1536];
    term_puts_t(term, "No writable AHCI/NVMe/VirtIO disks detected.\n");
    storage_format_diagnostics(diag, sizeof(diag));
    term_puts_t(term, diag);
    return;
  }

  for (int i = 0; i < count; i++) {
    storage_device_info_t info;
    if (storage_get_device(i, &info) < 0) {
      continue;
    }
    storage_format_size(info.sectors, capacity, sizeof(capacity));
    snprintf(line, sizeof(line), "  %d. %s%s  %s  %s  %s\n", i + 1,
             info.name, info.selected ? " *" : "  ", info.driver, capacity,
             info.model);
    term_puts_t(term, line);
  }
}

static void term_print_partitions(terminal_t *term) {
  static char partitions_report[4096];

  if (!storage_available()) {
    term_puts_t(term, "No selected writable AHCI/NVMe/VirtIO disk.\n");
    return;
  }
  if (orizon_install_format_partitions(partitions_report,
                                       sizeof(partitions_report)) < 0) {
    term_puts_t(term, partitions_report);
    return;
  }
  term_puts_t(term, partitions_report);
}

static int term_install_capture_disk(int choice, terminal_t *term) {
  storage_device_info_t info;
  char capacity[64];

  if (storage_select_device(choice) < 0 ||
      storage_get_device(choice, &info) < 0) {
    return -1;
  }

  storage_format_size(info.sectors, capacity, sizeof(capacity));
  term->install_disk_index = choice;
  snprintf(term->install_disk_name, sizeof(term->install_disk_name), "%s",
           info.name);
  snprintf(term->install_disk_summary, sizeof(term->install_disk_summary),
           "%s %s %s %s", info.name, info.driver, capacity, info.model);
  strcpy(term->install_disk_mode, "dual-boot-data");
  term->install_data_partition_index = -1;
  term->install_data_partition_name[0] = '\0';
  term->install_data_partition_summary[0] = '\0';
  return 0;
}

static int term_install_capture_data_partition(int choice, terminal_t *term) {
  orizon_install_partition_info_t part;
  char size_text[64];

  if (orizon_install_get_partition(choice, &part) < 0 ||
      !part.usable_for_data) {
    return -1;
  }
  storage_format_size(part.sectors, size_text, sizeof(size_text));
  term->install_data_partition_index = choice;
  snprintf(term->install_data_partition_name,
           sizeof(term->install_data_partition_name), "part%d", choice);
  snprintf(term->install_data_partition_summary,
           sizeof(term->install_data_partition_summary),
           "part%d %s %s %s LBA %lu..%lu", choice, size_text, part.type,
           part.name, (unsigned long)part.first_lba,
           (unsigned long)part.last_lba);
  return 0;
}

static void term_install_prompt(terminal_t *term) {
  switch (term->install_step) {
  case 0:
    term_puts_t(term, "\033[1;36mOrizon OS Installer\033[0m\n");
    term_puts_t(term, "This guided installer can install Orizon OS to disk.\n");
    term_puts_t(term,
                "dual-boot-data reuses one partition for Orizon while preserving the rest of the disk.\n\n");
    term_puts_t(term, "[1/8] Language\n");
    term_puts_t(term, "  1. Francais\n");
    term_puts_t(term, "  2. English\n");
    term_puts_t(term, "Choice: ");
    break;
  case 1:
    term_puts_t(term, "[2/8] Keyboard layout\n");
    term_puts_t(term, "  1. fr-azerty\n");
    term_puts_t(term, "  2. us-qwerty\n");
    term_puts_t(term, "Choice: ");
    break;
  case 2:
    term_puts_t(term, "[3/8] Target disk\n");
    term_print_disks(term);
    term_puts_t(term, "  m. manual-later (do not write disk)\n");
    term_puts_t(term, "Choose target disk number, or m: ");
    break;
  case 3:
    term_puts_t(term, "[4/8] Disk strategy\n");
    term_puts_t(term,
                "  1. dual-boot-data (preserve disk, use selected partition for Orizon)\n");
    term_puts_t(term,
                "  2. dual-boot-esp (preserve disk, write /EFI/Orizon only)\n");
    term_puts_t(term,
                "  3. guided-full-disk (ERASE target disk, full Orizon install)\n");
    term_puts_t(term, "Choice [1]: ");
    break;
  case 4:
    term_puts_t(term, "[5/8] Orizon data partition\n");
    term_puts_t(term,
                "Choose the empty/prepared partition Orizon may claim and overwrite.\n");
    term_print_partitions(term);
    term_puts_t(term, "Partition number: ");
    break;
  case 5:
    term_puts_t(term, "[6/8] Hostname\n");
    term_puts_t(term, "Hostname [orizon-os]: ");
    break;
  case 6:
    term_puts_t(term, "[7/8] Desktop environment\n");
    term_puts_t(term,
                "Install optional Hyprland-style desktop profile now?\n");
    term_puts_t(term,
                "  1. no  - keep the minimal console base (default)\n");
    term_puts_t(term,
                "  2. yes - install " ORIZON_DESKTOP_PACKAGE " profile\n");
    term_puts_t(term, "Choice [1]: ");
    break;
  case 7: {
    char line[160];
    term_puts_t(term, "[8/8] Summary\n");
    snprintf(line, sizeof(line), "  Language: %s\n", term->install_language);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Keyboard: %s\n", term->install_keyboard);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Disk:     %s\n",
             strcmp(term->install_disk_mode, "manual-later") == 0
                 ? "manual-later"
                 : term->install_disk_summary);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Hostname: %s\n", term->install_hostname);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Desktop:  %s\n", term->install_desktop);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Mode:     %s\n", term->install_disk_mode);
    term_puts_t(term, line);
    if (strcmp(term->install_disk_mode, "dual-boot-data") == 0) {
      snprintf(line, sizeof(line), "  Data:     %s\n",
               term->install_data_partition_summary);
      term_puts_t(term, line);
    }
    if (strcmp(term->install_disk_mode, "manual-later") == 0) {
      term_puts_t(term,
                  "  Preflight: /workspace/.orizon/install-report.txt will be saved before any disk write.\n");
      term_puts_t(term, "Type SAVE to store the plan, or cancel to abort: ");
    } else if (strcmp(term->install_disk_mode, "dual-boot-data") == 0) {
      term_puts_t(term,
                  "  Preflight: /workspace/.orizon/install-report.txt will be saved before any disk write.\n");
      snprintf(line, sizeof(line),
               "Type DUALDATA %s %s to claim that partition, or cancel to abort: ",
               term->install_disk_name, term->install_data_partition_name);
      term_puts_t(term, line);
    } else if (strcmp(term->install_disk_mode, "dual-boot-esp") == 0) {
      term_puts_t(term,
                  "  Preflight: /workspace/.orizon/install-report.txt will be saved before any disk write.\n");
      snprintf(line, sizeof(line),
               "Type DUALBOOT %s to write /EFI/Orizon only, or cancel to abort: ",
               term->install_disk_name);
      term_puts_t(term, line);
    } else {
      term_puts_t(term,
                  "  Preflight: /workspace/.orizon/install-report.txt will be saved before any disk write.\n");
      snprintf(line, sizeof(line),
               "Type ERASE %s to write this disk, or cancel to abort: ",
               term->install_disk_name);
      term_puts_t(term, line);
    }
    break;
  }
  default:
    break;
  }
  term_prepare_input(term);
}

static void term_install_finish(terminal_t *term, int success) {
  term->install_mode = 0;
  term->install_step = 0;
  if (success) {
    term_puts_t(term, "\nInstaller finished.\n");
  } else {
    term_puts_t(term, "\nInstaller stopped.\n");
  }
}

static void term_install_write_plan(terminal_t *term) {
  char plan[2048];
  char state[256];
  char marker[256];
  static char install_report[4096];
  orizon_install_config_t config;
  const char *disk_name =
      term->install_disk_name[0] ? term->install_disk_name : "none";
  const char *disk_summary =
      term->install_disk_summary[0] ? term->install_disk_summary : "none";
  const char *data_name = term->install_data_partition_name[0]
                              ? term->install_data_partition_name
                              : "none";
  const char *data_summary = term->install_data_partition_summary[0]
                                 ? term->install_data_partition_summary
                                 : "none";

  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/system");
  vfs_mkdir("/home");
  vfs_mkdir("/home/orizon");
  vfs_mkdir("/packages");
  vfs_mkdir("/logs");

  config.language = term->install_language;
  config.keyboard = term->install_keyboard;
  config.disk_mode = term->install_disk_mode;
  config.hostname = term->install_hostname;
  config.desktop_profile = term->install_desktop;
  config.disk_index = term->install_disk_index;
  config.disk_name = term->install_disk_name;
  config.data_partition_index = term->install_data_partition_index;
  orizon_install_format_plan(&config, install_report, sizeof(install_report));

  snprintf(plan, sizeof(plan),
           "installer-version 1\n"
           "os Orizon OS\n"
           "source live-iso\n"
           "preflight-report /workspace/.orizon/install-report.txt\n"
           "language %s\n"
           "keyboard %s\n"
           "hostname %s\n"
           "desktop %s\n"
           "desktop-package %s\n"
           "disk-mode %s\n"
           "disk-index %d\n"
           "disk-name %s\n"
           "disk-summary %s\n"
           "data-partition-index %d\n"
           "data-partition-name %s\n"
           "data-partition-summary %s\n"
           "disk-status %s\n"
           "boot-strategy %s\n"
           "write-mode %s\n"
           "next reboot-installed-disk\n",
           term->install_language, term->install_keyboard,
           term->install_hostname, term->install_desktop,
           ORIZON_DESKTOP_PACKAGE, term->install_disk_mode,
           term->install_disk_index, disk_name, disk_summary,
           term->install_data_partition_index, data_name, data_summary,
           storage_available() ? storage_status() : "unavailable",
           strcmp(term->install_disk_mode, "dual-boot-data") == 0
               ? "side-by-side-existing-esp-plus-orizon-data"
               : (strcmp(term->install_disk_mode, "dual-boot-esp") == 0
                      ? "side-by-side-existing-esp"
                      : "uefi-fallback-esp"),
           strcmp(term->install_disk_mode, "manual-later") == 0
               ? "plan-only-no-disk-write"
               : (strcmp(term->install_disk_mode, "dual-boot-data") == 0
                      ? "existing-esp-selected-partition-reused"
                      : (strcmp(term->install_disk_mode, "dual-boot-esp") == 0
                             ? "non-destructive-existing-esp"
                             : "destructive-full-disk")));

  snprintf(state, sizeof(state),
           "install configured: language=%s keyboard=%s disk=%s hostname=%s desktop=%s\n",
           term->install_language, term->install_keyboard,
           term->install_disk_mode, term->install_hostname,
           term->install_desktop);

  if (term_write_text_file("/workspace/.orizon/install-plan", plan) < 0 ||
      term_write_text_file("/workspace/.orizon/install-state", state) < 0 ||
      term_write_text_file("/workspace/.orizon/install-report.txt",
                           install_report) < 0 ||
      term_write_text_file("/workspace/.orizon/keyboard",
                           term->install_keyboard) < 0 ||
      term_write_text_file("/system/install-state", state) < 0 ||
      term_write_text_file("/system/hostname", term->install_hostname) < 0 ||
      term_write_text_file("/system/locale", term->install_language) < 0 ||
      term_write_text_file("/system/keyboard", term->install_keyboard) < 0) {
    term_puts_t(term, "\ninstall: failed to write staging files\n");
    term_install_finish(term, 0);
    return;
  }
  term_write_text_file("/system/data-layout",
                       "version 1\nroots /system /home /packages /logs /workspace\n");
  term_write_text_file("/home/orizon/README.txt",
                       "Home directory for Orizon OS user files.\n");
  term_write_text_file("/packages/README.txt",
                       "Local package cache and installed package metadata.\n");
  if (strcmp(term->install_disk_mode, "manual-later") == 0) {
    term_write_text_file("/workspace/.orizon/desktop-profile",
                         term->install_desktop);
  } else if (strcmp(term->install_desktop, ORIZON_DESKTOP_PROFILE) == 0) {
    char desktop_status[512];
    orizon_desktop_set_enabled(1, desktop_status, sizeof(desktop_status));
    gui_desktop_set_enabled(1);
    term_write_text_file("/workspace/.orizon/desktop-profile",
                         term->install_desktop);
    term_write_text_file("/workspace/.orizon/desktop-state",
                         desktop_status);
  } else {
    orizon_desktop_set_enabled(0, NULL, 0);
    term_write_text_file("/workspace/.orizon/desktop-profile", "none\n");
  }

  if (strcmp(term->install_disk_mode, "manual-later") == 0) {
    vfs_persist_save();
    term_puts_t(term, "\nInstaller plan saved for manual disk work.\n");
    term_install_finish(term, 1);
    return;
  }

  if (strcmp(term->install_disk_mode, "guided-full-disk") == 0) {
    term_puts_t(term, "\nPreparing /workspace for disk install...\n");
    if (vfs_persist_save() < 0) {
      term_puts_t(term,
                  "install: persistence not active yet; will save after layout creation\n");
    }
  } else {
    term_puts_t(term, "\nPreparing non-destructive ESP write...\n");
  }

  term_puts_t(term, "\n");
  if (orizon_install_run(&config, install_report, sizeof(install_report)) == 0) {
    term_puts_t(term, install_report);
    term_write_text_file("/workspace/.orizon/install-log", install_report);
    if (strcmp(term->install_disk_mode, "dual-boot-esp") == 0) {
      term_write_text_file("/workspace/.orizon/dualboot-prepared",
                           "dual boot ESP prepared\n"
                           "boot-file /EFI/Orizon/BOOTX64.EFI\n"
                           "data not-installed\n"
                           "desktop see-install-plan\n");
      term_write_text_file("/workspace/.orizon/install-state",
                           "dual boot ESP prepared\n");
      term_install_finish(term, 1);
      term_puts_t(term,
                  "Dual boot files are ready on the existing ESP.\n"
                  "Reboot, keep your main OS intact, then choose /EFI/Orizon/BOOTX64.EFI from firmware boot-file selection or add a firmware/BCD entry.\n"
                  "No Orizon data partition was created, so update/pkg remain disabled for safety.\n");
      return;
    }
    if (vfs_persist_enable_installed() < 0) {
      term_puts_t(term,
                  "install: warning, Orizon data partition was not enabled\n");
    }
    snprintf(marker, sizeof(marker),
             "Orizon OS installed\nlanguage=%s\nkeyboard=%s\nhostname=%s\n"
             "desktop=%s\nmode=%s\ndata-partition=%s\n"
             "next=shutdown-remove-installer\n",
             term->install_language, term->install_keyboard,
             term->install_hostname, term->install_desktop,
             term->install_disk_mode, data_name);
    term_write_text_file("/workspace/.orizon/installed", marker);
    term_write_text_file("/workspace/.orizon/install-state",
                         "install complete\nnext shutdown-remove-installer\n");
    term_write_text_file("/workspace/.orizon/keyboard",
                         term->install_keyboard);
    term_write_text_file("/system/hostname", term->install_hostname);
    term_write_text_file("/system/install-state", "install complete\n");
    term_write_text_file("/system/installed", "1\n");
    klog_persist_boot_if_installed();
    vfs_persist_save();
    term_install_finish(term, 1);
    if (strcmp(term->install_disk_mode, "dual-boot-data") == 0) {
      term_puts_t(term,
                  "SHUTDOWN in 5 seconds.\n"
                  "Remove/eject the ISO or USB installer before the next boot.\n"
                  "Then start the machine and choose /EFI/Orizon/BOOTX64.EFI from firmware boot selection.\n");
    } else {
      term_puts_t(term,
                  "SHUTDOWN in 5 seconds.\n"
                  "Remove/eject the ISO or USB installer before the next boot.\n"
                  "Then start the machine again to boot from the installed disk.\n");
    }
    power_schedule_shutdown(TIMER_HZ * 5);
  } else {
    term_puts_t(term, install_report);
    term_puts_t(term, "install: failed before marking disk bootable\n");
    term_install_finish(term, 0);
  }
}

static void term_install_submit(terminal_t *term, const char *line) {
  const char *value = term_skip_spaces(line);

  if (term_install_value_is(value, "cancel", "quit", "q")) {
    term_install_finish(term, 0);
    return;
  }

  switch (term->install_step) {
  case 0:
    if (term_install_value_is(value, "1", "fr", "francais") ||
        strcmp(value, "&") == 0) {
      strcpy(term->install_language, "fr_FR");
    } else if (term_install_value_is(value, "2", "en", "english") ||
               strcmp(value, "e") == 0) {
      strcpy(term->install_language, "en_US");
    } else {
      term_puts_t(term, "Choose 1 or 2.\n");
      term_install_prompt(term);
      return;
    }
    term->install_step++;
    term_install_prompt(term);
    return;
  case 1:
    if (term_install_value_is(value, "1", "fr", "azerty") ||
        strcmp(value, "&") == 0) {
      strcpy(term->install_keyboard, "fr-azerty");
    } else if (term_install_value_is(value, "2", "us", "qwerty") ||
               strcmp(value, "e") == 0) {
      strcpy(term->install_keyboard, "us-qwerty");
    } else {
      term_puts_t(term, "Choose 1 or 2.\n");
      term_install_prompt(term);
      return;
    }
    input_set_keyboard_layout(term->install_keyboard);
    term_puts_t(term, "Keyboard layout active: ");
    term_puts_t(term, input_keyboard_layout());
    term_puts_t(term, "\n");
    term->install_step++;
    term_install_prompt(term);
    return;
  case 2:
    if (term_install_value_is(value, "m", "manual", "later") ||
        strcmp(value, "manual-later") == 0) {
      strcpy(term->install_disk_mode, "manual-later");
      term->install_disk_index = -1;
      strcpy(term->install_disk_name, "none");
      strcpy(term->install_disk_summary, "manual-later");
      term->install_data_partition_index = -1;
      term->install_data_partition_name[0] = '\0';
      term->install_data_partition_summary[0] = '\0';
      term->install_step = 5;
      term_install_prompt(term);
      return;
    } else {
      int choice = 0;
      if (term_parse_uint(value, &choice) < 0 ||
          term_install_capture_disk(choice - 1, term) < 0) {
        term_puts_t(term, "Choose a listed disk number, or m.\n");
        term_install_prompt(term);
        return;
      }
      term_puts_t(term, "Selected target: ");
      term_puts_t(term, term->install_disk_summary);
      term_puts_t(term, "\n");
    }
    term->install_step++;
    term_install_prompt(term);
    return;
  case 3:
    if (*value == '\0' || term_install_value_is(value, "1", "dual", "dualboot") ||
        strcmp(value, "dual-boot-data") == 0 ||
        strcmp(value, "data") == 0) {
      strcpy(term->install_disk_mode, "dual-boot-data");
      term->install_step = 4;
      term_install_prompt(term);
      return;
    } else if (term_install_value_is(value, "2", "esp", "boot") ||
        strcmp(value, "dual-boot-esp") == 0) {
      strcpy(term->install_disk_mode, "dual-boot-esp");
      term->install_step = 5;
      term_install_prompt(term);
      return;
    } else if (term_install_value_is(value, "3", "full", "erase") ||
               strcmp(value, "guided-full-disk") == 0) {
      strcpy(term->install_disk_mode, "guided-full-disk");
      term->install_step = 5;
      term_install_prompt(term);
      return;
    } else {
      term_puts_t(term, "Choose 1 for dual-data, 2 for ESP only, or 3 for full erase install.\n");
      term_install_prompt(term);
      return;
    }
  case 4:
    if (strcmp(term->install_disk_mode, "dual-boot-data") != 0) {
      term->install_step = 5;
      term_install_prompt(term);
      return;
    } else {
      int choice = 0;
      if (term_parse_uint(value, &choice) < 0 ||
          term_install_capture_data_partition(choice, term) < 0) {
        term_puts_t(term,
                    "Choose a listed [data-candidate] partition prepared for Orizon.\n");
        term_install_prompt(term);
        return;
      }
      term_puts_t(term, "Selected Orizon data target: ");
      term_puts_t(term, term->install_data_partition_summary);
      term_puts_t(term, "\n");
      term->install_step = 5;
      term_install_prompt(term);
      return;
    }
  case 5:
    if (*value == '\0') {
      strcpy(term->install_hostname, "orizon-os");
    } else {
      strncpy(term->install_hostname, value,
              sizeof(term->install_hostname) - 1);
      term->install_hostname[sizeof(term->install_hostname) - 1] = '\0';
    }
    term->install_step++;
    term_install_prompt(term);
    return;
  case 6:
    if (*value == '\0' || term_install_value_is(value, "1", "no", "n") ||
        term_install_value_is(value, "non", "none", "console")) {
      strcpy(term->install_desktop, "none");
    } else if (term_install_value_is(value, "2", "yes", "y") ||
               term_install_value_is(value, "oui", "desktop", "hypr") ||
               term_install_value_is(value, "hyprland",
                                     ORIZON_DESKTOP_PROFILE, NULL)) {
      strcpy(term->install_desktop, ORIZON_DESKTOP_PROFILE);
    } else {
      term_puts_t(term, "Choose 1/no or 2/yes.\n");
      term_install_prompt(term);
      return;
    }
    term->install_step++;
    term_install_prompt(term);
    return;
  case 7:
    if (strcmp(term->install_disk_mode, "manual-later") == 0 &&
        (strcmp(value, "SAVE") == 0 || strcmp(value, "save") == 0)) {
      term_install_write_plan(term);
    } else if (strcmp(term->install_disk_mode, "dual-boot-data") == 0) {
      char expected[96];
      char expected_lower[96];
      snprintf(expected, sizeof(expected), "DUALDATA %s %s",
               term->install_disk_name, term->install_data_partition_name);
      snprintf(expected_lower, sizeof(expected_lower), "dualdata %s %s",
               term->install_disk_name, term->install_data_partition_name);
      if (strcmp(value, expected) == 0 || strcmp(value, expected_lower) == 0) {
        term_install_write_plan(term);
      } else {
        term_puts_t(term,
                    "Confirmation refused. Type the exact DUALDATA command.\n");
        term_install_prompt(term);
      }
    } else if (strcmp(term->install_disk_mode, "dual-boot-esp") == 0) {
      char expected[72];
      char expected_lower[72];
      snprintf(expected, sizeof(expected), "DUALBOOT %s",
               term->install_disk_name);
      snprintf(expected_lower, sizeof(expected_lower), "dualboot %s",
               term->install_disk_name);
      if (strcmp(value, expected) == 0 || strcmp(value, expected_lower) == 0) {
        term_install_write_plan(term);
      } else {
        term_puts_t(term,
                    "Confirmation refused. Type the exact DUALBOOT command.\n");
        term_install_prompt(term);
      }
    } else if (strcmp(term->install_disk_mode, "guided-full-disk") == 0) {
      char expected[64];
      char expected_lower[64];
      snprintf(expected, sizeof(expected), "ERASE %s", term->install_disk_name);
      snprintf(expected_lower, sizeof(expected_lower), "erase %s",
               term->install_disk_name);
      if (strcmp(value, expected) == 0 || strcmp(value, expected_lower) == 0) {
        term_install_write_plan(term);
      } else {
        term_puts_t(term, "Confirmation refused. Type the exact ERASE command.\n");
        term_install_prompt(term);
      }
    } else {
      term_puts_t(term, "Confirmation refused.\n");
      term_install_prompt(term);
    }
    return;
  default:
    term_install_finish(term, 0);
    return;
  }
}

static void term_start_installer(terminal_t *term) {
  if (term_install_already_complete()) {
    term_puts_t(term, "\ninstall: Orizon OS is already installed.\n");
    term_puts_t(term,
                "Reinstall is disabled from this command to protect your disk and /workspace.\n");
    term_puts_t(term, "Use install-status to review the installed state.\n");
    return;
  }

  term->install_mode = 1;
  term->install_step = 0;
  term->install_language[0] = '\0';
  term->install_keyboard[0] = '\0';
  term->install_disk_mode[0] = '\0';
  term->install_disk_index = -1;
  term->install_disk_name[0] = '\0';
  term->install_disk_summary[0] = '\0';
  term->install_data_partition_index = -1;
  term->install_data_partition_name[0] = '\0';
  term->install_data_partition_summary[0] = '\0';
  strcpy(term->install_hostname, "orizon-os");
  strcpy(term->install_desktop, "none");
  term_puts_t(term, "\n");
  term_install_prompt(term);
}

static char *term_find_group_separator(char *cmd, int *sep_len) {
  if (sep_len) {
    *sep_len = 0;
  }
  while (cmd && *cmd) {
    if (*cmd == ';') {
      if (sep_len) {
        *sep_len = 1;
      }
      return cmd;
    }
    if (cmd[0] == '&' && cmd[1] == '&') {
      if (sep_len) {
        *sep_len = 2;
      }
      return cmd;
    }
    cmd++;
  }
  return NULL;
}

static int term_extract_redirect(char *cmd, char *path, size_t path_size,
                                 int *append) {
  char *p;
  char *arg;
  size_t len = 0;

  if (!cmd || !path || path_size == 0 || !append) {
    return 0;
  }
  path[0] = '\0';
  *append = 0;
  for (p = cmd; *p; p++) {
    if (*p == '>') {
      break;
    }
  }
  if (*p != '>') {
    return 0;
  }
  *append = p[1] == '>';
  *p = '\0';
  arg = term_trim_mut(p + (*append ? 2 : 1));
  while (arg[len] && arg[len] != ' ' && arg[len] != '\t') {
    len++;
  }
  if (len == 0 || len >= path_size) {
    return -1;
  }
  memcpy(path, arg, len);
  path[len] = '\0';
  arg = term_trim_mut(arg + len);
  if (*arg != '\0') {
    return -1;
  }
  term_trim_mut(cmd);
  return 1;
}

static char *term_find_pipe(char *cmd) {
  while (cmd && *cmd) {
    if (*cmd == '|') {
      return cmd;
    }
    cmd++;
  }
  return NULL;
}

static size_t term_text_append(char *out, size_t out_size, size_t used,
                               const char *text, size_t len) {
  if (!out || out_size == 0 || !text) {
    return used;
  }
  if (used >= out_size - 1) {
    return used;
  }
  if (len > out_size - 1 - used) {
    len = out_size - 1 - used;
  }
  memcpy(out + used, text, len);
  used += len;
  out[used] = '\0';
  return used;
}

static int term_line_contains_mode(const char *line, size_t len,
                                   const char *pattern, int ignore_case) {
  size_t pattern_len = strlen(pattern ? pattern : "");

  if (pattern_len == 0) {
    return 1;
  }
  if (!line || pattern_len > len) {
    return 0;
  }
  for (size_t i = 0; i + pattern_len <= len; i++) {
    size_t j = 0;
    while (j < pattern_len) {
      char a = line[i + j];
      char b = pattern[j];
      if (ignore_case) {
        a = term_ascii_lower(a);
        b = term_ascii_lower(b);
      }
      if (a != b) {
        break;
      }
      j++;
    }
    if (j == pattern_len) {
      return 1;
    }
  }
  return 0;
}

static void term_pipe_grep(const char *input, const char *pattern, char *out,
                           size_t out_size, int ignore_case, int invert,
                           int show_numbers) {
  const char *p = input ? input : "";
  size_t used = 0;
  int matches = 0;
  int line_no = 1;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  while (*p) {
    const char *line = p;
    size_t len = 0;
    while (line[len] && line[len] != '\n') {
      len++;
    }
    int found = term_line_contains_mode(line, len, pattern, ignore_case);
    if ((found && !invert) || (!found && invert)) {
      if (show_numbers) {
        char prefix[24];
        snprintf(prefix, sizeof(prefix), "%d:", line_no);
        used = term_text_append(out, out_size, used, prefix, strlen(prefix));
      }
      used = term_text_append(out, out_size, used, line, len);
      used = term_text_append(out, out_size, used, "\n", 1);
      matches++;
    }
    p = line + len;
    if (*p == '\n') {
      p++;
    }
    line_no++;
  }
  if (matches == 0) {
    term_text_append(out, out_size, used, "grep: no matches\n",
                     strlen("grep: no matches\n"));
  }
}

static const char *term_parse_grep_options(const char *args, int *ignore_case,
                                           int *invert, int *show_numbers) {
  args = term_skip_spaces(args);
  if (ignore_case) {
    *ignore_case = 0;
  }
  if (invert) {
    *invert = 0;
  }
  if (show_numbers) {
    *show_numbers = 0;
  }
  while (args && args[0] == '-' && args[1]) {
    const char *p = args + 1;
    int consumed = 0;
    while (*p && *p != ' ' && *p != '\t') {
      if (*p == 'i') {
        if (ignore_case) {
          *ignore_case = 1;
        }
        consumed = 1;
      } else if (*p == 'v') {
        if (invert) {
          *invert = 1;
        }
        consumed = 1;
      } else if (*p == 'n') {
        if (show_numbers) {
          *show_numbers = 1;
        }
        consumed = 1;
      } else {
        return args;
      }
      p++;
    }
    if (!consumed) {
      return args;
    }
    args = term_skip_spaces(p);
  }
  return args;
}

static void term_text_count_stats(const char *input, size_t *lines,
                                  size_t *words, size_t *bytes) {
  const char *p = input ? input : "";
  int in_word = 0;

  if (lines) {
    *lines = 0;
  }
  if (words) {
    *words = 0;
  }
  if (bytes) {
    *bytes = 0;
  }
  while (*p) {
    char c = *p++;
    if (bytes) {
      (*bytes)++;
    }
    if (c == '\n' && lines) {
      (*lines)++;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      in_word = 0;
    } else if (!in_word) {
      if (words) {
        (*words)++;
      }
      in_word = 1;
    }
  }
  if (bytes && *bytes > 0 && lines && input[*bytes - 1] != '\n') {
    (*lines)++;
  }
}

static void term_pipe_wc(const char *input, char *out, size_t out_size) {
  size_t lines = 0;
  size_t words = 0;
  size_t bytes = 0;

  if (!out || out_size == 0) {
    return;
  }
  term_text_count_stats(input, &lines, &words, &bytes);
  snprintf(out, out_size, "%lu lines %lu words %lu bytes\n",
           (unsigned long)lines, (unsigned long)words,
           (unsigned long)bytes);
}

static void term_pipe_head(const char *input, int max_lines, char *out,
                           size_t out_size) {
  const char *p = input ? input : "";
  size_t used = 0;
  int lines = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  while (*p && lines < max_lines) {
    const char *line = p;
    size_t len = 0;
    while (line[len] && line[len] != '\n') {
      len++;
    }
    used = term_text_append(out, out_size, used, line, len);
    if (line[len] == '\n') {
      used = term_text_append(out, out_size, used, "\n", 1);
    }
    lines++;
    p = line + len;
    if (*p == '\n') {
      p++;
    }
  }
}

static void term_pipe_tail(const char *input, int max_lines, char *out,
                           size_t out_size) {
  size_t len = strlen(input ? input : "");
  size_t start = len;
  int lines = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  while (start > 0 && lines <= max_lines) {
    start--;
    if (input[start] == '\n') {
      lines++;
      if (lines > max_lines) {
        start++;
        break;
      }
    }
  }
  term_text_append(out, out_size, 0, (input ? input : "") + start,
                   len - start);
}

static int term_parse_pipe_line_count(const char *args, int *lines) {
  args = term_skip_spaces(args);
  *lines = 10;
  if (*args == '\0') {
    return 0;
  }
  if (*args == '-') {
    args++;
  }
  if (term_parse_uint(args, lines) < 0) {
    return -1;
  }
  while (*args >= '0' && *args <= '9') {
    args++;
  }
  return *term_skip_spaces(args) == '\0' ? 0 : -1;
}

static int term_pipe_tee(terminal_t *term, const char *path_arg,
                         const char *input, char *out, size_t out_size,
                         int append);

static int term_apply_pipe_stage(terminal_t *term, char *stage,
                                 const char *input, char *out,
                                 size_t out_size, int *opened_pager) {
  stage = term_trim_mut(stage);
  if (!stage || *stage == '\0') {
    term_puts_t(term, "pipe: missing command after '|'\n");
    return -1;
  }
  if (term_command_is(stage, "grep")) {
    int ignore_case = 0;
    int invert = 0;
    int show_numbers = 0;
    const char *pattern =
        term_parse_grep_options(stage + 4, &ignore_case, &invert,
                                &show_numbers);
    if (*pattern == '\0') {
      term_puts_t(term, "usage: cmd | grep [-i] [-v] [-n] <text>\n");
      return -1;
    }
    term_pipe_grep(input, pattern, out, out_size, ignore_case, invert,
                   show_numbers);
    return 0;
  }
  if (term_command_is(stage, "head")) {
    int lines = 10;
    if (term_parse_pipe_line_count(stage + 4, &lines) < 0) {
      term_puts_t(term, "usage: cmd | head [-n]\n");
      return -1;
    }
    term_pipe_head(input, lines, out, out_size);
    return 0;
  }
  if (term_command_is(stage, "tail")) {
    int lines = 10;
    if (term_parse_pipe_line_count(stage + 4, &lines) < 0) {
      term_puts_t(term, "usage: cmd | tail [-n]\n");
      return -1;
    }
    term_pipe_tail(input, lines, out, out_size);
    return 0;
  }
  if (term_command_is(stage, "less") || term_command_is(stage, "more")) {
    term_start_pager_text(term, "pipeline", input, strlen(input),
                          term_capture_truncated);
    if (opened_pager) {
      *opened_pager = 1;
    }
    return 0;
  }
  if (term_command_is(stage, "cat")) {
    snprintf(out, out_size, "%s", input ? input : "");
    return 0;
  }
  if (term_command_is(stage, "wc")) {
    term_pipe_wc(input, out, out_size);
    return 0;
  }
  if (term_command_is(stage, "tee")) {
    const char *args = term_skip_spaces(stage + 3);
    int append = 0;
    if (term_starts_with(args, "-a") &&
        (args[2] == '\0' || args[2] == ' ' || args[2] == '\t')) {
      append = 1;
      args = term_skip_spaces(args + 2);
    }
    return term_pipe_tee(term, args, input, out, out_size, append);
  }
  term_puts_t(term,
              "pipe: supported stages are grep, head, tail, wc, tee, less\n");
  return -1;
}

static int term_write_redirect_output(terminal_t *term, const char *path_arg,
                                      const char *text, int append) {
  char path[MAX_PATH];
  file_t *f;
  size_t len = strlen(text ? text : "");

  if (resolve_path(term->cwd, path_arg, path, sizeof(path)) < 0) {
    term_puts_t(term, "redirect: invalid path\n");
    return -1;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC));
  if (!f) {
    term_puts_t(term, "redirect: cannot open target\n");
    return -1;
  }
  if (len > 0 && vfs_write(f, text, len) != (ssize_t)len) {
    vfs_close(f);
    term_puts_t(term, "redirect: write failed\n");
    return -1;
  }
  vfs_close(f);
  term_puts_t(term, append ? "redirect: appended " : "redirect: wrote ");
  term_puts_t(term, path);
  term_puts_t(term, "\n");
  return 0;
}

static int term_pipe_tee(terminal_t *term, const char *path_arg,
                         const char *input, char *out, size_t out_size,
                         int append) {
  if (!path_arg || *term_skip_spaces(path_arg) == '\0') {
    term_puts_t(term, "usage: cmd | tee [-a] <file>\n");
    return -1;
  }
  if (term_write_redirect_output(term, term_skip_spaces(path_arg),
                                 input ? input : "", append) < 0) {
    return -1;
  }
  snprintf(out, out_size, "%s", input ? input : "");
  return 0;
}

static void term_execute_pipeline_or_redirect(terminal_t *term, char *cmd) {
  char redirect_path[MAX_PATH];
  char *pipe_pos;
  int append = 0;
  int redirect;
  int opened_pager = 0;

  cmd = term_trim_mut(cmd);
  if (!cmd || *cmd == '\0') {
    return;
  }
  redirect = term_extract_redirect(cmd, redirect_path, sizeof(redirect_path),
                                   &append);
  if (redirect < 0) {
    term_puts_t(term, "usage: <command> > <file> | <command> >> <file>\n");
    return;
  }
  cmd = term_trim_mut(cmd);
  pipe_pos = term_find_pipe(cmd);
  if (!pipe_pos && redirect == 0) {
    term_execute_single(term, cmd);
    return;
  }
  if (term_command_is_interactive(cmd)) {
    term_puts_t(term,
                "shell: this interactive command cannot be piped or redirected\n");
    return;
  }

  if (pipe_pos) {
    char *current = term_pipe_buf;
    char *next = term_pipe_tmp;
    char *stage;

    *pipe_pos = '\0';
    cmd = term_trim_mut(cmd);
    stage = pipe_pos + 1;
    if (*cmd == '\0') {
      term_puts_t(term, "pipe: missing command before '|'\n");
      return;
    }
    if (term_command_is_interactive(cmd)) {
      term_puts_t(term,
                  "shell: this interactive command cannot be piped\n");
      return;
    }
    term_capture_begin(term_pipe_buf, sizeof(term_pipe_buf));
    term_execute_single(term, cmd);
    term_capture_end();
    while (stage && *stage) {
      char *next_pipe = term_find_pipe(stage);
      if (next_pipe) {
        *next_pipe = '\0';
      }
      if (term_apply_pipe_stage(term, stage, current, next, sizeof(term_pipe_tmp),
                                &opened_pager) < 0) {
        return;
      }
      if (opened_pager) {
        return;
      }
      current = next;
      next = (next == term_pipe_tmp) ? term_pipe_buf : term_pipe_tmp;
      stage = next_pipe ? next_pipe + 1 : NULL;
    }
    if (redirect > 0) {
      term_write_redirect_output(term, redirect_path, current, append);
    } else {
      term_puts_t(term, current);
      if (current[0] && current[strlen(current) - 1] != '\n') {
        term_puts_t(term, "\n");
      }
    }
    return;
  }

  term_capture_begin(term_pipe_buf, sizeof(term_pipe_buf));
  term_execute_single(term, cmd);
  term_capture_end();
  term_write_redirect_output(term, redirect_path, term_pipe_buf, append);
}

static void term_execute_command_groups(terminal_t *term, char *cmd) {
  char *cursor = cmd;

  while (cursor && *cursor) {
    int sep_len = 0;
    char *sep = term_find_group_separator(cursor, &sep_len);
    char *segment = cursor;
    if (sep) {
      *sep = '\0';
      cursor = sep + sep_len;
    } else {
      cursor = NULL;
    }
    segment = term_trim_mut(segment);
    if (*segment) {
      term_execute_pipeline_or_redirect(term, segment);
    }
  }
}

static void term_get_render_cell(terminal_t *term, int row, int col, char *ch,
                                 uint8_t *fg, uint8_t *bg) {
  int total = term->scroll_count + TERM_ROWS;
  int start = total - TERM_ROWS - term->scroll_offset;
  int line;
  int idx;

  if (start < 0) {
    start = 0;
  }
  line = start + row;
  if (line < term->scroll_count) {
    idx = line * TERM_COLS + col;
    *ch = term->scroll_chars[idx];
    *fg = term->scroll_fg[idx];
    *bg = term->scroll_bg[idx];
  } else {
    idx = (line - term->scroll_count) * TERM_COLS + col;
    *ch = term->chars[idx];
    *fg = term->fg_colors[idx];
    *bg = term->bg_colors[idx];
  }
}

/* Render terminal */
void term_render(terminal_t *term) {
  if (!term || !term->visible) return;
  
  int base_x = term->content_x + TERM_PADDING;
  int base_y = term->content_y + TERM_PADDING;
  
  /* Background */
  fb_fill_rect(term->content_x, term->content_y,
               TERM_COLS * TERM_CHAR_W + TERM_PADDING * 2,
               TERM_ROWS * TERM_CHAR_H + TERM_PADDING * 2, term_colors[0]);
  
  /* Characters */
  for (int row = 0; row < TERM_ROWS; row++) {
    for (int col = 0; col < TERM_COLS; col++) {
      char ch;
      uint8_t fg;
      uint8_t bg;
      term_get_render_cell(term, row, col, &ch, &fg, &bg);
      term_draw_char(base_x + col * TERM_CHAR_W, base_y + row * TERM_CHAR_H,
                     ch, term_colors[fg & 0xF], term_colors[bg & 0xF]);
    }
  }
  
  /* Cursor */
  static int blink = 0;
  blink++;
  if (!term->pager_mode && term->scroll_offset == 0 && (blink / 15) % 2 == 0) {
    int cx = base_x + term->cursor_x * TERM_CHAR_W;
    int cy = base_y + term->cursor_y * TERM_CHAR_H;
    fb_fill_rect(cx, cy + TERM_CHAR_H - 2, TERM_CHAR_W, 2, term_colors[7]);
  }
}

static void term_print_help_shell(terminal_t *term) {
  term_puts_t(term, "\033[1;36mOrizon shell helpers\033[0m\n");
  term_puts_t(term, "  help shell       - Show shell operators and shortcuts\n");
  term_puts_t(term, "  shell status     - Show console buffers/capabilities\n");
  term_puts_t(term, "  cmd1 ; cmd2      - Run commands sequentially\n");
  term_puts_t(term, "  cmd > file       - Write command output to a file\n");
  term_puts_t(term, "  cmd >> file      - Append command output to a file\n");
  term_puts_t(term, "  cmd | grep [-i] [-v] [-n] text - Filter captured output\n");
  term_puts_t(term, "  cmd | head -20   - Keep the first lines\n");
  term_puts_t(term, "  cmd | tail -20   - Keep the last lines\n");
  term_puts_t(term, "  cmd | wc         - Count lines/words/bytes\n");
  term_puts_t(term, "  cmd | tee [-a] file - Save output and keep piping\n");
  term_puts_t(term, "  cmd | less       - Open captured output in the pager\n");
  term_puts_t(term, "  history grep text - Search saved command history\n");
  term_puts_t(term, "\n");
  term_puts_t(term, "Notes: pipes are intentionally small and diagnostic-focused.\n");
  term_puts_t(term, "Interactive commands such as less/edit/install/reboot cannot be piped.\n");
}

static void term_print_shell_status(terminal_t *term) {
  char line[160];

  term_puts_t(term, "\033[1;36mOrizon local shell\033[0m\n");
  term_puts_t(term, "mode framebuffer-console\n");
  snprintf(line, sizeof(line), "cwd %s\n", term->cwd[0] ? term->cwd : "/");
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "history count=%d max=%d path=%s\n",
           term->history_count, TERM_HISTORY_MAX, TERM_HISTORY_PATH);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "scrollback lines=%d max=%d key-step=%d\n",
           term->scroll_count, TERM_SCROLLBACK_LINES, TERM_KEY_SCROLL_LINES);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "pager buffer=%lu bytes page-lines=%d\n",
           (unsigned long)TERM_PAGER_MAX, TERM_PAGER_PAGE_LINES);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "pipeline buffer=%lu bytes stages=grep/head/tail/wc/tee/cat/less\n",
           (unsigned long)TERM_PIPE_MAX);
  term_puts_t(term, line);
  term_puts_t(term, "operators: ; && > >> |\n");
  term_puts_t(term, "grep-options: -i case-insensitive, -v invert, -n line numbers\n");
  term_puts_t(term, "editor: edit <file> with .show/.insert/.replace/.del/.save/.q\n");
  term_puts_t(term, "limits: no quoting, variables, globbing, subshells, or POSIX exit chaining yet\n");
}

/* Execute one parsed command */
static void term_execute_single(terminal_t *term, const char *cmd) {
  /* Skip whitespace */
  while (*cmd == ' ') cmd++;
  if (*cmd == '\0') return;

  if (term_command_is(cmd, "help")) {
    const char *topic = term_skip_spaces(cmd + 4);
    if (term_command_is(topic, "shell")) {
      term_print_help_shell(term);
      return;
    }
    term_puts_t(term, "\033[1;36mOrizon OS Core Console\033[0m\n");
    term_puts_t(term, "\033[33mFile Commands:\033[0m\n");
    term_puts_t(term, "  ls        - List directory contents\n");
    term_puts_t(term, "  cd <dir>  - Change directory\n");
    term_puts_t(term, "  pwd       - Print working directory\n");
    term_puts_t(term, "  cat <f>   - Display file contents\n");
    term_puts_t(term, "  less <f>  - Page a file (z/s, arrows, space, q)\n");
    term_puts_t(term, "  head [-n] <f> - Show first lines\n");
    term_puts_t(term, "  tail [-n] <f> - Show last lines\n");
    term_puts_t(term, "  grep [-i] [-v] [-n] <text> <f> - Search file text\n");
    term_puts_t(term, "  wc <f>    - Count lines, words and bytes\n");
    term_puts_t(term, "  find [p] [text] - Find entries\n");
    term_puts_t(term, "  stat <p>  - Show file or directory info\n");
    term_puts_t(term, "  tree [p]  - Show a small directory tree\n");
    term_puts_t(term, "  cp <s> <d> - Copy a file\n");
    term_puts_t(term, "  mv <s> <d> - Move or rename a file/dir\n");
    term_puts_t(term, "  edit <f>  - Edit a text file (.help inside)\n");
    term_puts_t(term, "  touch <f> - Create empty file\n");
    term_puts_t(term, "  write <f> <text>  - Replace file text\n");
    term_puts_t(term, "  append <f> <text> - Append file text\n");
    term_puts_t(term, "  mkdir <d> - Create directory\n");
    term_puts_t(term, "  rm <f>    - Remove file\n");
    term_puts_t(term, "  sync      - Save Orizon data roots to disk\n");
    term_puts_t(term, "  persist [status|slots|save|repair|restore] - Manage data snapshots\n");
    term_puts_t(term, "\033[33mPackages:\033[0m\n");
    term_puts_t(term, "  pkg list/status - Show installed package data\n");
    term_puts_t(term, "  pkg audit/doctor - Audit or diagnose package v5 state\n");
    term_puts_t(term, "  pkg search <q>  - Search builtin/installed/remote packages\n");
    term_puts_t(term, "  pkg remote      - Show cached signed remote package index\n");
    term_puts_t(term, "  pkg remote verify - Validate cached remote index\n");
    term_puts_t(term, "  pkg upgrade plan - Show signed upgrade plan\n");
    term_puts_t(term, "  pkg info <name> - Show package metadata/files\n");
    term_puts_t(term, "  pkg history    - Show package transaction history\n");
    term_puts_t(term,
                "  pkg sample [desktop|desktop-module] - Create a sample .opkg package\n");
    term_puts_t(term, "  pkg hash <file> - Print package payload sha256\n");
    term_puts_t(term, "  pkg verify <file> - Verify package hash/dependencies\n");
    if (term_install_already_complete()) {
      term_puts_t(term, "  pkg update      - Refresh packages through signed update\n");
      term_puts_t(term, "  pkg upgrade     - Plan then refresh packages through signed update\n");
      term_puts_t(term,
                  "  pkg install <file|desktop-package> - Install a verified package\n");
      term_puts_t(term, "  pkg remove <name> - Remove an installed package\n");
      term_puts_t(term, "  pkg rollback <name> - Restore last removed package snapshot\n");
    }
    term_puts_t(term, "\033[33mSystem:\033[0m\n");
    term_puts_t(term,
                "  desktop [start|stop|restart|reload|status|settings|config|doctor|logs|package] - Optional Hyprland-style desktop\n");
    term_puts_t(term, "  desktop dispatch exec terminal / killactive - Tiled desktop clients\n");
    term_puts_t(term, "  dmesg     - Show current kernel boot log\n");
    term_puts_t(term, "  system [status|health|snapshot|backup|init|services|logs|doctor|repair|rescue|firstboot done] - Installed/live admin\n");
    term_puts_t(term, "  system health - Concise PASS/WARN installed-state summary\n");
    term_puts_t(term, "  system snapshot - Write /workspace/.orizon/system-snapshot.txt\n");
    term_puts_t(term, "  system backup - Export non-secret config to admin-backup.txt\n");
    term_puts_t(term, "  services  - Show simple init/service policy\n");
    term_puts_t(term, "  system logs - Show boot-state, service-state and init logs\n");
    term_puts_t(term, "  doctor    - Audit roots/config/init state\n");
    term_puts_t(term, "  rescue    - Non-destructive recovery checklist\n");
    term_puts_t(term, "  sysinfo   - Compact OS/hardware/storage summary\n");
    term_puts_t(term, "  hw        - Hardware diagnostics\n");
    term_puts_t(term, "  hw next   - Hardware capture plan for future real-PC tests\n");
    term_puts_t(term, "  pci [bars] - List PCI devices and driver hints\n");
    term_puts_t(term, "  usb [rescan] - Show USB/HID/USB-Ethernet diagnostics\n");
    term_puts_t(term, "  input     - Keyboard/pointer/input bus diagnostics\n");
    term_puts_t(term, "  logs [name] - Read boot/storage/pci/network/usb/wifi/ssh/security/update logs\n");
    term_puts_t(term, "  report    - Compact health report + log tail\n");
    term_puts_t(term, "  report save - Write /workspace/hardware-report.txt\n");
    term_puts_t(term, "  selftest [network|storage|crypto|ssh|update] - Non-destructive checks\n");
    term_puts_t(term,
                "  security [policy|audit|keys|doctor|rotate ssh-hostkey] - Show hardening posture\n");
    term_puts_t(term, "  mounts    - Show Orizon data roots\n");
    term_puts_t(term, "  storage   - Show disk and persistence state\n");
    term_puts_t(term, "  disks     - List detected install disks\n");
    term_puts_t(term, "  disk identify | disk read-test [lba|last] - Read-only disk diagnostics\n");
    term_puts_t(term, "  gpt scan  - Read-only GPT partition scan\n");
    term_puts_t(term, "  partitions - List GPT partitions on selected disk\n");
    term_puts_t(term, "  storage select <n> - Select active disk\n");
    term_puts_t(term, "  storage diag/check - Explain and verify VM storage read-only\n");
    term_puts_t(term, "  net       - Show ethernet/IP status\n");
    term_puts_t(term, "  net dhcp  - Request IPv4 config from DHCP\n");
    term_puts_t(term,
                "  net check/renew/tcp/daily/tls/diag - VM network diagnostics\n");
    term_puts_t(term, "  net auto/reset/status - Manage IPv4 state\n");
    term_puts_t(term, "  net config ip <ip> gateway <gw> dns <dns> [subnet <mask>]\n");
    term_puts_t(term, "  wifi      - Show Wi-Fi hardware status\n");
    term_puts_t(term, "  wifi hw   - Probe Intel Wi-Fi CSR/MMIO registers\n");
    term_puts_t(term, "  wifi apm  - Wake Intel Wi-Fi NIC APM safely\n");
    term_puts_t(term, "  wifi firmware - Check Intel firmware availability\n");
    term_puts_t(term, "  wifi load - Stage Intel firmware DMA loader\n");
    term_puts_t(term, "  wifi upload [arm] - Prepare/arm first Intel FH firmware transfer\n");
    term_puts_t(term, "  wifi upload all [arm] - Prepare/arm all Intel firmware chunks\n");
    term_puts_t(term, "  wifi boot [arm] - Release/load Intel firmware CPU sequence\n");
    term_puts_t(term, "  wifi alive - Poll for Intel firmware alive interrupt\n");
    term_puts_t(term, "  wifi queues [arm] - Stage Intel command/RX/TX host rings\n");
    term_puts_t(term, "  wifi context [arm] - Stage Intel firmware context-info\n");
    term_puts_t(term, "  wifi scheduler [arm] - Stage Intel scheduler command frame\n");
    term_puts_t(term, "  wifi rx [poll] - Inspect Intel firmware RX responses\n");
    term_puts_t(term, "  wifi command [arm] - Ring doorbell + command diagnostics\n");
    term_puts_t(term, "  wifi nvm [arm] - Read Intel firmware NVM cache\n");
    term_puts_t(term, "  wifi nvm-info [arm] - Read Intel radio/NVM capabilities\n");
    term_puts_t(term, "  wifi bringup - Run Intel Wi-Fi boot/readiness sequence\n");
    term_puts_t(term, "  wifi crypto - Test WPA2 SHA-1/PBKDF2 primitives\n");
    term_puts_t(term, "  wifi scan [arm|poll] - Plan/send/poll experimental passive scan\n");
    term_puts_t(term,
                "  wifi connect - Prepare Wi-Fi auth/association frames\n");
    term_puts_t(term,
                "  wifi join <ssid> [password] - Auto Wi-Fi bringup/connect/WPA\n");
    term_puts_t(term,
                "  wifi online <ssid> [password] - Join, DHCP, DNS and GitHub TLS probe\n");
    term_puts_t(term,
                "  wifi validate <ssid> [password] - Persist AP/WPA2/DHCP/DNS/TLS proof\n");
    term_puts_t(term,
                "  wifi update <ssid> [password] - Validate Wi-Fi path then run update\n");
    term_puts_t(term,
                "  wifi wpa - Show WPA M1/M2/M3/M4 diagnostic state\n");
    term_puts_t(term,
                "  wifi key [pairwise|gtk] [arm] - Build/queue WPA SEC_KEY\n");
    term_puts_t(term,
                "  wifi data - Build protected CCMP diagnostic data frame\n");
    term_puts_t(term,
                "  wifi bind [arm] - Build/queue MAC/LINK/STA binding\n");
    term_puts_t(term,
                "  wifi tx [auth|assoc|m2|m4|data|all] - Stage Wi-Fi TX DMA only\n");
    term_puts_t(term,
                "  wifi txcmd [auth|assoc|m2|m4|data] [arm] - Build/queue TX_CMD\n");
    term_puts_t(term, "  ssh start/status/algorithms/stop - Manage TCP/22 SSH listener\n");
    term_puts_t(term, "  ping <host> / dns <host> / route - Network diagnostics\n");
    term_puts_t(term, "  install   - Start guided disk installer\n");
    term_puts_t(term, "  install-plan [mode] [desktop] - Save installer preflight report\n");
    term_puts_t(term, "  install-status - Show installer plan/state\n");
    term_puts_t(term, "  boot-check - Verify installed disk boot files\n");
    term_puts_t(term, "  dualboot-check - Verify /EFI/Orizon side-by-side boot files\n");
    term_puts_t(term, "  repair-boot - Rewrite installed boot files\n");
    term_puts_t(term, "  keyboard [fr|us] - Show or change keyboard layout\n");
    term_puts_t(term, "  reboot    - Save /workspace and restart the machine\n");
    term_puts_t(term, "  shutdown  - Save /workspace and power off\n");
    if (term_install_already_complete()) {
      term_puts_t(term, "  update    - Run Orizon full-upgrade\n");
      term_puts_t(term, "  rollback  - Restore the booted rollback slot\n");
      term_puts_t(term, "  rollback-status - Show rollback metadata\n");
      term_puts_t(term, "  bootguard [confirm|recover] - Show/confirm/arm rollback fallback\n");
    }
    term_puts_t(term, "  update status - Show manifest/signature/TLS/rollback state\n");
    term_puts_t(term, "  about     - Show Orizon build details\n");
    term_puts_t(term, "  version   - Show kernel build version\n");
    term_puts_t(term, "  neofetch  - System info\n");
    term_puts_t(term, "  uname     - Show OS info\n");
    term_puts_t(term, "  id        - Show user/group info\n");
    term_puts_t(term, "  hostname [set <name>] - Show or persist hostname\n");
    term_puts_t(term, "  history [-c|grep <text>] - Show/search/clear persistent history\n");
    term_puts_t(term, "  free      - Memory usage\n");
    term_puts_t(term, "  ps        - Process list\n");
    term_puts_t(term, "  clear     - Clear screen\n");
    term_puts_t(term, "  help      - This help message\n");
    term_puts_t(term, "  help shell - Shell operators and shortcuts\n");
    term_puts_t(term, "  shell status - Show local shell buffers/capabilities\n");
    term_puts_t(term, "\n");
    term_puts_t(term, "This build intentionally starts from a minimal core shell.\n");
    term_puts_t(term, "Tip: Tab completes commands/files; Up/Down browse saved history.\n");
    term_puts_t(term, "Tip: empty prompt z/s scrolls output; less uses z/s/space/q.\n");
    term_puts_t(term, "Tip: use 'cmd | less' for long diagnostic output.\n");
    term_puts_t(term, "Add new tools only when they belong in Orizon OS.\n");
  } else if (term_command_is(cmd, "shell")) {
    const char *args = term_skip_spaces(cmd + 5);
    if (*args == '\0' || term_command_is(args, "status") ||
        term_command_is(args, "diag") || term_command_is(args, "diagnostics")) {
      term_print_shell_status(term);
    } else if (term_command_is(args, "help")) {
      term_print_help_shell(term);
    } else {
      term_puts_t(term, "usage: shell [status|help]\n");
    }
  } else if (strncmp(cmd, "clear", 5) == 0) {
    term_clear_screen(term);
  } else if (term_command_is(cmd, "about")) {
    term_print_about(term);
  } else if (term_command_is(cmd, "version")) {
    term_print_version(term);
  } else if (term_command_is(cmd, "dmesg")) {
    if (term_install_already_complete()) {
      klog_persist_boot_if_installed();
    }
    term_print_klog(term, sizeof(term_diag_buf));
  } else if (term_command_is(cmd, "sysinfo")) {
    term_print_sysinfo(term);
  } else if (term_command_is(cmd, "system")) {
    term_print_system_state(term, cmd);
  } else if (term_command_is(cmd, "health")) {
    static char report[2048];
    orizon_system_format_health(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "snapshot")) {
    static char report[1024];
    orizon_system_write_snapshot(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "backup")) {
    static char report[1024];
    orizon_system_write_admin_backup(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "services")) {
    static char report[2048];
    orizon_system_format_services(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "journal")) {
    static char report[4096];
    orizon_system_format_logs(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "doctor")) {
    static char report[2048];
    orizon_system_format_doctor(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "init")) {
    static char report[3072];
    orizon_system_run_boot_tasks(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "rescue")) {
    static char report[1024];
    orizon_system_format_rescue(report, sizeof(report));
    term_puts_t(term, report);
  } else if (term_command_is(cmd, "firstboot")) {
    static char report[512];
    const char *args = term_skip_spaces(cmd + strlen("firstboot"));
    if (term_command_is(args, "done") || term_command_is(args, "confirm")) {
      orizon_system_mark_firstboot_done(report, sizeof(report));
      term_puts_t(term, report);
    } else {
      orizon_system_format_firstboot(report, sizeof(report));
      term_puts_t(term, report);
      term_puts_t(term, "usage: firstboot done\n");
    }
  } else if (term_command_is(cmd, "hw")) {
    term_print_hw(term, term_skip_spaces(cmd + 2));
  } else if (term_command_is(cmd, "pci")) {
    term_print_pci(term, cmd);
  } else if (term_command_is(cmd, "usb")) {
    term_print_usb_status(term, cmd);
  } else if (term_command_is(cmd, "input")) {
    term_print_input_status(term);
  } else if (term_command_is(cmd, "logs")) {
    term_print_log_summary(term, cmd);
  } else if (term_command_is(cmd, "report")) {
    const char *args = term_skip_spaces(cmd + 6);
    if (term_install_already_complete()) {
      klog_persist_boot_if_installed();
    }
    if (term_command_is(args, "next")) {
      static char next_report[2048];
      orizon_report_format_hardware_next(next_report, sizeof(next_report));
      term_puts_t(term, next_report);
      if (next_report[0] && next_report[strlen(next_report) - 1] != '\n') {
        term_puts_t(term, "\n");
      }
      return;
    }
    if (term_command_is(args, "save")) {
      char status[160];
      if (orizon_report_save(status, sizeof(status)) == 0) {
        term_puts_t(term, status);
        term_puts_t(term, "read with: cat ");
        term_puts_t(term, ORIZON_HARDWARE_REPORT_PATH);
        term_puts_t(term, "\n");
      } else {
        term_puts_t(term, status);
      }
      return;
    }
    term_print_report(term);
  } else if (strncmp(cmd, "ls", 2) == 0) {
    char path[MAX_PATH];
    const char *requested = term->cwd[0] ? term->cwd : "/";
    if (cmd[2] == ' ' && cmd[3]) requested = cmd + 3;

    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "ls: invalid path\n");
      return;
    }
    
    dirent_t entries[32];
    int count = vfs_readdir(path, entries, 32);
    if (count >= 0) {
      for (int i = 0; i < count; i++) {
        if (entries[i].type == 1) {
          term_puts_t(term, "\033[1;34m");
          term_puts_t(term, entries[i].name);
          term_puts_t(term, "/\033[0m  ");
        } else {
          term_puts_t(term, entries[i].name);
          term_puts_t(term, "  ");
        }
      }
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "ls: cannot access directory\n");
    }
  } else if (strncmp(cmd, "pwd", 3) == 0) {
    term_puts_t(term, term->cwd[0] ? term->cwd : "/");
    term_puts_t(term, "\n");
  } else if (strncmp(cmd, "cd ", 3) == 0) {
    const char *path = cmd + 3;
    while (*path == ' ') path++;
    
    char target[256];
    int is_dir = 0;

    if (resolve_path(term->cwd, path, target, sizeof(target)) < 0) {
      term_puts_t(term, "cd: invalid path\n");
      return;
    }

    if (vfs_stat(target, NULL, &is_dir) >= 0 && is_dir) {
      strncpy(term->cwd, target, 255);
      term->cwd[255] = '\0';
    } else {
      term_puts_t(term, "cd: no such directory: ");
      term_puts_t(term, path);
      term_puts_t(term, "\n");
    }
  } else if (term_command_is(cmd, "less") || term_command_is(cmd, "more")) {
    const char *filename = term_skip_spaces(cmd + 4);
    char path[MAX_PATH];

    if (*filename == '\0') {
      term_puts_t(term, "usage: less <file>\n");
      return;
    }
    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, "less: invalid path\n");
      return;
    }
    term_start_pager(term, filename, path);
  } else if (strncmp(cmd, "cat", 3) == 0 &&
             (cmd[3] == '\0' || cmd[3] == ' ')) {
    const char *filename = cmd + 3;
    while (*filename == ' ') filename++;

    if (*filename == '\0') {
      term_puts_t(term, "cat: missing file operand\n");
      return;
    }
    
    char path[256];
    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, "cat: invalid path\n");
      return;
    }

    int is_dir = 0;
    if (vfs_stat(path, NULL, &is_dir) < 0) {
      term_puts_t(term, "cat: ");
      term_puts_t(term, filename);
      term_puts_t(term, ": No such file\n");
      return;
    }
    if (is_dir) {
      term_puts_t(term, "cat: ");
      term_puts_t(term, filename);
      term_puts_t(term, ": Is a directory\n");
      return;
    }
    
    file_t *f = vfs_open(path, O_RDONLY);
    if (f) {
      char buf[512];
      ssize_t n;
      while ((n = vfs_read(f, buf, 511)) > 0) {
        buf[n] = '\0';
        term_puts_t(term, buf);
      }
      vfs_close(f);
      if (n < 0) {
        term_puts_t(term, "\ncat: read error\n");
        return;
      }
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "cat: ");
      term_puts_t(term, filename);
      term_puts_t(term, ": No such file\n");
    }
  } else if (term_command_is(cmd, "head")) {
    const char *args = term_skip_spaces(cmd + 4);
    const char *filename = args;
    int lines = 10;
    char path[MAX_PATH];

    if (*args == '-') {
      args++;
      if (term_parse_uint(args, &lines) < 0) {
        term_puts_t(term, "usage: head [-n] <file>\n");
        return;
      }
      while (*args >= '0' && *args <= '9') {
        args++;
      }
      filename = term_skip_spaces(args);
    }

    if (*filename == '\0') {
      term_puts_t(term, "usage: head [-n] <file>\n");
      return;
    }
    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, "head: invalid path\n");
      return;
    }
    term_print_head(term, filename, path, lines);
  } else if (term_command_is(cmd, "tail")) {
    const char *args = term_skip_spaces(cmd + 4);
    const char *filename = args;
    int lines = 10;
    char path[MAX_PATH];

    if (*args == '-') {
      args++;
      if (term_parse_uint(args, &lines) < 0) {
        term_puts_t(term, "usage: tail [-n] <file>\n");
        return;
      }
      while (*args >= '0' && *args <= '9') {
        args++;
      }
      filename = term_skip_spaces(args);
    }

    if (*filename == '\0') {
      term_puts_t(term, "usage: tail [-n] <file>\n");
      return;
    }
    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, "tail: invalid path\n");
      return;
    }
    term_print_tail(term, filename, path, lines);
  } else if (term_command_is(cmd, "grep")) {
    char pattern[MAX_PATH];
    char file_arg[MAX_PATH];
    char path[MAX_PATH];
    const char *args = cmd + 4;
    int ignore_case = 0;
    int invert = 0;
    int show_numbers = 0;

    args = term_parse_grep_options(args, &ignore_case, &invert,
                                   &show_numbers);

    if (term_split_two_paths(args, pattern, file_arg, sizeof(file_arg)) < 0) {
      term_puts_t(term, "usage: grep [-i] [-v] [-n] <text> <file>\n");
      return;
    }
    if (resolve_path(term->cwd, file_arg, path, sizeof(path)) < 0) {
      term_puts_t(term, "grep: invalid path\n");
      return;
    }
    if (ignore_case || invert || show_numbers) {
      static char grep_buf[4096];
      static char grep_out[4096];
      int n = term_read_regular_file(term, file_arg, path, grep_buf,
                                     sizeof(grep_buf), "grep");
      if (n < 0) {
        return;
      }
      term_pipe_grep(grep_buf, pattern, grep_out, sizeof(grep_out),
                     ignore_case, invert, show_numbers);
      term_puts_t(term, grep_out);
    } else {
      term_print_grep(term, pattern, file_arg, path);
    }
  } else if (term_command_is(cmd, "wc")) {
    const char *filename = term_skip_spaces(cmd + 2);
    char path[MAX_PATH];

    if (*filename == '\0') {
      term_puts_t(term, "usage: wc <file>\n");
      return;
    }
    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, "wc: invalid path\n");
      return;
    }
    term_print_wc(term, filename, path);
  } else if (term_command_is(cmd, "find")) {
    const char *args = term_skip_spaces(cmd + 4);
    char first[MAX_PATH] = {0};
    char second[MAX_NAME] = {0};
    char path[MAX_PATH];
    const char *pattern = "";
    int is_dir = 0;

    if (*args == '\0') {
      snprintf(path, sizeof(path), "%s", term->cwd[0] ? term->cwd : "/");
    } else {
      const char *p = args;
      size_t len = 0;
      while (p[len] && p[len] != ' ') {
        len++;
      }
      if (len >= sizeof(first)) {
        term_puts_t(term, "find: invalid argument\n");
        return;
      }
      for (size_t i = 0; i < len; i++) {
        first[i] = p[i];
      }
      first[len] = '\0';
      p = term_skip_spaces(p + len);
      if (*p) {
        strncpy(second, p, sizeof(second) - 1);
      }

      if (resolve_path(term->cwd, first, path, sizeof(path)) == 0 &&
          vfs_stat(path, NULL, &is_dir) == 0 && is_dir) {
        pattern = second;
      } else {
        snprintf(path, sizeof(path), "%s", term->cwd[0] ? term->cwd : "/");
        pattern = first;
      }
    }

    term_find_recursive(term, path, pattern, 0);
  } else if (term_command_is(cmd, "stat")) {
    char path[MAX_PATH];
    const char *requested = term_skip_spaces(cmd + 4);
    if (*requested == '\0') {
      requested = term->cwd[0] ? term->cwd : "/";
    }

    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "stat: invalid path\n");
      return;
    }
    term_print_stat(term, requested, path);
  } else if (term_command_is(cmd, "tree")) {
    char path[MAX_PATH];
    const char *requested = term_skip_spaces(cmd + 4);
    if (*requested == '\0') {
      requested = term->cwd[0] ? term->cwd : "/";
    }

    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "tree: invalid path\n");
      return;
    }
    term_print_tree(term, requested, path);
  } else if (term_command_is(cmd, "cp")) {
    char src_arg[MAX_PATH];
    char dst_arg[MAX_PATH];
    char src_path[MAX_PATH];
    char dst_path[MAX_PATH];
    int is_dir = 0;

    if (term_split_two_paths(cmd + 2, src_arg, dst_arg, sizeof(src_arg)) < 0) {
      term_puts_t(term, "usage: cp <source> <dest>\n");
      return;
    }
    if (resolve_path(term->cwd, src_arg, src_path, sizeof(src_path)) < 0 ||
        term_resolve_target_path(term->cwd, src_path, dst_arg, dst_path,
                                 sizeof(dst_path)) < 0) {
      term_puts_t(term, "cp: invalid path\n");
      return;
    }
    if (vfs_stat(src_path, NULL, &is_dir) < 0) {
      term_puts_t(term, "cp: source not found\n");
      return;
    }
    if (is_dir) {
      term_puts_t(term, "cp: directories are not supported yet\n");
      return;
    }
    if (strcmp(src_path, dst_path) == 0) {
      term_puts_t(term, "cp: source and destination are the same\n");
      return;
    }
    if (term_copy_file(src_path, dst_path) < 0) {
      term_puts_t(term, "cp: failed\n");
      return;
    }
    term_puts_t(term, "Copied: ");
    term_puts_t(term, dst_path);
    term_puts_t(term, "\n");
  } else if (term_command_is(cmd, "mv")) {
    char src_arg[MAX_PATH];
    char dst_arg[MAX_PATH];
    char src_path[MAX_PATH];
    char dst_path[MAX_PATH];

    if (term_split_two_paths(cmd + 2, src_arg, dst_arg, sizeof(src_arg)) < 0) {
      term_puts_t(term, "usage: mv <source> <dest>\n");
      return;
    }
    if (resolve_path(term->cwd, src_arg, src_path, sizeof(src_path)) < 0 ||
        term_resolve_target_path(term->cwd, src_path, dst_arg, dst_path,
                                 sizeof(dst_path)) < 0) {
      term_puts_t(term, "mv: invalid path\n");
      return;
    }
    if (vfs_rename(src_path, dst_path) < 0) {
      term_puts_t(term, "mv: failed\n");
      return;
    }
    term_rewrite_cwd_after_move(term, src_path, dst_path);
    term_puts_t(term, "Moved: ");
    term_puts_t(term, dst_path);
    term_puts_t(term, "\n");
  } else if (term_command_is(cmd, "edit")) {
    char path[MAX_PATH];
    const char *requested = term_skip_spaces(cmd + 4);
    if (*requested == '\0') {
      term_puts_t(term, "usage: edit <file>\n");
      return;
    }
    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "edit: invalid path\n");
      return;
    }
    term_start_editor(term, requested, path);
  } else if (strncmp(cmd, "touch ", 6) == 0) {
    const char *filename = cmd + 6;
    char path[256];
    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, "touch: invalid path\n");
      return;
    }
    if (vfs_create(path) >= 0) {
      term_puts_t(term, "Created: ");
      term_puts_t(term, filename);
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "touch: failed\n");
    }
  } else if (term_command_is(cmd, "write") || term_command_is(cmd, "append")) {
    int append = term_command_is(cmd, "append");
    const char *args = cmd + (append ? 6 : 5);
    const char *text = NULL;
    char filename[MAX_PATH];
    char path[MAX_PATH];

    if (term_split_path_and_text(args, filename, sizeof(filename), &text) < 0) {
      term_puts_t(term, append ? "usage: append <file> <text>\n"
                               : "usage: write <file> <text>\n");
      return;
    }

    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, append ? "append: invalid path\n" : "write: invalid path\n");
      return;
    }

    file_t *f = vfs_open(path, O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC));
    if (!f) {
      term_puts_t(term, append ? "append: failed\n" : "write: failed\n");
      return;
    }

    if (vfs_write(f, text, strlen(text)) < 0 ||
        vfs_write(f, "\n", 1) < 0) {
      term_puts_t(term, append ? "append: write error\n" : "write: write error\n");
      vfs_close(f);
      return;
    }

    vfs_close(f);
    term_puts_t(term, append ? "Appended: " : "Wrote: ");
    term_puts_t(term, path);
    term_puts_t(term, "\n");
  } else if (strncmp(cmd, "mkdir ", 6) == 0) {
    const char *dirname = cmd + 6;
    char path[256];
    if (resolve_path(term->cwd, dirname, path, sizeof(path)) < 0) {
      term_puts_t(term, "mkdir: invalid path\n");
      return;
    }
    if (vfs_mkdir(path) >= 0) {
      term_puts_t(term, "Created directory: ");
      term_puts_t(term, dirname);
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "mkdir: failed\n");
    }
  } else if (strncmp(cmd, "rm ", 3) == 0) {
    const char *filename = cmd + 3;
    char path[256];
    if (resolve_path(term->cwd, filename, path, sizeof(path)) < 0) {
      term_puts_t(term, "rm: invalid path\n");
      return;
    }
    if (vfs_delete(path) >= 0) {
      term_puts_t(term, "Removed: ");
      term_puts_t(term, filename);
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "rm: failed\n");
    }
  } else if (term_command_is(cmd, "sync")) {
    if (vfs_persist_save() == 0) {
      term_puts_t(term, "Synced Orizon data roots to disk\n");
    } else {
      term_puts_t(term, "sync: persistence unavailable\n");
    }
  } else if (term_command_is(cmd, "persist")) {
    term_print_persist(term, cmd);
  } else if (term_command_is(cmd, "disks")) {
    term_puts_t(term, "\033[1;36mDetected disks\033[0m\n");
    term_print_disks(term);
  } else if (term_command_is(cmd, "partitions")) {
    term_print_partitions(term);
  } else if (term_command_is(cmd, "gpt")) {
    const char *args = term_skip_spaces(cmd + 3);
    if (*args == '\0' || term_command_is(args, "scan")) {
      term_print_partitions(term);
    } else {
      term_puts_t(term, "usage: gpt scan\n");
    }
  } else if (term_command_is(cmd, "mounts")) {
    term_print_mounts(term);
  } else if (term_command_is(cmd, "disk")) {
    term_run_disk(term, cmd);
  } else if (term_command_is(cmd, "storage")) {
    char capacity[64];
    const char *args = term_skip_spaces(cmd + 7);
    if (term_command_is(args, "select")) {
      int choice = 0;
      args = term_skip_spaces(args + 6);
      if (term_parse_uint(args, &choice) < 0 ||
          storage_select_device(choice - 1) < 0) {
        term_puts_t(term, "usage: storage select <disk-number>\n");
        term_print_disks(term);
        return;
      }
      term_puts_t(term, "Selected active disk.\n");
      term_print_disks(term);
      return;
    }
    if (term_command_is(args, "diag") || term_command_is(args, "diagnostics")) {
      static char diag[2048];
      storage_format_diagnostics(diag, sizeof(diag));
      term_puts_t(term, diag);
      return;
    }
    if (term_command_is(args, "vmcheck") || term_command_is(args, "check") ||
        term_command_is(args, "verify") || term_command_is(args, "repair")) {
      static char report[8192];
      storage_format_vmcheck(report, sizeof(report));
      term_puts_t(term, report);
      return;
    }
    if (term_command_is(args, "detail") || term_command_is(args, "list")) {
      term_puts_t(term, "\033[1;36mStorage detail\033[0m\n");
      term_print_disks(term);
    }
    storage_format_capacity(capacity, sizeof(capacity));
    term_puts_t(term, "Disk: ");
    term_puts_t(term, storage_available() ? storage_status()
                                          : "no writable detected disk");
    term_puts_t(term, " (");
    term_puts_t(term, capacity);
    term_puts_t(term, ")\n");
    term_puts_t(term, "Data roots: ");
    term_puts_t(term, vfs_persist_status());
    term_puts_t(term, "\n");
  } else if (term_command_is(cmd, "keyboard")) {
    const char *args = term_skip_spaces(cmd + 8);
    if (*args == '\0') {
      term_puts_t(term, "Keyboard layout: ");
      term_puts_t(term, input_keyboard_layout());
      term_puts_t(term, "\nUse: keyboard fr | keyboard us\n");
    } else if (term_command_is(args, "fr") ||
               term_command_is(args, "fr-azerty") ||
               term_command_is(args, "azerty")) {
      input_set_keyboard_layout("fr-azerty");
      vfs_mkdir("/workspace/.orizon");
      vfs_mkdir("/system");
      term_write_text_file("/workspace/.orizon/keyboard", "fr-azerty\n");
      term_write_text_file("/system/keyboard", "fr-azerty\n");
      vfs_persist_save();
      term_puts_t(term, "Keyboard layout active: fr-azerty\n");
    } else if (term_command_is(args, "us") ||
               term_command_is(args, "us-qwerty") ||
               term_command_is(args, "qwerty")) {
      input_set_keyboard_layout("us-qwerty");
      vfs_mkdir("/workspace/.orizon");
      vfs_mkdir("/system");
      term_write_text_file("/workspace/.orizon/keyboard", "us-qwerty\n");
      term_write_text_file("/system/keyboard", "us-qwerty\n");
      vfs_persist_save();
      term_puts_t(term, "Keyboard layout active: us-qwerty\n");
    } else {
      term_puts_t(term, "usage: keyboard fr | keyboard us\n");
    }
  } else if (term_command_is(cmd, "net")) {
    term_run_net(term, cmd);
  } else if (term_command_is(cmd, "wifi")) {
    term_run_wifi(term, cmd);
  } else if (term_command_is(cmd, "ssh")) {
    term_run_ssh(term, cmd);
  } else if (term_command_is(cmd, "network-status")) {
    term_run_net(term, "net status");
  } else if (term_command_is(cmd, "ping")) {
    term_run_ping(term, cmd);
  } else if (term_command_is(cmd, "dns")) {
    term_run_dns(term, cmd);
  } else if (term_command_is(cmd, "route")) {
    term_run_route(term);
  } else if (term_command_is(cmd, "install")) {
    term_start_installer(term);
    return;
  } else if (term_command_is(cmd, "install-plan")) {
    term_run_install_plan(term, cmd);
  } else if (term_command_is(cmd, "install-status")) {
    char buf[2048];
    int any = 0;
    int n = term_read_text_file_silent("/workspace/.orizon/install-plan", buf,
                                       sizeof(buf));
    if (n > 0) {
      buf[n] = '\0';
      term_puts_t(term, "install-plan:\n");
      term_puts_t(term, buf);
      if (buf[n - 1] != '\n') {
        term_puts_t(term, "\n");
      }
      any = 1;
    }
    n = term_read_text_file_silent("/workspace/.orizon/install-report.txt", buf,
                                   sizeof(buf));
    if (n > 0) {
      term_puts_t(term, "preflight-report:\n");
      term_puts_t(term, buf);
      if (buf[n - 1] != '\n') {
        term_puts_t(term, "\n");
      }
      any = 1;
    }
    n = term_read_text_file_silent("/workspace/.orizon/install-log", buf,
                                   sizeof(buf));
    if (n > 0) {
      term_puts_t(term, "install-log:\n");
      term_puts_t(term, buf);
      if (buf[n - 1] != '\n') {
        term_puts_t(term, "\n");
      }
      any = 1;
    }
    n = term_read_text_file_silent("/workspace/.orizon/install-state", buf,
                                   sizeof(buf));
    if (n > 0) {
      term_puts_t(term, "state: ");
      term_puts_t(term, buf);
      if (buf[n - 1] != '\n') {
        term_puts_t(term, "\n");
      }
      any = 1;
    }
    if (!any) {
      term_puts_t(term,
                  "install-status: no installer state or preflight report saved yet\n");
    }
  } else if (term_command_is(cmd, "boot-check")) {
    static char boot_report[8192];
    orizon_install_boot_check(boot_report, sizeof(boot_report));
    term_puts_t(term, boot_report);
  } else if (term_command_is(cmd, "dualboot-check")) {
    static char dual_report[8192];
    orizon_install_dualboot_check(dual_report, sizeof(dual_report));
    term_puts_t(term, dual_report);
  } else if (term_command_is(cmd, "repair-boot")) {
    static char repair_report[8192];
    orizon_install_repair_boot(repair_report, sizeof(repair_report));
    term_puts_t(term, repair_report);
  } else if (term_command_is(cmd, "reboot") ||
             term_command_is(cmd, "restart")) {
    vfs_persist_save();
    term_puts_t(term,
                "REBOOT in 2 seconds.\n"
                "The VM should restart and return to the Orizon console.\n");
    power_schedule_reboot(TIMER_HZ * 2);
  } else if (term_command_is(cmd, "shutdown") ||
             term_command_is(cmd, "poweroff")) {
    vfs_persist_save();
    term_puts_t(term,
                "SHUTDOWN in 3 seconds.\n"
                "If this was an install boot, remove/eject the ISO or USB media now.\n");
    power_schedule_shutdown(TIMER_HZ * 3);
  } else if (term_command_is(cmd, "update") || term_command_is(cmd, "orizon-update")) {
    term_run_update(term, cmd);
  } else if (term_command_is(cmd, "rollback")) {
    term_run_rollback(term);
  } else if (term_command_is(cmd, "bootguard")) {
    term_run_bootguard(term, cmd);
  } else if (term_command_is(cmd, "rollback-status")) {
    static char buf[4096];
    orizon_update_rollback_status(buf, sizeof(buf));
    term_puts_t(term, buf);
  } else if (term_command_is(cmd, "desktop")) {
    term_run_desktop(term, cmd);
  } else if (term_command_is(cmd, "pkg")) {
    term_run_pkg(term, cmd);
  } else if (term_command_is(cmd, "selftest")) {
    term_run_selftest(term, cmd);
  } else if (term_command_is(cmd, "security")) {
    term_run_security(term, cmd);
  } else if (strncmp(cmd, "echo ", 5) == 0) {
    term_puts_t(term, cmd + 5);
    term_puts_t(term, "\n");
  } else if (strncmp(cmd, "neofetch", 8) == 0) {
    char uptime[40];
    char line[128];
    kmalloc_stats_t stats;
    term_format_duration(timer_uptime_seconds(), uptime, sizeof(uptime));
    kmalloc_get_stats(&stats);
    term_puts_t(term, "\033[36m");
    term_puts_t(term, "   ___  ____  ___ _____ ___  _   _      ___  ____ \n");
    term_puts_t(term, "  / _ \\|  _ \\|_ _|__  / / _ \\| \\ | |    / _ \\/ ___|\n");
    term_puts_t(term, " | | | | |_) || |  / / | | | |  \\| |   | | | \\___ \\\n");
    term_puts_t(term, " | |_| |  _ < | | / /_ | |_| | |\\  |   | |_| |___) |\n");
    term_puts_t(term, "  \\___/|_| \\_\\___/____| \\___/|_| \\_|    \\___/|____/\n");
    term_puts_t(term, "\033[0m\n");
    term_puts_t(term, "\033[33mOS:\033[0m      Orizon OS Core\n");
    term_puts_t(term, "\033[33mHost:\033[0m    Personal x86_64 base\n");
    term_puts_t(term, "\033[33mKernel:\033[0m  core-x86_64\n");
    term_puts_t(term, "\033[33mUptime:\033[0m  ");
    term_puts_t(term, uptime);
    term_puts_t(term, "\n");
    term_puts_t(term, "\033[33mShell:\033[0m   Orizon console\n");
    snprintf(line, sizeof(line), "%lu KB used / %lu KB free\n",
             (unsigned long)(stats.used / 1024),
             (unsigned long)(stats.free / 1024));
    term_puts_t(term, "\033[33mMemory:\033[0m  ");
    term_puts_t(term, line);
    term_puts_t(term, "\033[33mCPU:\033[0m     x86_64\n");
    term_puts_t(term, "\033[33mProfile:\033[0m Minimal development base\n");
  } else if (strncmp(cmd, "uname", 5) == 0) {
    if (strstr(cmd, "-a")) {
      term_puts_t(term, "Orizon OS core-x86_64 x86_64\n");
    } else {
      term_puts_t(term, "Orizon OS\n");
    }
  } else if (strncmp(cmd, "free", 4) == 0) {
    kmalloc_stats_t stats;
    kmalloc_get_stats(&stats);
    term_puts_t(term, "          total_kb used_kb free_kb largest_kb\n");
    char buf[64];
    snprintf(buf, 64, "Heap:     %8lu %7lu %7lu %10lu\n",
             (unsigned long)(stats.total / 1024),
             (unsigned long)(stats.used / 1024),
             (unsigned long)(stats.free / 1024),
             (unsigned long)(stats.largest_free / 1024));
    term_puts_t(term, buf);
    snprintf(buf, 64, "Blocks: used=%lu free=%lu total=%lu\n",
             (unsigned long)stats.used_blocks, (unsigned long)stats.free_blocks,
             (unsigned long)stats.blocks);
    term_puts_t(term, buf);
  } else if (strncmp(cmd, "ps", 2) == 0) {
    sched_process_t procs[SCHED_MAX_PROCESSES];
    int count = sched_snapshot(procs, SCHED_MAX_PROCESSES);
    term_puts_t(term, "  PID STATE      TICKS CMD\n");
    for (int i = 0; i < count; i++) {
      char line[96];
      snprintf(line, sizeof(line), "%5d %s %5lu %s\n", procs[i].pid,
               sched_state_name(procs[i].state),
               (unsigned long)procs[i].cpu_ticks, procs[i].name);
      term_puts_t(term, line);
    }
  } else if (term_command_is(cmd, "history")) {
    const char *args = term_skip_spaces(cmd + 7);
    if (strcmp(args, "-c") == 0 || term_command_is(args, "clear")) {
      term->history_count = 0;
      term->history_pos = 0;
      term_save_history(term);
      term_puts_t(term, "History cleared\n");
      return;
    }
    if (term_command_is(args, "grep") || term_command_is(args, "search")) {
      const char *query = term_skip_spaces(args + (term_command_is(args, "grep")
                                                       ? 4
                                                       : 6));
      int matches = 0;
      if (*query == '\0') {
        term_puts_t(term, "usage: history grep <text>\n");
        return;
      }
      for (int i = 0; i < term->history_count; i++) {
        if (term_line_contains_mode(term->history[i], strlen(term->history[i]),
                                    query, 1)) {
          char num[8];
          snprintf(num, 8, "%4d  ", i + 1);
          term_puts_t(term, num);
          term_puts_t(term, term->history[i]);
          term_puts_t(term, "\n");
          matches++;
        }
      }
      if (matches == 0) {
        term_puts_t(term, "history: no matches\n");
      }
      return;
    }
    if (*args != '\0') {
      term_puts_t(term, "usage: history [-c|grep <text>]\n");
      return;
    }
    for (int i = 0; i < term->history_count; i++) {
      char num[8];
      snprintf(num, 8, "%4d  ", i + 1);
      term_puts_t(term, num);
      term_puts_t(term, term->history[i]);
      term_puts_t(term, "\n");
    }
  } else if (strncmp(cmd, "whoami", 6) == 0) {
    term_puts_t(term, "root\n");
  } else if (strncmp(cmd, "id", 2) == 0) {
    term_puts_t(term, "uid=0(root) gid=0(root) groups=0(root)\n");
  } else if (term_command_is(cmd, "hostname")) {
    term_print_hostname_command(term, cmd);
  } else if (strncmp(cmd, "date", 4) == 0) {
    term_puts_t(term, "Thu Jan 23 00:00:00 UTC 2025\n");
  } else if (strncmp(cmd, "uptime", 6) == 0) {
    char uptime[40];
    char line[128];
    term_format_duration(timer_uptime_seconds(), uptime, sizeof(uptime));
    snprintf(line, sizeof(line), " %s up %s, 1 user, ticks=%lu\n", uptime,
             uptime, (unsigned long)timer_ticks());
    term_puts_t(term, line);
  } else {
    term_puts_t(term, cmd);
    term_puts_t(term, ": command not found\n");
  }
}

void term_render_in_rect(terminal_t *term, int x, int y, int width,
                         int height) {
  int cols;
  int rows;
  int base_x;
  int base_y;
  static int blink = 0;

  if (!term || !term->visible || width <= TERM_PADDING * 2 ||
      height <= TERM_PADDING * 2) {
    return;
  }

  cols = (width - TERM_PADDING * 2) / TERM_CHAR_W;
  rows = (height - TERM_PADDING * 2) / TERM_CHAR_H;
  if (cols > TERM_COLS) {
    cols = TERM_COLS;
  }
  if (rows > TERM_ROWS) {
    rows = TERM_ROWS;
  }
  if (cols <= 0 || rows <= 0) {
    return;
  }

  base_x = x + TERM_PADDING;
  base_y = y + TERM_PADDING;
  fb_fill_rect(x, y, width, height, term_colors[0]);
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      char ch;
      uint8_t fg;
      uint8_t bg;
      term_get_render_cell(term, row, col, &ch, &fg, &bg);
      term_draw_char(base_x + col * TERM_CHAR_W, base_y + row * TERM_CHAR_H,
                     ch, term_colors[fg & 0xF], term_colors[bg & 0xF]);
    }
  }

  blink++;
  if (!term->pager_mode && term->scroll_offset == 0 && (blink / 15) % 2 == 0 &&
      term->cursor_x < cols && term->cursor_y < rows) {
    int cx = base_x + term->cursor_x * TERM_CHAR_W;
    int cy = base_y + term->cursor_y * TERM_CHAR_H;
    fb_fill_rect(cx, cy + TERM_CHAR_H - 2, TERM_CHAR_W, 2, term_colors[7]);
  }
}

/* Execute command line */
void term_execute(terminal_t *term, const char *cmd) {
  char work[256];
  size_t len;

  while (cmd && *cmd == ' ') {
    cmd++;
  }
  if (!cmd || *cmd == '\0') {
    return;
  }

  term_add_history(term, cmd);
  term_puts_t(term, "\n");

  len = strlen(cmd);
  if (len >= sizeof(work)) {
    term_puts_t(term, "shell: command too long\n");
    return;
  }
  memcpy(work, cmd, len + 1);
  term_execute_command_groups(term, work);
}

/* Print prompt */
void term_prompt(terminal_t *term) {
  term_prompt_prefix(term);
  term_prepare_input(term);
}

/* Handle keyboard input */
void term_handle_key(terminal_t *term, int key) {
  if (!term) return;

  if (term->pager_mode) {
    term_handle_pager_key(term, key);
    return;
  }

  if (!term->edit_mode && !term->install_mode) {
    if (term_handle_scrollback_key(term, key)) {
      return;
    }
    if (key == '\t') {
      term_autocomplete(term);
      return;
    }
    if (key == KEY_UP) {
      if (term->history_count > 0 && term->history_pos > 0) {
        term->history_pos--;
        term_set_input_text(term, term->history[term->history_pos]);
      }
      return;
    }
    if (key == KEY_DOWN) {
      if (term->history_pos < term->history_count - 1) {
        term->history_pos++;
        term_set_input_text(term, term->history[term->history_pos]);
      } else {
        term->history_pos = term->history_count;
        term_set_input_text(term, "");
      }
      return;
    }
  }

  if (key == KEY_LEFT) {
    if (term->input_cursor > 0) {
      term->input_cursor--;
      term_redraw_input(term);
    }
    return;
  }
  if (key == KEY_RIGHT) {
    if (term->input_cursor < term->input_len) {
      term->input_cursor++;
      term_redraw_input(term);
    }
    return;
  }
  if (key == '\t') {
    if (term->edit_mode) {
      term_insert_input_text(term, "  ");
    }
    return;
  }

  if (key == '\n' || key == '\r') {
    term->input_buf[term->input_len] = '\0';
    if (term->install_mode) {
      term_puts_t(term, "\n");
      term_install_submit(term, term->input_buf);
    } else if (term->edit_mode) {
      term_editor_submit(term, term->input_buf);
    } else {
      term_execute(term, term->input_buf);
    }
    term->input_len = 0;
    if (!term->edit_mode && !term->install_mode && !term->pager_mode) {
      term_prompt(term);
    }
  } else if (key == '\b' || key == 127) {
    term_backspace_input(term);
  } else if (key >= 32 && key < 127) {
    term_insert_input_char(term, (char)key);
  }
}

/* Create terminal */
terminal_t *term_create(int x, int y) {
  terminal_t *term = kzalloc(sizeof(terminal_t));
  if (!term) return NULL;
  
  term->content_x = x;
  term->content_y = y;
  term->width = TERM_COLS * TERM_CHAR_W + TERM_PADDING * 2;
  term->height = TERM_ROWS * TERM_CHAR_H + TERM_PADDING * 2;
  term->current_fg = 7;
  term->current_bg = 0;
  term->visible = 1;
  strcpy(term->cwd, "/workspace");
  term_load_history(term);
  
  for (int i = 0; i < TERM_ROWS * TERM_COLS; i++) {
    term->chars[i] = ' ';
    term->fg_colors[i] = 7;
    term->bg_colors[i] = 0;
  }
  
  /* Welcome message (matching test version) */
  term_puts_t(term, "\033[1;36mOrizon OS Core Console\033[0m\n");
  term_puts_t(term,
              "Type '\033[33mhelp\033[0m' for commands or '\033[33mneofetch\033[0m' for system info.\n");
  term_puts_t(term,
              "This VM boots into a clean personal base with the console ready first.\n");
  if (term_install_already_complete()) {
    term_puts_t(term,
                "Installed disk detected. '\033[33minstall\033[0m' is disabled; use '\033[33minstall-status\033[0m'.\n\n");
  } else {
    term_puts_t(term,
                "Type '\033[33minstall\033[0m' to run the guided disk installer.\n\n");
  }
  term_prompt(term);
  
  return term;
}

/* Get/set active terminal */
terminal_t *term_get_active(void) { return active_term; }
void term_set_active(terminal_t *term) { active_term = term; }
