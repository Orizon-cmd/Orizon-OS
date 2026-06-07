/*
 * Orizon desktop internal client protocol state.
 *
 * The compositor records kernel-local client/dispatcher messages here so
 * diagnostics can prove the protocol seam is real without claiming Wayland or
 * upstream Hyprland socket compatibility.
 */

#include "../include/desktop.h"
#include "../include/desktop_protocol.h"
#include "../include/string.h"

typedef struct {
  uint64_t serial;
  uint64_t total;
  uint64_t dispatch_count;
  uint64_t spawn_count;
  uint64_t close_count;
  uint64_t focus_count;
  uint64_t workspace_count;
  uint64_t query_count;
  int accepted;
  char message[32];
  char source[48];
  char target[80];
  char result[96];
} orizon_desktop_protocol_state_t;

static orizon_desktop_protocol_state_t protocol_state = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, "boot", "kernel", "desktop", "ready"};

static void protocol_copy(char *dest, size_t dest_size, const char *src) {
  if (!dest || dest_size == 0) {
    return;
  }
  if (!src) {
    src = "";
  }
  size_t i = 0;
  for (; src[i] && i + 1 < dest_size; i++) {
    char c = src[i];
    if (c == '"' || c == '\\') {
      c = '\'';
    } else if ((unsigned char)c < 32) {
      c = ' ';
    }
    dest[i] = c;
  }
  dest[i] = '\0';
}

void orizon_desktop_protocol_record(const char *message, const char *source,
                                    const char *target, const char *result,
                                    int accepted) {
  protocol_state.serial++;
  protocol_state.total++;
  protocol_state.accepted = accepted ? 1 : 0;
  protocol_copy(protocol_state.message, sizeof(protocol_state.message),
                message ? message : "unknown");
  protocol_copy(protocol_state.source, sizeof(protocol_state.source),
                source ? source : "kernel");
  protocol_copy(protocol_state.target, sizeof(protocol_state.target),
                target ? target : "");
  protocol_copy(protocol_state.result, sizeof(protocol_state.result),
                result ? result : "");

  if (message && strcmp(message, "dispatch") == 0) {
    protocol_state.dispatch_count++;
  } else if (message && strcmp(message, "spawn-client") == 0) {
    protocol_state.spawn_count++;
  } else if (message && strcmp(message, "close-client") == 0) {
    protocol_state.close_count++;
  } else if (message && strcmp(message, "focus-client") == 0) {
    protocol_state.focus_count++;
  } else if (message && strcmp(message, "workspace") == 0) {
    protocol_state.workspace_count++;
  } else if (message && strcmp(message, "query-state") == 0) {
    protocol_state.query_count++;
  }
}

uint64_t orizon_desktop_protocol_serial(void) { return protocol_state.serial; }

uint64_t orizon_desktop_protocol_message_count(void) {
  return protocol_state.total;
}

const char *orizon_desktop_protocol_last_message(void) {
  return protocol_state.message;
}

const char *orizon_desktop_protocol_contract(void) {
  return "desktop-protocol-v0 local-kernel-dispatch trace";
}

const char *orizon_desktop_protocol_message_contract(void) {
  return "dispatch,spawn-client,close-client,focus-client,workspace,config-keyword,query-state";
}

const char *orizon_desktop_protocol_client_contract(void) {
  return "internal-tiled-client-v0 stable-id,workspace,class,title,state";
}

const char *orizon_desktop_protocol_limits(void) {
  return "no-wayland-wire,no-hyprland-socket,no-xdg-shell,no-layer-shell-runtime,no-external-clients";
}

void orizon_desktop_protocol_format_state(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "internal-protocol-state: serial=%llu total=%llu last=%s "
           "source=%s target=\"%s\" accepted=%s result=\"%s\"\n"
           "internal-protocol-counts: dispatch=%llu spawn-client=%llu "
           "close-client=%llu focus-client=%llu workspace=%llu query-state=%llu\n"
           "internal-protocol-contract: %s\n"
           "internal-client-contract: %s\n"
           "internal-protocol-boundary: local-kernel-only wayland=no wlroots=no "
           "hyprland-socket=no external-clients=no limits=%s\n",
           (unsigned long long)protocol_state.serial,
           (unsigned long long)protocol_state.total, protocol_state.message,
           protocol_state.source, protocol_state.target,
           protocol_state.accepted ? "yes" : "no", protocol_state.result,
           (unsigned long long)protocol_state.dispatch_count,
           (unsigned long long)protocol_state.spawn_count,
           (unsigned long long)protocol_state.close_count,
           (unsigned long long)protocol_state.focus_count,
           (unsigned long long)protocol_state.workspace_count,
           (unsigned long long)protocol_state.query_count,
           orizon_desktop_protocol_message_contract(),
           orizon_desktop_protocol_client_contract(),
           orizon_desktop_protocol_limits());
}

void orizon_desktop_protocol_format_state_json(char *out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  snprintf(out, out_size,
           "\"runtime\":{\"serial\":%llu,\"total\":%llu,\"lastMessage\":\"%s\","
           "\"source\":\"%s\",\"target\":\"%s\",\"accepted\":%s,"
           "\"result\":\"%s\",\"counts\":{\"dispatch\":%llu,"
           "\"spawnClient\":%llu,\"closeClient\":%llu,\"focusClient\":%llu,"
           "\"workspace\":%llu,\"queryState\":%llu},"
           "\"contract\":\"%s\",\"clientContract\":\"%s\","
           "\"limits\":\"%s\","
           "\"localKernelOnly\":true,\"waylandTraffic\":false,"
           "\"hyprlandSocket\":false}",
           (unsigned long long)protocol_state.serial,
           (unsigned long long)protocol_state.total, protocol_state.message,
           protocol_state.source, protocol_state.target,
           protocol_state.accepted ? "true" : "false", protocol_state.result,
           (unsigned long long)protocol_state.dispatch_count,
           (unsigned long long)protocol_state.spawn_count,
           (unsigned long long)protocol_state.close_count,
           (unsigned long long)protocol_state.focus_count,
           (unsigned long long)protocol_state.workspace_count,
           (unsigned long long)protocol_state.query_count,
           orizon_desktop_protocol_message_contract(),
           orizon_desktop_protocol_client_contract(),
           orizon_desktop_protocol_limits());
}
