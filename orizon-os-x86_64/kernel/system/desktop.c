/*
 * Orizon OS x86_64 - Optional Hyprland-style desktop profile
 *
 * This is not upstream Hyprland/Wayland yet. It records an installable,
 * persistent desktop profile that the compositor can consume safely.
 */

#include "../include/desktop.h"
#include "../include/input_layout.h"
#include "../include/string.h"
#include "../include/system_state.h"
#include "../include/timer.h"
#include "../include/vfs.h"

#define DESKTOP_PKG_META_PATH \
  "/workspace/.orizon/pkgdb/installed/" ORIZON_DESKTOP_PACKAGE ".meta"

static const char *desktop_default_config =
    "# Orizon desktop policy v1\n"
    "enabled no\n"
    "profile " ORIZON_DESKTOP_PROFILE "\n"
    "package " ORIZON_DESKTOP_PACKAGE "\n"
    "session orizon-compositor\n"
    "terminal default-open\n"
    "shortcut-open-terminal F1\n"
    "shortcut-close-terminal F2\n"
    "note upstream-hyprland-not-embedded-yet\n";

static const char *desktop_enabled_config =
    "# Orizon desktop policy v1\n"
    "enabled yes\n"
    "profile " ORIZON_DESKTOP_PROFILE "\n"
    "package " ORIZON_DESKTOP_PACKAGE "\n"
    "session orizon-compositor\n"
    "terminal default-open\n"
    "shortcut-open-terminal F1\n"
    "shortcut-close-terminal F2\n"
    "note upstream-hyprland-not-embedded-yet\n";

static const char *desktop_session_config =
    "# Orizon desktop session v1\n"
    "theme graphite\n"
    "wallpaper aurora\n"
    "layout dwindle\n"
    "bar yes\n"
    "launcher yes\n"
    "autostart-terminal yes\n"
    "focus-follows-mouse no\n";

static const char *desktop_settings_config =
    "# Orizon desktop system settings v1\n"
    "# Created when the optional desktop is selected during install or via package.\n"
    "# This is the central settings layer consumed by the Orizon compositor.\n"
    "scale 1\n"
    "gaps-in 6\n"
    "gaps-out 12\n"
    "border-size 2\n"
    "rounding 8\n"
    "animations yes\n"
    "shadows yes\n"
    "focus-ring yes\n"
    "shadow-range 18\n"
    "animation-ticks 18\n"
    "animation-curve orizon-pop\n"
    "render-profile balanced\n"
    "idle-timeout-seconds 0\n"
    "lock-on-idle no\n"
    "default-terminal orizon-terminal\n"
    "launcher-provider builtin\n"
    "bar-position top\n"
    "keyboard-layout us\n"
    "pointer-profile flat\n";

static const char *desktop_user_config =
    "# Orizon Hyprland-style desktop profile\n"
    "# Syntax intentionally mirrors common Hyprland concepts while the real\n"
    "# Wayland/Hyprland stack is not embedded in Orizon yet.\n"
    "$mod = SUPER\n"
    "$terminal = orizon-terminal\n"
    "$menu = orizon-launcher\n"
    "monitor = ,preferred,auto,1\n"
    "exec-once = terminal\n"
    "input:kb_layout = us\n"
    "input:follow_mouse = 0\n"
    "input:repeat_rate = 40\n"
    "input:repeat_delay = 300\n"
    "input:touchpad:natural_scroll = false\n"
    "general:layout = dwindle\n"
    "general:gaps_in = 6\n"
    "general:gaps_out = 12\n"
    "general:border_size = 2\n"
    "general:col.active_border = rgba(8bd5ffcc)\n"
    "general:col.inactive_border = rgba(2a2f3acc)\n"
    "decoration:rounding = 8\n"
    "decoration:shadow:enabled = true\n"
    "decoration:shadow:range = 18\n"
    "render:focus_ring = true\n"
    "render:profile = balanced\n"
    "decoration:blur:enabled = false\n"
    "animations:enabled = true\n"
    "animations:tick_budget = 18\n"
    "animations:curve = orizon-pop\n"
    "bezier = orizon-pop, 0.16, 1, 0.3, 1\n"
    "animation = windows, 1, 2, orizon-pop\n"
    "cursor:no_hardware_cursors = true\n"
    "render:direct_scanout = false\n"
    "debug:disable_logs = false\n"
    "misc:disable_hyprland_logo = false\n"
    "misc:force_default_wallpaper = 0\n"
    "windowrulev2 = tile,class:^(orizon-.*)$\n"
    "layerrule = blur, launcher\n"
    "source = ~/.config/hypr/orizon-local.conf\n"
    "bind = $mod, RETURN, exec, terminal\n"
    "bind = $mod, Q, killactive\n"
    "bind = $mod, D, exec, orizon-launcher\n"
    "bind = $mod, A, exec, desktop autostart\n"
    "bind = $mod, B, exec, desktop bar toggle\n"
    "bind = $mod, F, exec, desktop focus toggle\n"
    "bind = $mod, M, fullscreen\n"
    "bind = $mod, P, pseudo\n"
    "bind = $mod SHIFT, P, pin\n"
    "bind = $mod, J, togglesplit\n"
    "bind = $mod SHIFT, J, layoutmsg, orientationnext\n"
    "bind = $mod, S, layoutmsg, swapwithmaster\n"
    "bind = $mod SHIFT, S, layoutmsg, focusmaster\n"
    "bindl = , XF86AudioMute, exec, desktop logs\n"
    "bind = $mod, R, submap, resize\n"
    "bind = $mod SHIFT, R, submap, reset\n"
    "bind = $mod, H, movefocus, l\n"
    "bind = $mod, L, movefocus, r\n"
    "bind = $mod, left, movefocus, l\n"
    "bind = $mod, right, movefocus, r\n"
    "bind = $mod, up, movefocus, u\n"
    "bind = $mod, down, movefocus, d\n"
    "bind = $mod SHIFT, left, swapwindow, l\n"
    "bind = $mod SHIFT, right, swapwindow, r\n"
    "bind = $mod SHIFT, up, swapwindow, u\n"
    "bind = $mod SHIFT, down, swapwindow, d\n"
    "bind = $mod, Tab, cyclenext\n"
    "bind = $mod SHIFT, Tab, swapnext\n"
    "bind = $mod, C, exec, desktop session\n"
    "bind = $mod, 1, workspace, 1\n"
    "bind = $mod, 2, workspace, 2\n"
    "bind = $mod, 3, workspace, 3\n"
    "bind = $mod SHIFT, 1, movetoworkspace, 1\n"
    "bind = $mod SHIFT, 2, movetoworkspace, 2\n"
    "bind = $mod SHIFT, 3, movetoworkspace, 3\n"
    "bind = F1, exec, desktop open terminal\n"
    "bind = F2, killactive\n"
    "bind = F4, fullscreen\n"
    "bind = F5, pseudo\n"
    "bind = F9, submap, resize\n"
    "bind = F10, submap, move\n"
    "bind = F11, submap, launch\n"
    "bind = F12, submap, reset\n"
    "submap = resize\n"
    "bind = , right, resizeactive, 5 0\n"
    "bind = , left, resizeactive, -5 0\n"
    "bind = , up, resizeactive, 0 5\n"
    "bind = , down, resizeactive, 0 -5\n"
    "bind = , escape, submap, reset\n"
    "submap = move\n"
    "bind = , right, movefocus, r\n"
    "bind = , left, movefocus, l\n"
    "bind = , 1, movetoworkspace, 1\n"
    "bind = , 2, movetoworkspace, 2\n"
    "bind = , 3, movetoworkspace, 3\n"
    "bind = , escape, submap, reset\n"
    "submap = launch\n"
    "bind = , t, exec, terminal\n"
    "bind = , s, exec, orizon-settings\n"
    "bind = , l, exec, orizon-logs\n"
    "bind = , p, exec, orizon-packages\n"
    "bind = , u, exec, orizon-update-viewer\n"
    "bind = , d, exec, orizon-launcher\n"
    "bind = , q, killactive\n"
    "bind = , escape, submap, reset\n"
    "submap = default\n"
    "dwindle:pseudotile = true\n"
    "dwindle:preserve_split = true\n";

static const char *desktop_binds_runtime_config =
    "# Orizon generated Hyprland-style binds v1\n"
    "# Rewritten by `desktop config apply` from /home/orizon/.config/hypr/orizon-hypr.conf.\n"
    "source built-in-template\n"
    "bind = $mod, RETURN, exec, terminal\n"
    "bind = $mod, Q, killactive\n"
    "bind = $mod, D, exec, orizon-launcher\n"
    "bind = $mod, M, fullscreen\n"
    "bind = $mod, P, pseudo\n"
    "bind = $mod, J, togglesplit\n"
    "bind = $mod, S, layoutmsg, swapwithmaster\n"
    "bind = $mod, H, movefocus, l\n"
    "bind = $mod, L, movefocus, r\n"
    "bind = $mod, left, movefocus, l\n"
    "bind = $mod, right, movefocus, r\n"
    "bind = $mod, up, movefocus, u\n"
    "bind = $mod, down, movefocus, d\n"
    "bind = $mod SHIFT, left, swapwindow, l\n"
    "bind = $mod SHIFT, right, swapwindow, r\n"
    "bind = $mod SHIFT, up, swapwindow, u\n"
    "bind = $mod SHIFT, down, swapwindow, d\n"
    "bind = F9, submap, resize\n"
    "bind = F10, submap, move\n"
    "bind = F11, submap, launch\n"
    "bind = F12, submap, reset\n"
    "submap = launch\n"
    "bind = , t, exec, terminal\n"
    "bind = , s, exec, orizon-settings\n"
    "bind = , l, exec, orizon-logs\n"
    "bind = , p, exec, orizon-packages\n"
    "bind = , u, exec, orizon-update-viewer\n"
    "bind = , d, exec, orizon-launcher\n"
    "bind = , q, killactive\n"
    "submap = default\n"
    "bind = $mod, 1, workspace, 1\n"
    "bind = $mod SHIFT, 1, movetoworkspace, 1\n";

static const char *desktop_autostart_runtime_config =
    "# Orizon generated Hyprland-style autostart v1\n"
    "# Rewritten by `desktop config apply`.\n"
    "source built-in-template\n"
    "exec-once = terminal\n";

static const char *desktop_rules_runtime_config =
    "# Orizon generated Hyprland-style window rules v1\n"
    "# Rewritten by `desktop config apply`.\n"
    "source built-in-template\n"
    "windowrulev2 = tile,class:^(orizon-.*)$\n";

static const char *desktop_monitors_runtime_config =
    "# Orizon generated Hyprland-style monitor hints v1\n"
    "# Rewritten by `desktop config apply`.\n"
    "source built-in-template\n"
    "monitor = ,preferred,auto,1\n";

static const char *desktop_layers_runtime_config =
    "# Orizon generated Hyprland-style layer rules v1\n"
    "# Rewritten by `desktop config apply`.\n"
    "source built-in-template\n"
    "layerrule = blur, launcher\n";

static const char *desktop_runtime_config =
    "# Orizon generated Hyprland-style runtime state v1\n"
    "# Rewritten by `desktop config apply`.\n"
    "source built-in-template\n"
    "env-count 0\n"
    "workspace-hints 0\n"
    "layout-hints 0\n"
    "input-hints 0\n"
    "decoration-hints 0\n"
    "cursor-hints 0\n"
    "render-hints 0\n"
    "debug-hints 0\n"
    "animation-hints 0\n"
    "submap = default\n"
    "settings-hub yes\n"
    "settings-path " ORIZON_DESKTOP_SETTINGS_PATH "\n"
    "session-path " ORIZON_DESKTOP_SESSION_PATH "\n"
    "user-config-path " ORIZON_DESKTOP_USER_CONFIG_PATH "\n"
    "backend-path " ORIZON_DESKTOP_BACKEND_PATH "\n"
    "protocol-path " ORIZON_DESKTOP_PROTOCOL_PATH "\n"
    "sources 1\n"
    "source = ~/.config/hypr/orizon-local.conf\n";

static const char *desktop_backend_config =
    "# Orizon desktop compositor backend map v1\n"
    "# Truth file for the current VM-safe Hyprland-style facade.\n"
    "api compositor-orchestrator\n"
    "backend-current framebuffer-vm\n"
    "backend-current-file gui/compositor.c\n"
    "backend-future wayland-wlroots\n"
    "render-path software-backbuffer\n"
    "client-model tiled-internal\n"
    "external-wayland-clients no\n"
    "manual-window-drag no\n"
    "taskbar no\n"
    "waybar installed-no future-package\n"
    "vm-ready yes\n"
    "hardware-validated no\n"
    "truth hyprland-style-facade-not-upstream\n";

static const char *desktop_protocol_config =
    "# Orizon desktop internal protocol map v1\n"
    "# Prepared split between compositor API and the current framebuffer backend.\n"
    "protocol orizon-desktop-ipc-v0\n"
    "transport internal-kernel-dispatch\n"
    "messages dispatch,spawn-client,close-client,focus-client,workspace,config-keyword,query-state\n"
    "security local-kernel-only\n"
    "wayland no\n"
    "wlroots no\n"
    "xdg-shell no\n"
    "layer-shell prepared-only\n"
    "xwayland no\n"
    "external-clients no\n"
    "status prepared\n";

static const char *desktop_modules_config =
    "# Orizon desktop modular packaging map v1\n"
    "# This prepares a split desktop without installing Waybar yet.\n"
    "module " ORIZON_DESKTOP_PACKAGE_CORE
    " state=prepared kind=runtime provides=policy,session,settings,logs,backend-map,protocol-map sample='pkg sample " ORIZON_DESKTOP_PACKAGE_CORE "' install='pkg install " ORIZON_DESKTOP_PACKAGE_CORE "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
    "module " ORIZON_DESKTOP_PACKAGE
    " state=prepared kind=profile provides=hyprland-style-config,dispatchers,tiling,backend-diagnostics current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
    "module " ORIZON_DESKTOP_PACKAGE_TERMINAL
    " state=prepared kind=app provides=terminal-client shortcut=F1 sample='pkg sample " ORIZON_DESKTOP_PACKAGE_TERMINAL "' install='pkg install " ORIZON_DESKTOP_PACKAGE_TERMINAL "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
    "module " ORIZON_DESKTOP_PACKAGE_SETTINGS
    " state=prepared kind=app provides=settings,logs,packages,update-viewers shortcut=F11+s sample='pkg sample " ORIZON_DESKTOP_PACKAGE_SETTINGS "' install='pkg install " ORIZON_DESKTOP_PACKAGE_SETTINGS "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
    "module " ORIZON_DESKTOP_PACKAGE_LAUNCHER
    " state=prepared kind=app provides=launcher-overlay shortcut=SUPER+D/F3 sample='pkg sample " ORIZON_DESKTOP_PACKAGE_LAUNCHER "' install='pkg install " ORIZON_DESKTOP_PACKAGE_LAUNCHER "' current-bundle=" ORIZON_DESKTOP_PACKAGE "\n"
    "module " ORIZON_DESKTOP_PACKAGE_WAYBAR
    " state=planned kind=bar provides=waybar-style-layer package-later=yes installed=no\n"
    "policy no-windows-taskbar\n"
    "policy no-free-drag-window-moving\n"
    "architecture current-backend=framebuffer-vm future-backend=wayland-wlroots protocol=orizon-desktop-ipc-v0\n"
    "install-meta package-current=" ORIZON_DESKTOP_PACKAGE "\n"
    "install-meta package-split-prepared=yes\n";

static const char *desktop_state_config =
    "# Orizon desktop session manager state v2\n"
    "schema-version 2\n"
    "health PASS\n"
    "desired-state stopped\n"
    "runtime-state inactive\n"
    "last-action defaults\n"
    "last-ticks 0\n"
    "boot-mode unknown\n"
    "installed-marker unknown\n"
    "policy disabled\n"
    "autostart-terminal yes\n"
    "focus-follows-mouse no\n"
    "layout dwindle\n"
    "start-count 0\n"
    "stop-count 0\n"
    "restart-count 0\n"
    "reload-count 0\n"
    "recover-count 0\n"
    "crash-count 0\n"
    "crash-recover ready\n"
    "recover-command desktop recover\n"
    "rescue-command desktop rescue\n"
    "client-model-command desktop client-model\n"
    "hyprctl-clientmodel-command desktop hyprctl clientmodel\n"
    "manual-window-drag no\n";

static void desktop_append(char *out, size_t out_size, size_t *used,
                           const char *text) {
  size_t len;

  if (!out || !used || !text || *used >= out_size) {
    return;
  }
  len = strlen(text);
  if (*used + len >= out_size) {
    len = out_size - *used - 1;
  }
  if (len > 0) {
    memcpy(out + *used, text, len);
    *used += len;
  }
  out[*used] = '\0';
}

typedef struct {
  const char *id;
  const char *title;
  const char *class_name;
  const char *module;
  const char *backend;
  const char *surface;
  const char *status;
  const char *command;
  const char *shortcut;
  const char *aliases;
  const char *description;
} desktop_app_entry_t;

static const desktop_app_entry_t desktop_app_catalog[] = {
    {"terminal",
     "Terminal",
     ORIZON_DESKTOP_PACKAGE_TERMINAL,
     ORIZON_DESKTOP_PACKAGE_TERMINAL,
     "terminal",
     "tiled-client",
     "installed",
     "desktop launch terminal",
     "SUPER+Return/F1/F11+t",
     "orizon-terminal,kitty",
     "shell client backed by the Orizon terminal surface"},
    {"settings",
     "Settings",
     ORIZON_DESKTOP_PACKAGE_SETTINGS,
     ORIZON_DESKTOP_PACKAGE_SETTINGS,
     "native-app",
     "tiled-client",
     "installed",
     "desktop launch settings",
     "F11+s",
     "desktop-settings,orizon-settings",
     "settings client for theme, gaps, input, binds, and autostart"},
    {"logs",
     "Logs",
     "orizon-logs",
     ORIZON_DESKTOP_PACKAGE,
     "native-app",
     "tiled-client",
     "installed",
     "desktop launch logs",
     "F11+l",
     "log-viewer,logs-viewer,orizon-logs",
     "logs viewer for persistent desktop and session diagnostics"},
    {"packages",
     "Packages",
     "orizon-packages",
     ORIZON_DESKTOP_PACKAGE,
     "native-app",
     "tiled-client",
     "installed",
     "desktop launch packages",
     "F11+p",
     "pkg,package-viewer,orizon-packages",
     "package viewer for desktop modules and package-manager state"},
    {"update",
     "Update",
     "orizon-update-viewer",
     ORIZON_DESKTOP_PACKAGE,
     "native-app",
     "tiled-client",
     "installed",
     "desktop launch update",
     "F11+u",
     "updater,update-viewer,orizon-update-viewer",
     "update viewer for manifest, signature, bootguard, and rollback state"},
    {"launcher",
     "Launcher",
     ORIZON_DESKTOP_PACKAGE_LAUNCHER,
     ORIZON_DESKTOP_PACKAGE_LAUNCHER,
     "overlay",
     "overlay",
     "installed",
     "desktop launch launcher",
     "SUPER+D/F3/F11+d",
     "orizon-launcher",
     "dispatch overlay only; no taskbar, start menu, or free-drag desktop"},
};

static const desktop_app_entry_t *desktop_find_app_entry(const char *app) {
  size_t count = sizeof(desktop_app_catalog) / sizeof(desktop_app_catalog[0]);

  if (!app || !app[0]) {
    return NULL;
  }
  for (size_t i = 0; i < count; i++) {
    const desktop_app_entry_t *entry = &desktop_app_catalog[i];
    if (strcmp(app, entry->id) == 0 ||
        strcmp(app, entry->class_name) == 0 ||
        strcmp(app, entry->title) == 0) {
      return entry;
    }
  }
  if (strcmp(app, "kitty") == 0) {
    return &desktop_app_catalog[0];
  }
  if (strcmp(app, "desktop-settings") == 0) {
    return &desktop_app_catalog[1];
  }
  if (strcmp(app, "log-viewer") == 0 ||
      strcmp(app, "logs-viewer") == 0) {
    return &desktop_app_catalog[2];
  }
  if (strcmp(app, "pkg") == 0 || strcmp(app, "package-viewer") == 0) {
    return &desktop_app_catalog[3];
  }
  if (strcmp(app, "updater") == 0 || strcmp(app, "update-viewer") == 0) {
    return &desktop_app_catalog[4];
  }
  return NULL;
}

static int desktop_write_text_file(const char *path, const char *text) {
  file_t *f;

  if (!path) {
    return -EINVAL;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC);
  if (!f) {
    return -EIO;
  }
  if (text && vfs_write(f, text, strlen(text)) < 0) {
    vfs_close(f);
    return -EIO;
  }
  vfs_close(f);
  return 0;
}

static int desktop_append_text_file(const char *path, const char *text) {
  file_t *f;

  if (!path) {
    return -EINVAL;
  }
  f = vfs_open(path, O_CREAT | O_WRONLY | O_APPEND);
  if (!f) {
    return -EIO;
  }
  if (text && vfs_write(f, text, strlen(text)) < 0) {
    vfs_close(f);
    return -EIO;
  }
  vfs_close(f);
  return 0;
}

static int desktop_read_text_file(const char *path, char *buf, size_t cap) {
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
  if (n < 0) {
    return -1;
  }
  buf[used] = '\0';
  return (int)used;
}

static int desktop_token_safe(const char *value) {
  int seen = 0;

  if (!value) {
    return 0;
  }
  while (*value) {
    char c = *value++;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      seen = 1;
      continue;
    }
    return 0;
  }
  return seen;
}

static int desktop_bool_value(const char *value, int fallback) {
  if (!value || !value[0]) {
    return fallback;
  }
  if (strcmp(value, "yes") == 0 || strcmp(value, "true") == 0 ||
      strcmp(value, "on") == 0 || strcmp(value, "1") == 0) {
    return 1;
  }
  if (strcmp(value, "no") == 0 || strcmp(value, "false") == 0 ||
      strcmp(value, "off") == 0 || strcmp(value, "0") == 0) {
    return 0;
  }
  return fallback;
}

static int desktop_parse_int_value(const char *value, int fallback) {
  int n = 0;
  int seen = 0;

  if (!value || !value[0]) {
    return fallback;
  }
  while (*value >= '0' && *value <= '9') {
    seen = 1;
    n = n * 10 + (*value - '0');
    value++;
  }
  return seen ? n : fallback;
}

static int desktop_clamp_int(int value, int min, int max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

static int desktop_settings_key_known(const char *key) {
  static const char *keys[] = {
      "scale",
      "gaps-in",
      "gaps-out",
      "border-size",
      "rounding",
      "animations",
      "shadows",
      "focus-ring",
      "shadow-range",
      "animation-ticks",
      "animation-curve",
      "render-profile",
      "idle-timeout-seconds",
      "lock-on-idle",
      "default-terminal",
      "launcher-provider",
      "bar-position",
      "keyboard-layout",
      "pointer-profile",
  };
  size_t i;

  if (!key || !key[0]) {
    return 0;
  }
  for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    if (strcmp(key, keys[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

static int desktop_session_get_value(const char *text, const char *key,
                                     char *out, size_t out_size,
                                     const char *fallback) {
  const char *p;
  size_t key_len;

  if (!out || out_size == 0 || !key) {
    return -1;
  }
  snprintf(out, out_size, "%s", fallback ? fallback : "");
  if (!text) {
    return -1;
  }
  key_len = strlen(key);
  p = text;
  while (*p) {
    const char *line = p;
    size_t line_len = 0;
    while (p[line_len] && p[line_len] != '\n') {
      line_len++;
    }
    if (line_len > key_len && strncmp(line, key, key_len) == 0 &&
        line[key_len] == ' ') {
      size_t value_len = line_len - key_len - 1;
      if (value_len >= out_size) {
        value_len = out_size - 1;
      }
      memcpy(out, line + key_len + 1, value_len);
      out[value_len] = '\0';
      return 0;
    }
    p += line_len;
    if (*p == '\n') {
      p++;
    }
  }
  return -1;
}

typedef struct {
  int parsed_lines;
  int variables;
  int monitors;
  int binds;
  int supported_binds;
  int exec_once;
  int envs;
  int windowrules;
  int layerrules;
  int workspaces;
  int sources;
  int submaps;
  int animation_rules;
  int input_hints;
  int layout_hints;
  int decoration_hints;
  int cursor_hints;
  int render_hints;
  int debug_hints;
  int device_hints;
  int misc_hints;
  int mouse_binds;
  int locked_binds;
  int supported_settings;
  int prepared_keywords;
  int ignored_keywords;
  int malformed_lines;
  int applied_settings;
  int generated_user_config;
  int runtime_lines;
} desktop_hypr_summary_t;

typedef struct {
  char binds[2048];
  char autostart[768];
  char rules[1024];
  char monitors[768];
  char layers[768];
  char runtime[2048];
  size_t binds_used;
  size_t autostart_used;
  size_t rules_used;
  size_t monitors_used;
  size_t layers_used;
  size_t runtime_used;
} desktop_hypr_runtime_t;

static void desktop_trim_copy(char *out, size_t out_size, const char *start,
                              int len) {
  int begin = 0;
  int end = len;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!start || len <= 0) {
    return;
  }
  while (begin < end &&
         (start[begin] == ' ' || start[begin] == '\t' ||
          start[begin] == '\r')) {
    begin++;
  }
  while (end > begin &&
         (start[end - 1] == ' ' || start[end - 1] == '\t' ||
          start[end - 1] == '\r')) {
    end--;
  }
  len = end - begin;
  if (len >= (int)out_size) {
    len = (int)out_size - 1;
  }
  if (len > 0) {
    memcpy(out, start + begin, (size_t)len);
  }
  out[len] = '\0';
}

static void desktop_strip_inline_comment(char *line) {
  int quoted = 0;

  if (!line) {
    return;
  }
  for (int i = 0; line[i]; i++) {
    if (line[i] == '"') {
      quoted = !quoted;
    }
    if (!quoted && line[i] == '#') {
      line[i] = '\0';
      return;
    }
  }
}

static const char *desktop_find_char(const char *text, char needle) {
  if (!text) {
    return NULL;
  }
  while (*text) {
    if (*text == needle) {
      return text;
    }
    text++;
  }
  return NULL;
}

static int desktop_hypr_key_value(const char *line, char *key,
                                  size_t key_size, char *value,
                                  size_t value_size) {
  const char *eq;
  const char *comma;
  const char *split = NULL;
  int key_len;

  if (!line || !key || !value || key_size == 0 || value_size == 0) {
    return -1;
  }
  key[0] = '\0';
  value[0] = '\0';
  eq = desktop_find_char(line, '=');
  comma = desktop_find_char(line, ',');
  split = eq ? eq : comma;
  if (!split) {
    return -1;
  }
  key_len = (int)(split - line);
  desktop_trim_copy(key, key_size, line, key_len);
  desktop_trim_copy(value, value_size, split + 1, (int)strlen(split + 1));
  return key[0] && value[0] ? 0 : -1;
}

static int desktop_hypr_key_global(const char *key) {
  return strncmp(key, "bind", 4) == 0 || strcmp(key, "monitor") == 0 ||
         strcmp(key, "env") == 0 || strcmp(key, "exec") == 0 ||
         strcmp(key, "exec-once") == 0 ||
         strcmp(key, "exec-shutdown") == 0 ||
         strcmp(key, "windowrule") == 0 ||
         strcmp(key, "windowrulev2") == 0 || strcmp(key, "layerrule") == 0 ||
         strcmp(key, "workspace") == 0 || strcmp(key, "source") == 0 ||
         strcmp(key, "submap") == 0 || strcmp(key, "bezier") == 0 ||
         strcmp(key, "animation") == 0 || strcmp(key, "plugin") == 0 ||
         strcmp(key, "permission") == 0 || strcmp(key, "blurls") == 0 ||
         key[0] == '$';
}

static void desktop_hypr_join_key(char sections[][32], int depth,
                                  const char *key, char *out,
                                  size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!key) {
    return;
  }
  if (desktop_find_char(key, ':') || desktop_hypr_key_global(key) ||
      depth <= 0) {
    snprintf(out, out_size, "%s", key);
    return;
  }
  for (int i = 0; i < depth; i++) {
    if (!sections[i][0]) {
      continue;
    }
    if (used > 0 && used + 1 < out_size) {
      out[used++] = ':';
      out[used] = '\0';
    }
    size_t len = strlen(sections[i]);
    if (used + len >= out_size) {
      len = out_size - used - 1;
    }
    memcpy(out + used, sections[i], len);
    used += len;
    out[used] = '\0';
  }
  if (used > 0 && used + 1 < out_size) {
    out[used++] = ':';
    out[used] = '\0';
  }
  snprintf(out + used, out_size - used, "%s", key);
}

static int desktop_hypr_dispatch_supported(const char *value) {
  return value &&
         (strstr(value, "exec") || strstr(value, "killactive") ||
          strstr(value, "workspace") || strstr(value, "movetoworkspace") ||
          strstr(value, "movefocus") || strstr(value, "fullscreen") ||
          strstr(value, "pseudo") || strstr(value, "pin") ||
          strstr(value, "focusmaster") || strstr(value, "swapwithmaster") ||
          strstr(value, "cyclenext") || strstr(value, "swapnext") ||
          strstr(value, "swapwindow") ||
          strstr(value, "togglesplit") || strstr(value, "layoutmsg") ||
          strstr(value, "resizeactive") || strstr(value, "submap"));
}

static int desktop_hypr_key_safe(const char *value) {
  int seen = 0;

  if (!value) {
    return 0;
  }
  while (*value) {
    char c = *value++;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == ':' || c == '$') {
      seen = 1;
      continue;
    }
    return 0;
  }
  return seen;
}

static int desktop_hypr_value_safe(const char *value) {
  int seen = 0;

  if (!value) {
    return 0;
  }
  while (*value) {
    unsigned char c = (unsigned char)*value++;
    if (c == '\n' || c == '\r' || c < 32) {
      return 0;
    }
    seen = 1;
  }
  return seen;
}

static int desktop_hypr_copy_token_value(char *out, size_t out_size,
                                         const char *value) {
  int len = 0;

  if (!out || out_size == 0 || !value) {
    return -1;
  }
  while (*value == ' ' || *value == '\t') {
    value++;
  }
  while (value[len] && value[len] != ',' && value[len] != ' ' &&
         value[len] != '\t' && value[len] != '\r') {
    len++;
  }
  desktop_trim_copy(out, out_size, value, len);
  return desktop_token_safe(out) ? 0 : -1;
}

static void desktop_hypr_runtime_init(desktop_hypr_runtime_t *runtime) {
  if (!runtime) {
    return;
  }
  memset(runtime, 0, sizeof(*runtime));
  desktop_append(runtime->binds, sizeof(runtime->binds), &runtime->binds_used,
                 "# Orizon generated Hyprland-style binds v1\n"
                 "source user-config\n");
  desktop_append(runtime->autostart, sizeof(runtime->autostart),
                 &runtime->autostart_used,
                 "# Orizon generated Hyprland-style autostart v1\n"
                 "source user-config\n");
  desktop_append(runtime->rules, sizeof(runtime->rules), &runtime->rules_used,
                 "# Orizon generated Hyprland-style window rules v1\n"
                 "source user-config\n");
  desktop_append(runtime->monitors, sizeof(runtime->monitors),
                 &runtime->monitors_used,
                 "# Orizon generated Hyprland-style monitor hints v1\n"
                 "source user-config\n");
  desktop_append(runtime->layers, sizeof(runtime->layers),
                 &runtime->layers_used,
                 "# Orizon generated Hyprland-style layer rules v1\n"
                 "source user-config\n");
  desktop_append(runtime->runtime, sizeof(runtime->runtime),
                 &runtime->runtime_used,
                 "# Orizon generated Hyprland-style runtime state v1\n"
                 "source user-config\n");
}

static void desktop_hypr_runtime_append(char *buf, size_t buf_size,
                                        size_t *used, const char *key,
                                        const char *value) {
  char line[256];

  if (!buf || !used || !key || !value || !value[0]) {
    return;
  }
  snprintf(line, sizeof(line), "%s = %s\n", key, value);
  desktop_append(buf, buf_size, used, line);
}

static int desktop_hypr_is_supported_setting_key(const char *key) {
  return key &&
         (strcmp(key, "general:layout") == 0 ||
          strcmp(key, "general:gaps_in") == 0 ||
          strcmp(key, "general:gaps_out") == 0 ||
          strcmp(key, "general:border_size") == 0 ||
          strcmp(key, "decoration:rounding") == 0 ||
          strcmp(key, "decoration:shadow:enabled") == 0 ||
          strcmp(key, "decoration:shadow:range") == 0 ||
          strcmp(key, "decoration:drop_shadow") == 0 ||
          strcmp(key, "animations:enabled") == 0 ||
          strcmp(key, "animations:tick_budget") == 0 ||
          strcmp(key, "animations:curve") == 0 ||
          strcmp(key, "render:focus_ring") == 0 ||
          strcmp(key, "render:profile") == 0 ||
          strcmp(key, "input:kb_layout") == 0 ||
          strcmp(key, "input:follow_mouse") == 0);
}

static int desktop_hypr_key_runtime_hint(const char *key) {
  if (!key || !key[0] || desktop_hypr_is_supported_setting_key(key)) {
    return 0;
  }
  return strcmp(key, "layerrule") == 0 || strcmp(key, "animation") == 0 ||
         strcmp(key, "bezier") == 0 || strncmp(key, "general:", 8) == 0 ||
         strncmp(key, "decoration:", 11) == 0 ||
         strncmp(key, "animations:", 11) == 0 ||
         strncmp(key, "input:", 6) == 0 || strncmp(key, "misc:", 5) == 0 ||
         strncmp(key, "cursor:", 7) == 0 ||
         strncmp(key, "render:", 7) == 0 ||
         strncmp(key, "debug:", 6) == 0 ||
         strncmp(key, "opengl:", 7) == 0 ||
         strncmp(key, "device:", 7) == 0 ||
         strncmp(key, "plugin:", 7) == 0 ||
         strncmp(key, "permission:", 11) == 0 ||
         strncmp(key, "group:", 6) == 0 ||
         strncmp(key, "binds:", 6) == 0 ||
         strncmp(key, "ecosystem:", 10) == 0 ||
         strncmp(key, "dwindle:", 8) == 0 ||
         strncmp(key, "master:", 7) == 0 ||
         strncmp(key, "gestures:", 9) == 0 ||
         strncmp(key, "xwayland:", 9) == 0;
}

static const char *desktop_hypr_runtime_path_for_key(const char *key) {
  if (!key) {
    return NULL;
  }
  if (strcmp(key, "monitor") == 0) {
    return ORIZON_DESKTOP_MONITORS_PATH;
  }
  if (strncmp(key, "bind", 4) == 0) {
    return ORIZON_DESKTOP_BINDS_PATH;
  }
  if (strcmp(key, "exec-once") == 0 || strcmp(key, "exec") == 0 ||
      strcmp(key, "exec-shutdown") == 0) {
    return ORIZON_DESKTOP_AUTOSTART_PATH;
  }
  if (strcmp(key, "windowrule") == 0 || strcmp(key, "windowrulev2") == 0) {
    return ORIZON_DESKTOP_RULES_PATH;
  }
  if (strcmp(key, "layerrule") == 0) {
    return ORIZON_DESKTOP_LAYERS_PATH;
  }
  if (strcmp(key, "env") == 0 || strcmp(key, "workspace") == 0 ||
      strcmp(key, "source") == 0 || strcmp(key, "submap") == 0 ||
      strcmp(key, "animation") == 0 || strcmp(key, "bezier") == 0 ||
      key[0] == '$' || desktop_hypr_key_runtime_hint(key)) {
    return ORIZON_DESKTOP_RUNTIME_PATH;
  }
  return NULL;
}

static int desktop_append_hypr_runtime_keyword(const char *key,
                                               const char *value) {
  const char *path = desktop_hypr_runtime_path_for_key(key);
  char line[256];

  if (!path || !key || !value) {
    return 0;
  }
  snprintf(line, sizeof(line), "%s = %s\n", key, value);
  return desktop_append_text_file(path, line);
}

static int desktop_hypr_runtime_get_value(const char *path, const char *key,
                                          char *out, size_t out_size) {
  char cfg[2048];
  int n;
  int pos = 0;
  int found = 0;

  if (!path || !key || !out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  n = desktop_read_text_file(path, cfg, sizeof(cfg));
  if (n <= 0) {
    return -1;
  }
  while (pos < n) {
    int start = pos;
    int end;
    char line[256];
    char line_key[96];
    char line_value[128];

    while (pos < n && cfg[pos] != '\n') {
      pos++;
    }
    end = pos;
    if (pos < n && cfg[pos] == '\n') {
      pos++;
    }
    desktop_trim_copy(line, sizeof(line), cfg + start, end - start);
    if (desktop_hypr_key_value(line, line_key, sizeof(line_key), line_value,
                               sizeof(line_value)) == 0 &&
        strcmp(line_key, key) == 0) {
      snprintf(out, out_size, "%s", line_value);
      found = 1;
    }
  }
  return found ? 0 : -1;
}

static int desktop_hypr_apply_pair(const char *key, const char *value,
                                   orizon_desktop_session_t *session,
                                   orizon_desktop_settings_t *settings,
                                   desktop_hypr_summary_t *summary,
                                   int apply,
                                   desktop_hypr_runtime_t *runtime) {
  char token[48];
  int supported = 0;
  int applied = 0;

  if (!key || !value || !summary) {
    return 0;
  }
  if (key[0] == '$') {
    summary->variables++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "monitor") == 0) {
    summary->monitors++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->monitors,
                                  sizeof(runtime->monitors),
                                  &runtime->monitors_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strncmp(key, "bind", 4) == 0) {
    summary->binds++;
    if (strcmp(key, "bindm") == 0) {
      summary->mouse_binds++;
    }
    if (strcmp(key, "bindl") == 0 || strcmp(key, "bindle") == 0) {
      summary->locked_binds++;
    }
    if (desktop_hypr_dispatch_supported(value)) {
      summary->supported_binds++;
    }
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->binds, sizeof(runtime->binds),
                                  &runtime->binds_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "animation") == 0 || strcmp(key, "bezier") == 0) {
    summary->animation_rules++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "exec-once") == 0 || strcmp(key, "exec") == 0 ||
      strcmp(key, "exec-shutdown") == 0) {
    summary->exec_once++;
    summary->prepared_keywords++;
    if (apply && strcmp(key, "exec-once") == 0 && strstr(value, "terminal") &&
        session) {
      session->autostart_terminal = 1;
      applied = 1;
    }
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->autostart,
                                  sizeof(runtime->autostart),
                                  &runtime->autostart_used, key, value);
      summary->runtime_lines++;
    }
    summary->applied_settings += applied;
    return 0;
  }
  if (strcmp(key, "env") == 0) {
    summary->envs++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "windowrule") == 0 || strcmp(key, "windowrulev2") == 0) {
    summary->windowrules++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->rules, sizeof(runtime->rules),
                                  &runtime->rules_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "layerrule") == 0) {
    summary->layerrules++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->layers, sizeof(runtime->layers),
                                  &runtime->layers_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "workspace") == 0) {
    summary->workspaces++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "source") == 0) {
    summary->sources++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "submap") == 0) {
    summary->submaps++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }
  if (strcmp(key, "plugin") == 0 || strcmp(key, "permission") == 0 ||
      strcmp(key, "blurls") == 0) {
    summary->misc_hints++;
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }

  if (strcmp(key, "general:layout") == 0) {
    supported = 1;
    if (apply && session && desktop_hypr_copy_token_value(token, sizeof(token),
                                                          value) == 0) {
      snprintf(session->layout, sizeof(session->layout), "%s", token);
      applied = 1;
    }
  } else if (strcmp(key, "general:gaps_in") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->gaps_in =
          desktop_clamp_int(desktop_parse_int_value(value, settings->gaps_in),
                            0, 48);
      applied = 1;
    }
  } else if (strcmp(key, "general:gaps_out") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->gaps_out =
          desktop_clamp_int(desktop_parse_int_value(value, settings->gaps_out),
                            0, 64);
      applied = 1;
    }
  } else if (strcmp(key, "general:border_size") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->border_size = desktop_clamp_int(
          desktop_parse_int_value(value, settings->border_size), 0, 8);
      applied = 1;
    }
  } else if (strcmp(key, "decoration:rounding") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->rounding =
          desktop_clamp_int(desktop_parse_int_value(value, settings->rounding),
                            0, 24);
      applied = 1;
    }
  } else if (strcmp(key, "decoration:shadow:enabled") == 0 ||
             strcmp(key, "decoration:drop_shadow") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->shadows_enabled =
          desktop_bool_value(value, settings->shadows_enabled);
      applied = 1;
    }
  } else if (strcmp(key, "decoration:shadow:range") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->shadow_range = desktop_clamp_int(
          desktop_parse_int_value(value, settings->shadow_range), 0, 32);
      applied = 1;
    }
  } else if (strcmp(key, "animations:enabled") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->animations_enabled =
          desktop_bool_value(value, settings->animations_enabled);
      applied = 1;
    }
  } else if (strcmp(key, "animations:tick_budget") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->animation_ticks = desktop_clamp_int(
          desktop_parse_int_value(value, settings->animation_ticks), 4, 60);
      applied = 1;
    }
  } else if (strcmp(key, "animations:curve") == 0) {
    supported = 1;
    if (apply && settings &&
        desktop_hypr_copy_token_value(token, sizeof(token), value) == 0) {
      snprintf(settings->animation_curve, sizeof(settings->animation_curve),
               "%s", token);
      applied = 1;
    }
  } else if (strcmp(key, "render:focus_ring") == 0) {
    supported = 1;
    if (apply && settings) {
      settings->focus_ring_enabled =
          desktop_bool_value(value, settings->focus_ring_enabled);
      applied = 1;
    }
  } else if (strcmp(key, "render:profile") == 0) {
    supported = 1;
    if (apply && settings &&
        desktop_hypr_copy_token_value(token, sizeof(token), value) == 0) {
      snprintf(settings->render_profile, sizeof(settings->render_profile),
               "%s", token);
      applied = 1;
    }
  } else if (strcmp(key, "input:kb_layout") == 0) {
    supported = 1;
    if (apply && settings && desktop_hypr_copy_token_value(
                               token, sizeof(token), value) == 0) {
      snprintf(settings->keyboard_layout, sizeof(settings->keyboard_layout),
               "%s", token);
      applied = 1;
    }
  } else if (strcmp(key, "input:follow_mouse") == 0) {
    supported = 1;
    if (apply && session) {
      session->focus_follows_mouse =
          desktop_parse_int_value(value, 0) > 0 ||
          desktop_bool_value(value, session->focus_follows_mouse);
      applied = 1;
    }
  }

  if (!supported && desktop_hypr_key_runtime_hint(key)) {
    if (strncmp(key, "input:", 6) == 0 ||
        strncmp(key, "device:", 7) == 0) {
      summary->input_hints++;
      if (strncmp(key, "device:", 7) == 0) {
        summary->device_hints++;
      }
    } else if (strncmp(key, "dwindle:", 8) == 0 ||
               strncmp(key, "master:", 7) == 0 ||
               strncmp(key, "general:", 8) == 0 ||
               strncmp(key, "group:", 6) == 0 ||
               strncmp(key, "binds:", 6) == 0) {
      summary->layout_hints++;
    } else if (strncmp(key, "decoration:", 11) == 0) {
      summary->decoration_hints++;
    } else if (strncmp(key, "cursor:", 7) == 0) {
      summary->cursor_hints++;
    } else if (strncmp(key, "render:", 7) == 0 ||
               strncmp(key, "opengl:", 7) == 0) {
      summary->render_hints++;
    } else if (strncmp(key, "debug:", 6) == 0) {
      summary->debug_hints++;
    } else if (strncmp(key, "misc:", 5) == 0) {
      summary->misc_hints++;
    } else if (strncmp(key, "plugin:", 7) == 0 ||
               strncmp(key, "permission:", 11) == 0 ||
               strncmp(key, "ecosystem:", 10) == 0 ||
               strncmp(key, "gestures:", 9) == 0 ||
               strncmp(key, "xwayland:", 9) == 0) {
      summary->misc_hints++;
    }
    summary->prepared_keywords++;
    if (apply && runtime) {
      desktop_hypr_runtime_append(runtime->runtime, sizeof(runtime->runtime),
                                  &runtime->runtime_used, key, value);
      summary->runtime_lines++;
    }
    return 0;
  }

  if (supported) {
    summary->supported_settings++;
    summary->applied_settings += applied;
  } else {
    summary->ignored_keywords++;
  }
  return supported;
}

static int desktop_hypr_scan_config(const char *cfg, int apply,
                                    desktop_hypr_summary_t *summary,
                                    orizon_desktop_session_t *session,
                                    orizon_desktop_settings_t *settings,
                                    desktop_hypr_runtime_t *runtime) {
  int pos = 0;
  int depth = 0;
  int len;
  char sections[4][32];

  if (!cfg || !summary) {
    return -1;
  }
  memset(sections, 0, sizeof(sections));
  len = (int)strlen(cfg);
  while (pos < len) {
    int start = pos;
    int end;
    char line[224];
    char key[64];
    char value[128];
    char full_key[96];

    while (pos < len && cfg[pos] != '\n') {
      pos++;
    }
    end = pos;
    if (pos < len && cfg[pos] == '\n') {
      pos++;
    }
    desktop_trim_copy(line, sizeof(line), cfg + start, end - start);
    desktop_strip_inline_comment(line);
    desktop_trim_copy(line, sizeof(line), line, (int)strlen(line));
    if (!line[0]) {
      continue;
    }
    if (strcmp(line, "}") == 0) {
      if (depth > 0) {
        depth--;
      }
      continue;
    }
    if (line[strlen(line) - 1] == '{') {
      line[strlen(line) - 1] = '\0';
      desktop_trim_copy(key, sizeof(key), line, (int)strlen(line));
      if (desktop_token_safe(key) && depth < 4) {
        snprintf(sections[depth], sizeof(sections[depth]), "%s", key);
        depth++;
      } else {
        summary->malformed_lines++;
      }
      continue;
    }
    summary->parsed_lines++;
    if (desktop_hypr_key_value(line, key, sizeof(key), value,
                               sizeof(value)) < 0) {
      summary->malformed_lines++;
      continue;
    }
    desktop_hypr_join_key(sections, depth, key, full_key, sizeof(full_key));
    desktop_hypr_apply_pair(full_key, value, session, settings, summary,
                            apply, runtime);
  }
  return 0;
}

static int desktop_hypr_exec_once_applies(const char *key, const char *value) {
  return key && value && strcmp(key, "exec-once") == 0 &&
         strstr(value, "terminal") != NULL;
}

static const char *desktop_hypr_trace_route(const char *key,
                                            const char *value) {
  const char *runtime_path;

  if (!key) {
    return "none";
  }
  runtime_path = desktop_hypr_runtime_path_for_key(key);
  if (desktop_hypr_is_supported_setting_key(key)) {
    return runtime_path ? "session/settings+runtime" : "session/settings";
  }
  if (desktop_hypr_exec_once_applies(key, value)) {
    return "session+autostart-runtime";
  }
  return runtime_path ? runtime_path : "none";
}

void orizon_desktop_format_config_trace(char *out, size_t out_size) {
  char cfg[4096];
  char line[256];
  size_t used = 0;
  int n;
  int pos = 0;
  int len;
  int depth = 0;
  int line_no = 0;
  int traced = 0;
  int apply_count = 0;
  int prepare_count = 0;
  int ignored_count = 0;
  int malformed_count = 0;
  int section_count = 0;
  int truncated = 0;
  char sections[4][32];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  memset(sections, 0, sizeof(sections));
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used,
                 "Orizon desktop Hyprland config trace\n");
  snprintf(line, sizeof(line), "path: %s\n",
           ORIZON_DESKTOP_USER_CONFIG_PATH);
  desktop_append(out, out_size, &used, line);
  n = desktop_read_text_file(ORIZON_DESKTOP_USER_CONFIG_PATH, cfg,
                             sizeof(cfg));
  if (n <= 0) {
    snprintf(cfg, sizeof(cfg), "%s", desktop_user_config);
    desktop_append(out, out_size, &used,
                   "source: built-in template preview\n");
  } else {
    desktop_append(out, out_size, &used, "source: user config\n");
  }
  desktop_append(out, out_size, &used,
                 "model: read-only parser trace; no Wayland/wlroots, no manual-drag\n");
  len = (int)strlen(cfg);
  while (pos < len) {
    int start = pos;
    int end;
    char raw[224];
    char key[64];
    char value[128];
    char full_key[96];
    desktop_hypr_summary_t local;
    const char *status = "IGNORE";
    const char *route = "none";

    if (used + 220 >= out_size) {
      truncated = 1;
      break;
    }
    while (pos < len && cfg[pos] != '\n') {
      pos++;
    }
    end = pos;
    if (pos < len && cfg[pos] == '\n') {
      pos++;
    }
    line_no++;
    desktop_trim_copy(raw, sizeof(raw), cfg + start, end - start);
    desktop_strip_inline_comment(raw);
    desktop_trim_copy(raw, sizeof(raw), raw, (int)strlen(raw));
    if (!raw[0]) {
      continue;
    }
    if (strcmp(raw, "}") == 0) {
      if (depth > 0) {
        depth--;
        snprintf(line, sizeof(line),
                 "line %d: SECTION-CLOSE depth=%d status=OK\n",
                 line_no, depth);
        section_count++;
      } else {
        malformed_count++;
        snprintf(line, sizeof(line),
                 "line %d: ERROR unmatched-section-close\n", line_no);
      }
      desktop_append(out, out_size, &used, line);
      traced++;
      continue;
    }
    if (raw[strlen(raw) - 1] == '{') {
      raw[strlen(raw) - 1] = '\0';
      desktop_trim_copy(key, sizeof(key), raw, (int)strlen(raw));
      if (desktop_token_safe(key) && depth < 4) {
        snprintf(sections[depth], sizeof(sections[depth]), "%s", key);
        depth++;
        section_count++;
        snprintf(line, sizeof(line),
                 "line %d: SECTION %s depth=%d status=OK\n", line_no, key,
                 depth);
      } else {
        malformed_count++;
        snprintf(line, sizeof(line),
                 "line %d: ERROR malformed-section name=%s\n", line_no,
                 key[0] ? key : "(empty)");
      }
      desktop_append(out, out_size, &used, line);
      traced++;
      continue;
    }
    if (desktop_hypr_key_value(raw, key, sizeof(key), value,
                               sizeof(value)) < 0) {
      malformed_count++;
      snprintf(line, sizeof(line),
               "line %d: ERROR malformed-key-value text=\"%s\"\n",
               line_no, raw);
      desktop_append(out, out_size, &used, line);
      traced++;
      continue;
    }
    desktop_hypr_join_key(sections, depth, key, full_key, sizeof(full_key));
    memset(&local, 0, sizeof(local));
    desktop_hypr_apply_pair(full_key, value, NULL, NULL, &local, 0, NULL);
    route = desktop_hypr_trace_route(full_key, value);
    if (desktop_hypr_is_supported_setting_key(full_key) ||
        desktop_hypr_exec_once_applies(full_key, value)) {
      status = local.prepared_keywords > 0 ? "APPLY+PREPARE" : "APPLY";
      apply_count++;
      if (local.prepared_keywords > 0) {
        prepare_count++;
      }
    } else if (local.prepared_keywords > 0) {
      status = "PREPARE";
      prepare_count++;
    } else {
      status = "IGNORE";
      ignored_count++;
    }
    snprintf(line, sizeof(line),
             "line %d: %s key=%s route=%s value=\"%s\"\n", line_no, status,
             full_key, route, value);
    desktop_append(out, out_size, &used, line);
    traced++;
  }
  snprintf(line, sizeof(line),
           "summary: lines=%d traced=%d sections=%d apply=%d prepare=%d ignored=%d malformed=%d truncated=%s\n",
           line_no, traced, section_count, apply_count, prepare_count,
           ignored_count, malformed_count, truncated ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "commands: desktop config doctor | desktop config apply | desktop hyprctl configtrace\n");
}

static int desktop_write_session(const orizon_desktop_session_t *session) {
  char text[384];

  if (!session) {
    return -1;
  }
  snprintf(text, sizeof(text),
           "# Orizon desktop session v1\n"
           "theme %s\n"
           "wallpaper %s\n"
           "layout %s\n"
           "bar %s\n"
           "launcher %s\n"
           "autostart-terminal %s\n"
           "focus-follows-mouse %s\n",
           session->theme[0] ? session->theme : "graphite",
           session->wallpaper[0] ? session->wallpaper : "aurora",
           session->layout[0] ? session->layout : "dwindle",
           session->bar_enabled ? "yes" : "no",
           session->launcher_enabled ? "yes" : "no",
           session->autostart_terminal ? "yes" : "no",
           session->focus_follows_mouse ? "yes" : "no");
  return desktop_write_text_file(ORIZON_DESKTOP_SESSION_PATH, text);
}

static void desktop_settings_defaults(orizon_desktop_settings_t *settings) {
  if (!settings) {
    return;
  }
  memset(settings, 0, sizeof(*settings));
  settings->scale = 1;
  settings->gaps_in = 6;
  settings->gaps_out = 12;
  settings->border_size = 2;
  settings->rounding = 8;
  settings->animations_enabled = 1;
  settings->shadows_enabled = 1;
  settings->focus_ring_enabled = 1;
  settings->shadow_range = 18;
  settings->animation_ticks = 18;
  settings->idle_timeout_seconds = 0;
  settings->lock_on_idle = 0;
  snprintf(settings->default_terminal, sizeof(settings->default_terminal),
           "%s", "orizon-terminal");
  snprintf(settings->launcher_provider, sizeof(settings->launcher_provider),
           "%s", "builtin");
  snprintf(settings->bar_position, sizeof(settings->bar_position), "%s",
           "top");
  snprintf(settings->keyboard_layout, sizeof(settings->keyboard_layout), "%s",
           "us");
  snprintf(settings->pointer_profile, sizeof(settings->pointer_profile), "%s",
           "flat");
  snprintf(settings->animation_curve, sizeof(settings->animation_curve), "%s",
           "orizon-pop");
  snprintf(settings->render_profile, sizeof(settings->render_profile), "%s",
           "balanced");
}

static int desktop_write_settings(const orizon_desktop_settings_t *settings) {
  char text[1024];

  if (!settings) {
    return -1;
  }
  snprintf(text, sizeof(text),
           "# Orizon desktop system settings v1\n"
           "# Created when the optional desktop is selected during install or via package.\n"
           "# This is the central settings layer consumed by the Orizon compositor.\n"
           "scale %d\n"
           "gaps-in %d\n"
           "gaps-out %d\n"
           "border-size %d\n"
           "rounding %d\n"
           "animations %s\n"
           "shadows %s\n"
           "focus-ring %s\n"
           "shadow-range %d\n"
           "animation-ticks %d\n"
           "animation-curve %s\n"
           "render-profile %s\n"
           "idle-timeout-seconds %d\n"
           "lock-on-idle %s\n"
           "default-terminal %s\n"
           "launcher-provider %s\n"
           "bar-position %s\n"
           "keyboard-layout %s\n"
           "pointer-profile %s\n",
           desktop_clamp_int(settings->scale, 1, 3),
           desktop_clamp_int(settings->gaps_in, 0, 48),
           desktop_clamp_int(settings->gaps_out, 0, 64),
           desktop_clamp_int(settings->border_size, 0, 8),
           desktop_clamp_int(settings->rounding, 0, 24),
           settings->animations_enabled ? "yes" : "no",
           settings->shadows_enabled ? "yes" : "no",
           settings->focus_ring_enabled ? "yes" : "no",
           desktop_clamp_int(settings->shadow_range, 0, 32),
           desktop_clamp_int(settings->animation_ticks, 4, 60),
           settings->animation_curve[0] ? settings->animation_curve
                                        : "orizon-pop",
           settings->render_profile[0] ? settings->render_profile : "balanced",
           desktop_clamp_int(settings->idle_timeout_seconds, 0, 86400),
           settings->lock_on_idle ? "yes" : "no",
           settings->default_terminal[0] ? settings->default_terminal
                                         : "orizon-terminal",
           settings->launcher_provider[0] ? settings->launcher_provider
                                          : "builtin",
           settings->bar_position[0] ? settings->bar_position : "top",
           settings->keyboard_layout[0] ? settings->keyboard_layout : "us",
           settings->pointer_profile[0] ? settings->pointer_profile : "flat");
  return desktop_write_text_file(ORIZON_DESKTOP_SETTINGS_PATH, text);
}

static int desktop_write_user_config_from_state(
    const orizon_desktop_session_t *session,
    const orizon_desktop_settings_t *settings) {
  char text[6144];
  const char *terminal;
  const char *keyboard;

  if (!session || !settings) {
    return -1;
  }
  terminal = settings->default_terminal[0] ? settings->default_terminal
                                           : "orizon-terminal";
  keyboard = settings->keyboard_layout[0] ? settings->keyboard_layout : "us";
  snprintf(text, sizeof(text),
           "# Orizon Hyprland-style desktop profile\n"
           "# Generated from central desktop settings.\n"
           "# source-session = " ORIZON_DESKTOP_SESSION_PATH "\n"
           "# source-settings = " ORIZON_DESKTOP_SETTINGS_PATH "\n"
           "# sync-command = desktop settings sync\n"
           "$mod = SUPER\n"
           "$terminal = %s\n"
           "$menu = orizon-launcher\n"
           "monitor = ,preferred,auto,%d\n"
           "%s"
           "input:kb_layout = %s\n"
           "input:follow_mouse = %d\n"
           "input:repeat_rate = 40\n"
           "input:repeat_delay = 300\n"
           "input:touchpad:natural_scroll = false\n"
           "general:layout = %s\n"
           "general:gaps_in = %d\n"
           "general:gaps_out = %d\n"
           "general:border_size = %d\n"
           "general:col.active_border = rgba(8bd5ffcc)\n"
           "general:col.inactive_border = rgba(2a2f3acc)\n"
           "decoration:rounding = %d\n"
           "decoration:shadow:enabled = %s\n"
           "decoration:shadow:range = %d\n"
           "render:focus_ring = %s\n"
           "render:profile = %s\n"
           "decoration:blur:enabled = false\n"
           "animations:enabled = %s\n"
           "animations:tick_budget = %d\n"
           "animations:curve = %s\n"
           "bezier = orizon-pop, 0.16, 1, 0.3, 1\n"
           "animation = windows, 1, 2, orizon-pop\n"
           "cursor:no_hardware_cursors = true\n"
           "render:direct_scanout = false\n"
           "debug:disable_logs = false\n"
           "misc:disable_hyprland_logo = false\n"
           "misc:force_default_wallpaper = 0\n"
           "windowrulev2 = tile,class:^(orizon-.*)$\n"
           "layerrule = blur, launcher\n"
           "source = ~/.config/hypr/orizon-local.conf\n"
           "bind = $mod, RETURN, exec, terminal\n"
           "bind = $mod, Q, killactive\n"
           "bind = $mod, D, exec, orizon-launcher\n"
           "bind = $mod, A, exec, desktop autostart\n"
           "bind = $mod, B, exec, desktop bar toggle\n"
           "bind = $mod, F, exec, desktop focus toggle\n"
           "bind = $mod, M, fullscreen\n"
           "bind = $mod, P, pseudo\n"
           "bind = $mod SHIFT, P, pin\n"
           "bind = $mod, J, togglesplit\n"
           "bind = $mod SHIFT, J, layoutmsg, orientationnext\n"
           "bind = $mod, S, layoutmsg, swapwithmaster\n"
           "bind = $mod SHIFT, S, layoutmsg, focusmaster\n"
           "bind = $mod, R, submap, resize\n"
           "bind = $mod SHIFT, R, submap, reset\n"
           "bind = $mod, H, movefocus, l\n"
           "bind = $mod, L, movefocus, r\n"
           "bind = $mod, left, movefocus, l\n"
           "bind = $mod, right, movefocus, r\n"
           "bind = $mod, up, movefocus, u\n"
           "bind = $mod, down, movefocus, d\n"
           "bind = $mod SHIFT, left, swapwindow, l\n"
           "bind = $mod SHIFT, right, swapwindow, r\n"
           "bind = $mod SHIFT, up, swapwindow, u\n"
           "bind = $mod SHIFT, down, swapwindow, d\n"
           "bind = $mod, Tab, cyclenext\n"
           "bind = $mod SHIFT, Tab, swapnext\n"
           "bind = $mod, C, exec, desktop session\n"
           "bind = $mod, 1, workspace, 1\n"
           "bind = $mod, 2, workspace, 2\n"
           "bind = $mod, 3, workspace, 3\n"
           "bind = $mod SHIFT, 1, movetoworkspace, 1\n"
           "bind = $mod SHIFT, 2, movetoworkspace, 2\n"
           "bind = $mod SHIFT, 3, movetoworkspace, 3\n"
           "bind = F1, exec, desktop open terminal\n"
           "bind = F2, killactive\n"
           "bind = F4, fullscreen\n"
           "bind = F5, pseudo\n"
           "bind = F9, submap, resize\n"
           "bind = F10, submap, move\n"
           "bind = F11, submap, launch\n"
           "bind = F12, submap, reset\n"
           "submap = resize\n"
           "bind = , right, resizeactive, 5 0\n"
           "bind = , left, resizeactive, -5 0\n"
           "bind = , up, resizeactive, 0 5\n"
           "bind = , down, resizeactive, 0 -5\n"
           "bind = , escape, submap, reset\n"
           "submap = move\n"
           "bind = , right, movefocus, r\n"
           "bind = , left, movefocus, l\n"
           "bind = , 1, movetoworkspace, 1\n"
           "bind = , 2, movetoworkspace, 2\n"
           "bind = , 3, movetoworkspace, 3\n"
           "bind = , escape, submap, reset\n"
           "submap = launch\n"
           "bind = , t, exec, terminal\n"
           "bind = , s, exec, orizon-settings\n"
           "bind = , l, exec, orizon-logs\n"
           "bind = , p, exec, orizon-packages\n"
           "bind = , u, exec, orizon-update-viewer\n"
           "bind = , d, exec, orizon-launcher\n"
           "bind = , q, killactive\n"
           "bind = , escape, submap, reset\n"
           "submap = default\n"
           "dwindle:pseudotile = true\n"
           "dwindle:preserve_split = true\n",
           terminal, desktop_clamp_int(settings->scale, 1, 3),
           session->autostart_terminal ? "exec-once = terminal\n"
                                       : "# exec-once disabled by /system session\n",
           keyboard, session->focus_follows_mouse ? 1 : 0,
           session->layout[0] ? session->layout : "dwindle",
           desktop_clamp_int(settings->gaps_in, 0, 48),
           desktop_clamp_int(settings->gaps_out, 0, 64),
           desktop_clamp_int(settings->border_size, 0, 8),
           desktop_clamp_int(settings->rounding, 0, 24),
           settings->shadows_enabled ? "true" : "false",
           desktop_clamp_int(settings->shadow_range, 0, 32),
           settings->focus_ring_enabled ? "true" : "false",
           settings->render_profile[0] ? settings->render_profile : "balanced",
           settings->animations_enabled ? "true" : "false",
           desktop_clamp_int(settings->animation_ticks, 4, 60),
           settings->animation_curve[0] ? settings->animation_curve
                                        : "orizon-pop");
  return desktop_write_text_file(ORIZON_DESKTOP_USER_CONFIG_PATH, text);
}

static void desktop_ensure_dirs(void) {
  vfs_mkdir("/system");
  vfs_mkdir("/system/share");
  vfs_mkdir("/home");
  vfs_mkdir("/home/orizon");
  vfs_mkdir("/home/orizon/.config");
  vfs_mkdir("/home/orizon/.config/hypr");
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/packages");
  vfs_mkdir("/logs");
}

static void desktop_log_event(const char *event) {
  char line[192];

  if (!event) {
    return;
  }
  snprintf(line, sizeof(line), "ticks=%lu desktop %s\n",
           (unsigned long)timer_ticks(), event);
  desktop_append_text_file(ORIZON_DESKTOP_LOG_PATH, line);
}

static void desktop_session_log_event(const char *action, const char *runtime,
                                      const char *note) {
  char line[256];

  if (!action) {
    return;
  }
  snprintf(line, sizeof(line),
           "ticks=%lu action=%s runtime=%s boot-mode=%s note=%s\n",
           (unsigned long)timer_ticks(), action ? action : "unknown",
           runtime ? runtime : "unknown",
           orizon_system_is_installed() ? "installed" : "live-iso",
           note ? note : "none");
  desktop_append_text_file(ORIZON_DESKTOP_SESSION_LOG_PATH, line);
}

static int desktop_policy_enabled_from_file(void) {
  char cfg[768];

  if (desktop_read_text_file(ORIZON_DESKTOP_CONFIG_PATH, cfg, sizeof(cfg)) <=
      0) {
    return 0;
  }
  return (strstr(cfg, "enabled yes") || strstr(cfg, "enabled true") ||
          strstr(cfg, "enabled on"))
             ? 1
             : 0;
}

static int desktop_state_counter(const char *key) {
  char cfg[1024];
  char value[32];

  if (!key || desktop_read_text_file(ORIZON_DESKTOP_STATE_PATH, cfg,
                                     sizeof(cfg)) <= 0) {
    return 0;
  }
  desktop_session_get_value(cfg, key, value, sizeof(value), "0");
  return desktop_parse_int_value(value, 0);
}

static int desktop_write_session_state(const char *desired,
                                       const char *runtime,
                                       const char *action,
                                       const char *note) {
  char text[1280];
  orizon_desktop_session_t session;
  int installed;
  int policy_enabled;
  int start_count;
  int stop_count;
  int restart_count;
  int reload_count;
  int recover_count;
  int crash_count;
  const char *health = "PASS";

  if (!desired) {
    desired = "stopped";
  }
  if (!runtime) {
    runtime = "inactive";
  }
  if (!action) {
    action = "unknown";
  }
  if (!note) {
    note = "none";
  }
  orizon_desktop_load_session(&session);
  installed = orizon_system_is_installed();
  policy_enabled = desktop_policy_enabled_from_file();
  start_count = desktop_state_counter("start-count");
  stop_count = desktop_state_counter("stop-count");
  restart_count = desktop_state_counter("restart-count");
  reload_count = desktop_state_counter("reload-count");
  recover_count = desktop_state_counter("recover-count");
  crash_count = desktop_state_counter("crash-count");

  if (strcmp(action, "start") == 0) {
    start_count++;
  } else if (strcmp(action, "stop") == 0) {
    stop_count++;
  } else if (strcmp(action, "restart") == 0) {
    restart_count++;
  } else if (strcmp(action, "reload") == 0) {
    reload_count++;
  } else if (strcmp(action, "recover") == 0) {
    recover_count++;
  }
  if ((strcmp(desired, "started") == 0 && !policy_enabled) ||
      (strcmp(desired, "started") == 0 && strcmp(runtime, "active") != 0) ||
      (strcmp(desired, "stopped") == 0 && strcmp(runtime, "inactive") != 0)) {
    health = "WARN";
  }

  snprintf(text, sizeof(text),
           "# Orizon desktop session manager state v2\n"
           "schema-version 2\n"
           "health %s\n"
           "desired-state %s\n"
           "runtime-state %s\n"
           "last-action %s\n"
           "last-ticks %lu\n"
           "boot-mode %s\n"
           "installed-marker %s\n"
           "policy %s\n"
           "autostart-terminal %s\n"
           "focus-follows-mouse %s\n"
           "layout %s\n"
           "start-count %d\n"
           "stop-count %d\n"
           "restart-count %d\n"
           "reload-count %d\n"
           "recover-count %d\n"
           "crash-count %d\n"
           "crash-recover ready\n"
           "recover-command desktop recover\n"
           "rescue-command desktop rescue\n"
           "config-apply-command desktop config apply\n"
           "config-trace-command desktop config trace\n"
           "client-model-command desktop client-model\n"
           "hyprctl-clientmodel-command desktop hyprctl clientmodel\n"
           "settings-doctor-command desktop settings doctor\n"
           "state-path " ORIZON_DESKTOP_STATE_PATH "\n"
           "session-log " ORIZON_DESKTOP_SESSION_LOG_PATH "\n"
           "manual-window-drag no\n"
           "note %s\n",
           health, desired, runtime, action, (unsigned long)timer_ticks(),
           installed ? "installed" : "live-iso",
           installed ? "present" : "missing",
           policy_enabled ? "enabled" : "disabled",
           session.autostart_terminal ? "yes" : "no",
           session.focus_follows_mouse ? "yes" : "no", session.layout,
           start_count, stop_count, restart_count, reload_count, recover_count,
           crash_count, note);
  desktop_session_log_event(action, runtime, note);
  return desktop_write_text_file(ORIZON_DESKTOP_STATE_PATH, text);
}

int orizon_desktop_ensure_defaults(void) {
  int rc = 0;
  int policy_enabled;

  desktop_ensure_dirs();
  if (!vfs_exists(ORIZON_DESKTOP_CONFIG_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_CONFIG_PATH,
                              desktop_default_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_TEMPLATE_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_TEMPLATE_PATH,
                              desktop_user_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_SESSION_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_SESSION_PATH,
                              desktop_session_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_SETTINGS_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_SETTINGS_PATH,
                              desktop_settings_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_BINDS_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_BINDS_PATH,
                              desktop_binds_runtime_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_AUTOSTART_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_AUTOSTART_PATH,
                              desktop_autostart_runtime_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_RULES_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_RULES_PATH,
                              desktop_rules_runtime_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_MONITORS_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_MONITORS_PATH,
                              desktop_monitors_runtime_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_LAYERS_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_LAYERS_PATH,
                              desktop_layers_runtime_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_RUNTIME_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_RUNTIME_PATH,
                              desktop_runtime_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_MODULES_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_MODULES_PATH,
                              desktop_modules_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_BACKEND_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_BACKEND_PATH,
                              desktop_backend_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_PROTOCOL_PATH) &&
      desktop_write_text_file(ORIZON_DESKTOP_PROTOCOL_PATH,
                              desktop_protocol_config) < 0) {
    rc = -1;
  }
  if (!vfs_exists(ORIZON_DESKTOP_STATE_PATH)) {
    policy_enabled = desktop_policy_enabled_from_file();
    if (desktop_write_session_state(policy_enabled ? "started" : "stopped",
                                    policy_enabled ? "active" : "inactive",
                                    "defaults",
                                    "created-missing-state") < 0) {
      if (desktop_write_text_file(ORIZON_DESKTOP_STATE_PATH,
                                  desktop_state_config) < 0) {
        rc = -1;
      }
    }
  }
  return rc;
}

int orizon_desktop_is_enabled(void) {
  return desktop_policy_enabled_from_file();
}

int orizon_desktop_write_user_config(char *status, size_t status_size) {
  int rc;

  desktop_ensure_dirs();
  rc = desktop_write_text_file(ORIZON_DESKTOP_USER_CONFIG_PATH,
                               desktop_user_config);
  if (status && status_size) {
    snprintf(status, status_size, "desktop config: %s\npath: %s\n",
             rc == 0 ? "written" : "failed",
             ORIZON_DESKTOP_USER_CONFIG_PATH);
  }
  return rc;
}

int orizon_desktop_load_session(orizon_desktop_session_t *session) {
  char cfg[768];
  char value[48];

  if (!session) {
    return -1;
  }
  memset(session, 0, sizeof(*session));
  snprintf(session->theme, sizeof(session->theme), "%s", "graphite");
  snprintf(session->wallpaper, sizeof(session->wallpaper), "%s", "aurora");
  snprintf(session->layout, sizeof(session->layout), "%s", "dwindle");
  session->bar_enabled = 1;
  session->launcher_enabled = 1;
  session->autostart_terminal = 1;
  session->focus_follows_mouse = 0;

  orizon_desktop_ensure_defaults();
  if (desktop_read_text_file(ORIZON_DESKTOP_SESSION_PATH, cfg,
                             sizeof(cfg)) <= 0) {
    return -1;
  }
  if (desktop_session_get_value(cfg, "theme", value, sizeof(value),
                                session->theme) == 0 &&
      desktop_token_safe(value)) {
    snprintf(session->theme, sizeof(session->theme), "%s", value);
  }
  if (desktop_session_get_value(cfg, "wallpaper", value, sizeof(value),
                                session->wallpaper) == 0 &&
      desktop_token_safe(value)) {
    snprintf(session->wallpaper, sizeof(session->wallpaper), "%s", value);
  }
  if (desktop_session_get_value(cfg, "layout", value, sizeof(value),
                                session->layout) == 0 &&
      desktop_token_safe(value)) {
    snprintf(session->layout, sizeof(session->layout), "%s", value);
  }
  if (desktop_session_get_value(cfg, "bar", value, sizeof(value), "yes") ==
      0) {
    session->bar_enabled = desktop_bool_value(value, 1);
  }
  if (desktop_session_get_value(cfg, "launcher", value, sizeof(value),
                                "yes") == 0) {
    session->launcher_enabled = desktop_bool_value(value, 1);
  }
  if (desktop_session_get_value(cfg, "autostart-terminal", value,
                                sizeof(value), "yes") == 0) {
    session->autostart_terminal = desktop_bool_value(value, 1);
  }
  if (desktop_session_get_value(cfg, "focus-follows-mouse", value,
                                sizeof(value), "no") == 0) {
    session->focus_follows_mouse = desktop_bool_value(value, 0);
  }
  return 0;
}

int orizon_desktop_load_settings(orizon_desktop_settings_t *settings) {
  char cfg[1536];
  char value[64];

  if (!settings) {
    return -1;
  }
  desktop_settings_defaults(settings);
  orizon_desktop_ensure_defaults();
  if (desktop_read_text_file(ORIZON_DESKTOP_SETTINGS_PATH, cfg,
                             sizeof(cfg)) <= 0) {
    return -1;
  }
  if (desktop_session_get_value(cfg, "scale", value, sizeof(value), "1") ==
      0) {
    settings->scale =
        desktop_clamp_int(desktop_parse_int_value(value, 1), 1, 3);
  }
  if (desktop_session_get_value(cfg, "gaps-in", value, sizeof(value), "6") ==
      0) {
    settings->gaps_in =
        desktop_clamp_int(desktop_parse_int_value(value, 6), 0, 48);
  }
  if (desktop_session_get_value(cfg, "gaps-out", value, sizeof(value), "12") ==
      0) {
    settings->gaps_out =
        desktop_clamp_int(desktop_parse_int_value(value, 12), 0, 64);
  }
  if (desktop_session_get_value(cfg, "border-size", value, sizeof(value),
                                "2") == 0) {
    settings->border_size =
        desktop_clamp_int(desktop_parse_int_value(value, 2), 0, 8);
  }
  if (desktop_session_get_value(cfg, "rounding", value, sizeof(value), "8") ==
      0) {
    settings->rounding =
        desktop_clamp_int(desktop_parse_int_value(value, 8), 0, 24);
  }
  if (desktop_session_get_value(cfg, "animations", value, sizeof(value),
                                "yes") == 0) {
    settings->animations_enabled = desktop_bool_value(value, 1);
  }
  if (desktop_session_get_value(cfg, "shadows", value, sizeof(value), "yes") ==
      0) {
    settings->shadows_enabled = desktop_bool_value(value, 1);
  }
  if (desktop_session_get_value(cfg, "focus-ring", value, sizeof(value),
                                "yes") == 0) {
    settings->focus_ring_enabled = desktop_bool_value(value, 1);
  }
  if (desktop_session_get_value(cfg, "shadow-range", value, sizeof(value),
                                "18") == 0) {
    settings->shadow_range =
        desktop_clamp_int(desktop_parse_int_value(value, 18), 0, 32);
  }
  if (desktop_session_get_value(cfg, "animation-ticks", value, sizeof(value),
                                "18") == 0) {
    settings->animation_ticks =
        desktop_clamp_int(desktop_parse_int_value(value, 18), 4, 60);
  }
  if (desktop_session_get_value(cfg, "idle-timeout-seconds", value,
                                sizeof(value), "0") == 0) {
    settings->idle_timeout_seconds =
        desktop_clamp_int(desktop_parse_int_value(value, 0), 0, 86400);
  }
  if (desktop_session_get_value(cfg, "lock-on-idle", value, sizeof(value),
                                "no") == 0) {
    settings->lock_on_idle = desktop_bool_value(value, 0);
  }
  if (desktop_session_get_value(cfg, "default-terminal", value, sizeof(value),
                                settings->default_terminal) == 0 &&
      desktop_token_safe(value)) {
    snprintf(settings->default_terminal, sizeof(settings->default_terminal),
             "%s", value);
  }
  if (desktop_session_get_value(cfg, "launcher-provider", value,
                                sizeof(value), settings->launcher_provider) ==
          0 &&
      desktop_token_safe(value)) {
    snprintf(settings->launcher_provider, sizeof(settings->launcher_provider),
             "%s", value);
  }
  if (desktop_session_get_value(cfg, "bar-position", value, sizeof(value),
                                settings->bar_position) == 0 &&
      desktop_token_safe(value)) {
    snprintf(settings->bar_position, sizeof(settings->bar_position), "%s",
             value);
  }
  if (desktop_session_get_value(cfg, "keyboard-layout", value, sizeof(value),
                                settings->keyboard_layout) == 0 &&
      desktop_token_safe(value)) {
    snprintf(settings->keyboard_layout, sizeof(settings->keyboard_layout), "%s",
             value);
  }
  if (desktop_session_get_value(cfg, "pointer-profile", value, sizeof(value),
                                settings->pointer_profile) == 0 &&
      desktop_token_safe(value)) {
    snprintf(settings->pointer_profile, sizeof(settings->pointer_profile), "%s",
             value);
  }
  if (desktop_session_get_value(cfg, "animation-curve", value, sizeof(value),
                                settings->animation_curve) == 0 &&
      desktop_token_safe(value)) {
    snprintf(settings->animation_curve, sizeof(settings->animation_curve), "%s",
             value);
  }
  if (desktop_session_get_value(cfg, "render-profile", value, sizeof(value),
                                settings->render_profile) == 0 &&
      desktop_token_safe(value)) {
    snprintf(settings->render_profile, sizeof(settings->render_profile), "%s",
             value);
  }
  return 0;
}

int orizon_desktop_set_session_option(const char *key, const char *value,
                                      char *status, size_t status_size) {
  orizon_desktop_session_t session;
  int rc;

  if (status && status_size) {
    status[0] = '\0';
  }
  if (!key || !value || !value[0] || !desktop_token_safe(value)) {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop session: invalid value\n"
               "allowed: letters, numbers, dash, underscore, dot\n");
    }
    return -1;
  }
  orizon_desktop_load_session(&session);
  if (strcmp(key, "theme") == 0) {
    snprintf(session.theme, sizeof(session.theme), "%s", value);
  } else if (strcmp(key, "wallpaper") == 0) {
    snprintf(session.wallpaper, sizeof(session.wallpaper), "%s", value);
  } else if (strcmp(key, "layout") == 0) {
    snprintf(session.layout, sizeof(session.layout), "%s", value);
  } else if (strcmp(key, "bar") == 0) {
    session.bar_enabled = desktop_bool_value(value, session.bar_enabled);
  } else if (strcmp(key, "launcher") == 0) {
    session.launcher_enabled =
        desktop_bool_value(value, session.launcher_enabled);
  } else if (strcmp(key, "autostart-terminal") == 0) {
    session.autostart_terminal =
        desktop_bool_value(value, session.autostart_terminal);
  } else if (strcmp(key, "focus") == 0 ||
             strcmp(key, "focus-follows-mouse") == 0) {
    session.focus_follows_mouse =
        desktop_bool_value(value, session.focus_follows_mouse);
  } else {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop session: unknown key '%s'\n"
               "keys: theme wallpaper layout bar launcher autostart-terminal focus\n",
               key);
    }
    return -2;
  }
  rc = desktop_write_session(&session);
  desktop_log_event("session updated");
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop session: %s\n"
             "%s: %s\n"
             "path: %s\n"
             "apply: desktop apply or desktop status\n",
             rc == 0 ? "updated" : "write-failed", key, value,
             ORIZON_DESKTOP_SESSION_PATH);
  }
  return rc;
}

int orizon_desktop_set_setting(const char *key, const char *value, char *status,
                               size_t status_size) {
  orizon_desktop_settings_t settings;
  int rc;

  if (status && status_size) {
    status[0] = '\0';
  }
  if (!key || !key[0] || !value || !value[0] || !desktop_token_safe(key) ||
      !desktop_token_safe(value)) {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop settings: invalid key/value\n"
               "allowed: letters, numbers, dash, underscore, dot\n");
    }
    return -1;
  }
  orizon_desktop_load_settings(&settings);
  if (strcmp(key, "scale") == 0) {
    settings.scale =
        desktop_clamp_int(desktop_parse_int_value(value, settings.scale), 1, 3);
  } else if (strcmp(key, "gaps-in") == 0) {
    settings.gaps_in = desktop_clamp_int(
        desktop_parse_int_value(value, settings.gaps_in), 0, 48);
  } else if (strcmp(key, "gaps-out") == 0) {
    settings.gaps_out = desktop_clamp_int(
        desktop_parse_int_value(value, settings.gaps_out), 0, 64);
  } else if (strcmp(key, "border-size") == 0) {
    settings.border_size = desktop_clamp_int(
        desktop_parse_int_value(value, settings.border_size), 0, 8);
  } else if (strcmp(key, "rounding") == 0) {
    settings.rounding = desktop_clamp_int(
        desktop_parse_int_value(value, settings.rounding), 0, 24);
  } else if (strcmp(key, "animations") == 0) {
    settings.animations_enabled =
        desktop_bool_value(value, settings.animations_enabled);
  } else if (strcmp(key, "shadows") == 0) {
    settings.shadows_enabled = desktop_bool_value(value, settings.shadows_enabled);
  } else if (strcmp(key, "focus-ring") == 0) {
    settings.focus_ring_enabled =
        desktop_bool_value(value, settings.focus_ring_enabled);
  } else if (strcmp(key, "shadow-range") == 0) {
    settings.shadow_range = desktop_clamp_int(
        desktop_parse_int_value(value, settings.shadow_range), 0, 32);
  } else if (strcmp(key, "animation-ticks") == 0) {
    settings.animation_ticks = desktop_clamp_int(
        desktop_parse_int_value(value, settings.animation_ticks), 4, 60);
  } else if (strcmp(key, "animation-curve") == 0) {
    snprintf(settings.animation_curve, sizeof(settings.animation_curve), "%s",
             value);
  } else if (strcmp(key, "render-profile") == 0) {
    snprintf(settings.render_profile, sizeof(settings.render_profile), "%s",
             value);
  } else if (strcmp(key, "idle-timeout-seconds") == 0) {
    settings.idle_timeout_seconds = desktop_clamp_int(
        desktop_parse_int_value(value, settings.idle_timeout_seconds), 0,
        86400);
  } else if (strcmp(key, "lock-on-idle") == 0) {
    settings.lock_on_idle = desktop_bool_value(value, settings.lock_on_idle);
  } else if (strcmp(key, "default-terminal") == 0) {
    snprintf(settings.default_terminal, sizeof(settings.default_terminal),
             "%s", value);
  } else if (strcmp(key, "launcher-provider") == 0) {
    snprintf(settings.launcher_provider, sizeof(settings.launcher_provider),
             "%s", value);
  } else if (strcmp(key, "bar-position") == 0) {
    snprintf(settings.bar_position, sizeof(settings.bar_position), "%s",
             value);
  } else if (strcmp(key, "keyboard-layout") == 0) {
    snprintf(settings.keyboard_layout, sizeof(settings.keyboard_layout), "%s",
             value);
  } else if (strcmp(key, "pointer-profile") == 0) {
    snprintf(settings.pointer_profile, sizeof(settings.pointer_profile), "%s",
             value);
  } else {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop settings: unknown key '%s'\n"
               "keys: scale gaps-in gaps-out border-size rounding animations shadows focus-ring shadow-range animation-ticks animation-curve render-profile idle-timeout-seconds lock-on-idle default-terminal launcher-provider bar-position keyboard-layout pointer-profile\n",
               key);
    }
    return -2;
  }
  rc = desktop_write_settings(&settings);
  desktop_log_event("settings updated");
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop settings: %s\n"
             "%s: %s\n"
             "path: %s\n"
             "apply: desktop apply or desktop hyprctl reload\n",
             rc == 0 ? "updated" : "write-failed", key, value,
             ORIZON_DESKTOP_SETTINGS_PATH);
  }
  return rc;
}

static const char *desktop_normalize_keyboard_layout(const char *value) {
  if (!value) {
    return NULL;
  }
  if (strcmp(value, "fr") == 0 || strcmp(value, "fr-azerty") == 0 ||
      strcmp(value, "azerty") == 0) {
    return "fr-azerty";
  }
  if (strcmp(value, "us") == 0 || strcmp(value, "us-qwerty") == 0 ||
      strcmp(value, "qwerty") == 0) {
    return "us-qwerty";
  }
  return NULL;
}

static int desktop_pointer_profile_supported(const char *value) {
  return value && (strcmp(value, "flat") == 0 ||
                   strcmp(value, "natural") == 0 ||
                   strcmp(value, "precise") == 0 ||
                   strcmp(value, "accelerated") == 0);
}

static int desktop_write_keyboard_layout_files(const char *layout) {
  char text[32];

  if (!layout) {
    return -1;
  }
  snprintf(text, sizeof(text), "%s\n", layout);
  vfs_mkdir("/workspace");
  vfs_mkdir("/workspace/.orizon");
  vfs_mkdir("/system");
  desktop_write_text_file("/workspace/.orizon/keyboard", text);
  return desktop_write_text_file("/system/keyboard", text);
}

void orizon_desktop_format_input(char *out, size_t out_size) {
  orizon_desktop_settings_t settings;
  orizon_desktop_session_t session;
  size_t used = 0;
  char line[192];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_load_settings(&settings);
  orizon_desktop_load_session(&session);
  desktop_append(out, out_size, &used, "Orizon desktop input\n");
  desktop_append(out, out_size, &used,
                 "model: Hyprland-style keyboard/pointer facade over VM framebuffer input\n");
  snprintf(line, sizeof(line), "keyboard-layout: %s\n",
           settings.keyboard_layout);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "active-kernel-layout: %s\n",
           input_keyboard_layout());
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "pointer-profile: %s\n",
           settings.pointer_profile);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "focus-follows-mouse: %s\n",
           session.focus_follows_mouse ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "manual-window-drag: no\n");
  desktop_append(out, out_size, &used,
                 "set-layout: desktop input layout <fr|us>\n");
  desktop_append(out, out_size, &used,
                 "set-pointer: desktop input pointer <flat|natural|precise|accelerated>\n");
  desktop_append(out, out_size, &used,
                 "set-focus: desktop input focus <on|off|toggle>\n");
  desktop_append(out, out_size, &used,
                 "submaps: desktop keymap | desktop dispatch submap <default|resize|move|launch>\n");
  desktop_append(out, out_size, &used,
                 "paths: /system/desktop-settings.conf, /system/keyboard, /workspace/.orizon/keyboard\n");
  desktop_append(out, out_size, &used,
                 "limits: no libinput/Wayland backend yet; VM-safe diagnostics only\n");
}

int orizon_desktop_set_input(const char *key, const char *value, char *status,
                             size_t status_size) {
  orizon_desktop_settings_t settings;
  orizon_desktop_session_t session;
  const char *layout;
  int rc = 0;

  if (status && status_size) {
    status[0] = '\0';
  }
  if (!key || !key[0] || !value || !value[0] || !desktop_token_safe(key) ||
      !desktop_token_safe(value)) {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop input: invalid key/value\n"
               "usage: desktop input layout <fr|us> | pointer <flat|natural|precise|accelerated> | focus <on|off|toggle>\n");
    }
    return -1;
  }

  if (strcmp(key, "layout") == 0 || strcmp(key, "keyboard") == 0 ||
      strcmp(key, "keyboard-layout") == 0 || strcmp(key, "kb-layout") == 0) {
    layout = desktop_normalize_keyboard_layout(value);
    if (!layout) {
      if (status && status_size) {
        snprintf(status, status_size,
                 "desktop input: unsupported keyboard layout '%s'\n"
                 "supported: fr, fr-azerty, us, us-qwerty\n",
                 value);
      }
      return -2;
    }
    orizon_desktop_load_settings(&settings);
    snprintf(settings.keyboard_layout, sizeof(settings.keyboard_layout), "%s",
             layout);
    rc = desktop_write_settings(&settings);
    input_set_keyboard_layout(layout);
    desktop_write_keyboard_layout_files(layout);
    desktop_log_event("input keyboard layout updated");
    vfs_persist_save();
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop input: updated\n"
               "keyboard-layout: %s\n"
               "active-kernel-layout: %s\n"
               "paths: /system/desktop-settings.conf /system/keyboard /workspace/.orizon/keyboard\n"
               "apply: desktop keymap | desktop input\n",
               layout, input_keyboard_layout());
    }
    return rc;
  }

  if (strcmp(key, "pointer") == 0 || strcmp(key, "pointer-profile") == 0 ||
      strcmp(key, "mouse") == 0) {
    if (!desktop_pointer_profile_supported(value)) {
      if (status && status_size) {
        snprintf(status, status_size,
                 "desktop input: unsupported pointer profile '%s'\n"
                 "supported: flat, natural, precise, accelerated\n",
                 value);
      }
      return -2;
    }
    orizon_desktop_load_settings(&settings);
    snprintf(settings.pointer_profile, sizeof(settings.pointer_profile), "%s",
             value);
    rc = desktop_write_settings(&settings);
    desktop_log_event("input pointer profile updated");
    vfs_persist_save();
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop input: updated\n"
               "pointer-profile: %s\n"
               "manual-window-drag: no\n"
               "note: profile is recorded for VM diagnostics; no libinput backend yet\n",
               value);
    }
    return rc;
  }

  if (strcmp(key, "focus") == 0 || strcmp(key, "follow-mouse") == 0 ||
      strcmp(key, "focus-follows-mouse") == 0) {
    orizon_desktop_load_session(&session);
    if (strcmp(value, "toggle") == 0) {
      session.focus_follows_mouse = !session.focus_follows_mouse;
    } else {
      session.focus_follows_mouse =
          desktop_bool_value(value, session.focus_follows_mouse);
    }
    rc = desktop_write_session(&session);
    desktop_log_event("input focus policy updated");
    vfs_persist_save();
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop input: updated\n"
               "focus-follows-mouse: %s\n"
               "manual-window-drag: no\n"
               "apply: desktop pointer | desktop keymap\n",
               session.focus_follows_mouse ? "yes" : "no");
    }
    return rc;
  }

  if (status && status_size) {
    snprintf(status, status_size,
             "desktop input: unknown key '%s'\n"
             "keys: layout keyboard pointer pointer-profile focus focus-follows-mouse\n",
             key);
  }
  return -2;
}

int orizon_desktop_repair_settings(char *status, size_t status_size) {
  int rc;

  desktop_ensure_dirs();
  rc = desktop_write_text_file(ORIZON_DESKTOP_SETTINGS_PATH,
                               desktop_settings_config);
  desktop_log_event("settings repaired defaults");
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop settings: %s\n"
             "path: %s\n"
             "source: built-in defaults\n",
             rc == 0 ? "repaired" : "write-failed",
             ORIZON_DESKTOP_SETTINGS_PATH);
  }
  return rc;
}

int orizon_desktop_apply_settings_preset(const char *preset, char *status,
                                         size_t status_size) {
  orizon_desktop_settings_t settings;
  char event[96];
  int rc;

  if (status && status_size) {
    status[0] = '\0';
  }
  if (!preset || !preset[0] || !desktop_token_safe(preset)) {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop settings preset: invalid value\n"
               "presets: default compact cozy performance accessibility locked\n");
    }
    return -1;
  }

  desktop_settings_defaults(&settings);
  if (strcmp(preset, "default") == 0 || strcmp(preset, "graphite") == 0) {
    /* Built-in defaults already loaded. */
  } else if (strcmp(preset, "compact") == 0) {
    settings.gaps_in = 2;
    settings.gaps_out = 4;
    settings.border_size = 1;
    settings.rounding = 4;
    settings.shadows_enabled = 0;
    settings.shadow_range = 8;
    settings.animation_ticks = 12;
    snprintf(settings.render_profile, sizeof(settings.render_profile), "%s",
             "compact");
  } else if (strcmp(preset, "cozy") == 0 ||
             strcmp(preset, "comfortable") == 0) {
    settings.gaps_in = 10;
    settings.gaps_out = 18;
    settings.border_size = 2;
    settings.rounding = 12;
    settings.shadows_enabled = 1;
    settings.shadow_range = 22;
    settings.animation_ticks = 22;
    snprintf(settings.animation_curve, sizeof(settings.animation_curve), "%s",
             "orizon-slide");
    snprintf(settings.render_profile, sizeof(settings.render_profile), "%s",
             "cozy");
  } else if (strcmp(preset, "performance") == 0 ||
             strcmp(preset, "vm") == 0) {
    settings.gaps_in = 4;
    settings.gaps_out = 8;
    settings.border_size = 1;
    settings.rounding = 0;
    settings.animations_enabled = 0;
    settings.shadows_enabled = 0;
    settings.focus_ring_enabled = 1;
    settings.shadow_range = 0;
    settings.animation_ticks = 8;
    snprintf(settings.render_profile, sizeof(settings.render_profile), "%s",
             "performance");
  } else if (strcmp(preset, "accessibility") == 0 ||
             strcmp(preset, "large") == 0) {
    settings.scale = 2;
    settings.gaps_in = 12;
    settings.gaps_out = 20;
    settings.border_size = 4;
    settings.rounding = 10;
    settings.animations_enabled = 0;
    settings.shadows_enabled = 1;
    settings.focus_ring_enabled = 1;
    settings.shadow_range = 24;
    settings.animation_ticks = 10;
    snprintf(settings.render_profile, sizeof(settings.render_profile), "%s",
             "accessibility");
    snprintf(settings.pointer_profile, sizeof(settings.pointer_profile), "%s",
             "adaptive");
  } else if (strcmp(preset, "locked") == 0 ||
             strcmp(preset, "secure") == 0) {
    settings.gaps_in = 6;
    settings.gaps_out = 10;
    settings.border_size = 2;
    settings.rounding = 6;
    settings.idle_timeout_seconds = 300;
    settings.lock_on_idle = 1;
    settings.animations_enabled = 0;
    settings.shadows_enabled = 0;
    settings.focus_ring_enabled = 1;
    settings.shadow_range = 0;
    settings.animation_ticks = 8;
    snprintf(settings.render_profile, sizeof(settings.render_profile), "%s",
             "locked");
  } else {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop settings preset: unknown '%s'\n"
               "presets: default compact cozy performance accessibility locked\n",
               preset);
    }
    return -2;
  }

  rc = desktop_write_settings(&settings);
  snprintf(event, sizeof(event), "settings preset applied name=%s", preset);
  desktop_log_event(event);
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop settings preset: %s\n"
             "preset: %s\n"
             "scale: %d\n"
             "gaps: in=%d out=%d\n"
             "border-size: %d\n"
             "rounding: %d\n"
             "animations: %s\n"
             "shadows: %s\n"
             "idle-timeout-seconds: %d\n"
             "lock-on-idle: %s\n"
             "path: %s\n"
             "apply: desktop hyprctl reload\n",
             rc == 0 ? "applied" : "write-failed", preset, settings.scale,
             settings.gaps_in, settings.gaps_out, settings.border_size,
             settings.rounding, settings.animations_enabled ? "yes" : "no",
             settings.shadows_enabled ? "yes" : "no",
             settings.idle_timeout_seconds, settings.lock_on_idle ? "yes" : "no",
             ORIZON_DESKTOP_SETTINGS_PATH);
  }
  return rc;
}

int orizon_desktop_export_settings(char *status, size_t status_size) {
  orizon_desktop_session_t session;
  orizon_desktop_settings_t settings;
  int rc;

  if (status && status_size) {
    status[0] = '\0';
  }
  desktop_ensure_dirs();
  orizon_desktop_ensure_defaults();
  orizon_desktop_load_session(&session);
  orizon_desktop_load_settings(&settings);
  rc = desktop_write_user_config_from_state(&session, &settings);
  desktop_log_event("settings exported user-config");
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop settings export: %s\n"
             "source-session: %s\n"
             "source-settings: %s\n"
             "target-user-config: %s\n"
             "theme: %s wallpaper: %s layout: %s\n"
             "settings: gaps=%d/%d border=%d rounding=%d animations=%s ticks=%d curve=%s shadows=%s shadow-range=%d focus-ring=%s render=%s input=%s\n"
             "next: desktop settings sync | desktop config apply\n",
             rc == 0 ? "written" : "write-failed",
             ORIZON_DESKTOP_SESSION_PATH, ORIZON_DESKTOP_SETTINGS_PATH,
             ORIZON_DESKTOP_USER_CONFIG_PATH, session.theme,
             session.wallpaper, session.layout, settings.gaps_in,
             settings.gaps_out, settings.border_size, settings.rounding,
             settings.animations_enabled ? "yes" : "no",
             settings.animation_ticks, settings.animation_curve,
             settings.shadows_enabled ? "yes" : "no",
             settings.shadow_range,
             settings.focus_ring_enabled ? "yes" : "no",
             settings.render_profile, settings.keyboard_layout);
  }
  return rc;
}

int orizon_desktop_sync_settings(char *status, size_t status_size) {
  char apply_report[1024];
  int export_rc;
  int apply_rc;

  if (status && status_size) {
    status[0] = '\0';
  }
  export_rc = orizon_desktop_export_settings(NULL, 0);
  apply_rc = orizon_desktop_apply_hypr_config(apply_report,
                                              sizeof(apply_report));
  desktop_log_event("settings synced central-to-runtime");
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop settings sync: %s\n"
             "export: %s\n"
             "apply: %s\n"
             "settings: %s\n"
             "session: %s\n"
             "user-config: %s\n"
             "runtime-files: %s %s %s %s %s %s\n"
             "manual-window-drag: no\n",
             (export_rc == 0 && apply_rc == 0) ? "synced" : "warn",
             export_rc == 0 ? "ok" : "failed",
             apply_rc == 0 ? "ok" : "warn", ORIZON_DESKTOP_SETTINGS_PATH,
             ORIZON_DESKTOP_SESSION_PATH, ORIZON_DESKTOP_USER_CONFIG_PATH,
             ORIZON_DESKTOP_BINDS_PATH, ORIZON_DESKTOP_AUTOSTART_PATH,
             ORIZON_DESKTOP_RULES_PATH, ORIZON_DESKTOP_MONITORS_PATH,
             ORIZON_DESKTOP_LAYERS_PATH, ORIZON_DESKTOP_RUNTIME_PATH);
  }
  return export_rc == 0 && apply_rc == 0 ? 0 : -1;
}

int orizon_desktop_apply_preset(const char *preset, char *status,
                                size_t status_size) {
  orizon_desktop_session_t session;
  int rc;
  char event[96];

  if (status && status_size) {
    status[0] = '\0';
  }
  if (!preset || !preset[0] || !desktop_token_safe(preset)) {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop preset: invalid value\n"
               "known: graphite moss ember frost focus default\n");
    }
    return -1;
  }

  orizon_desktop_load_session(&session);
  if (strcmp(preset, "default") == 0 || strcmp(preset, "graphite") == 0) {
    snprintf(session.theme, sizeof(session.theme), "%s", "graphite");
    snprintf(session.wallpaper, sizeof(session.wallpaper), "%s", "aurora");
    snprintf(session.layout, sizeof(session.layout), "%s", "dwindle");
    session.bar_enabled = 1;
    session.launcher_enabled = 1;
    session.autostart_terminal = 1;
    session.focus_follows_mouse = 0;
  } else if (strcmp(preset, "moss") == 0) {
    snprintf(session.theme, sizeof(session.theme), "%s", "moss");
    snprintf(session.wallpaper, sizeof(session.wallpaper), "%s", "moss");
    snprintf(session.layout, sizeof(session.layout), "%s", "dwindle");
    session.bar_enabled = 1;
    session.launcher_enabled = 1;
    session.autostart_terminal = 1;
    session.focus_follows_mouse = 1;
  } else if (strcmp(preset, "ember") == 0) {
    snprintf(session.theme, sizeof(session.theme), "%s", "ember");
    snprintf(session.wallpaper, sizeof(session.wallpaper), "%s", "dawn");
    snprintf(session.layout, sizeof(session.layout), "%s", "dwindle");
    session.bar_enabled = 1;
    session.launcher_enabled = 1;
    session.autostart_terminal = 1;
    session.focus_follows_mouse = 0;
  } else if (strcmp(preset, "frost") == 0) {
    snprintf(session.theme, sizeof(session.theme), "%s", "frost");
    snprintf(session.wallpaper, sizeof(session.wallpaper), "%s", "noir");
    snprintf(session.layout, sizeof(session.layout), "%s", "dwindle");
    session.bar_enabled = 1;
    session.launcher_enabled = 1;
    session.autostart_terminal = 1;
    session.focus_follows_mouse = 0;
  } else if (strcmp(preset, "focus") == 0) {
    snprintf(session.theme, sizeof(session.theme), "%s", "graphite");
    snprintf(session.wallpaper, sizeof(session.wallpaper), "%s", "noir");
    snprintf(session.layout, sizeof(session.layout), "%s", "monocle");
    session.bar_enabled = 0;
    session.launcher_enabled = 1;
    session.autostart_terminal = 1;
    session.focus_follows_mouse = 0;
  } else {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop preset: unknown '%s'\n"
               "known: graphite moss ember frost focus default\n",
               preset);
    }
    return -2;
  }

  rc = desktop_write_session(&session);
  snprintf(event, sizeof(event), "preset applied name=%s", preset);
  desktop_log_event(event);
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop preset: %s\n"
             "preset: %s\n"
             "theme: %s\n"
             "wallpaper: %s\n"
             "layout: %s\n"
             "bar: %s\n"
             "launcher: %s\n"
             "autostart-terminal: %s\n"
             "focus-follows-mouse: %s\n"
             "path: %s\n",
             rc == 0 ? "applied" : "write-failed", preset, session.theme,
             session.wallpaper, session.layout,
             session.bar_enabled ? "yes" : "no",
             session.launcher_enabled ? "yes" : "no",
             session.autostart_terminal ? "yes" : "no",
             session.focus_follows_mouse ? "yes" : "no",
             ORIZON_DESKTOP_SESSION_PATH);
  }
  return rc;
}

int orizon_desktop_apply_hypr_config(char *status, size_t status_size) {
  char cfg[4096];
  char line[256];
  orizon_desktop_session_t session;
  orizon_desktop_settings_t settings;
  desktop_hypr_runtime_t runtime;
  desktop_hypr_summary_t summary;
  int n;
  int rc1;
  int rc2;
  int rc_binds;
  int rc_autostart;
  int rc_rules;
  int rc_monitors;
  int rc_layers;
  int rc_runtime;

  if (status && status_size) {
    status[0] = '\0';
  }
  memset(&summary, 0, sizeof(summary));
  desktop_hypr_runtime_init(&runtime);
  desktop_ensure_dirs();
  orizon_desktop_ensure_defaults();
  if (!vfs_exists(ORIZON_DESKTOP_USER_CONFIG_PATH)) {
    if (orizon_desktop_write_user_config(NULL, 0) == 0) {
      summary.generated_user_config = 1;
    }
  }
  n = desktop_read_text_file(ORIZON_DESKTOP_USER_CONFIG_PATH, cfg, sizeof(cfg));
  if (n <= 0) {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop config apply: failed\n"
               "source: %s missing or unreadable\n"
               "fix: desktop write-config\n",
               ORIZON_DESKTOP_USER_CONFIG_PATH);
    }
    return -1;
  }
  orizon_desktop_load_session(&session);
  orizon_desktop_load_settings(&settings);
  desktop_hypr_scan_config(cfg, 1, &summary, &session, &settings, &runtime);
  rc1 = desktop_write_session(&session);
  rc2 = desktop_write_settings(&settings);
  rc_binds = desktop_write_text_file(ORIZON_DESKTOP_BINDS_PATH,
                                     runtime.binds);
  rc_autostart = desktop_write_text_file(ORIZON_DESKTOP_AUTOSTART_PATH,
                                         runtime.autostart);
  rc_rules = desktop_write_text_file(ORIZON_DESKTOP_RULES_PATH,
                                     runtime.rules);
  rc_monitors = desktop_write_text_file(ORIZON_DESKTOP_MONITORS_PATH,
                                        runtime.monitors);
  rc_layers = desktop_write_text_file(ORIZON_DESKTOP_LAYERS_PATH,
                                      runtime.layers);
  rc_runtime = desktop_write_text_file(ORIZON_DESKTOP_RUNTIME_PATH,
                                       runtime.runtime);
  desktop_log_event("hypr config applied");
  vfs_persist_save();

  if (status && status_size) {
    snprintf(status, status_size,
             "desktop config apply: %s\n"
             "source: %s%s\n"
             "parsed-lines: %d malformed: %d\n"
             "supported-settings: %d applied: %d prepared-keywords: %d ignored: %d runtime-lines: %d\n"
             "binds: total=%d supported-dispatchers=%d mouse=%d locked=%d monitors=%d exec-once=%d env=%d windowrules=%d layerrules=%d workspaces=%d sources=%d variables=%d\n"
             "hints: input=%d layout=%d animations=%d misc=%d submaps=%d\n"
             "runtime-files: binds=%s autostart=%s rules=%s monitors=%s layers=%s state=%s\n"
             "session: layout=%s autostart-terminal=%s focus-follows-mouse=%s\n"
             "settings: gaps=%d/%d border=%d rounding=%d animations=%s ticks=%d curve=%s shadows=%s shadow-range=%d focus-ring=%s render=%s keyboard=%s\n"
             "note: monitor/env/windowrule/source are now persisted as Orizon runtime hints; real Wayland outputs remain future work.\n",
             (rc1 == 0 && rc2 == 0 && rc_binds == 0 && rc_autostart == 0 &&
              rc_rules == 0 && rc_monitors == 0 && rc_layers == 0 &&
              rc_runtime == 0)
                 ? "applied"
                 : "write-failed",
             ORIZON_DESKTOP_USER_CONFIG_PATH,
             summary.generated_user_config ? " generated-from-template" : "",
             summary.parsed_lines, summary.malformed_lines,
             summary.supported_settings, summary.applied_settings,
             summary.prepared_keywords, summary.ignored_keywords,
             summary.runtime_lines,
             summary.binds, summary.supported_binds, summary.mouse_binds,
             summary.locked_binds, summary.monitors, summary.exec_once,
             summary.envs, summary.windowrules, summary.layerrules,
             summary.workspaces, summary.sources, summary.variables,
             summary.input_hints, summary.layout_hints,
             summary.animation_rules, summary.misc_hints, summary.submaps,
             rc_binds == 0 ? ORIZON_DESKTOP_BINDS_PATH : "write-failed",
             rc_autostart == 0 ? ORIZON_DESKTOP_AUTOSTART_PATH
                               : "write-failed",
             rc_rules == 0 ? ORIZON_DESKTOP_RULES_PATH : "write-failed",
             rc_monitors == 0 ? ORIZON_DESKTOP_MONITORS_PATH
                              : "write-failed",
             rc_layers == 0 ? ORIZON_DESKTOP_LAYERS_PATH : "write-failed",
             rc_runtime == 0 ? ORIZON_DESKTOP_RUNTIME_PATH : "write-failed",
             session.layout,
             session.autostart_terminal ? "yes" : "no",
             session.focus_follows_mouse ? "yes" : "no",
             settings.gaps_in, settings.gaps_out, settings.border_size,
             settings.rounding,
             settings.animations_enabled ? "yes" : "no",
             settings.animation_ticks, settings.animation_curve,
             settings.shadows_enabled ? "yes" : "no",
             settings.shadow_range,
             settings.focus_ring_enabled ? "yes" : "no",
             settings.render_profile, settings.keyboard_layout);
    if (summary.applied_settings == 0 &&
        strlen(status) + 80 < status_size) {
      snprintf(line, sizeof(line),
               "hint: add general:gaps_in, general:layout, decoration:rounding, decoration:shadow:range, render:focus_ring, animations:enabled, input:kb_layout.\n");
      strcat(status, line);
    }
  }
  return rc1 == 0 && rc2 == 0 && rc_binds == 0 && rc_autostart == 0 &&
                 rc_rules == 0 && rc_monitors == 0 && rc_layers == 0 &&
                 rc_runtime == 0
             ? 0
             : -1;
}

int orizon_desktop_apply_hypr_keyword(const char *key, const char *value,
                                      char *status, size_t status_size) {
  orizon_desktop_session_t session;
  orizon_desktop_settings_t settings;
  desktop_hypr_summary_t summary;
  int rc1;
  int rc2;
  int runtime_rc = 0;
  const char *runtime_path;

  if (status && status_size) {
    status[0] = '\0';
  }
  if (!key || !value || !key[0] || !value[0] ||
      !desktop_hypr_key_safe(key) || !desktop_hypr_value_safe(value)) {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop keyword: invalid key/value\n"
               "usage: desktop keyword <hypr-key> <value>\n");
    }
    return -1;
  }

  memset(&summary, 0, sizeof(summary));
  desktop_ensure_dirs();
  orizon_desktop_ensure_defaults();
  orizon_desktop_load_session(&session);
  orizon_desktop_load_settings(&settings);
  desktop_hypr_apply_pair(key, value, &session, &settings, &summary, 1, NULL);
  rc1 = desktop_write_session(&session);
  rc2 = desktop_write_settings(&settings);
  runtime_path = desktop_hypr_runtime_path_for_key(key);
  if (runtime_path) {
    runtime_rc = desktop_append_hypr_runtime_keyword(key, value);
  }
  desktop_log_event("hypr keyword applied");
  vfs_persist_save();

  if (status && status_size) {
    snprintf(status, status_size,
             "desktop keyword: %s\n"
             "key: %s\n"
             "value: %s\n"
             "supported-settings: %d applied: %d runtime-hint: %s\n"
             "session: layout=%s autostart-terminal=%s focus-follows-mouse=%s\n"
             "settings: gaps=%d/%d border=%d rounding=%d animations=%s ticks=%d curve=%s shadows=%s shadow-range=%d focus-ring=%s render=%s keyboard=%s\n"
             "apply: desktop hyprctl reload\n",
             (rc1 == 0 && rc2 == 0 && runtime_rc == 0 &&
              (summary.supported_settings > 0 || runtime_path))
                 ? "applied"
                 : "warn",
             key, value, summary.supported_settings, summary.applied_settings,
             runtime_path ? runtime_path : "none",
             session.layout,
             session.autostart_terminal ? "yes" : "no",
             session.focus_follows_mouse ? "yes" : "no",
             settings.gaps_in, settings.gaps_out, settings.border_size,
             settings.rounding,
             settings.animations_enabled ? "yes" : "no",
             settings.animation_ticks, settings.animation_curve,
             settings.shadows_enabled ? "yes" : "no",
             settings.shadow_range,
             settings.focus_ring_enabled ? "yes" : "no",
             settings.render_profile, settings.keyboard_layout);
    if (summary.supported_settings == 0 && !runtime_path &&
        strlen(status) + 96 < status_size) {
      strcat(status,
             "note: key is not implemented yet; supported settings and runtime hint keys are documented by desktop config doctor.\n");
    }
  }
  return (rc1 == 0 && rc2 == 0 && runtime_rc == 0 &&
          (summary.supported_settings > 0 || runtime_path))
             ? 0
             : 1;
}

int orizon_desktop_set_enabled(int enabled, char *status, size_t status_size) {
  int rc;
  int user_rc = 0;

  desktop_ensure_dirs();
  orizon_desktop_ensure_defaults();
  rc = desktop_write_text_file(ORIZON_DESKTOP_CONFIG_PATH,
                               enabled ? desktop_enabled_config
                                       : desktop_default_config);
  if (enabled) {
    user_rc = orizon_desktop_write_user_config(NULL, 0);
  }
  desktop_log_event(enabled ? "enabled profile=" ORIZON_DESKTOP_PROFILE
                            : "disabled");
  desktop_write_session_state(enabled ? "started" : "stopped",
                              enabled ? "active" : "inactive",
                              enabled ? "enable" : "disable",
                              "compat-enable-disable");
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop: %s\nprofile: %s\nconfig: %s\nuser-config: %s\n"
             "settings: %s\nsession-state: %s\n"
             "terminal-shortcuts: F1=open F2=close\n",
             enabled ? "enabled" : "disabled", ORIZON_DESKTOP_PROFILE,
             rc == 0 ? ORIZON_DESKTOP_CONFIG_PATH : "write-failed",
             enabled ? (user_rc == 0 ? ORIZON_DESKTOP_USER_CONFIG_PATH
                                      : "write-failed")
                     : "kept-if-present",
             ORIZON_DESKTOP_SETTINGS_PATH, ORIZON_DESKTOP_STATE_PATH);
  }
  return rc == 0 && user_rc == 0 ? 0 : -1;
}

int orizon_desktop_session_manager(const char *action, char *status,
                                   size_t status_size) {
  char apply_report[768];
  const char *desired = "stopped";
  const char *runtime = "inactive";
  const char *note = "none";
  int rc = 0;
  int apply_rc = 0;
  int enabled;

  if (status && status_size) {
    status[0] = '\0';
  }
  if (!action || !action[0]) {
    action = "status";
  }
  desktop_ensure_dirs();
  orizon_desktop_ensure_defaults();

  if (strcmp(action, "rescue") == 0) {
    if (status && status_size) {
      orizon_desktop_format_session_rescue(status, status_size);
    }
    return 0;
  }

  if (strcmp(action, "start") == 0) {
    rc = orizon_desktop_set_enabled(1, NULL, 0);
    apply_rc = orizon_desktop_apply_hypr_config(apply_report,
                                                sizeof(apply_report));
    desired = "started";
    runtime = "active";
    note = "session-started-autostart-ready";
  } else if (strcmp(action, "stop") == 0) {
    rc = orizon_desktop_set_enabled(0, NULL, 0);
    apply_rc = -2;
    desired = "stopped";
    runtime = "inactive";
    note = "session-stopped-policy-disabled";
  } else if (strcmp(action, "restart") == 0) {
    rc = orizon_desktop_set_enabled(1, NULL, 0);
    apply_rc = orizon_desktop_apply_hypr_config(apply_report,
                                                sizeof(apply_report));
    desired = "started";
    runtime = "active";
    note = "session-restarted";
  } else if (strcmp(action, "reload") == 0) {
    enabled = orizon_desktop_is_enabled();
    apply_rc = orizon_desktop_apply_hypr_config(apply_report,
                                                sizeof(apply_report));
    desired = enabled ? "started" : "stopped";
    runtime = enabled ? "active" : "inactive";
    note = enabled ? "config-reloaded-active" : "config-reloaded-standby";
  } else if (strcmp(action, "recover") == 0) {
    enabled = orizon_desktop_is_enabled();
    orizon_desktop_ensure_defaults();
    if (enabled) {
      orizon_desktop_write_user_config(NULL, 0);
    }
    apply_rc = orizon_desktop_apply_hypr_config(apply_report,
                                                sizeof(apply_report));
    desired = enabled ? "started" : "stopped";
    runtime = enabled ? "active" : "inactive";
    note = enabled ? "recover-reloaded-active"
                   : "recover-repaired-standby";
  } else {
    if (status && status_size) {
      snprintf(status, status_size,
               "usage: desktop start|stop|restart|reload|recover|rescue\n");
    }
    return -EINVAL;
  }

  if (strcmp(action, "stop") != 0 && apply_rc != 0) {
    runtime = orizon_desktop_is_enabled() ? "degraded" : "standby-degraded";
    note = "config-apply-warn-run-desktop-rescue";
  }

  if (desktop_write_session_state(desired, runtime, action, note) < 0) {
    rc = -1;
  }
  desktop_log_event(note);
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop session-manager: %s\n"
             "health: %s\n"
             "desired-state: %s\nruntime-state: %s\n"
             "boot-mode: %s\n"
             "installed-marker: %s\n"
             "policy: %s\n"
             "config-apply: %s\n"
             "state: %s\nsession-log: %s\n"
             "autostart: terminal=%s launcher=recorded\n"
             "manual-window-drag: no\n"
             "recover: desktop recover\n"
             "rescue: desktop rescue\n",
             action, strcmp(runtime, "active") == 0 ||
                         (strcmp(desired, "stopped") == 0 &&
                          strcmp(runtime, "inactive") == 0)
                         ? "PASS"
                         : "WARN",
             desired, runtime,
             orizon_system_is_installed() ? "installed" : "live-iso",
             orizon_system_is_installed() ? "present" : "missing",
             orizon_desktop_is_enabled() ? "enabled" : "disabled",
             apply_rc == 0 ? "ok" : (strcmp(action, "stop") == 0 ? "skipped"
                                                                   : "warn"),
             ORIZON_DESKTOP_STATE_PATH, ORIZON_DESKTOP_SESSION_LOG_PATH,
             strstr(note, "standby") ? "prepared" : "ready");
  }
  return (rc == 0 && (apply_rc == 0 || strcmp(action, "stop") == 0)) ? 0 : -1;
}

int orizon_desktop_reset(char *status, size_t status_size) {
  int rc;
  int template_rc;

  desktop_ensure_dirs();
  rc = desktop_write_text_file(ORIZON_DESKTOP_CONFIG_PATH,
                               desktop_default_config);
  template_rc = desktop_write_text_file(ORIZON_DESKTOP_TEMPLATE_PATH,
                                        desktop_user_config);
  desktop_write_text_file(ORIZON_DESKTOP_SESSION_PATH, desktop_session_config);
  desktop_write_text_file(ORIZON_DESKTOP_SETTINGS_PATH,
                          desktop_settings_config);
  desktop_write_text_file(ORIZON_DESKTOP_BINDS_PATH,
                          desktop_binds_runtime_config);
  desktop_write_text_file(ORIZON_DESKTOP_AUTOSTART_PATH,
                          desktop_autostart_runtime_config);
  desktop_write_text_file(ORIZON_DESKTOP_RULES_PATH,
                          desktop_rules_runtime_config);
  desktop_write_text_file(ORIZON_DESKTOP_MONITORS_PATH,
                          desktop_monitors_runtime_config);
  desktop_write_text_file(ORIZON_DESKTOP_LAYERS_PATH,
                          desktop_layers_runtime_config);
  desktop_write_text_file(ORIZON_DESKTOP_RUNTIME_PATH, desktop_runtime_config);
  desktop_write_text_file(ORIZON_DESKTOP_MODULES_PATH, desktop_modules_config);
  desktop_write_text_file(ORIZON_DESKTOP_BACKEND_PATH, desktop_backend_config);
  desktop_write_text_file(ORIZON_DESKTOP_PROTOCOL_PATH,
                          desktop_protocol_config);
  desktop_write_session_state("stopped", "inactive", "reset",
                              "profile-defaults-restored");
  desktop_log_event("reset profile=" ORIZON_DESKTOP_PROFILE);
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop: reset\n"
             "enabled: no\n"
             "profile: %s\n"
             "config: %s\n"
             "template: %s\n"
             "settings: %s\n"
             "modules: %s\n"
             "user-config: kept-if-present %s\n",
             ORIZON_DESKTOP_PROFILE,
             rc == 0 ? ORIZON_DESKTOP_CONFIG_PATH : "write-failed",
             template_rc == 0 ? ORIZON_DESKTOP_TEMPLATE_PATH
                              : "write-failed",
             ORIZON_DESKTOP_SETTINGS_PATH,
             ORIZON_DESKTOP_MODULES_PATH,
             ORIZON_DESKTOP_USER_CONFIG_PATH);
  }
  return rc == 0 && template_rc == 0 ? 0 : -1;
}

void orizon_desktop_format_status(char *out, size_t out_size) {
  char line[256];
  char state_cfg[768];
  char desired[32];
  char runtime[32];
  char action[32];
  size_t used = 0;
  int enabled;
  int state_ok;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  enabled = orizon_desktop_is_enabled();
  state_ok = desktop_read_text_file(ORIZON_DESKTOP_STATE_PATH, state_cfg,
                                    sizeof(state_cfg)) > 0;
  desktop_session_get_value(state_ok ? state_cfg : NULL, "desired-state",
                            desired, sizeof(desired),
                            enabled ? "started" : "stopped");
  desktop_session_get_value(state_ok ? state_cfg : NULL, "runtime-state",
                            runtime, sizeof(runtime),
                            enabled ? "active" : "inactive");
  desktop_session_get_value(state_ok ? state_cfg : NULL, "last-action",
                            action, sizeof(action), "unknown");
  desktop_append(out, out_size, &used, "Orizon desktop\n");
  snprintf(line, sizeof(line), "enabled: %s\n", enabled ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "boot-mode: %s\n",
           orizon_system_is_installed() ? "installed" : "live-iso");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "session-state: desired=%s runtime=%s last-action=%s path=%s\n",
           desired, runtime, action, ORIZON_DESKTOP_STATE_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "session-log: %s\n",
           ORIZON_DESKTOP_SESSION_LOG_PATH);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "profile: " ORIZON_DESKTOP_PROFILE "\n");
  desktop_append(out, out_size, &used,
                 "session: orizon-compositor (Hyprland-style)\n");
  desktop_append(out, out_size, &used,
                 "upstream-hyprland: not embedded yet\n");
  desktop_append(out, out_size, &used,
                 "install-policy: optional during install or via package\n");
  snprintf(line, sizeof(line), "package: %s\n", ORIZON_DESKTOP_PACKAGE);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "package-installed: %s path=%s\n",
           vfs_exists(DESKTOP_PKG_META_PATH) ? "yes" : "no",
           DESKTOP_PKG_META_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "config: %s\n",
           ORIZON_DESKTOP_CONFIG_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "user-config: %s %s\n",
           ORIZON_DESKTOP_USER_CONFIG_PATH,
           vfs_exists(ORIZON_DESKTOP_USER_CONFIG_PATH) ? "present"
                                                       : "not-written-yet");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "template: %s\n",
           ORIZON_DESKTOP_TEMPLATE_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "session-config: %s\n",
           ORIZON_DESKTOP_SESSION_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "settings-config: %s\n",
           ORIZON_DESKTOP_SETTINGS_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "modules-map: %s\n",
           ORIZON_DESKTOP_MODULES_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "binds-runtime: %s\n",
           ORIZON_DESKTOP_BINDS_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "autostart-runtime: %s\n",
           ORIZON_DESKTOP_AUTOSTART_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "rules-runtime: %s\n",
           ORIZON_DESKTOP_RULES_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "monitor-runtime: %s\n",
           ORIZON_DESKTOP_MONITORS_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "layers-runtime: %s\n",
           ORIZON_DESKTOP_LAYERS_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "state-runtime: %s\n",
           ORIZON_DESKTOP_STATE_PATH);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "apps: desktop launch terminal|settings|logs|packages|update|launcher; desktop app <id>; F2/killactive\n");
  desktop_append(out, out_size, &used,
                 "admin: desktop start | desktop stop | desktop restart | desktop reload | desktop recover | desktop rescue | desktop settings | desktop doctor | desktop logs\n");
}

void orizon_desktop_format_config(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used, "Orizon desktop config\n");
  desktop_append(out, out_size, &used, "path: ");
  desktop_append(out, out_size, &used, ORIZON_DESKTOP_CONFIG_PATH);
  desktop_append(out, out_size, &used, "\n\n== disabled-default ==\n");
  desktop_append(out, out_size, &used, desktop_default_config);
  desktop_append(out, out_size, &used, "\n== enabled-profile ==\n");
  desktop_append(out, out_size, &used, desktop_enabled_config);
  desktop_append(out, out_size, &used, "\n== hypr-style-user-config ==\n");
  desktop_append(out, out_size, &used, desktop_user_config);
  desktop_append(out, out_size, &used, "\n== session-default ==\n");
  desktop_append(out, out_size, &used, desktop_session_config);
  desktop_append(out, out_size, &used, "\n== system-settings-default ==\n");
  desktop_append(out, out_size, &used, desktop_settings_config);
  desktop_append(out, out_size, &used, "\n== generated-runtime-files ==\n");
  desktop_append(out, out_size, &used,
                 ORIZON_DESKTOP_BINDS_PATH "\n"
                 ORIZON_DESKTOP_AUTOSTART_PATH "\n"
                 ORIZON_DESKTOP_RULES_PATH "\n"
                 ORIZON_DESKTOP_MONITORS_PATH "\n"
                 ORIZON_DESKTOP_LAYERS_PATH "\n"
                 ORIZON_DESKTOP_RUNTIME_PATH "\n"
                 ORIZON_DESKTOP_STATE_PATH "\n");
  desktop_append(out, out_size, &used, "\n== session-state-default ==\n");
  desktop_append(out, out_size, &used, desktop_state_config);
  desktop_append(out, out_size, &used,
                 "\ncommands: desktop config doctor | desktop config apply | desktop write-config\n");
}

void orizon_desktop_format_config_doctor(char *out, size_t out_size) {
  char cfg[4096];
  char line[256];
  size_t used = 0;
  size_t size = 0;
  int n;
  desktop_hypr_summary_t summary;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  memset(&summary, 0, sizeof(summary));
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used,
                 "Orizon desktop Hyprland config doctor\n");
  snprintf(line, sizeof(line), "path: %s\n",
           ORIZON_DESKTOP_USER_CONFIG_PATH);
  desktop_append(out, out_size, &used, line);
  if (vfs_stat(ORIZON_DESKTOP_USER_CONFIG_PATH, &size, NULL) == 0 &&
      size > 0) {
    snprintf(line, sizeof(line), "file PASS bytes=%lu\n",
             (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "file WARN missing; run desktop write-config or desktop config apply\n");
  }
  n = desktop_read_text_file(ORIZON_DESKTOP_USER_CONFIG_PATH, cfg,
                             sizeof(cfg));
  if (n <= 0) {
    snprintf(cfg, sizeof(cfg), "%s", desktop_user_config);
    n = (int)strlen(cfg);
    desktop_append(out, out_size, &used,
                   "source: built-in template preview\n");
  } else {
    desktop_append(out, out_size, &used,
                   "source: user config\n");
  }
  desktop_hypr_scan_config(cfg, 0, &summary, NULL, NULL, NULL);
  snprintf(line, sizeof(line),
           "syntax: %s parsed-lines=%d malformed=%d\n",
           summary.malformed_lines ? "WARN" : "PASS", summary.parsed_lines,
           summary.malformed_lines);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "keywords: variables=%d monitors=%d binds=%d supported-binds=%d mouse-binds=%d locked-binds=%d exec-once=%d env=%d windowrules=%d layerrules=%d workspaces=%d sources=%d submaps=%d\n",
           summary.variables, summary.monitors, summary.binds,
           summary.supported_binds, summary.mouse_binds,
           summary.locked_binds, summary.exec_once, summary.envs,
           summary.windowrules, summary.layerrules, summary.workspaces,
           summary.sources, summary.submaps);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "runtime-hints: input=%d device=%d layout=%d decoration=%d animations=%d cursor=%d render=%d debug=%d misc=%d\n",
           summary.input_hints, summary.device_hints, summary.layout_hints,
           summary.decoration_hints, summary.animation_rules,
           summary.cursor_hints, summary.render_hints, summary.debug_hints,
           summary.misc_hints);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line),
           "apply-support: settings=%d prepared=%d ignored=%d\n",
           summary.supported_settings, summary.prepared_keywords,
           summary.ignored_keywords);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "supported-apply: general:layout,gaps_in,gaps_out,border_size; decoration:rounding,shadow:enabled,shadow:range; render:focus_ring,profile; animations:enabled,tick_budget,curve; input:kb_layout,follow_mouse; exec-once terminal\n");
  desktop_append(out, out_size, &used,
                 "runtime-hints: monitor, bind/bindm/bindl/bindr, exec/exec-once, env, windowrule/windowrulev2, layerrule, workspace, source, submap, animation/bezier, input/device/decoration/cursor/render/debug/misc/dwindle/master/group hints -> /system/desktop-*.conf\n");
  desktop_append(out, out_size, &used,
                 "apply: desktop config apply  # writes supported values into session/settings\n");
  snprintf(line, sizeof(line), "summary: %s\n",
           summary.malformed_lines ? "WARN" : "PASS");
  desktop_append(out, out_size, &used, line);
}

void orizon_desktop_format_config_errors(char *out, size_t out_size) {
  char cfg[4096];
  char line[256];
  size_t used = 0;
  int n;
  desktop_hypr_summary_t summary;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  memset(&summary, 0, sizeof(summary));
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used,
                 "Hyprland config errors\n");
  snprintf(line, sizeof(line), "path: %s\n",
           ORIZON_DESKTOP_USER_CONFIG_PATH);
  desktop_append(out, out_size, &used, line);
  n = desktop_read_text_file(ORIZON_DESKTOP_USER_CONFIG_PATH, cfg,
                             sizeof(cfg));
  if (n <= 0) {
    snprintf(cfg, sizeof(cfg), "%s", desktop_user_config);
    desktop_append(out, out_size, &used,
                   "source: built-in template preview\n");
  } else {
    desktop_append(out, out_size, &used, "source: user config\n");
  }
  desktop_hypr_scan_config(cfg, 0, &summary, NULL, NULL, NULL);
  snprintf(line, sizeof(line),
           "summary: %s parsed=%d malformed=%d ignored=%d prepared=%d\n",
           summary.malformed_lines ? "WARN" : "PASS", summary.parsed_lines,
           summary.malformed_lines, summary.ignored_keywords,
           summary.prepared_keywords);
  desktop_append(out, out_size, &used, line);
  if (summary.malformed_lines == 0) {
    desktop_append(out, out_size, &used,
                   "errors: none in the supported Orizon parser subset\n");
  } else {
    desktop_append(out, out_size, &used,
                   "errors: malformed lines detected; run desktop config doctor for details\n");
  }
  snprintf(line, sizeof(line),
           "prepared-detail: layerrules=%d animations=%d input-hints=%d device-hints=%d layout-hints=%d decoration-hints=%d cursor-hints=%d render-hints=%d debug-hints=%d misc-hints=%d\n",
           summary.layerrules, summary.animation_rules, summary.input_hints,
           summary.device_hints, summary.layout_hints,
           summary.decoration_hints, summary.cursor_hints,
           summary.render_hints, summary.debug_hints, summary.misc_hints);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "notes: unsupported but safe Hyprland keywords are reported as ignored/prepared, not hard errors\n");
}

static void desktop_append_file_dump(char *out, size_t out_size, size_t *used,
                                     const char *title, const char *path) {
  char line[160];
  char cfg[1536];
  int n;

  if (!out || !used || !title || !path) {
    return;
  }
  snprintf(line, sizeof(line), "\n== %s ==\npath: %s\n", title, path);
  desktop_append(out, out_size, used, line);
  n = desktop_read_text_file(path, cfg, sizeof(cfg));
  if (n <= 0) {
    desktop_append(out, out_size, used, "(missing)\n");
    return;
  }
  desktop_append(out, out_size, used, cfg);
  if (cfg[0] && cfg[strlen(cfg) - 1] != '\n') {
    desktop_append(out, out_size, used, "\n");
  }
}

void orizon_desktop_format_runtime(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used,
                 "Orizon desktop Hyprland runtime\n");
  desktop_append(out, out_size, &used,
                 "source: generated by desktop config apply and desktop keyword\n");
  desktop_append_file_dump(out, out_size, &used, "binds",
                           ORIZON_DESKTOP_BINDS_PATH);
  desktop_append_file_dump(out, out_size, &used, "autostart",
                           ORIZON_DESKTOP_AUTOSTART_PATH);
  desktop_append_file_dump(out, out_size, &used, "window-rules",
                           ORIZON_DESKTOP_RULES_PATH);
  desktop_append_file_dump(out, out_size, &used, "monitor-hints",
                           ORIZON_DESKTOP_MONITORS_PATH);
  desktop_append_file_dump(out, out_size, &used, "layer-rules",
                           ORIZON_DESKTOP_LAYERS_PATH);
  desktop_append_file_dump(out, out_size, &used, "runtime-state",
                           ORIZON_DESKTOP_RUNTIME_PATH);
  desktop_append(out, out_size, &used,
                 "commands: desktop keyword <key> <value> | desktop config apply | desktop hyprctl getoption <key> | desktop dispatch submap <name>\n");
}

void orizon_desktop_format_rules(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used,
                 "Orizon desktop window rules\n");
  desktop_append(out, out_size, &used,
                 "scope: Hyprland-style rule intent; true Wayland matching is future work\n");
  desktop_append_file_dump(out, out_size, &used, "rules",
                           ORIZON_DESKTOP_RULES_PATH);
  desktop_append(out, out_size, &used,
                 "add: desktop keyword windowrulev2 <rule>\n");
}

void orizon_desktop_format_monitor_hints(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used,
                 "Orizon desktop monitor hints\n");
  desktop_append(out, out_size, &used,
                 "scope: Hyprland-style monitor config intent plus VM framebuffer monitor facade\n");
  desktop_append_file_dump(out, out_size, &used, "monitor-hints",
                           ORIZON_DESKTOP_MONITORS_PATH);
  desktop_append(out, out_size, &used,
                 "add: desktop keyword monitor <name,resolution,position,scale>\n");
}

void orizon_desktop_format_hypr_option(const char *key, char *out,
                                       size_t out_size) {
  orizon_desktop_session_t session;
  orizon_desktop_settings_t settings;
  const char *known = "yes";
  const char *kind = "int";
  const char *runtime_path;
  char value[64];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (!key || !key[0] || !desktop_hypr_key_safe(key)) {
    snprintf(out, out_size,
             "hyprctl getoption: invalid key\n"
             "usage: desktop hyprctl getoption general:gaps_in\n");
    return;
  }
  orizon_desktop_load_session(&session);
  orizon_desktop_load_settings(&settings);
  value[0] = '\0';
  if (strcmp(key, "general:layout") == 0) {
    kind = "str";
    snprintf(value, sizeof(value), "%s", session.layout);
  } else if (strcmp(key, "general:gaps_in") == 0) {
    snprintf(value, sizeof(value), "%d", settings.gaps_in);
  } else if (strcmp(key, "general:gaps_out") == 0) {
    snprintf(value, sizeof(value), "%d", settings.gaps_out);
  } else if (strcmp(key, "general:border_size") == 0) {
    snprintf(value, sizeof(value), "%d", settings.border_size);
  } else if (strcmp(key, "decoration:rounding") == 0) {
    snprintf(value, sizeof(value), "%d", settings.rounding);
  } else if (strcmp(key, "decoration:shadow:enabled") == 0 ||
             strcmp(key, "decoration:drop_shadow") == 0) {
    kind = "bool";
    snprintf(value, sizeof(value), "%s",
             settings.shadows_enabled ? "true" : "false");
  } else if (strcmp(key, "decoration:shadow:range") == 0) {
    snprintf(value, sizeof(value), "%d", settings.shadow_range);
  } else if (strcmp(key, "animations:enabled") == 0) {
    kind = "bool";
    snprintf(value, sizeof(value), "%s",
             settings.animations_enabled ? "true" : "false");
  } else if (strcmp(key, "animations:tick_budget") == 0) {
    snprintf(value, sizeof(value), "%d", settings.animation_ticks);
  } else if (strcmp(key, "animations:curve") == 0) {
    kind = "str";
    snprintf(value, sizeof(value), "%s", settings.animation_curve);
  } else if (strcmp(key, "render:focus_ring") == 0) {
    kind = "bool";
    snprintf(value, sizeof(value), "%s",
             settings.focus_ring_enabled ? "true" : "false");
  } else if (strcmp(key, "render:profile") == 0) {
    kind = "str";
    snprintf(value, sizeof(value), "%s", settings.render_profile);
  } else if (strcmp(key, "input:kb_layout") == 0) {
    kind = "str";
    snprintf(value, sizeof(value), "%s", settings.keyboard_layout);
  } else if (strcmp(key, "input:follow_mouse") == 0) {
    kind = "bool";
    snprintf(value, sizeof(value), "%s",
             session.focus_follows_mouse ? "true" : "false");
  } else if ((runtime_path = desktop_hypr_runtime_path_for_key(key)) != NULL) {
    kind = "runtime";
    if (desktop_hypr_runtime_get_value(runtime_path, key, value,
                                       sizeof(value)) < 0) {
      snprintf(value, sizeof(value), "%s", "prepared-no-value-yet");
    }
  } else {
    known = "no";
    kind = "unknown";
    snprintf(value, sizeof(value), "%s", "not-implemented");
  }

  snprintf(out, out_size,
           "option %s\n"
           "known: %s\n"
           "type: %s\n"
           "value: %s\n"
           "source: Orizon desktop session/settings/runtime\n"
           "runtime-file: %s\n"
           "set: desktop keyword %s <value>\n",
           key, known, kind, value,
           strcmp(kind, "runtime") == 0
               ? desktop_hypr_runtime_path_for_key(key)
               : "none",
           key);
}

void orizon_desktop_format_session(char *out, size_t out_size) {
  orizon_desktop_session_t session;
  char line[192];
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_load_session(&session);
  desktop_append(out, out_size, &used, "Orizon desktop session\n");
  snprintf(line, sizeof(line), "path: %s\n", ORIZON_DESKTOP_SESSION_PATH);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "theme: %s\n", session.theme);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "wallpaper: %s\n", session.wallpaper);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "layout: %s\n", session.layout);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "bar: %s\n",
           session.bar_enabled ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "launcher: %s\n",
           session.launcher_enabled ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "autostart-terminal: %s\n",
           session.autostart_terminal ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "focus-follows-mouse: %s\n",
           session.focus_follows_mouse ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "set: desktop theme <name> | desktop wallpaper <name> | desktop layout <name> | desktop bar on|off\n");
  desktop_append(out, out_size, &used,
                 "system-settings: desktop settings | desktop settings set <key> <value>\n");
  desktop_append(out, out_size, &used,
                 "preset: desktop preset <graphite|moss|ember|frost|focus>\n");
  desktop_append(out, out_size, &used,
                 "focus: desktop focus on|off|toggle\n");
  desktop_append(out, out_size, &used,
                 "dispatch: desktop dispatch exec|killactive|workspace|movetoworkspace|movetoworkspacesilent|movefocus|fullscreen|pseudo|pin\n");
  desktop_append(out, out_size, &used,
                 "runtime: desktop binds|rules|monitors|runtime|layers|keyword\n");
  desktop_append(out, out_size, &used,
                 "manager: desktop start|stop|restart|reload|recover|rescue | desktop state\n");
  desktop_append(out, out_size, &used,
                 "hyprctl: desktop hyprctl version|systeminfo|clients|workspaces|activeworkspace|activewindow|monitors|binds|layers|layouts|animations|devices|cursorpos|splash|configerrors|rollinglog|getoption|keyword|dispatch\n");
  desktop_append(out, out_size, &used,
                 "launcher: desktop launcher | desktop launch <app>\n");
  desktop_append(out, out_size, &used,
                 "windows: desktop windows | desktop workspace <1-10|next|empty|+/-n|previous> | desktop dispatch movetoworkspace <target>\n");
}

void orizon_desktop_format_session_state(char *out, size_t out_size) {
  char state[1024];
  char log[1024];
  size_t used = 0;
  int n;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used, "Orizon desktop session manager\n");
  desktop_append(out, out_size, &used,
                 "commands: desktop start | desktop stop | desktop restart | desktop reload | desktop recover | desktop rescue\n");
  desktop_append(out, out_size, &used,
                 "scope: Hyprland-style compositor facade, VM/ZimaOS-safe\n");
  desktop_append(out, out_size, &used,
                 "health: PASS means desired/runtime/policy are coherent; WARN means use desktop rescue/recover\n");
  desktop_append(out, out_size, &used,
                 "manual-window-drag: no\n");
  desktop_append(out, out_size, &used, "state-path: ");
  desktop_append(out, out_size, &used, ORIZON_DESKTOP_STATE_PATH);
  desktop_append(out, out_size, &used, "\n\n== state ==\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_STATE_PATH, state, sizeof(state));
  desktop_append(out, out_size, &used, n > 0 ? state : desktop_state_config);
  if (out[0] && out[strlen(out) - 1] != '\n') {
    desktop_append(out, out_size, &used, "\n");
  }
  desktop_append(out, out_size, &used, "\n== session-log ==\n");
  desktop_append(out, out_size, &used, "path: ");
  desktop_append(out, out_size, &used, ORIZON_DESKTOP_SESSION_LOG_PATH);
  desktop_append(out, out_size, &used, "\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_SESSION_LOG_PATH, log, sizeof(log));
  desktop_append(out, out_size, &used, n > 0 ? log : "empty\n");
  if (out[0] && out[strlen(out) - 1] != '\n') {
    desktop_append(out, out_size, &used, "\n");
  }
}

void orizon_desktop_format_settings(char *out, size_t out_size) {
  orizon_desktop_settings_t settings;
  char line[192];
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_load_settings(&settings);
  desktop_append(out, out_size, &used, "Orizon desktop system settings\n");
  snprintf(line, sizeof(line), "path: %s\n", ORIZON_DESKTOP_SETTINGS_PATH);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "created-by: installer desktop selection or pkg install " ORIZON_DESKTOP_PACKAGE "\n");
  snprintf(line, sizeof(line), "scale: %d\n", settings.scale);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "gaps-in: %d\n", settings.gaps_in);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "gaps-out: %d\n", settings.gaps_out);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "border-size: %d\n", settings.border_size);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "rounding: %d\n", settings.rounding);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "animations: %s\n",
           settings.animations_enabled ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "shadows: %s\n",
           settings.shadows_enabled ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "focus-ring: %s\n",
           settings.focus_ring_enabled ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "shadow-range: %d\n",
           settings.shadow_range);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "animation-ticks: %d\n",
           settings.animation_ticks);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "animation-curve: %s\n",
           settings.animation_curve);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "render-profile: %s\n",
           settings.render_profile);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "idle-timeout-seconds: %d\n",
           settings.idle_timeout_seconds);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "lock-on-idle: %s\n",
           settings.lock_on_idle ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "default-terminal: %s\n",
           settings.default_terminal);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "launcher-provider: %s\n",
           settings.launcher_provider);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "bar-position: %s\n", settings.bar_position);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "keyboard-layout: %s\n",
           settings.keyboard_layout);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "pointer-profile: %s\n",
           settings.pointer_profile);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "set: desktop settings set <key> <value>\n");
  desktop_append(out, out_size, &used,
                 "paths: desktop settings paths  # show central settings hub\n");
  desktop_append(out, out_size, &used,
                 "export: desktop settings export  # write ~/.config/hypr/orizon-hypr.conf from /system\n");
  desktop_append(out, out_size, &used,
                 "sync: desktop settings sync  # export then regenerate runtime files\n");
  desktop_append(out, out_size, &used,
                 "presets: desktop settings presets | desktop settings preset <name>\n");
  desktop_append(out, out_size, &used,
                 "doctor: desktop settings doctor  # validate system settings file\n");
  desktop_append(out, out_size, &used,
                 "repair: desktop settings repair  # rewrite safe defaults\n");
  desktop_append(out, out_size, &used,
                 "note: this is system-wide desktop policy, not per-window runtime state.\n");
}

static void desktop_append_path_status(char *out, size_t out_size,
                                       size_t *used, const char *label,
                                       const char *path) {
  char line[192];
  size_t size = 0;
  int ok;

  ok = path && vfs_stat(path, &size, NULL) == 0 && size > 0;
  snprintf(line, sizeof(line), "%s: %s %s bytes=%lu\n", label,
           path ? path : "none", ok ? "PASS" : "WARN",
           (unsigned long)(ok ? size : 0));
  desktop_append(out, out_size, used, line);
}

void orizon_desktop_format_session_rescue(char *out, size_t out_size) {
  char state[1024];
  char desired[32];
  char runtime[32];
  char health[32];
  char policy[32];
  char line[192];
  size_t used = 0;
  int n;
  int installed;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  installed = orizon_system_is_installed();
  n = desktop_read_text_file(ORIZON_DESKTOP_STATE_PATH, state, sizeof(state));
  if (n <= 0) {
    snprintf(state, sizeof(state), "%s", desktop_state_config);
  }
  desktop_session_get_value(state, "desired-state", desired, sizeof(desired),
                            "unknown");
  desktop_session_get_value(state, "runtime-state", runtime, sizeof(runtime),
                            "unknown");
  desktop_session_get_value(state, "health", health, sizeof(health), "WARN");
  desktop_session_get_value(state, "policy", policy, sizeof(policy),
                            orizon_desktop_is_enabled() ? "enabled"
                                                        : "disabled");

  desktop_append(out, out_size, &used, "Orizon desktop rescue\n");
  desktop_append(out, out_size, &used,
                 "scope: non-destructive Hyprland-style session recovery checklist\n");
  snprintf(line, sizeof(line),
           "summary: health=%s desired=%s runtime=%s policy=%s boot=%s\n",
           health, desired, runtime, policy,
           installed ? "installed" : "live-iso");
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "safe-actions:\n"
                 "  desktop state              inspect manager state/log\n"
                 "  desktop recover            rewrite defaults and reapply config\n"
                 "  desktop config doctor      inspect Hyprland-style config\n"
                 "  desktop settings doctor    inspect /system desktop settings\n"
                 "  desktop settings sync      export /system settings and refresh runtime hints\n"
                 "  desktop stop               disable policy without deleting config\n"
                 "  desktop start              enable policy and apply config\n");
  desktop_append(out, out_size, &used,
                 installed ? "persistence: installed VM marker present\n"
                           : "persistence: live ISO/unmarked; install VM before relying on durable state\n");
  desktop_append(out, out_size, &used,
                 "limits: no Waybar package installed now; no taskbar/menu-start; no free-drag window moving\n");
  desktop_append(out, out_size, &used, "\n== files ==\n");
  desktop_append_path_status(out, out_size, &used, "policy",
                             ORIZON_DESKTOP_CONFIG_PATH);
  desktop_append_path_status(out, out_size, &used, "session",
                             ORIZON_DESKTOP_SESSION_PATH);
  desktop_append_path_status(out, out_size, &used, "settings",
                             ORIZON_DESKTOP_SETTINGS_PATH);
  desktop_append_path_status(out, out_size, &used, "state",
                             ORIZON_DESKTOP_STATE_PATH);
  desktop_append_path_status(out, out_size, &used, "session-log",
                             ORIZON_DESKTOP_SESSION_LOG_PATH);
  desktop_append_path_status(out, out_size, &used, "user-config",
                             ORIZON_DESKTOP_USER_CONFIG_PATH);
  desktop_append_path_status(out, out_size, &used, "binds-runtime",
                             ORIZON_DESKTOP_BINDS_PATH);
  desktop_append_path_status(out, out_size, &used, "runtime-state",
                             ORIZON_DESKTOP_RUNTIME_PATH);
  desktop_append_path_status(out, out_size, &used, "backend-map",
                             ORIZON_DESKTOP_BACKEND_PATH);
  desktop_append_path_status(out, out_size, &used, "protocol-map",
                             ORIZON_DESKTOP_PROTOCOL_PATH);
  desktop_append(out, out_size, &used,
                 "\nnext: desktop recover if files are WARN, then desktop state\n");
}

void orizon_desktop_format_settings_paths(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used, "Orizon desktop settings hub\n");
  desktop_append(out, out_size, &used,
                 "model: /system is the source of truth; /home/orizon/.config/hypr mirrors Hyprland-style user config\n");
  desktop_append(out, out_size, &used,
                 "install: created by installer desktop selection or pkg install " ORIZON_DESKTOP_PACKAGE "\n");
  desktop_append_path_status(out, out_size, &used, "policy",
                             ORIZON_DESKTOP_CONFIG_PATH);
  desktop_append_path_status(out, out_size, &used, "session",
                             ORIZON_DESKTOP_SESSION_PATH);
  desktop_append_path_status(out, out_size, &used, "settings",
                             ORIZON_DESKTOP_SETTINGS_PATH);
  desktop_append_path_status(out, out_size, &used, "modules",
                             ORIZON_DESKTOP_MODULES_PATH);
  desktop_append_path_status(out, out_size, &used, "user-config",
                             ORIZON_DESKTOP_USER_CONFIG_PATH);
  desktop_append_path_status(out, out_size, &used, "template",
                             ORIZON_DESKTOP_TEMPLATE_PATH);
  desktop_append_path_status(out, out_size, &used, "binds-runtime",
                             ORIZON_DESKTOP_BINDS_PATH);
  desktop_append_path_status(out, out_size, &used, "autostart-runtime",
                             ORIZON_DESKTOP_AUTOSTART_PATH);
  desktop_append_path_status(out, out_size, &used, "rules-runtime",
                             ORIZON_DESKTOP_RULES_PATH);
  desktop_append_path_status(out, out_size, &used, "monitors-runtime",
                             ORIZON_DESKTOP_MONITORS_PATH);
  desktop_append_path_status(out, out_size, &used, "layers-runtime",
                             ORIZON_DESKTOP_LAYERS_PATH);
  desktop_append_path_status(out, out_size, &used, "runtime-state",
                             ORIZON_DESKTOP_RUNTIME_PATH);
  desktop_append_path_status(out, out_size, &used, "backend-map",
                             ORIZON_DESKTOP_BACKEND_PATH);
  desktop_append_path_status(out, out_size, &used, "protocol-map",
                             ORIZON_DESKTOP_PROTOCOL_PATH);
  desktop_append_path_status(out, out_size, &used, "session-state",
                             ORIZON_DESKTOP_STATE_PATH);
  desktop_append_path_status(out, out_size, &used, "session-log",
                             ORIZON_DESKTOP_SESSION_LOG_PATH);
  desktop_append(out, out_size, &used,
                 "commands: desktop settings export | desktop settings sync | desktop config apply | desktop settings doctor\n");
  desktop_append(out, out_size, &used,
                 "limits: this is a Hyprland-style facade; no wlroots/Wayland backend yet and no manual window dragging.\n");
}

void orizon_desktop_format_backend(char *out, size_t out_size) {
  char cfg[1024];
  size_t used = 0;
  int n;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used, "Orizon desktop backend\n");
  desktop_append(out, out_size, &used,
                 "current-backend: framebuffer-vm\n");
  desktop_append(out, out_size, &used,
                 "api: compositor-orchestrator\n");
  desktop_append(out, out_size, &used,
                 "renderer: software-backbuffer\n");
  desktop_append(out, out_size, &used,
                 "clients: tiled-internal only; external-wayland-clients=no\n");
  desktop_append(out, out_size, &used,
                 "future-backend: wayland-wlroots prepared, not implemented\n");
  desktop_append(out, out_size, &used,
                 "policy: manual-window-drag=no taskbar=no waybar-installed=no\n");
  desktop_append(out, out_size, &used,
                 "hardware-validation: no real PC/Lenovo validation claimed\n");
  desktop_append(out, out_size, &used,
                 "path: " ORIZON_DESKTOP_BACKEND_PATH "\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_BACKEND_PATH, cfg, sizeof(cfg));
  if (n > 0) {
    desktop_append(out, out_size, &used, "\n== backend.conf ==\n");
    desktop_append(out, out_size, &used, cfg);
  } else {
    desktop_append(out, out_size, &used,
                   "backend-map WARN missing; run desktop reset\n");
  }
}

void orizon_desktop_format_protocol(char *out, size_t out_size) {
  char cfg[1024];
  size_t used = 0;
  int n;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used, "Orizon desktop protocol\n");
  desktop_append(out, out_size, &used,
                 "protocol: orizon-desktop-ipc-v0\n");
  desktop_append(out, out_size, &used,
                 "transport: internal-kernel-dispatch\n");
  desktop_append(out, out_size, &used,
                 "messages: dispatch spawn-client close-client focus-client workspace config-keyword query-state\n");
  desktop_append(out, out_size, &used,
                 "wayland: no\nwlroots: no\nxdg-shell: no\nlayer-shell: prepared-only\nxwayland: no\n");
  desktop_append(out, out_size, &used,
                 "status: prepared split between Orizon compositor API and framebuffer backend\n");
  desktop_append(out, out_size, &used,
                 "path: " ORIZON_DESKTOP_PROTOCOL_PATH "\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_PROTOCOL_PATH, cfg, sizeof(cfg));
  if (n > 0) {
    desktop_append(out, out_size, &used, "\n== protocol.conf ==\n");
    desktop_append(out, out_size, &used, cfg);
  } else {
    desktop_append(out, out_size, &used,
                   "protocol-map WARN missing; run desktop reset\n");
  }
}

void orizon_desktop_format_settings_presets(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used, "Orizon desktop settings presets\n");
  desktop_append(out, out_size, &used,
                 "default       scale=1 gaps=6/12 border=2 rounding=8 anim=18 curve=orizon-pop shadows=18 render=balanced\n");
  desktop_append(out, out_size, &used,
                 "compact       scale=1 gaps=2/4 border=1 rounding=4 anim=12 shadows=8 render=compact\n");
  desktop_append(out, out_size, &used,
                 "cozy          scale=1 gaps=10/18 border=2 rounding=12 anim=22 curve=orizon-slide shadows=22 render=cozy\n");
  desktop_append(out, out_size, &used,
                 "performance   scale=1 gaps=4/8 border=1 rounding=0 anim=no shadow=0 render=performance\n");
  desktop_append(out, out_size, &used,
                 "accessibility scale=2 gaps=12/20 border=4 rounding=10 focus-ring=yes shadow=24 render=accessibility\n");
  desktop_append(out, out_size, &used,
                 "locked        scale=1 gaps=6/10 border=2 rounding=6 idle-lock=300s\n");
  desktop_append(out, out_size, &used,
                 "aliases: graphite=default, comfortable=cozy, vm=performance, large=accessibility, secure=locked\n");
  desktop_append(out, out_size, &used,
                 "apply: desktop settings preset <default|compact|cozy|performance|accessibility|locked>\n");
}

void orizon_desktop_format_settings_doctor(char *out, size_t out_size) {
  char cfg[2048];
  char key[48];
  char line[192];
  orizon_desktop_settings_t settings;
  size_t used = 0;
  size_t size = 0;
  int n;
  int known = 0;
  int unknown = 0;
  int malformed = 0;
  int fail = 0;
  int warn = 0;
  int pos = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  orizon_desktop_load_settings(&settings);
  desktop_append(out, out_size, &used, "Orizon desktop settings doctor\n");
  snprintf(line, sizeof(line), "path: %s\n", ORIZON_DESKTOP_SETTINGS_PATH);
  desktop_append(out, out_size, &used, line);

  if (vfs_stat(ORIZON_DESKTOP_SETTINGS_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "file PASS bytes=%lu\n",
             (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "file FAIL missing or empty; run desktop settings repair\n");
    fail = 1;
  }

  n = desktop_read_text_file(ORIZON_DESKTOP_SETTINGS_PATH, cfg, sizeof(cfg));
  if (n <= 0) {
    fail = 1;
  } else {
    while (pos < n) {
      int start = pos;
      int end;
      int p;
      int k = 0;

      while (pos < n && cfg[pos] != '\n') {
        pos++;
      }
      end = pos;
      if (pos < n && cfg[pos] == '\n') {
        pos++;
      }
      p = start;
      while (p < end && (cfg[p] == ' ' || cfg[p] == '\t' || cfg[p] == '\r')) {
        p++;
      }
      if (p >= end || cfg[p] == '#') {
        continue;
      }
      while (p < end && cfg[p] != ' ' && cfg[p] != '\t' && cfg[p] != '\r') {
        if (k + 1 < (int)sizeof(key)) {
          key[k++] = cfg[p];
        }
        p++;
      }
      key[k] = '\0';
      while (p < end && (cfg[p] == ' ' || cfg[p] == '\t' || cfg[p] == '\r')) {
        p++;
      }
      if (k == 0 || p >= end) {
        malformed++;
      } else if (desktop_settings_key_known(key)) {
        known++;
      } else {
        unknown++;
      }
    }
  }

  if (known > 0 && unknown == 0 && malformed == 0) {
    snprintf(line, sizeof(line), "schema PASS known-keys=%d\n", known);
  } else {
    snprintf(line, sizeof(line),
             "schema WARN known-keys=%d unknown-lines=%d malformed-lines=%d\n",
             known, unknown, malformed);
    warn = 1;
  }
  desktop_append(out, out_size, &used, line);
  if (settings.scale < 1 || settings.scale > 3 || settings.gaps_in < 0 ||
      settings.gaps_out < 0 || settings.border_size < 0) {
    desktop_append(out, out_size, &used, "runtime FAIL invalid clamped values\n");
    fail = 1;
  } else {
    snprintf(line, sizeof(line),
             "runtime PASS scale=%d gaps=%d/%d border=%d rounding=%d focus-ring=%s shadow-range=%d animation-ticks=%d\n",
             settings.scale, settings.gaps_in, settings.gaps_out,
             settings.border_size, settings.rounding,
             settings.focus_ring_enabled ? "yes" : "no",
             settings.shadow_range, settings.animation_ticks);
    desktop_append(out, out_size, &used, line);
  }
  snprintf(line, sizeof(line),
           "policy terminal=%s launcher=%s bar=%s keyboard=%s pointer=%s render=%s curve=%s\n",
           settings.default_terminal, settings.launcher_provider,
           settings.bar_position, settings.keyboard_layout,
           settings.pointer_profile, settings.render_profile,
           settings.animation_curve);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "safe-fix: desktop settings repair | desktop settings preset default\n");
  snprintf(line, sizeof(line), "summary: %s\n",
           fail ? "FAIL" : (warn ? "WARN" : "PASS"));
  desktop_append(out, out_size, &used, line);
}

void orizon_desktop_format_modules(char *out, size_t out_size) {
  char cfg[1536];
  size_t used = 0;
  int n;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  desktop_append(out, out_size, &used, "Orizon desktop modules\n");
  desktop_append(out, out_size, &used,
                 "goal: modular Hyprland-style desktop packaging, no Windows taskbar, no free-drag windows\n");
  desktop_append(out, out_size, &used,
                 "prepared-now: " ORIZON_DESKTOP_PACKAGE_CORE " " ORIZON_DESKTOP_PACKAGE " " ORIZON_DESKTOP_PACKAGE_TERMINAL " " ORIZON_DESKTOP_PACKAGE_SETTINGS " " ORIZON_DESKTOP_PACKAGE_LAUNCHER "\n");
  desktop_append(out, out_size, &used,
                 "sample-now: pkg sample " ORIZON_DESKTOP_PACKAGE_CORE " | pkg sample " ORIZON_DESKTOP_PACKAGE_TERMINAL " | pkg sample " ORIZON_DESKTOP_PACKAGE_SETTINGS " | pkg sample " ORIZON_DESKTOP_PACKAGE_LAUNCHER "\n");
  desktop_append(out, out_size, &used,
                 "install-now: pkg install " ORIZON_DESKTOP_PACKAGE_CORE " | pkg install " ORIZON_DESKTOP_PACKAGE_TERMINAL " | pkg install " ORIZON_DESKTOP_PACKAGE_SETTINGS " | pkg install " ORIZON_DESKTOP_PACKAGE_LAUNCHER "\n");
  desktop_append(out, out_size, &used,
                 "planned-later: " ORIZON_DESKTOP_PACKAGE_WAYBAR " as separate package only, not installed now\n");
  desktop_append(out, out_size, &used,
                 "compat-meta: pkg install " ORIZON_DESKTOP_PACKAGE " remains the current all-in-one path while split packages are prepared\n");
  desktop_append(out, out_size, &used,
                 "module-dir: " ORIZON_DESKTOP_MODULE_DIR "\n");
  desktop_append(out, out_size, &used,
                 "path: " ORIZON_DESKTOP_MODULES_PATH "\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_MODULES_PATH, cfg, sizeof(cfg));
  if (n > 0) {
    desktop_append(out, out_size, &used, "\n== modules.conf ==\n");
    desktop_append(out, out_size, &used, cfg);
  } else {
    desktop_append(out, out_size, &used,
                   "modules.conf missing WARN run desktop reset or desktop settings sync\n");
  }
}

void orizon_desktop_format_apps(char *out, size_t out_size) {
  size_t used = 0;
  size_t count = sizeof(desktop_app_catalog) / sizeof(desktop_app_catalog[0]);
  char line[256];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used, "Orizon desktop apps\n");
  desktop_append(out, out_size, &used,
                 "model: compositor-managed Hyprland-style clients; tiling only, floating=no, manual-drag=no\n");
  desktop_append(out, out_size, &used,
                 "launcher-policy: overlay only; no Windows taskbar, no start menu, Waybar is future package\n");
  for (size_t i = 0; i < count; i++) {
    const desktop_app_entry_t *app = &desktop_app_catalog[i];
    snprintf(line, sizeof(line),
             "%-9s status=%s class=%s module=%s surface=%s backend=%s command='%s' shortcut=%s\n",
             app->id, app->status, app->class_name, app->module,
             app->surface, app->backend, app->command, app->shortcut);
    desktop_append(out, out_size, &used, line);
  }
  desktop_append(out, out_size, &used,
                 "detail: desktop app <terminal|settings|logs|packages|update|launcher>\n");
  desktop_append(out, out_size, &used,
                 "next-apps file-manager,wallpaper-daemon are not implemented yet; waybar-style bar is future package\n");
}

void orizon_desktop_format_app_detail(const char *app, char *out,
                                      size_t out_size) {
  const desktop_app_entry_t *entry;
  size_t used = 0;
  char line[256];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  entry = desktop_find_app_entry(app);
  desktop_append(out, out_size, &used, "Orizon desktop app\n");
  if (!entry) {
    snprintf(line, sizeof(line), "unknown: %s\n",
             (app && app[0]) ? app : "(empty)");
    desktop_append(out, out_size, &used, line);
    desktop_append(out, out_size, &used,
                   "known: terminal settings logs packages update launcher\n");
    desktop_append(out, out_size, &used,
                   "usage: desktop app <id> | desktop launch <id>\n");
    return;
  }
  snprintf(line, sizeof(line), "id: %s\n", entry->id);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "title: %s\n", entry->title);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "class: %s\n", entry->class_name);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "module: %s\n", entry->module);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "status: %s\n", entry->status);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "backend: %s\n", entry->backend);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "surface: %s\n", entry->surface);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used, "floating: no\n");
  desktop_append(out, out_size, &used, "manual-drag: no\n");
  desktop_append(out, out_size, &used, "taskbar: no\n");
  snprintf(line, sizeof(line), "command: %s\n", entry->command);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "shortcut: %s\n", entry->shortcut);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "aliases: %s\n", entry->aliases);
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "description: %s\n", entry->description);
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "inspect: desktop clients | desktop activewindow | desktop apps\n");
}

void orizon_desktop_format_profiles(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used, "Orizon desktop profiles\n");
  desktop_append(out, out_size, &used,
                 "presets: graphite moss ember frost focus default\n");
  desktop_append(out, out_size, &used,
                 "themes: graphite moss ember frost\n");
  desktop_append(out, out_size, &used,
                 "wallpapers: aurora dawn noir moss\n");
  desktop_append(out, out_size, &used,
                 "layouts: dwindle master monocle\n");
  desktop_append(out, out_size, &used,
                 "apps: terminal/settings/logs/packages/update enabled as tiling clients\n");
  desktop_append(out, out_size, &used,
                 "set: desktop theme <name>; desktop wallpaper <name>; desktop layout <name>\n");
  desktop_append(out, out_size, &used,
                 "apply: desktop preset <name>; focus: desktop focus on|off|toggle\n");
  desktop_append(out, out_size, &used,
                 "limits: symbolic profiles only; no real Hyprland/wlroots theme loader yet\n");
}

void orizon_desktop_format_autostart(char *out, size_t out_size) {
  orizon_desktop_session_t session;
  size_t used = 0;
  char line[128];
  char cfg[768];

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_load_session(&session);
  desktop_append(out, out_size, &used, "Orizon desktop autostart\n");
  snprintf(line, sizeof(line), "terminal: %s\n",
           session.autostart_terminal ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  snprintf(line, sizeof(line), "launcher: %s\n",
           session.launcher_enabled ? "available" : "disabled");
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "commands: desktop autostart terminal on|off|toggle\n");
  desktop_append(out, out_size, &used,
                 "path: " ORIZON_DESKTOP_SESSION_PATH "\n");
  desktop_append(out, out_size, &used,
                 "hypr-runtime: " ORIZON_DESKTOP_AUTOSTART_PATH "\n");
  if (desktop_read_text_file(ORIZON_DESKTOP_AUTOSTART_PATH, cfg,
                             sizeof(cfg)) > 0) {
    desktop_append(out, out_size, &used, "\n== exec-once ==\n");
    desktop_append(out, out_size, &used, cfg);
  }
  desktop_append(out, out_size, &used,
                 "limits: terminal autostart is executable; other exec-once entries are recorded as prepared runtime hints\n");
}

void orizon_desktop_format_shortcuts(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used, "Orizon desktop shortcuts\n");
  desktop_append(out, out_size, &used, "F1: dispatch exec terminal\n");
  desktop_append(out, out_size, &used, "F2: dispatch killactive\n");
  desktop_append(out, out_size, &used, "F3: toggle launcher overlay\n");
  desktop_append(out, out_size, &used, "F4: dispatch fullscreen\n");
  desktop_append(out, out_size, &used, "F5: dispatch pseudo\n");
  desktop_append(out, out_size, &used, "F6: dispatch cyclenext\n");
  desktop_append(out, out_size, &used, "F7/F8: dispatch workspace +1/-1\n");
  desktop_append(out, out_size, &used,
                 "F9/F10/F11/F12: submap resize/move/launch/default\n");
  desktop_append(out, out_size, &used,
                 "Enter / Space on empty focus: dispatch exec terminal\n");
  desktop_append(out, out_size, &used,
                 "resize submap: arrows or HJKL adjust tiling ratios; R resets; S toggles split\n");
  desktop_append(out, out_size, &used,
                 "move submap: arrows or HJKL focus; 1/2/3 move focused client to workspace; P pins\n");
  desktop_append(out, out_size, &used,
                 "launch submap: T terminal; S settings; L logs; P packages; U update; D launcher; Q killactive; Esc/F12 default\n");
  desktop_append(out, out_size, &used,
                 "Hypr-style template: SUPER+Return exec terminal, SUPER+Q killactive, SUPER+D launcher\n");
  desktop_append(out, out_size, &used,
                 "SUPER+A: autostart settings; SUPER+B: bar toggle placeholder; SUPER+R: session/settings placeholder\n");
  desktop_append(out, out_size, &used,
                 "SUPER+F: focus toggle placeholder; SUPER+P: profile list placeholder\n");
  desktop_append(out, out_size, &used,
                 "workspaces: SUPER+1/2/3 workspace; SUPER+Shift+1/2/3 movetoworkspace; dispatch supports next/empty/+/-n\n");
  desktop_append(out, out_size, &used,
                 "dispatchers: exec terminal/settings/logs/packages/update | killactive | movefocus l/r/u/d | cyclenext | swapnext | swapwindow | fullscreen | pseudo | pin | resizeactive\n");
  desktop_append(out, out_size, &used,
                 "status: desktop status; config: desktop config; package: desktop package\n");
}

void orizon_desktop_format_doctor(char *out, size_t out_size) {
  char line[256];
  size_t used = 0;
  int fail = 0;
  int warn = 0;
  int is_dir = 0;
  int enabled;
  size_t size = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  enabled = orizon_desktop_is_enabled();

  desktop_append(out, out_size, &used, "desktop doctor:\n");
  desktop_append(out, out_size, &used,
                 "scope non-destructive; checks optional Hyprland-style profile state\n");

  if (vfs_stat("/system", NULL, &is_dir) == 0 && is_dir) {
    desktop_append(out, out_size, &used, "dir /system PASS\n");
  } else {
    desktop_append(out, out_size, &used, "dir /system FAIL\n");
    fail = 1;
  }
  is_dir = 0;
  if (vfs_stat("/home/orizon/.config/hypr", NULL, &is_dir) == 0 && is_dir) {
    desktop_append(out, out_size, &used,
                   "dir /home/orizon/.config/hypr PASS\n");
  } else {
    desktop_append(out, out_size, &used,
                   "dir /home/orizon/.config/hypr FAIL\n");
    fail = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_CONFIG_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "config %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_CONFIG_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used, "config missing FAIL\n");
    fail = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_TEMPLATE_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "template %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_TEMPLATE_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used, "template missing FAIL\n");
    fail = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_SESSION_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "session %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_SESSION_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used, "session missing FAIL\n");
    fail = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_SETTINGS_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "settings %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_SETTINGS_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "settings missing WARN run desktop settings repair\n");
    warn = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_MODULES_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "modules %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_MODULES_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "modules missing WARN run desktop reset\n");
    warn = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_BACKEND_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "backend-map %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_BACKEND_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "backend-map missing WARN run desktop reset\n");
    warn = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_PROTOCOL_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "protocol-map %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_PROTOCOL_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "protocol-map missing WARN run desktop reset\n");
    warn = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_BINDS_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "binds-runtime %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_BINDS_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "binds-runtime missing WARN run desktop config apply\n");
    warn = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_AUTOSTART_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "autostart-runtime %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_AUTOSTART_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "autostart-runtime missing WARN run desktop config apply\n");
    warn = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_RULES_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "rules-runtime %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_RULES_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  }
  if (vfs_stat(ORIZON_DESKTOP_MONITORS_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "monitors-runtime %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_MONITORS_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  }
  if (vfs_stat(ORIZON_DESKTOP_LAYERS_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "layers-runtime %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_LAYERS_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  }
  if (vfs_stat(ORIZON_DESKTOP_STATE_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "session-state %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_STATE_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "session-state missing WARN run desktop recover\n");
    warn = 1;
  }
  if (vfs_stat(ORIZON_DESKTOP_USER_CONFIG_PATH, &size, NULL) == 0 &&
      size > 0) {
    snprintf(line, sizeof(line), "user-config %s PASS bytes=%lu\n",
             ORIZON_DESKTOP_USER_CONFIG_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else if (enabled) {
    desktop_append(out, out_size, &used,
                   "user-config missing WARN run desktop write-config\n");
    warn = 1;
  } else {
    desktop_append(out, out_size, &used,
                   "user-config absent OK optional until enabled\n");
  }
  if (vfs_stat(DESKTOP_PKG_META_PATH, &size, NULL) == 0 && size > 0) {
    snprintf(line, sizeof(line), "package-meta %s PASS bytes=%lu\n",
             DESKTOP_PKG_META_PATH, (unsigned long)size);
    desktop_append(out, out_size, &used, line);
  } else {
    desktop_append(out, out_size, &used,
                   "package-meta absent OK install with pkg install "
                   ORIZON_DESKTOP_PACKAGE "\n");
  }
  snprintf(line, sizeof(line), "enabled %s\n", enabled ? "yes" : "no");
  desktop_append(out, out_size, &used, line);
  desktop_append(out, out_size, &used,
                 "upstream-hyprland WARN not embedded; profile is Orizon compositor compatible\n");
  warn = 1;
  snprintf(line, sizeof(line), "summary: %s\n",
           fail ? "FAIL" : (warn ? "WARN" : "PASS"));
  desktop_append(out, out_size, &used, line);
}

void orizon_desktop_format_log(char *out, size_t out_size) {
  char log[1536];
  char session_log[1024];
  size_t used = 0;
  int n;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used, "desktop log:\n");
  desktop_append(out, out_size, &used, "path: " ORIZON_DESKTOP_LOG_PATH "\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_LOG_PATH, log, sizeof(log));
  if (n <= 0) {
    desktop_append(out, out_size, &used, "empty\n");
  } else {
    desktop_append(out, out_size, &used, log);
    if (out[0] && out[strlen(out) - 1] != '\n') {
      desktop_append(out, out_size, &used, "\n");
    }
  }
  desktop_append(out, out_size, &used, "\nsession-manager log:\n");
  desktop_append(out, out_size, &used,
                 "path: " ORIZON_DESKTOP_SESSION_LOG_PATH "\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_SESSION_LOG_PATH, session_log,
                             sizeof(session_log));
  if (n <= 0) {
    desktop_append(out, out_size, &used, "empty\n");
  } else {
    desktop_append(out, out_size, &used, session_log);
  }
  if (out[0] && out[strlen(out) - 1] != '\n') {
    desktop_append(out, out_size, &used, "\n");
  }
}

void orizon_desktop_format_rolling_log(char *out, size_t out_size) {
  char log[1536];
  size_t used = 0;
  int n;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used,
                 "Hyprland rolling log\n");
  desktop_append(out, out_size, &used,
                 "source: Orizon desktop event log\n");
  desktop_append(out, out_size, &used,
                 "path: " ORIZON_DESKTOP_LOG_PATH "\n");
  n = desktop_read_text_file(ORIZON_DESKTOP_LOG_PATH, log, sizeof(log));
  if (n <= 0) {
    desktop_append(out, out_size, &used, "empty\n");
    return;
  }
  desktop_append(out, out_size, &used, log);
  if (out[0] && out[strlen(out) - 1] != '\n') {
    desktop_append(out, out_size, &used, "\n");
  }
}
