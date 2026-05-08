/*
 * Orizon OS x86_64 - Boot Payload Information
 */

#ifndef _BOOTINFO_H
#define _BOOTINFO_H

#include "types.h"

const void *boot_kernel_image(void);
size_t boot_kernel_image_size(void);
const void *boot_efi_image(void);
size_t boot_efi_image_size(void);
const char *boot_payload_status(void);
int boot_payloads_ready(void);

#endif /* _BOOTINFO_H */
