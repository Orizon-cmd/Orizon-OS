/*
 * Orizon OS x86_64 - Optional Desktop Profile
 */

#ifndef _DESKTOP_H
#define _DESKTOP_H

#include "types.h"

#define ORIZON_DESKTOP_CONFIG_PATH "/system/desktop.conf"
#define ORIZON_DESKTOP_USER_CONFIG_PATH "/home/orizon/.config/hypr/orizon-hypr.conf"
#define ORIZON_DESKTOP_TEMPLATE_PATH "/system/share/orizon-desktop-hypr.conf"
#define ORIZON_DESKTOP_SESSION_PATH "/system/desktop-session.conf"
#define ORIZON_DESKTOP_LOG_PATH "/logs/desktop.log"
#define ORIZON_DESKTOP_PACKAGE_PATH "/workspace/packages/orizon-desktop-hypr.opkg"
#define ORIZON_DESKTOP_PROFILE "hyprland-inspired"
#define ORIZON_DESKTOP_PACKAGE "orizon-desktop-hypr"

typedef struct {
  char theme[32];
  char wallpaper[32];
  char layout[32];
  int bar_enabled;
  int launcher_enabled;
  int autostart_terminal;
} orizon_desktop_session_t;

int orizon_desktop_ensure_defaults(void);
int orizon_desktop_is_enabled(void);
int orizon_desktop_set_enabled(int enabled, char *status, size_t status_size);
int orizon_desktop_reset(char *status, size_t status_size);
int orizon_desktop_write_user_config(char *status, size_t status_size);
int orizon_desktop_load_session(orizon_desktop_session_t *session);
int orizon_desktop_set_session_option(const char *key, const char *value,
                                      char *status, size_t status_size);
void orizon_desktop_format_status(char *out, size_t out_size);
void orizon_desktop_format_config(char *out, size_t out_size);
void orizon_desktop_format_session(char *out, size_t out_size);
void orizon_desktop_format_apps(char *out, size_t out_size);
void orizon_desktop_format_shortcuts(char *out, size_t out_size);
void orizon_desktop_format_doctor(char *out, size_t out_size);
void orizon_desktop_format_log(char *out, size_t out_size);

#endif /* _DESKTOP_H */
