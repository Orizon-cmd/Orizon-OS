/*
 * Orizon OS x86_64 - Optional Hyprland-style desktop profile
 *
 * This is not upstream Hyprland/Wayland yet. It records an installable,
 * persistent desktop profile that the compositor can consume safely.
 */

#include "../include/desktop.h"
#include "../include/string.h"
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
    "layout floating\n"
    "bar yes\n"
    "launcher yes\n"
    "autostart-terminal yes\n"
    "focus follows-mouse no\n";

static const char *desktop_user_config =
    "# Orizon Hyprland-style desktop profile\n"
    "# Syntax intentionally mirrors common Hyprland concepts while the real\n"
    "# Wayland/Hyprland stack is not embedded in Orizon yet.\n"
    "$mod = SUPER\n"
    "monitor = ,preferred,auto,1\n"
    "general:gaps_in = 6\n"
    "general:gaps_out = 12\n"
    "general:border_size = 2\n"
    "decoration:rounding = 8\n"
    "animations:enabled = true\n"
    "misc:disable_hyprland_logo = false\n"
    "misc:force_default_wallpaper = 0\n"
    "bind = $mod, RETURN, exec, orizon-terminal\n"
    "bind = $mod, Q, closewindow\n"
    "bind = $mod, D, exec, orizon-launcher\n"
    "bind = $mod, B, exec, desktop bar toggle\n"
    "bind = $mod, R, exec, desktop session\n"
    "bind = F1, exec, desktop open terminal\n"
    "bind = F2, exec, desktop close terminal\n";

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
           "focus follows-mouse no\n",
           session->theme[0] ? session->theme : "graphite",
           session->wallpaper[0] ? session->wallpaper : "aurora",
           session->layout[0] ? session->layout : "floating",
           session->bar_enabled ? "yes" : "no",
           session->launcher_enabled ? "yes" : "no",
           session->autostart_terminal ? "yes" : "no");
  return desktop_write_text_file(ORIZON_DESKTOP_SESSION_PATH, text);
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

int orizon_desktop_ensure_defaults(void) {
  int rc = 0;

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
  return rc;
}

int orizon_desktop_is_enabled(void) {
  char cfg[768];

  if (desktop_read_text_file(ORIZON_DESKTOP_CONFIG_PATH, cfg, sizeof(cfg)) <=
      0) {
    return 0;
  }
  if (strstr(cfg, "enabled yes") || strstr(cfg, "enabled true") ||
      strstr(cfg, "enabled on")) {
    return 1;
  }
  return 0;
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
  snprintf(session->layout, sizeof(session->layout), "%s", "floating");
  session->bar_enabled = 1;
  session->launcher_enabled = 1;
  session->autostart_terminal = 1;

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
  } else {
    if (status && status_size) {
      snprintf(status, status_size,
               "desktop session: unknown key '%s'\n"
               "keys: theme wallpaper layout bar launcher autostart-terminal\n",
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

int orizon_desktop_set_enabled(int enabled, char *status, size_t status_size) {
  int rc;
  int user_rc = 0;

  desktop_ensure_dirs();
  rc = desktop_write_text_file(ORIZON_DESKTOP_CONFIG_PATH,
                               enabled ? desktop_enabled_config
                                       : desktop_default_config);
  if (enabled) {
    user_rc = orizon_desktop_write_user_config(NULL, 0);
  }
  desktop_log_event(enabled ? "enabled profile=" ORIZON_DESKTOP_PROFILE
                            : "disabled");
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop: %s\nprofile: %s\nconfig: %s\nuser-config: %s\n"
             "terminal-shortcuts: F1=open F2=close\n",
             enabled ? "enabled" : "disabled", ORIZON_DESKTOP_PROFILE,
             rc == 0 ? ORIZON_DESKTOP_CONFIG_PATH : "write-failed",
             enabled ? (user_rc == 0 ? ORIZON_DESKTOP_USER_CONFIG_PATH
                                      : "write-failed")
                     : "kept-if-present");
  }
  return rc == 0 && user_rc == 0 ? 0 : -1;
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
  desktop_log_event("reset profile=" ORIZON_DESKTOP_PROFILE);
  vfs_persist_save();
  if (status && status_size) {
    snprintf(status, status_size,
             "desktop: reset\n"
             "enabled: no\n"
             "profile: %s\n"
             "config: %s\n"
             "template: %s\n"
             "user-config: kept-if-present %s\n",
             ORIZON_DESKTOP_PROFILE,
             rc == 0 ? ORIZON_DESKTOP_CONFIG_PATH : "write-failed",
             template_rc == 0 ? ORIZON_DESKTOP_TEMPLATE_PATH
                              : "write-failed",
             ORIZON_DESKTOP_USER_CONFIG_PATH);
  }
  return rc == 0 && template_rc == 0 ? 0 : -1;
}

void orizon_desktop_format_status(char *out, size_t out_size) {
  char line[256];
  size_t used = 0;
  int enabled;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  orizon_desktop_ensure_defaults();
  enabled = orizon_desktop_is_enabled();
  desktop_append(out, out_size, &used, "Orizon desktop\n");
  snprintf(line, sizeof(line), "enabled: %s\n", enabled ? "yes" : "no");
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
  desktop_append(out, out_size, &used,
                 "terminal: F1/open or click desktop; F2/desktop close terminal\n");
  desktop_append(out, out_size, &used,
                 "admin: desktop doctor | desktop logs | desktop shortcuts | desktop reset\n");
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
  desktop_append(out, out_size, &used,
                 "set: desktop theme <name> | desktop wallpaper <name> | desktop layout <name> | desktop bar on|off\n");
  desktop_append(out, out_size, &used,
                 "launcher: desktop launcher | desktop launch terminal\n");
  desktop_append(out, out_size, &used,
                 "windows: desktop windows | desktop workspace <n> | desktop move terminal <n>\n");
}

void orizon_desktop_format_apps(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used, "Orizon desktop apps\n");
  desktop_append(out, out_size, &used,
                 "terminal  installed=yes command='desktop launch terminal' shortcut=F1\n");
  desktop_append(out, out_size, &used,
                 "settings  prepared=yes command='desktop session' shortcut=SUPER+R\n");
  desktop_append(out, out_size, &used,
                 "packages  prepared=yes command='pkg search desktop' shortcut=none\n");
  desktop_append(out, out_size, &used,
                 "launcher  prepared=yes command='desktop launcher' shortcut=SUPER+D\n");
  desktop_append(out, out_size, &used,
                 "next-apps file-manager,status-bar,wallpaper-daemon are not implemented yet\n");
}

void orizon_desktop_format_shortcuts(char *out, size_t out_size) {
  size_t used = 0;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';
  desktop_append(out, out_size, &used, "Orizon desktop shortcuts\n");
  desktop_append(out, out_size, &used, "F1: open terminal window\n");
  desktop_append(out, out_size, &used, "F2: close terminal window\n");
  desktop_append(out, out_size, &used, "F3: toggle launcher overlay\n");
  desktop_append(out, out_size, &used,
                 "click desktop / Enter / Space: reopen terminal when closed\n");
  desktop_append(out, out_size, &used,
                 "Hypr-style template: SUPER+Return terminal, SUPER+Q close, SUPER+D launcher placeholder\n");
  desktop_append(out, out_size, &used,
                 "SUPER+B: bar toggle placeholder; SUPER+R: session/settings placeholder\n");
  desktop_append(out, out_size, &used,
                 "workspaces: desktop workspace <n>; desktop move terminal <n>\n");
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
    return;
  }
  desktop_append(out, out_size, &used, log);
  if (out[0] && out[strlen(out) - 1] != '\n') {
    desktop_append(out, out_size, &used, "\n");
  }
}
