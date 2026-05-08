/*
 * Orizon OS x86_64 - VT100 Terminal Emulator
 */

#include "../include/gui.h"
#include "../include/input_layout.h"
#include "../include/kmalloc.h"
#include "../include/install.h"
#include "../include/net.h"
#include "../include/netstack.h"
#include "../include/packages.h"
#include "../include/power.h"
#include "../include/ps2.h"
#include "../include/sched.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/terminal.h"
#include "../include/timer.h"
#include "../include/update.h"
#include "../include/vfs.h"

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
  char install_hostname[64];
  
  /* History */
  char history[16][256];
  int history_count;
  int history_pos;
} terminal_t;

static terminal_t *active_term = NULL;

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
  const char *p;

  if (!input || out_size < 2) {
    return -1;
  }

  while (*input == ' ') {
    input++;
  }
  if (*input == '\0') {
    return -1;
  }

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

static void term_add_history(terminal_t *term, const char *cmd) {
  if (!cmd || *cmd == '\0') {
    return;
  }
  if (term->history_count > 0 &&
      strcmp(term->history[term->history_count - 1], cmd) == 0) {
    term->history_pos = term->history_count;
    return;
  }

  if (term->history_count >= 16) {
    for (int i = 1; i < 16; i++) {
      strncpy(term->history[i - 1], term->history[i], 255);
      term->history[i - 1][255] = '\0';
    }
    term->history_count = 15;
  }

  strncpy(term->history[term->history_count], cmd, 255);
  term->history[term->history_count][255] = '\0';
  term->history_count++;
  term->history_pos = term->history_count;
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
  term_puts_t(term, "Type lines to append. Commands: .save, .q, .clear\n");
  if (term->edit_len > 0) {
    term_puts_t(term, "Loaded existing file; new lines append at the end.\n");
  }
  term_editor_prompt(term);
}

static void term_editor_submit(terminal_t *term, const char *line) {
  term_puts_t(term, "\n");

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

  if (strcmp(line, ".save") == 0) {
    file_t *f = vfs_open(term->edit_path, O_CREAT | O_WRONLY | O_TRUNC);
    if (!f) {
      term_puts_t(term, "edit: save failed\n");
      term_editor_prompt(term);
      return;
    }
    if (term->edit_len > 0 &&
        vfs_write(f, term->edit_buf, term->edit_len) < 0) {
      term_puts_t(term, "edit: write error\n");
      vfs_close(f);
      term_editor_prompt(term);
      return;
    }
    vfs_close(f);
    term->edit_mode = 0;
    term_puts_t(term, "Saved: ");
    term_puts_t(term, term->edit_path);
    term_puts_t(term, "\n");
    return;
  }

  size_t len = strlen(line);
  if (term->edit_len + len + 1 >= TERM_EDIT_MAX) {
    term_puts_t(term, "edit: buffer full\n");
    term_editor_prompt(term);
    return;
  }

  memcpy(term->edit_buf + term->edit_len, line, len);
  term->edit_len += len;
  term->edit_buf[term->edit_len++] = '\n';
  term->edit_buf[term->edit_len] = '\0';
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

static int term_install_already_complete(void);

static void term_run_update(terminal_t *term) {
  static char report[8192];

  if (!term_install_already_complete()) {
    term_puts_t(term,
                "update: unavailable in live boot. Install Orizon OS first.\n");
    return;
  }
  orizon_update_full_upgrade(report, sizeof(report));
  term_puts_t(term, report);
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
  term_puts_t(term, "  pkg sample        - Create a sample .opkg package\n");
  term_puts_t(term, "  pkg hash <file>   - Print package payload sha256\n");
  if (term_install_already_complete()) {
    term_puts_t(term, "  pkg install <file> - Install a verified local package\n");
  } else {
    term_puts_t(term,
                "  pkg install <file> - Available after disk install only\n");
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

  term_puts_t(term, "pkg: unknown command. Try 'pkg help'.\n");
}

static void term_print_net_status(terminal_t *term) {
  char line[256];
  net_format_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
  netstack_format_status(line, sizeof(line));
  term_puts_t(term, line);
  term_puts_t(term, "\n");
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

static void term_install_prompt(terminal_t *term) {
  switch (term->install_step) {
  case 0:
    term_puts_t(term, "\033[1;36mOrizon OS Installer\033[0m\n");
    term_puts_t(term, "This guided installer can install Orizon OS to disk.\n");
    term_puts_t(term,
                "guided-full-disk rewrites the target disk layout.\n\n");
    term_puts_t(term, "[1/5] Language\n");
    term_puts_t(term, "  1. Francais\n");
    term_puts_t(term, "  2. English\n");
    term_puts_t(term, "Choice: ");
    break;
  case 1:
    term_puts_t(term, "[2/5] Keyboard layout\n");
    term_puts_t(term, "  1. fr-azerty\n");
    term_puts_t(term, "  2. us-qwerty\n");
    term_puts_t(term, "Choice: ");
    break;
  case 2:
    term_puts_t(term, "[3/5] Disk configuration\n");
    term_puts_t(term, "Detected storage: ");
    term_puts_t(term, storage_available() ? storage_status()
                                          : "no writable AHCI disk");
    term_puts_t(term, "\n");
    term_puts_t(term, "  1. guided-full-disk\n");
    term_puts_t(term, "  2. manual-later\n");
    term_puts_t(term, "Choice: ");
    break;
  case 3:
    term_puts_t(term, "[4/5] Hostname\n");
    term_puts_t(term, "Hostname [orizon-os]: ");
    break;
  case 4: {
    char line[160];
    term_puts_t(term, "[5/5] Summary\n");
    snprintf(line, sizeof(line), "  Language: %s\n", term->install_language);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Keyboard: %s\n", term->install_keyboard);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Disk:     %s\n", term->install_disk_mode);
    term_puts_t(term, line);
    snprintf(line, sizeof(line), "  Hostname: %s\n", term->install_hostname);
    term_puts_t(term, line);
    term_puts_t(term,
                "Type INSTALL to write the disk, or cancel to abort: ");
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

  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/system");

  snprintf(plan, sizeof(plan),
           "installer-version 1\n"
           "os Orizon OS\n"
           "source live-iso\n"
           "language %s\n"
           "keyboard %s\n"
           "hostname %s\n"
           "disk-mode %s\n"
           "disk-status %s\n"
           "boot-strategy uefi-fallback-esp\n"
           "write-mode %s\n"
           "next reboot-installed-disk\n",
           term->install_language, term->install_keyboard,
           term->install_hostname, term->install_disk_mode,
           storage_available() ? storage_status() : "unavailable",
           strcmp(term->install_disk_mode, "manual-later") == 0
               ? "plan-only-no-disk-write"
               : "destructive-full-disk");

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

  if (strcmp(term->install_disk_mode, "manual-later") == 0) {
    vfs_persist_save();
    term_puts_t(term, "\nInstaller plan saved for manual disk work.\n");
    term_install_finish(term, 1);
    return;
  }

  term_puts_t(term, "\nSaving /workspace before disk write...\n");
  if (vfs_persist_save() < 0) {
    term_puts_t(term,
                "install: cannot save /workspace, aborting to avoid data loss\n");
    term_install_finish(term, 0);
    return;
  }

  config.language = term->install_language;
  config.keyboard = term->install_keyboard;
  config.disk_mode = term->install_disk_mode;
  config.hostname = term->install_hostname;
  term_puts_t(term, "\n");
  if (orizon_install_run(&config, install_report, sizeof(install_report)) == 0) {
    term_puts_t(term, install_report);
    term_write_text_file("/workspace/.orizon/install-log", install_report);
    snprintf(marker, sizeof(marker),
             "Orizon OS installed\nlanguage=%s\nkeyboard=%s\nhostname=%s\n"
             "next=shutdown-remove-installer\n",
             term->install_language, term->install_keyboard,
             term->install_hostname);
    term_write_text_file("/workspace/.orizon/installed", marker);
    term_write_text_file("/workspace/.orizon/install-state",
                         "install complete\nnext shutdown-remove-installer\n");
    term_write_text_file("/workspace/.orizon/keyboard",
                         term->install_keyboard);
    term_write_text_file("/system/install-state", "install complete\n");
    term_write_text_file("/system/installed", "1\n");
    vfs_persist_save();
    term_install_finish(term, 1);
    term_puts_t(term,
                "SHUTDOWN in 5 seconds.\n"
                "Remove/eject the ISO or USB installer before the next boot.\n"
                "Then start the machine again to boot from the installed disk.\n");
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
    if (term_install_value_is(value, "1", "guided", "full") ||
        strcmp(value, "&") == 0) {
      strcpy(term->install_disk_mode, "guided-full-disk");
    } else if (term_install_value_is(value, "2", "manual", "later") ||
               strcmp(value, "e") == 0) {
      strcpy(term->install_disk_mode, "manual-later");
    } else {
      term_puts_t(term, "Choose 1 or 2.\n");
      term_install_prompt(term);
      return;
    }
    term->install_step++;
    term_install_prompt(term);
    return;
  case 3:
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
  case 4:
    if (strcmp(value, "INSTALL") == 0 || strcmp(value, "install") == 0) {
      term_install_write_plan(term);
    } else {
      term_puts_t(term, "Confirmation refused. Type INSTALL exactly.\n");
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
    term_puts_t(term, "  edit <f>  - Open the line editor\n");
    term_puts_t(term, "  touch <f> - Create empty file\n");
    term_puts_t(term, "  write <f> <text>  - Replace file text\n");
    term_puts_t(term, "  append <f> <text> - Append file text\n");
    term_puts_t(term, "  mkdir <d> - Create directory\n");
    term_puts_t(term, "  rm <f>    - Remove file\n");
    term_puts_t(term, "  sync      - Save /workspace to disk\n");
    term_puts_t(term, "\033[33mPackages:\033[0m\n");
    term_puts_t(term, "  pkg list/status - Show installed package data\n");
    term_puts_t(term, "  pkg sample      - Create a sample .opkg package\n");
    term_puts_t(term, "  pkg hash <file> - Print package payload sha256\n");
    if (term_install_already_complete()) {
      term_puts_t(term, "  pkg install <file> - Install a verified package\n");
    }
    term_puts_t(term, "\033[33mSystem:\033[0m\n");
    term_puts_t(term, "  storage   - Show persistence state\n");
    term_puts_t(term, "  net       - Show ethernet status\n");
    term_puts_t(term, "  install   - Start guided disk installer\n");
    term_puts_t(term, "  install-status - Show installer plan/state\n");
    term_puts_t(term, "  keyboard  - Show active keyboard layout\n");
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
    term_puts_t(term, "  history   - Show command history\n");
    term_puts_t(term, "  free      - Memory usage\n");
    term_puts_t(term, "  ps        - Process list\n");
    term_puts_t(term, "  clear     - Clear screen\n");
    term_puts_t(term, "  help      - This help message\n");
    term_puts_t(term, "\n");
    term_puts_t(term, "This build intentionally starts from a minimal core shell.\n");
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
      term_puts_t(term, "Synced /workspace to disk\n");
    } else {
      term_puts_t(term, "sync: persistence unavailable\n");
    }
  } else if (term_command_is(cmd, "storage")) {
    term_puts_t(term, vfs_persist_status());
    term_puts_t(term, "\n");
  } else if (term_command_is(cmd, "keyboard")) {
    term_puts_t(term, "Keyboard layout: ");
    term_puts_t(term, input_keyboard_layout());
    term_puts_t(term, "\n");
  } else if (term_command_is(cmd, "net")) {
    term_print_net_status(term);
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
  } else if (strncmp(cmd, "history", 7) == 0) {
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
  const char *cwd = (term && term->cwd[0]) ? term->cwd : "/";
  term_puts_t(term, "\033[32morizon-os\033[0m:");
  term_puts_t(term, "\033[34m");
  term_puts_t(term, cwd);
  term_puts_t(term, "\033[0m$ ");
  term_prepare_input(term);
}

/* Handle keyboard input */
void term_handle_key(terminal_t *term, int key) {
  if (!term) return;

  if (!term->edit_mode && !term->install_mode) {
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
