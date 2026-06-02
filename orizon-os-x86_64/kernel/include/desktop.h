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
#define ORIZON_DESKTOP_SETTINGS_PATH "/system/desktop-settings.conf"
#define ORIZON_DESKTOP_BINDS_PATH "/system/desktop-binds.conf"
#define ORIZON_DESKTOP_AUTOSTART_PATH "/system/desktop-autostart.conf"
#define ORIZON_DESKTOP_RULES_PATH "/system/desktop-rules.conf"
#define ORIZON_DESKTOP_MONITORS_PATH "/system/desktop-monitors.conf"
#define ORIZON_DESKTOP_LAYERS_PATH "/system/desktop-layers.conf"
#define ORIZON_DESKTOP_RUNTIME_PATH "/system/desktop-runtime.conf"
#define ORIZON_DESKTOP_STATE_PATH "/system/desktop-state.conf"
#define ORIZON_DESKTOP_MODULES_PATH "/system/desktop-modules.conf"
#define ORIZON_DESKTOP_LOG_PATH "/logs/desktop.log"
#define ORIZON_DESKTOP_SESSION_LOG_PATH "/logs/desktop-session.log"
#define ORIZON_DESKTOP_PACKAGE_PATH "/workspace/packages/orizon-desktop-hypr.opkg"
#define ORIZON_DESKTOP_PROFILE "hyprland-inspired"
#define ORIZON_DESKTOP_PACKAGE "orizon-desktop-hypr"
#define ORIZON_DESKTOP_PACKAGE_VERSION "0.28.0"
#define ORIZON_DESKTOP_PACKAGE_CORE "orizon-desktop-core"
#define ORIZON_DESKTOP_PACKAGE_TERMINAL "orizon-terminal"
#define ORIZON_DESKTOP_PACKAGE_SETTINGS "orizon-settings"
#define ORIZON_DESKTOP_PACKAGE_LAUNCHER "orizon-launcher"
#define ORIZON_DESKTOP_PACKAGE_WAYBAR "orizon-waybar"

typedef struct {
  char theme[32];
  char wallpaper[32];
  char layout[32];
  int bar_enabled;
  int launcher_enabled;
  int autostart_terminal;
  int focus_follows_mouse;
} orizon_desktop_session_t;

typedef struct {
  int scale;
  int gaps_in;
  int gaps_out;
  int border_size;
  int rounding;
  int animations_enabled;
  int shadows_enabled;
  int idle_timeout_seconds;
  int lock_on_idle;
  char default_terminal[32];
  char launcher_provider[32];
  char bar_position[16];
  char keyboard_layout[16];
  char pointer_profile[16];
} orizon_desktop_settings_t;

int orizon_desktop_ensure_defaults(void);
int orizon_desktop_is_enabled(void);
int orizon_desktop_set_enabled(int enabled, char *status, size_t status_size);
int orizon_desktop_reset(char *status, size_t status_size);
int orizon_desktop_write_user_config(char *status, size_t status_size);
int orizon_desktop_load_session(orizon_desktop_session_t *session);
int orizon_desktop_load_settings(orizon_desktop_settings_t *settings);
int orizon_desktop_set_session_option(const char *key, const char *value,
                                      char *status, size_t status_size);
int orizon_desktop_set_setting(const char *key, const char *value, char *status,
                               size_t status_size);
int orizon_desktop_repair_settings(char *status, size_t status_size);
int orizon_desktop_apply_settings_preset(const char *preset, char *status,
                                         size_t status_size);
int orizon_desktop_export_settings(char *status, size_t status_size);
int orizon_desktop_sync_settings(char *status, size_t status_size);
int orizon_desktop_apply_preset(const char *preset, char *status,
                                size_t status_size);
int orizon_desktop_apply_hypr_config(char *status, size_t status_size);
int orizon_desktop_apply_hypr_keyword(const char *key, const char *value,
                                      char *status, size_t status_size);
int orizon_desktop_session_manager(const char *action, char *status,
                                   size_t status_size);
void orizon_desktop_format_status(char *out, size_t out_size);
void orizon_desktop_format_config(char *out, size_t out_size);
void orizon_desktop_format_config_doctor(char *out, size_t out_size);
void orizon_desktop_format_config_errors(char *out, size_t out_size);
void orizon_desktop_format_hypr_option(const char *key, char *out,
                                       size_t out_size);
void orizon_desktop_format_runtime(char *out, size_t out_size);
void orizon_desktop_format_rules(char *out, size_t out_size);
void orizon_desktop_format_monitor_hints(char *out, size_t out_size);
void orizon_desktop_format_session(char *out, size_t out_size);
void orizon_desktop_format_session_state(char *out, size_t out_size);
void orizon_desktop_format_session_rescue(char *out, size_t out_size);
void orizon_desktop_format_settings(char *out, size_t out_size);
void orizon_desktop_format_settings_paths(char *out, size_t out_size);
void orizon_desktop_format_settings_presets(char *out, size_t out_size);
void orizon_desktop_format_settings_doctor(char *out, size_t out_size);
void orizon_desktop_format_modules(char *out, size_t out_size);
void orizon_desktop_format_apps(char *out, size_t out_size);
void orizon_desktop_format_profiles(char *out, size_t out_size);
void orizon_desktop_format_autostart(char *out, size_t out_size);
void orizon_desktop_format_shortcuts(char *out, size_t out_size);
void orizon_desktop_format_doctor(char *out, size_t out_size);
void orizon_desktop_format_log(char *out, size_t out_size);
void orizon_desktop_format_rolling_log(char *out, size_t out_size);

#endif /* _DESKTOP_H */
