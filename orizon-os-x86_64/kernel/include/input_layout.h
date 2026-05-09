/*
 * Orizon OS x86_64 - Keyboard layout mapping
 */

#ifndef _INPUT_LAYOUT_H
#define _INPUT_LAYOUT_H

#include "types.h"

void input_set_keyboard_layout(const char *layout);
const char *input_keyboard_layout(void);
void input_load_keyboard_layout_from_vfs(void);

int input_map_ps2_scancode(uint8_t scancode, int shift, int altgr,
                           int caps_lock);
int input_map_hid_usage(uint8_t usage, int shift, int altgr, int caps_lock);

#endif /* _INPUT_LAYOUT_H */
