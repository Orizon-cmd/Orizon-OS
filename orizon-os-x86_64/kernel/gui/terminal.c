/*
 * Orizon OS x86_64 - VT100 Terminal Emulator
 */

#include "../include/gui.h"
#include "../include/i2c_hid.h"
#include "../include/acpi.h"
#include "../include/bootinfo.h"
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
#include "../include/sched.h"
#include "../include/ssh.h"
#include "../include/storage.h"
#include "../include/string.h"
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
#define TERM_HISTORY_MAX 32
#define TERM_HISTORY_PATH "/workspace/.orizon/history"

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
  
  /* History */
  char history[TERM_HISTORY_MAX][256];
  int history_count;
  int history_pos;
} terminal_t;

static terminal_t *active_term = NULL;
static char term_diag_buf[32768];

static int term_install_already_complete(void);
static int term_read_text_file_silent(const char *path, char *buf, size_t cap);

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
      "about", "append", "boot-check", "cat", "cd", "clear", "cp", "date",
      "disks", "dmesg", "dns", "dualboot-check", "edit", "echo", "find",
      "free", "grep", "head", "help", "history", "hostname", "hw", "id",
      "install", "install-status",
      "input", "keyboard", "ls", "mkdir", "mounts", "mv",
      "neofetch", "net", "network-status", "logs", "pci", "ping", "pkg", "poweroff", "ps", "pwd", "report", "rollback",
      "rollback-status", "repair-boot", "rm", "shutdown", "stat", "storage", "partitions", "sync",
      "sysinfo", "ssh", "touch", "tree", "route", "uname", "update", "uptime", "version", "wifi", "whoami",
      "write"};
  const char *matches[16];
  int count = 0;

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (!term_install_already_complete() &&
        (strcmp(commands[i], "update") == 0 ||
         strcmp(commands[i], "rollback") == 0 ||
         strcmp(commands[i], "rollback-status") == 0)) {
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
  term_puts_t(term, "  USB HID keyboard: supported; generic USB mouse is still pending\n");
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

static void term_print_hw(terminal_t *term) {
  char line[256];
  char uptime[40];
  char vendor[13];
  uint32_t a, b, c, d;
  kmalloc_stats_t stats;
  pci_device_info_t devs[24];

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
                "Use: logs boot | logs network | logs ssh | logs update | logs install | logs all\n");
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
    if (vfs_exists(ORIZON_SSH_LOG_PATH)) {
      term_print_file_tail(term, ORIZON_SSH_LOG_PATH, ORIZON_SSH_LOG_PATH, 1024);
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
  if (term_command_is(args, "network") || term_command_is(args, "net")) {
    term_print_file_tail(term, netstack_log_path(), netstack_log_path(), 8192);
    return;
  }
  if (term_command_is(args, "ssh")) {
    term_print_file_tail(term, ORIZON_SSH_LOG_PATH, ORIZON_SSH_LOG_PATH, 8192);
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
    term_print_file_tail(term, netstack_log_path(), netstack_log_path(), 4096);
    term_print_file_tail(term, ORIZON_SSH_LOG_PATH, ORIZON_SSH_LOG_PATH, 4096);
    term_print_file_tail(term, "/workspace/.orizon/install-log",
                         "/workspace/.orizon/install-log", 4096);
    term_print_file_tail(term, "/workspace/.orizon/rollback-info",
                         "/workspace/.orizon/rollback-info", 4096);
    return;
  }

  term_puts_t(term, "usage: logs [boot|network|ssh|update|install|all]\n");
}

static void term_print_diagnostic_hints(terminal_t *term) {
  int any = 0;

  if (!storage_available()) {
    if (!any) {
      term_puts_t(term, "Hints:\n");
      any = 1;
    }
    term_puts_t(term,
                "  - No AHCI/NVMe disk is ready; install/update need writable storage.\n");
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

static void term_run_update(terminal_t *term) {
  static char report[8192];

  if (!term_install_already_complete()) {
    term_puts_t(term,
                "update: unavailable in live boot. Install Orizon OS first.\n");
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

static void term_pkg_help(terminal_t *term) {
  term_puts_t(term, "\033[1;36mOrizon packages\033[0m\n");
  term_puts_t(term, "  pkg list          - List installed packages\n");
  term_puts_t(term, "  pkg status        - Show package manager state\n");
  term_puts_t(term, "  pkg info <name>   - Show package metadata/files\n");
  term_puts_t(term, "  pkg sample        - Create a sample .opkg package\n");
  term_puts_t(term, "  pkg hash <file>   - Print package payload sha256\n");
  if (term_install_already_complete()) {
    term_puts_t(term, "  pkg install <file> - Install a verified local package\n");
    term_puts_t(term, "  pkg remove <name> - Remove an installed package\n");
  } else {
    term_puts_t(term,
                "  pkg install <file> - Available after disk install only\n");
    term_puts_t(term,
                "  pkg remove <name> - Available after disk install only\n");
  }
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

  if (term_command_is(args, "sample")) {
    orizon_pkg_write_sample(report, sizeof(report));
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

  if (term_command_is(args, "install")) {
    char path[MAX_PATH];
    const char *requested = term_skip_spaces(args + 7);
    if (!term_install_already_complete()) {
      term_puts_t(term,
                  "pkg install: unavailable in live boot. Install Orizon OS first.\n");
      return;
    }
    if (*requested == '\0') {
      term_puts_t(term, "usage: pkg install <file>\n");
      return;
    }
    if (resolve_path(term->cwd, requested, path, sizeof(path)) < 0) {
      term_puts_t(term, "pkg install: invalid path\n");
      return;
    }
    orizon_pkg_install_file(path, report, sizeof(report));
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
    orizon_pkg_remove(name, report, sizeof(report));
    term_puts_t(term, report);
    return;
  }

  term_puts_t(term, "pkg: unknown command. Try 'pkg help'.\n");
}

static void term_print_net_status(terminal_t *term) {
  char line[256];
  net_format_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  wifi_format_status(line, sizeof(line));
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

static void term_run_net(terminal_t *term, const char *cmd) {
  const char *args = term_skip_spaces(cmd + 3);
  char line[256];

  if (term_command_is(args, "dhcp")) {
    term_puts_t(term, "net: configuring IPv4 with DHCP...\n");
    netstack_reset();
    if (netstack_configure_ipv4_dhcp() == 0) {
      term_puts_t(term, "net: DHCP configured\n");
    } else {
      term_puts_t(term, "net: DHCP failed\n");
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

  term_puts_t(term,
              "usage: wifi [status|hw|apm|firmware|load|upload [arm|all [arm]]|boot [arm]|alive|queues [arm]|context [arm]|scheduler [arm]|rx [poll]|command [arm]|nvm [arm]|nvm-info [arm]|bringup|crypto|wpa|key [pairwise|gtk] [arm]|data|bind [arm]|scan [arm|poll]|connect <ssid> [password]|join <ssid> [password]|tx [auth|assoc|m2|m4|data|all]|txcmd [auth|assoc|m2|m4|data] [arm]]\n");
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
  char state[256];

  if (vfs_exists("/workspace/.orizon/installed")) {
    return 1;
  }
  if (term_read_text_file_silent("/workspace/.orizon/install-state", state,
                                 sizeof(state)) > 0 &&
      strstr(state, "install complete")) {
    return 1;
  }
  return 0;
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
    term_puts_t(term, "No AHCI/NVMe disks detected.\n");
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
    term_puts_t(term, "No selected writable AHCI/NVMe disk.\n");
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
    term_puts_t(term, "[1/7] Language\n");
    term_puts_t(term, "  1. Francais\n");
    term_puts_t(term, "  2. English\n");
    term_puts_t(term, "Choice: ");
    break;
  case 1:
    term_puts_t(term, "[2/7] Keyboard layout\n");
    term_puts_t(term, "  1. fr-azerty\n");
    term_puts_t(term, "  2. us-qwerty\n");
    term_puts_t(term, "Choice: ");
    break;
  case 2:
    term_puts_t(term, "[3/7] Target disk\n");
    term_print_disks(term);
    term_puts_t(term, "  m. manual-later (do not write disk)\n");
    term_puts_t(term, "Choose target disk number, or m: ");
    break;
  case 3:
    term_puts_t(term, "[4/7] Disk strategy\n");
    term_puts_t(term,
                "  1. dual-boot-data (preserve disk, use selected partition for Orizon)\n");
    term_puts_t(term,
                "  2. dual-boot-esp (preserve disk, write /EFI/Orizon only)\n");
    term_puts_t(term,
                "  3. guided-full-disk (ERASE target disk, full Orizon install)\n");
    term_puts_t(term, "Choice [1]: ");
    break;
  case 4:
    term_puts_t(term, "[5/7] Orizon data partition\n");
    term_puts_t(term,
                "Choose the empty/prepared partition Orizon may claim and overwrite.\n");
    term_print_partitions(term);
    term_puts_t(term, "Partition number: ");
    break;
  case 5:
    term_puts_t(term, "[6/7] Hostname\n");
    term_puts_t(term, "Hostname [orizon-os]: ");
    break;
  case 6: {
    char line[160];
    term_puts_t(term, "[7/7] Summary\n");
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
    snprintf(line, sizeof(line), "  Mode:     %s\n", term->install_disk_mode);
    term_puts_t(term, line);
    if (strcmp(term->install_disk_mode, "dual-boot-data") == 0) {
      snprintf(line, sizeof(line), "  Data:     %s\n",
               term->install_data_partition_summary);
      term_puts_t(term, line);
    }
    if (strcmp(term->install_disk_mode, "manual-later") == 0) {
      term_puts_t(term, "Type SAVE to store the plan, or cancel to abort: ");
    } else if (strcmp(term->install_disk_mode, "dual-boot-data") == 0) {
      snprintf(line, sizeof(line),
               "Type DUALDATA %s %s to claim that partition, or cancel to abort: ",
               term->install_disk_name, term->install_data_partition_name);
      term_puts_t(term, line);
    } else if (strcmp(term->install_disk_mode, "dual-boot-esp") == 0) {
      snprintf(line, sizeof(line),
               "Type DUALBOOT %s to write /EFI/Orizon only, or cancel to abort: ",
               term->install_disk_name);
      term_puts_t(term, line);
    } else {
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

  snprintf(plan, sizeof(plan),
           "installer-version 1\n"
           "os Orizon OS\n"
           "source live-iso\n"
           "language %s\n"
           "keyboard %s\n"
           "hostname %s\n"
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
           term->install_hostname, term->install_disk_mode,
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
           "install configured: language=%s keyboard=%s disk=%s hostname=%s\n",
           term->install_language, term->install_keyboard,
           term->install_disk_mode, term->install_hostname);

  if (term_write_text_file("/workspace/.orizon/install-plan", plan) < 0 ||
      term_write_text_file("/workspace/.orizon/install-state", state) < 0 ||
      term_write_text_file("/workspace/.orizon/keyboard",
                           term->install_keyboard) < 0 ||
      term_write_text_file("/system/install-state", state) < 0 ||
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

  config.language = term->install_language;
  config.keyboard = term->install_keyboard;
  config.disk_mode = term->install_disk_mode;
  config.hostname = term->install_hostname;
  config.disk_index = term->install_disk_index;
  config.disk_name = term->install_disk_name;
  config.data_partition_index = term->install_data_partition_index;
  term_puts_t(term, "\n");
  if (orizon_install_run(&config, install_report, sizeof(install_report)) == 0) {
    term_puts_t(term, install_report);
    term_write_text_file("/workspace/.orizon/install-log", install_report);
    if (strcmp(term->install_disk_mode, "dual-boot-esp") == 0) {
      term_write_text_file("/workspace/.orizon/dualboot-prepared",
                           "dual boot ESP prepared\n"
                           "boot-file /EFI/Orizon/BOOTX64.EFI\n"
                           "data not-installed\n");
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
             "mode=%s\ndata-partition=%s\n"
             "next=shutdown-remove-installer\n",
             term->install_language, term->install_keyboard,
             term->install_hostname, term->install_disk_mode, data_name);
    term_write_text_file("/workspace/.orizon/installed", marker);
    term_write_text_file("/workspace/.orizon/install-state",
                         "install complete\nnext shutdown-remove-installer\n");
    term_write_text_file("/workspace/.orizon/keyboard",
                         term->install_keyboard);
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
  term_puts_t(term, "\n");
  term_install_prompt(term);
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
  if (term->scroll_offset == 0 && (blink / 15) % 2 == 0) {
    int cx = base_x + term->cursor_x * TERM_CHAR_W;
    int cy = base_y + term->cursor_y * TERM_CHAR_H;
    fb_fill_rect(cx, cy + TERM_CHAR_H - 2, TERM_CHAR_W, 2, term_colors[7]);
  }
}

/* Execute command */
void term_execute(terminal_t *term, const char *cmd) {
  /* Skip whitespace */
  while (*cmd == ' ') cmd++;
  if (*cmd == '\0') return;
  
  term_add_history(term, cmd);
  
  term_puts_t(term, "\n");
  
  if (strncmp(cmd, "help", 4) == 0) {
    term_puts_t(term, "\033[1;36mOrizon OS Core Console\033[0m\n");
    term_puts_t(term, "\033[33mFile Commands:\033[0m\n");
    term_puts_t(term, "  ls        - List directory contents\n");
    term_puts_t(term, "  cd <dir>  - Change directory\n");
    term_puts_t(term, "  pwd       - Print working directory\n");
    term_puts_t(term, "  cat <f>   - Display file contents\n");
    term_puts_t(term, "  head [-n] <f> - Show first lines\n");
    term_puts_t(term, "  grep <text> <f> - Search file text\n");
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
    term_puts_t(term, "  sync      - Save /workspace to disk\n");
    term_puts_t(term, "\033[33mPackages:\033[0m\n");
    term_puts_t(term, "  pkg list/status - Show installed package data\n");
    term_puts_t(term, "  pkg info <name> - Show package metadata/files\n");
    term_puts_t(term, "  pkg sample      - Create a sample .opkg package\n");
    term_puts_t(term, "  pkg hash <file> - Print package payload sha256\n");
    if (term_install_already_complete()) {
      term_puts_t(term, "  pkg install <file> - Install a verified package\n");
      term_puts_t(term, "  pkg remove <name> - Remove an installed package\n");
    }
    term_puts_t(term, "\033[33mSystem:\033[0m\n");
    term_puts_t(term, "  dmesg     - Show current kernel boot log\n");
    term_puts_t(term, "  sysinfo   - Compact OS/hardware/storage summary\n");
    term_puts_t(term, "  hw        - Hardware diagnostics\n");
    term_puts_t(term, "  pci [bars] - List PCI devices and driver hints\n");
    term_puts_t(term, "  input     - Keyboard/pointer/input bus diagnostics\n");
    term_puts_t(term, "  logs [name] - Read recent boot/network/update/install logs\n");
    term_puts_t(term, "  report    - Compact health report + log tail\n");
    term_puts_t(term, "  mounts    - Show Orizon data roots\n");
    term_puts_t(term, "  storage   - Show disk and persistence state\n");
    term_puts_t(term, "  disks     - List detected install disks\n");
    term_puts_t(term, "  partitions - List GPT partitions on selected disk\n");
    term_puts_t(term, "  storage select <n> - Select active disk\n");
    term_puts_t(term, "  net       - Show ethernet/IP status\n");
    term_puts_t(term, "  net dhcp  - Request IPv4 config from DHCP\n");
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
    term_puts_t(term, "  install-status - Show installer plan/state\n");
    term_puts_t(term, "  boot-check - Verify installed disk boot files\n");
    term_puts_t(term, "  dualboot-check - Verify /EFI/Orizon side-by-side boot files\n");
    term_puts_t(term, "  repair-boot - Rewrite installed boot files\n");
    term_puts_t(term, "  keyboard [fr|us] - Show or change keyboard layout\n");
    term_puts_t(term, "  shutdown  - Save /workspace and power off\n");
    if (term_install_already_complete()) {
      term_puts_t(term, "  update    - Run Orizon full-upgrade\n");
      term_puts_t(term, "  rollback  - Restore the booted rollback slot\n");
      term_puts_t(term, "  rollback-status - Show rollback metadata\n");
    }
    term_puts_t(term, "  about     - Show Orizon build details\n");
    term_puts_t(term, "  version   - Show kernel build version\n");
    term_puts_t(term, "  neofetch  - System info\n");
    term_puts_t(term, "  uname     - Show OS info\n");
    term_puts_t(term, "  id        - Show user/group info\n");
    term_puts_t(term, "  hostname  - Show hostname\n");
    term_puts_t(term, "  history [-c] - Show or clear persistent history\n");
    term_puts_t(term, "  free      - Memory usage\n");
    term_puts_t(term, "  ps        - Process list\n");
    term_puts_t(term, "  clear     - Clear screen\n");
    term_puts_t(term, "  help      - This help message\n");
    term_puts_t(term, "\n");
    term_puts_t(term, "This build intentionally starts from a minimal core shell.\n");
    term_puts_t(term, "Tip: Tab completes commands/files; Up/Down browse saved history.\n");
    term_puts_t(term, "Add new tools only when they belong in Orizon OS.\n");
  } else if (strncmp(cmd, "clear", 5) == 0) {
    for (int row = 0; row < TERM_ROWS; row++) term_clear_line(term, row);
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->scroll_offset = 0;
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
  } else if (term_command_is(cmd, "hw")) {
    term_print_hw(term);
  } else if (term_command_is(cmd, "pci")) {
    term_print_pci(term, cmd);
  } else if (term_command_is(cmd, "input")) {
    term_print_input_status(term);
  } else if (term_command_is(cmd, "logs")) {
    term_print_log_summary(term, cmd);
  } else if (term_command_is(cmd, "report")) {
    if (term_install_already_complete()) {
      klog_persist_boot_if_installed();
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
  } else if (term_command_is(cmd, "grep")) {
    char pattern[MAX_PATH];
    char file_arg[MAX_PATH];
    char path[MAX_PATH];

    if (term_split_two_paths(cmd + 4, pattern, file_arg, sizeof(file_arg)) < 0) {
      term_puts_t(term, "usage: grep <text> <file>\n");
      return;
    }
    if (resolve_path(term->cwd, file_arg, path, sizeof(path)) < 0) {
      term_puts_t(term, "grep: invalid path\n");
      return;
    }
    term_print_grep(term, pattern, file_arg, path);
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
  } else if (term_command_is(cmd, "disks")) {
    term_puts_t(term, "\033[1;36mDetected disks\033[0m\n");
    term_print_disks(term);
  } else if (term_command_is(cmd, "partitions")) {
    term_print_partitions(term);
  } else if (term_command_is(cmd, "mounts")) {
    term_print_mounts(term);
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
    if (term_command_is(args, "detail") || term_command_is(args, "list")) {
      term_puts_t(term, "\033[1;36mStorage detail\033[0m\n");
      term_print_disks(term);
    }
    storage_format_capacity(capacity, sizeof(capacity));
    term_puts_t(term, "Disk: ");
    term_puts_t(term, storage_available() ? storage_status()
                                          : "no writable AHCI/NVMe disk");
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
  } else if (term_command_is(cmd, "install-status")) {
    char buf[2048];
    int n = term_read_regular_file(term, "install-plan",
                                   "/workspace/.orizon/install-plan", buf,
                                   sizeof(buf), "install-status");
    if (n > 0) {
      buf[n] = '\0';
      term_puts_t(term, buf);
      if (buf[n - 1] != '\n') {
        term_puts_t(term, "\n");
      }
    }
    n = term_read_text_file_silent("/workspace/.orizon/install-state", buf,
                                   sizeof(buf));
    if (n > 0) {
      term_puts_t(term, "state: ");
      term_puts_t(term, buf);
      if (buf[n - 1] != '\n') {
        term_puts_t(term, "\n");
      }
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
  } else if (term_command_is(cmd, "shutdown") ||
             term_command_is(cmd, "poweroff")) {
    vfs_persist_save();
    term_puts_t(term,
                "SHUTDOWN in 3 seconds.\n"
                "If this was an install boot, remove/eject the ISO or USB media now.\n");
    power_schedule_shutdown(TIMER_HZ * 3);
  } else if (term_command_is(cmd, "update") || term_command_is(cmd, "orizon-update")) {
    term_run_update(term);
  } else if (term_command_is(cmd, "rollback")) {
    term_run_rollback(term);
  } else if (term_command_is(cmd, "rollback-status")) {
    char buf[1024];
    int n = term_read_regular_file(term, "rollback-info",
                                   "/workspace/.orizon/rollback-info", buf,
                                   sizeof(buf), "rollback-status");
    if (n > 0) {
      buf[n] = '\0';
      term_puts_t(term, buf);
      if (buf[n - 1] != '\n') {
        term_puts_t(term, "\n");
      }
    }
  } else if (term_command_is(cmd, "pkg")) {
    term_run_pkg(term, cmd);
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
    if (strcmp(term_skip_spaces(cmd + 7), "-c") == 0) {
      term->history_count = 0;
      term->history_pos = 0;
      term_save_history(term);
      term_puts_t(term, "History cleared\n");
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
  } else if (strncmp(cmd, "hostname", 8) == 0) {
    term_puts_t(term, "orizon-os\n");
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

/* Print prompt */
void term_prompt(terminal_t *term) {
  term_prompt_prefix(term);
  term_prepare_input(term);
}

/* Handle keyboard input */
void term_handle_key(terminal_t *term, int key) {
  if (!term) return;

  if (!term->edit_mode && !term->install_mode) {
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
    if (!term->edit_mode && !term->install_mode) {
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
