/*
 * Orizon desktop internal client protocol.
 *
 * This is a VM-safe kernel-local protocol trace for compositor/client actions.
 * It is not Wayland, wlroots, xdg-shell, layer-shell, or upstream Hyprland IPC.
 */

#ifndef _DESKTOP_PROTOCOL_H
#define _DESKTOP_PROTOCOL_H

#include "types.h"

void orizon_desktop_protocol_record(const char *message, const char *source,
                                    const char *target, const char *result,
                                    int accepted);
uint64_t orizon_desktop_protocol_serial(void);
uint64_t orizon_desktop_protocol_message_count(void);
const char *orizon_desktop_protocol_last_message(void);
const char *orizon_desktop_protocol_contract(void);
const char *orizon_desktop_protocol_message_contract(void);
const char *orizon_desktop_protocol_client_contract(void);
const char *orizon_desktop_protocol_limits(void);
void orizon_desktop_protocol_format_state(char *out, size_t out_size);
void orizon_desktop_protocol_format_state_json(char *out, size_t out_size);

#endif /* _DESKTOP_PROTOCOL_H */
