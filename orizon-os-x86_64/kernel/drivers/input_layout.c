/*
 * Orizon OS x86_64 - Keyboard layout mapping
 */

#include "../include/input_layout.h"
#include "../include/string.h"
#include "../include/vfs.h"

enum {
  INPUT_LAYOUT_US_QWERTY = 0,
  INPUT_LAYOUT_FR_AZERTY = 1,
};

static int current_layout = INPUT_LAYOUT_US_QWERTY;

static const char ps2_us[128] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
};

static const char ps2_us_shift[128] = {
    0,    27,   '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
};

static const char ps2_fr[128] = {
    0,    27,   '&',  'e',  '"',  '\'', '(',  '-',
    'e',  '_',  'c',  'a',  ')',  '=',  '\b', '\t',
    'a',  'z',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '^',  '$',  '\n', 0,    'q',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  'm',
    'u',  '`',  0,    '*',  'w',  'x',  'c',  'v',
    'b',  'n',  ',',  ';',  ':',  '!',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
};

static const char ps2_fr_shift[128] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  ')',  '+',  '\b', '\t',
    'A',  'Z',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '[',  ']',  '\n', 0,    'Q',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  'M',
    '%',  '~',  0,    '|',  'W',  'X',  'C',  'V',
    'B',  'N',  '?',  '.',  '/',  '?',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
};

static int is_lower(char c) {
  return c >= 'a' && c <= 'z';
}

static int apply_case(char c, int shift, int caps_lock) {
  if (is_lower(c) && (shift ^ caps_lock)) {
    return c - ('a' - 'A');
  }
  return c;
}

static int layout_is_fr_name(const char *layout) {
  return layout &&
         (strncmp(layout, "fr", 2) == 0 || strstr(layout, "azerty") != NULL);
}

void input_set_keyboard_layout(const char *layout) {
  current_layout = layout_is_fr_name(layout) ? INPUT_LAYOUT_FR_AZERTY
                                             : INPUT_LAYOUT_US_QWERTY;
}

const char *input_keyboard_layout(void) {
  return current_layout == INPUT_LAYOUT_FR_AZERTY ? "fr-azerty" : "us-qwerty";
}

static int read_text_file(const char *path, char *buf, size_t cap) {
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

void input_load_keyboard_layout_from_vfs(void) {
  char buf[256];
  char *line;

  if (read_text_file("/workspace/.orizon/keyboard", buf, sizeof(buf)) > 0) {
    input_set_keyboard_layout(buf);
    return;
  }

  if (read_text_file("/workspace/.orizon/install-plan", buf, sizeof(buf)) <= 0) {
    return;
  }
  line = strstr(buf, "keyboard ");
  if (line) {
    input_set_keyboard_layout(line + 9);
  }
}

int input_map_ps2_scancode(uint8_t scancode, int shift, int caps_lock) {
  const char *normal = current_layout == INPUT_LAYOUT_FR_AZERTY ? ps2_fr : ps2_us;
  const char *shifted =
      current_layout == INPUT_LAYOUT_FR_AZERTY ? ps2_fr_shift : ps2_us_shift;
  char base;

  if (scancode >= 128) {
    return 0;
  }

  base = normal[scancode];
  if (is_lower(base)) {
    return apply_case(base, shift, caps_lock);
  }
  return shift ? shifted[scancode] : base;
}

static int hid_us(uint8_t usage, int shift, int caps_lock) {
  static const char normal[128] = {
      [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4',
      [0x22] = '5', [0x23] = '6', [0x24] = '7', [0x25] = '8',
      [0x26] = '9', [0x27] = '0', [0x28] = '\n', [0x2B] = '\t',
      [0x2C] = ' ', [0x2D] = '-', [0x2E] = '=', [0x2F] = '[',
      [0x30] = ']', [0x31] = '\\', [0x32] = '#', [0x33] = ';',
      [0x34] = '\'', [0x35] = '`', [0x36] = ',', [0x37] = '.',
      [0x38] = '/',
  };
  static const char shifted[128] = {
      [0x1E] = '!', [0x1F] = '@', [0x20] = '#', [0x21] = '$',
      [0x22] = '%', [0x23] = '^', [0x24] = '&', [0x25] = '*',
      [0x26] = '(', [0x27] = ')', [0x28] = '\n', [0x2B] = '\t',
      [0x2C] = ' ', [0x2D] = '_', [0x2E] = '+', [0x2F] = '{',
      [0x30] = '}', [0x31] = '|', [0x32] = '~', [0x33] = ':',
      [0x34] = '"', [0x35] = '~', [0x36] = '<', [0x37] = '>',
      [0x38] = '?',
  };
  char base;

  if (usage >= 0x04 && usage <= 0x1D) {
    base = (char)('a' + (usage - 0x04));
    return apply_case(base, shift, caps_lock);
  }
  if (usage >= 128) {
    return 0;
  }
  return shift ? shifted[usage] : normal[usage];
}

static int hid_fr(uint8_t usage, int shift, int caps_lock) {
  static const char normal[128] = {
      [0x04] = 'q', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
      [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
      [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
      [0x10] = ',', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
      [0x14] = 'a', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
      [0x18] = 'u', [0x19] = 'v', [0x1A] = 'z', [0x1B] = 'x',
      [0x1C] = 'y', [0x1D] = 'w', [0x1E] = '&', [0x1F] = 'e',
      [0x20] = '"', [0x21] = '\'', [0x22] = '(', [0x23] = '-',
      [0x24] = 'e', [0x25] = '_', [0x26] = 'c', [0x27] = 'a',
      [0x28] = '\n', [0x2B] = '\t', [0x2C] = ' ', [0x2D] = ')',
      [0x2E] = '=', [0x2F] = '^', [0x30] = '$', [0x31] = '*',
      [0x32] = '<', [0x33] = 'm', [0x34] = 'u', [0x35] = '`',
      [0x36] = ';', [0x37] = ':', [0x38] = '!',
  };
  static const char shifted[128] = {
      [0x04] = 'Q', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D',
      [0x08] = 'E', [0x09] = 'F', [0x0A] = 'G', [0x0B] = 'H',
      [0x0C] = 'I', [0x0D] = 'J', [0x0E] = 'K', [0x0F] = 'L',
      [0x10] = '?', [0x11] = 'N', [0x12] = 'O', [0x13] = 'P',
      [0x14] = 'A', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
      [0x18] = 'U', [0x19] = 'V', [0x1A] = 'Z', [0x1B] = 'X',
      [0x1C] = 'Y', [0x1D] = 'W', [0x1E] = '1', [0x1F] = '2',
      [0x20] = '3', [0x21] = '4', [0x22] = '5', [0x23] = '6',
      [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
      [0x28] = '\n', [0x2B] = '\t', [0x2C] = ' ', [0x2D] = ')',
      [0x2E] = '+', [0x2F] = '[', [0x30] = ']', [0x31] = '|',
      [0x32] = '>', [0x33] = 'M', [0x34] = '%', [0x35] = '~',
      [0x36] = '.', [0x37] = '/', [0x38] = '?',
  };
  char base;

  if (usage >= 128) {
    return 0;
  }
  base = normal[usage];
  if (is_lower(base)) {
    return apply_case(base, shift, caps_lock);
  }
  return shift ? shifted[usage] : base;
}

int input_map_hid_usage(uint8_t usage, int shift, int caps_lock) {
  return current_layout == INPUT_LAYOUT_FR_AZERTY
             ? hid_fr(usage, shift, caps_lock)
             : hid_us(usage, shift, caps_lock);
}
