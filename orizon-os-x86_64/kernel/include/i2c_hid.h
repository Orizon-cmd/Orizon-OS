/*
 * Orizon OS x86_64 - Intel LPSS / I2C-HID diagnostics and basic pointer path
 */

#ifndef _I2C_HID_H
#define _I2C_HID_H

#include "types.h"

void i2c_hid_init(void);
void i2c_hid_poll(void);
void i2c_hid_format_status(char *buf, size_t size);

#endif /* _I2C_HID_H */
