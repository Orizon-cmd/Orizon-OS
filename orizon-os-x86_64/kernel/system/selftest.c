/*
 * Orizon OS x86_64 - Non-destructive self tests
 *
 * These checks are safe for live ISO use: they inspect state, run pure crypto
 * vectors, and perform only read-only disk probes.
 */

#include "../include/selftest.h"
#include "../include/aes_gcm.h"
#include "../include/net.h"
#include "../include/netstack.h"
#include "../include/sha256.h"
#include "../include/ssh.h"
#include "../include/storage.h"
#include "../include/string.h"
#include "../include/update.h"
#include "../include/wifi.h"

static void selftest_append(char *out, size_t out_size, size_t *used,
                            const char *text) {
  size_t len;

  if (!out || !used || !text || *used >= out_size) {
    return;
  }
  len = strlen(text);
  if (*used + len >= out_size) {
    len = out_size - *used - 1;
  }
  memcpy(out + *used, text, len);
  *used += len;
  out[*used] = '\0';
}

static void selftest_line(char *out, size_t out_size, size_t *used,
                          const char *name, const char *result,
                          const char *detail) {
  char line[256];

  snprintf(line, sizeof(line), "%-22s %s %s\n", name, result,
           detail ? detail : "");
  selftest_append(out, out_size, used, line);
}

static int selftest_scope_is(const char *scope, const char *name) {
  return !scope || scope[0] == '\0' || strcmp(scope, "all") == 0 ||
         strcmp(scope, name) == 0;
}

static int selftest_network(char *out, size_t out_size, size_t *used) {
  const netstack_status_t *ip = netstack_get_status();
  char detail[160];
  int fail = 0;

  snprintf(detail, sizeof(detail), "link=%s driver=%s",
           net_link_up() ? "up" : "down",
           net_get_status()->driver ? net_get_status()->driver : "none");
  selftest_line(out, out_size, used, "network.link",
                net_link_up() ? "PASS" : "WARN", detail);
  if (!net_link_up()) {
    fail = 1;
  }

  snprintf(detail, sizeof(detail), "ipv4=%s status=\"%s\"",
           ip->ipv4_ready ? "ready" : "not-ready", ip->status);
  selftest_line(out, out_size, used, "network.ipv4",
                ip->ipv4_ready ? "PASS" : "WARN", detail);
  return fail ? 1 : 0;
}

static int selftest_storage(char *out, size_t out_size, size_t *used) {
  char detail[256];
  char cap[64];
  int count = storage_device_count();
  int rc;

  storage_format_capacity(cap, sizeof(cap));
  snprintf(detail, sizeof(detail), "devices=%d selected=%d capacity=%s",
           count, storage_selected_device() + 1, cap);
  selftest_line(out, out_size, used, "storage.detect",
                count > 0 ? "PASS" : "WARN", detail);
  rc = storage_read_test(0, detail, sizeof(detail));
  selftest_line(out, out_size, used, "storage.read-only",
                rc == 0 ? "PASS" : (rc > 0 ? "WARN" : "FAIL"), detail);
  return rc < 0 ? -1 : (count > 0 && rc == 0 ? 0 : 1);
}

static int selftest_crypto(char *out, size_t out_size, size_t *used) {
  char hash[SHA256_HEX_SIZE];
  int aes_unwrap = aes128_key_unwrap_selftest();
  int aes_ccm = aes128_ccm_selftest();
  int ok;

  sha256_buffer_hex("abc", 3, hash);
  ok = strcmp(hash,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0;
  selftest_line(out, out_size, used, "crypto.sha256", ok ? "PASS" : "FAIL",
                hash);
  selftest_line(out, out_size, used, "crypto.aes-unwrap",
                aes_unwrap == 0 ? "PASS" : "FAIL", "RFC3394 vector");
  selftest_line(out, out_size, used, "crypto.aes-ccm",
                aes_ccm == 0 ? "PASS" : "FAIL", "CCMP-style vector");
  return ok && aes_unwrap == 0 && aes_ccm == 0 ? 0 : -1;
}

static int selftest_ssh(char *out, size_t out_size, size_t *used) {
  const ssh_status_t *st = ssh_get_status();
  char detail[160];

  snprintf(detail, sizeof(detail), "enabled=%s connected=%s sessions=%lu",
           st->enabled ? "yes" : "no", st->connected ? "yes" : "no",
           (unsigned long)st->sessions);
  selftest_line(out, out_size, used, "ssh.listener",
                st->enabled ? "PASS" : "WARN", detail);
  snprintf(detail, sizeof(detail), "hostkey=%s auth=%s",
           st->hostkey_persistent ? "persistent" :
           (st->hostkey_bootstrap ? "bootstrap" : "generated"),
           st->auth_configured ? "password" : "disabled");
  selftest_line(out, out_size, used, "ssh.identity",
                st->hostkey_loaded ? "PASS" : "WARN", detail);
  return st->enabled && st->hostkey_loaded ? 0 : 1;
}

static int selftest_update(char *out, size_t out_size, size_t *used) {
  char detail[512];

  orizon_update_format_status(detail, sizeof(detail));
  selftest_line(out, out_size, used, "update.status", "PASS",
                "non-destructive status available");
  selftest_append(out, out_size, used, detail);
  if (detail[0] && detail[strlen(detail) - 1] != '\n') {
    selftest_append(out, out_size, used, "\n");
  }
  return 0;
}

int orizon_selftest_format(const char *scope, char *out, size_t out_size) {
  size_t used = 0;
  int fail = 0;
  int warn = 0;
  int rc;

  if (!out || out_size == 0) {
    return -1;
  }
  out[0] = '\0';
  selftest_append(out, out_size, &used,
                  "Orizon selftest (non-destructive live checks)\n");

  if (selftest_scope_is(scope, "network")) {
    rc = selftest_network(out, out_size, &used);
    fail += rc < 0;
    warn += rc > 0;
  }
  if (selftest_scope_is(scope, "storage")) {
    rc = selftest_storage(out, out_size, &used);
    fail += rc < 0;
    warn += rc > 0;
  }
  if (selftest_scope_is(scope, "crypto")) {
    rc = selftest_crypto(out, out_size, &used);
    fail += rc < 0;
    warn += rc > 0;
  }
  if (selftest_scope_is(scope, "ssh")) {
    rc = selftest_ssh(out, out_size, &used);
    fail += rc < 0;
    warn += rc > 0;
  }
  if (selftest_scope_is(scope, "update")) {
    rc = selftest_update(out, out_size, &used);
    fail += rc < 0;
    warn += rc > 0;
  }

  if (used == strlen("Orizon selftest (non-destructive live checks)\n")) {
    selftest_line(out, out_size, &used, "usage", "WARN",
                  "selftest [network|storage|crypto|ssh|update]");
    warn++;
  }
  selftest_append(out, out_size, &used, "summary: ");
  if (fail) {
    selftest_append(out, out_size, &used, "FAIL\n");
    return -1;
  }
  selftest_append(out, out_size, &used, warn ? "WARN\n" : "PASS\n");
  return warn ? 1 : 0;
}
