/*
 * USB HID keyboard (boot protocol) handling
 */

#include "../include/types.h"
#include "../include/ps2.h"
#include "../include/input_layout.h"
#include "../include/string.h"

void usb_hid_handle_key(int key);

static uint8_t prev_keys[6] = {0};
static int caps_lock = 0;
static int num_lock = 1;

static int usb_hid_key_is_down(uint8_t key) {
  for (int i = 0; i < 6; i++) {
    if (prev_keys[i] == key) {
      return 1;
    }
  }
  return 0;
}

static int usb_hid_report_has_rollover(const uint8_t *keys) {
  for (int i = 0; i < 6; i++) {
    if (keys[i] >= 1 && keys[i] <= 3) {
      return 1;
    }
  }
  return 0;
}

static int usb_hid_keypad_output(uint8_t key) {
  switch (key) {
  case 0x54: return '/';
  case 0x55: return '*';
  case 0x56: return '-';
  case 0x57: return '+';
  case 0x58: return '\n';
  case 0x59: return num_lock ? '1' : KEY_DOWN;
  case 0x5A: return num_lock ? '2' : KEY_DOWN;
  case 0x5B: return num_lock ? '3' : KEY_DOWN;
  case 0x5C: return num_lock ? '4' : KEY_LEFT;
  case 0x5D: return num_lock ? '5' : 0;
  case 0x5E: return num_lock ? '6' : KEY_RIGHT;
  case 0x5F: return num_lock ? '7' : KEY_UP;
  case 0x60: return num_lock ? '8' : KEY_UP;
  case 0x61: return num_lock ? '9' : KEY_UP;
  case 0x62: return num_lock ? '0' : 0;
  case 0x63: return num_lock ? '.' : '\b';
  default: return 0;
  }
}

void usb_hid_kbd_handle_report(const uint8_t *rep, int len) {
  if (!rep || len < 8) {
    return;
  }

  if (usb_hid_report_has_rollover(rep + 2)) {
    memset(prev_keys, 0, sizeof(prev_keys));
    return;
  }

  uint8_t mods = rep[0];
  int shift = (mods & 0x22) != 0; /* LSHIFT or RSHIFT */
  int altgr = (mods & 0x40) != 0; /* Right Alt / AltGr */

  /* Check newly pressed keys */
  for (int i = 2; i < 8; i++) {
    uint8_t key = rep[i];

    if (key == 0) continue;

    if (usb_hid_key_is_down(key)) continue;

    int out = 0;
    switch (key) {
    case 0x28: out = '\n'; break;            /* Enter */
    case 0x29: out = KEY_ESC; break;         /* Escape */
    case 0x2A: out = '\b'; break;            /* Backspace */
    case 0x2B: out = '\t'; break;            /* Tab */
    case 0x39:                               /* Caps Lock */
      caps_lock = !caps_lock;
      break;
    case 0x53:                               /* Num Lock */
      num_lock = !num_lock;
      break;
    case 0x3A: out = KEY_F1; break;
    case 0x3B: out = KEY_F2; break;
    case 0x3C: out = KEY_F3; break;
    case 0x3D: out = KEY_F4; break;
    case 0x3E: out = KEY_F5; break;
    case 0x3F: out = KEY_F6; break;
    case 0x40: out = KEY_F7; break;
    case 0x41: out = KEY_F8; break;
    case 0x42: out = KEY_F9; break;
    case 0x43: out = KEY_F10; break;
    case 0x44: out = KEY_F11; break;
    case 0x45: out = KEY_F12; break;
    case 0x4F: out = KEY_RIGHT; break;
    case 0x50: out = KEY_LEFT; break;
    case 0x51: out = KEY_DOWN; break;
    case 0x52: out = KEY_UP; break;
    default:
      out = usb_hid_keypad_output(key);
      if (!out) {
        out = input_map_hid_usage(key, shift, altgr, caps_lock);
      }
      break;
    }

    if (out) {
      usb_hid_handle_key(out);
    }
  }

  for (int i = 0; i < 6; i++) {
    prev_keys[i] = rep[i + 2];
  }
}
