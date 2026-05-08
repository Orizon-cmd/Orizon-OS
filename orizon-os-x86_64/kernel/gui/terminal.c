/*
 * Orizon OS x86_64 - VT100 Terminal Emulator
 */

#include "../include/gui.h"
#include "../include/kmalloc.h"
#include "../include/string.h"
#include "../include/terminal.h"
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

/* Terminal state */
typedef struct terminal {
  char chars[TERM_ROWS * TERM_COLS];
  uint8_t fg_colors[TERM_ROWS * TERM_COLS];
  uint8_t bg_colors[TERM_ROWS * TERM_COLS];
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
  char cwd[256];

  /* Line editor */
  int edit_mode;
  char edit_path[MAX_PATH];
  char edit_buf[TERM_EDIT_MAX];
  size_t edit_len;
  
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

/* Scroll up */
static void term_scroll_up(terminal_t *term) {
  for (int row = 0; row < TERM_ROWS - 1; row++) {
    memcpy(&term->chars[row * TERM_COLS], &term->chars[(row + 1) * TERM_COLS], TERM_COLS);
    memcpy(&term->fg_colors[row * TERM_COLS], &term->fg_colors[(row + 1) * TERM_COLS], TERM_COLS);
    memcpy(&term->bg_colors[row * TERM_COLS], &term->bg_colors[(row + 1) * TERM_COLS], TERM_COLS);
  }
  term_clear_line(term, TERM_ROWS - 1);
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

static void term_print_about(terminal_t *term) {
  char line[128];

  term_puts_t(term, "\033[1;36mOrizon OS\033[0m\n");
  term_puts_t(term, "Profile: Minimal development base\n");
  term_puts_t(term, "Kernel: core-x86_64\n");
  snprintf(line, sizeof(line), "Console: %dx%d\n", TERM_COLS, TERM_ROWS);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Display: %lux%lu\n",
           (unsigned long)screen_width, (unsigned long)screen_height);
  term_puts_t(term, line);
  snprintf(line, sizeof(line), "Heap used: %lu bytes\n",
           (unsigned long)kmalloc_get_used());
  term_puts_t(term, line);
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
      int idx = row * TERM_COLS + col;
      term_draw_char(base_x + col * TERM_CHAR_W, base_y + row * TERM_CHAR_H,
                     term->chars[idx],
                     term_colors[term->fg_colors[idx] & 0xF],
                     term_colors[term->bg_colors[idx] & 0xF]);
    }
  }
  
  /* Cursor */
  static int blink = 0;
  blink++;
  if ((blink / 15) % 2 == 0) {
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
  
  /* Save to history */
  if (term->history_count < 16) {
    strncpy(term->history[term->history_count++], cmd, 255);
  }
  
  term_puts_t(term, "\n");
  
  if (strncmp(cmd, "help", 4) == 0) {
    term_puts_t(term, "\033[1;36mOrizon OS Core Console\033[0m\n");
    term_puts_t(term, "\033[33mFile Commands:\033[0m\n");
    term_puts_t(term, "  ls        - List directory contents\n");
    term_puts_t(term, "  cd <dir>  - Change directory\n");
    term_puts_t(term, "  pwd       - Print working directory\n");
    term_puts_t(term, "  cat <f>   - Display file contents\n");
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
    term_puts_t(term, "\033[33mSystem:\033[0m\n");
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
  } else if (strncmp(cmd, "echo ", 5) == 0) {
    term_puts_t(term, cmd + 5);
    term_puts_t(term, "\n");
  } else if (strncmp(cmd, "neofetch", 8) == 0) {
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
    term_puts_t(term, "\033[33mUptime:\033[0m  0 mins\n");
    term_puts_t(term, "\033[33mShell:\033[0m   Orizon console\n");
    term_puts_t(term, "\033[33mMemory:\033[0m  64 MB heap\n");
    term_puts_t(term, "\033[33mCPU:\033[0m     x86_64\n");
    term_puts_t(term, "\033[33mProfile:\033[0m Minimal development base\n");
  } else if (strncmp(cmd, "uname", 5) == 0) {
    if (strstr(cmd, "-a")) {
      term_puts_t(term, "Orizon OS core-x86_64 x86_64\n");
    } else {
      term_puts_t(term, "Orizon OS\n");
    }
  } else if (strncmp(cmd, "free", 4) == 0) {
    term_puts_t(term, "              total        used        free\n");
    char buf[64];
    snprintf(buf, 64, "Mem:       %8lu  %8lu  %8lu\n", 
             (unsigned long)(64*1024*1024), 
             kmalloc_get_used(), 
             kmalloc_get_free());
    term_puts_t(term, buf);
  } else if (strncmp(cmd, "ps", 2) == 0) {
    term_puts_t(term, "  PID TTY          TIME CMD\n");
    term_puts_t(term, "    1 ?        00:00:00 kernel\n");
    term_puts_t(term, "    2 tty1     00:00:00 shell\n");
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
    term_puts_t(term, " 00:00:00 up 0 min, 1 user\n");
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
}

/* Handle keyboard input */
void term_handle_key(terminal_t *term, int key) {
  if (!term) return;
  
  if (key == '\n' || key == '\r') {
    term->input_buf[term->input_len] = '\0';
    if (term->edit_mode) {
      term_editor_submit(term, term->input_buf);
    } else {
      term_execute(term, term->input_buf);
    }
    term->input_len = 0;
    if (!term->edit_mode) {
      term_prompt(term);
    }
  } else if (key == '\b' || key == 127) {
    if (term->input_len > 0) {
      term->input_len--;
      term->cursor_x--;
      term->chars[term->cursor_y * TERM_COLS + term->cursor_x] = ' ';
    }
  } else if (key >= 32 && key < 127 && term->input_len < 255) {
    term->input_buf[term->input_len++] = (char)key;
    term_putc(term, (char)key);
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
              "This VM boots into a clean personal base with the console ready first.\n\n");
  term_prompt(term);
  
  return term;
}

/* Get/set active terminal */
terminal_t *term_get_active(void) { return active_term; }
void term_set_active(terminal_t *term) { active_term = term; }
