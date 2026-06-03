from __future__ import annotations

import argparse
import base64
import json
import re
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

from common import connect_ssh, load_json, parse_env_file, read_required, run_command, run_sudo_command
from deploy_x86_64_tree_vm import deploy_remote_tree
from provision_orizon_vm import build_domain_xml


MATRIX_CASES = {
    "nat-e1000e": {"network_name": "default", "network_model": "e1000e"},
    "nat-virtio": {"network_name": "default", "network_model": "virtio"},
    "nat-rtl8139": {"network_name": "default", "network_model": "rtl8139"},
    "bridge-e1000e": {"network_name": "", "network_model": "e1000e"},
    "bridge-virtio": {"network_name": "", "network_model": "virtio"},
    "bridge-rtl8139": {"network_name": "", "network_model": "rtl8139"},
}

STATUS_PASS = "PASS"
STATUS_WARN = "WARN"
STATUS_FAIL = "FAIL"
STATUS_SKIP = "SKIP"

KEYMAP = {" ": "KEY_SPACE", "\n": "KEY_ENTER", "-": "KEY_MINUS"}
for _ch in "abcdefghijklmnopqrstuvwxyz0123456789":
    KEYMAP[_ch] = "KEY_" + _ch.upper()


def print_console(text: str) -> None:
    try:
        print(text)
        return
    except OSError:
        pass

    safe = []
    for ch in text:
        code = ord(ch)
        if ch in "\n\r\t":
            safe.append(ch)
        elif ch == "\x1b":
            safe.append(ch)
        elif 0x20 <= code <= 0xD7FF or 0xE000 <= code <= 0x10FFFF:
            safe.append(ch)
        else:
            safe.append("\uFFFD")
    sys.stdout.write("".join(safe) + "\n")
    sys.stdout.flush()


def matrix_config(base: dict, case_name: str, overrides: dict) -> dict:
    cfg = dict(base)
    name = f"{base.get('name', 'orizon')}-matrix-{case_name}"
    cfg["name"] = name
    cfg["title"] = f"Orizon OS Matrix {case_name}"
    cfg["remote_disk_path"] = f"/DATA/VM/{name}.img"
    cfg["network_name"] = overrides["network_name"]
    cfg["network_model"] = overrides["network_model"]
    return cfg


def apply_storage_profile(cfg: dict, disk_bus: str) -> dict:
    if disk_bus == "sata":
        return cfg
    updated = dict(cfg)
    updated["name"] = f"{cfg['name']}-{disk_bus}"
    updated["title"] = f"{cfg.get('title', cfg['name'])} {disk_bus}"
    updated["remote_disk_path"] = f"/DATA/VM/{updated['name']}.img"
    updated["disk_bus"] = disk_bus
    updated["disk_target_dev"] = "vda" if disk_bus == "virtio" else "sda"
    return updated


def define_vm(client, sudo_password: str, cfg: dict) -> None:
    xml_text = build_domain_xml(cfg)
    remote_xml = f"/tmp/{cfg['name']}.xml"
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8") as handle:
        handle.write(xml_text)
        local_xml = Path(handle.name)
    try:
        sftp = client.open_sftp()
        sftp.put(str(local_xml), remote_xml)
        existing = run_sudo_command(client, sudo_password, "virsh list --all --name")
        if cfg["name"] in existing.splitlines():
            state = run_sudo_command(
                client,
                sudo_password,
                f"virsh domstate {cfg['name']} || true",
                check=False,
            ).strip().lower()
            if state == "running":
                run_sudo_command(client, sudo_password, f"virsh destroy {cfg['name']}")
            run_sudo_command(client, sudo_password, f"virsh undefine {cfg['name']} --nvram || true")
        run_sudo_command(client, sudo_password, f"virsh define {remote_xml}")
        run_command(client, f"rm -f {remote_xml}", check=False)
    finally:
        local_xml.unlink(missing_ok=True)


def send_console_text(client, sudo_password: str, vm_name: str, text: str) -> None:
    for ch in text:
        key = KEYMAP.get(ch)
        if not key:
            raise ValueError(f"Cannot send unsupported console character: {ch!r}")
        run_sudo_command(
            client,
            sudo_password,
            f"virsh send-key --holdtime 80 {vm_name} {key}",
            check=False,
        )
        time.sleep(0.12 if ch != "\n" else 0.5)


def configure_ssh_console(
    client,
    sudo_password: str,
    vm_name: str,
    password: str,
    *,
    rounds: int = 1,
    initial_wait: int = 0,
) -> None:
    # Console input is sent blind through libvirt. Keep this sequence
    # idempotent so tests can safely re-arm SSH after DHCP/IP discovery.
    commands = (
        "keyboard us",
        "net dhcp",
        "ssh stop",
        "ssh lockout clear",
        f"ssh password {password}",
        "ssh auth max 5",
        "ssh auth lockout 30",
        "ssh start",
    )
    if initial_wait:
        time.sleep(initial_wait)
    for round_index in range(rounds):
        for cmd in commands:
            send_console_text(client, sudo_password, vm_name, cmd + "\n")
            time.sleep(1.5)
        if round_index + 1 < rounds:
            time.sleep(5)


def boot_and_start_ssh(client, sudo_password: str, vm_name: str, password: str) -> None:
    # Give the framebuffer shell time to appear, then replay the setup once to
    # avoid losing password/start commands during slower boots.
    configure_ssh_console(
        client,
        sudo_password,
        vm_name,
        password,
        rounds=2,
        initial_wait=20,
    )


def find_nat_ip(
    client,
    sudo_password: str,
    vm_name: str,
    mac: str,
    network_name: str,
    timeout: int,
) -> str:
    deadline = time.time() + timeout
    pattern = re.compile(re.escape(mac) + r"\s+ipv4\s+([0-9.]+)/")
    while time.time() < deadline:
        leases = run_sudo_command(
            client,
            sudo_password,
            f"virsh net-dhcp-leases {network_name} || true",
            check=False,
        )
        match = pattern.search(leases)
        if match:
            return match.group(1)

        # libvirt can lag before net-dhcp-leases is populated. Probe the
        # domain ARP view and host neighbor table before declaring NAT failed.
        for probe in (
            f"virsh domifaddr {vm_name} --source lease || true",
            f"virsh domifaddr {vm_name} --source arp || true",
            "ip -4 neigh show || true",
        ):
            out = run_sudo_command(client, sudo_password, probe, check=False)
            ip = _extract_ipv4_for_mac(out, mac)
            if ip:
                return ip
        time.sleep(2)
    return ""


def _extract_ipv4_for_mac(text: str, mac: str) -> str:
    wanted = mac.lower()
    for line in text.splitlines():
        if wanted not in line.lower():
            continue
        match = re.search(r"\b([0-9]+(?:\.[0-9]+){3})(?:/[0-9]+)?\b", line)
        if match:
            return match.group(1)
    return ""


def find_bridge_ip(
    client,
    sudo_password: str,
    vm_name: str,
    mac: str,
    bridge_device: str,
    timeout: int,
) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        probes = [
            f"virsh domifaddr {vm_name} --source arp || true",
            "ip -4 neigh show || true",
        ]
        if bridge_device:
            probes.append(f"ip -4 neigh show dev {bridge_device} || true")
        for probe in probes:
            out = run_sudo_command(client, sudo_password, probe, check=False)
            ip = _extract_ipv4_for_mac(out, mac)
            if ip:
                return ip
        time.sleep(2)
    return ""


def find_guest_ip(
    client,
    sudo_password: str,
    cfg: dict,
    vm_name: str,
    mac: str,
    timeout: int,
) -> str:
    if cfg["network_name"]:
        return find_nat_ip(
            client,
            sudo_password,
            vm_name,
            mac,
            cfg["network_name"],
            timeout,
        )
    return find_bridge_ip(
        client,
        sudo_password,
        vm_name,
        mac,
        cfg.get("bridge_device", ""),
        timeout,
    )


def wait_domstate(
    client,
    sudo_password: str,
    vm_name: str,
    wanted: set[str],
    timeout: int,
) -> str:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        last = run_sudo_command(
            client,
            sudo_password,
            f"virsh domstate {vm_name} || true",
            check=False,
        ).strip().lower()
        if last in wanted:
            return last
        time.sleep(1)
    return last


def capture_framebuffer_smoke(client, sudo_password: str, vm_name: str) -> tuple[bool, int]:
    remote_ppm = f"/tmp/{vm_name}-framebuffer.ppm"
    out = run_sudo_command(
        client,
        sudo_password,
        "sh -c 'rm -f {0}; virsh screenshot {1} {0} >/dev/null 2>&1; "
        "if [ -s {0} ]; then wc -c < {0}; else echo 0; fi; rm -f {0}'".format(
            remote_ppm, vm_name
        ),
        check=False,
    ).strip()
    try:
        size = int(out.splitlines()[-1])
    except (ValueError, IndexError):
        size = 0
    return size > 4096, size


def matrix_result(case_name: str, status: str, detail: str, cfg: dict, ip: str = "") -> dict[str, str]:
    return {
        "case": case_name,
        "status": status,
        "detail": detail,
        "ip": ip,
        "vm_name": cfg.get("name", ""),
        "network_model": cfg.get("network_model", ""),
        "network_name": cfg.get("network_name", "") or "bridge",
        "disk_bus": cfg.get("disk_bus", "sata"),
        "disk_path": cfg.get("remote_disk_path", ""),
    }


def write_case_log(output_dir: Path | None, case_name: str, lines: list[str]) -> None:
    if not output_dir:
        return
    output_dir.mkdir(parents=True, exist_ok=True)
    safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", case_name)
    (output_dir / f"{safe_name}.log").write_text(
        "\n".join(lines).rstrip() + "\n",
        encoding="utf-8",
    )


def write_matrix_report(output_dir: Path | None, results: list[dict[str, str]]) -> None:
    if not output_dir:
        return
    output_dir.mkdir(parents=True, exist_ok=True)
    counts = {status: 0 for status in (STATUS_PASS, STATUS_WARN, STATUS_FAIL, STATUS_SKIP)}
    for row in results:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    payload = {
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "counts": counts,
        "results": results,
        "hardware_validation": "not-claimed",
    }
    (output_dir / "matrix-summary.json").write_text(
        json.dumps(payload, indent=2) + "\n",
        encoding="utf-8",
    )
    lines = [
        "# Orizon VM Matrix Summary",
        "",
        "Real hardware validation: not claimed.",
        "",
        "## Counts",
        "",
        f"- PASS: {counts.get(STATUS_PASS, 0)}",
        f"- WARN: {counts.get(STATUS_WARN, 0)}",
        f"- FAIL: {counts.get(STATUS_FAIL, 0)}",
        f"- SKIP: {counts.get(STATUS_SKIP, 0)}",
        "",
        "## Cases",
        "",
        "| Case | Status | Network | Disk | IP | Detail |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in results:
        network = f"{row['network_model']}/{row['network_name']}"
        detail = row["detail"].replace("|", "\\|")
        lines.append(
            f"| `{row['case']}` | {row['status']} | `{network}` | "
            f"`{row['disk_bus']}` | `{row['ip'] or '-'}` | {detail} |"
        )
    (output_dir / "matrix-summary.md").write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
    )


def domif_mac(client, sudo_password: str, vm_name: str) -> str:
    out = run_sudo_command(client, sudo_password, f"virsh domiflist {vm_name}")
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 5 and re.fullmatch(r"[0-9a-fA-F:]{17}", fields[4]):
            return fields[4].lower()
    return ""


def run_ssh_checks(
    client,
    ip: str,
    password: str,
    *,
    timeout: int,
    include_update: bool,
) -> str:
    encoded = base64.b64encode(password.encode("utf-8")).decode("ascii")
    commands = [
        ("status", "ssh: enabled="),
        ("system status", "Orizon system status"),
        ("system health", "summary:"),
        ("system snapshot", "system snapshot:"),
        ("cat /workspace/.orizon/system-snapshot.txt", "Orizon system snapshot"),
        ("system backup", "system backup:"),
        ("cat /workspace/.orizon/admin-backup.txt", "Orizon admin backup"),
        ("system services", "Orizon init/services"),
        ("system logs", "Orizon system logs"),
        ("system doctor", "summary:"),
        ("system init", "system init:"),
        ("system firstboot", "Orizon first boot"),
        ("security", "Orizon security status"),
        ("security", "vfs.policy: version=2"),
        ("security", "policy-denies:"),
        ("security policy", "security policy:"),
        ("security policy", "policy-version: 2"),
        ("security audit", "security audit:"),
        ("security audit", "state-files: policy=/system/security-policy"),
        ("security keys", "security keys:"),
        ("security keys", "rotation-summary: ssh-hostkey=runtime"),
        ("security doctor", "summary:"),
        ("security doctor", "policy.state"),
        ("logs security", "audit:"),
        ("rescue", "Orizon rescue mode"),
        ("hostname", "orizon"),
        ("system repair", "system repair:"),
        ("net status", "ipv4=yes"),
        ("net check", "network summary:"),
        ("net daily", "network daily:"),
        ("timer", "source="),
        ("ping 8.8.8.8", "reply from"),
        ("dns raw.githubusercontent.com", " -> "),
        ("net tcp raw.githubusercontent.com 443", "tcp: PASS"),
        ("net tcp raw.githubusercontent.com 443 attempts 2", "tcp retry summary: PASS"),
        ("pkg status", "Orizon package manager"),
        ("pkg audit", "pkg audit:"),
        ("pkg doctor", "summary:"),
        ("pkg cache", "pkg cache:"),
        ("pkg help", "pkg simulate"),
        ("pkg search orizon", "pkg search:"),
        ("pkg search desktop", "orizon-desktop-hypr"),
        ("pkg remote", "package remote:"),
        ("pkg remote verify", "pkg remote verify:"),
        ("pkg upgrade plan", "pkg upgrade plan:"),
        ("pkg sample", "Sample package written"),
        ("pkg sample orizon-desktop-core", "Desktop module package written"),
        ("pkg verify /workspace/packages/orizon-desktop-core.opkg", "package verify: OK"),
        ("pkg sample orizon-terminal", "Desktop module package written"),
        ("pkg sample orizon-waybar", "planned later"),
        ("pkg simulate /workspace/packages/orizon-hello.opkg", "dry-run"),
        ("pkg verify /workspace/packages/orizon-hello.opkg", "package verify: OK"),
        ("pkg install /workspace/packages/orizon-hello.opkg", "install"),
        ("desktop status", "Orizon desktop"),
        ("desktop start", "desktop session-manager: start"),
        ("desktop state", "desired-state started"),
        ("desktop rescue", "Orizon desktop rescue"),
        ("desktop reload", "desktop session-manager: reload"),
        ("desktop restart", "desktop session-manager: restart"),
        ("desktop stop", "desired-state: stopped"),
        ("desktop start", "desired-state: started"),
        ("desktop config", "Hyprland-style"),
        ("desktop config doctor", "layerrules="),
        ("desktop config apply", "layers=/system/desktop-layers.conf"),
        ("desktop config trace", "APPLY"),
        ("desktop session", "Orizon desktop session"),
        ("desktop settings", "Orizon desktop system settings"),
        ("desktop settings presets", "Orizon desktop settings presets"),
        ("desktop settings doctor", "summary:"),
        ("desktop settings paths", "Orizon desktop settings hub"),
        ("desktop settings export", "desktop settings export: written"),
        ("desktop settings sync", "desktop settings sync: synced"),
        ("desktop settings preset compact", "desktop settings preset: applied"),
        ("desktop settings set gaps-in 10", "desktop settings: updated"),
        ("desktop settings set border-size 3", "desktop settings: updated"),
        ("desktop settings set focus-ring no", "desktop settings: updated"),
        ("desktop settings set render-profile performance", "desktop settings: updated"),
        ("desktop hyprctl getoption render:focus_ring", "value: false"),
        ("desktop hyprctl getoption render:profile", "value: performance"),
        ("desktop settings repair", "desktop settings: repaired"),
        ("desktop input", "Orizon desktop input"),
        ("desktop input layout fr", "keyboard-layout: fr-azerty"),
        ("desktop keymap", "keyboard-layout: fr-azerty"),
        ("desktop input layout us", "keyboard-layout: us-qwerty"),
        ("desktop input pointer natural", "pointer-profile: natural"),
        ("desktop input focus toggle", "focus-follows-mouse:"),
        ("desktop input submap launch", "submap launch"),
        ("desktop input submap reset", "submap default"),
        ("desktop pointer", "Orizon desktop pointer"),
        ("desktop keymap", "Orizon desktop keymap"),
        ("desktop apps", "Orizon desktop apps"),
        ("desktop profiles", "Orizon desktop profiles"),
        ("desktop preset moss", "desktop preset: applied"),
        ("desktop binds", "/system/desktop-binds.conf"),
        ("desktop rules", "Orizon desktop window rules"),
        ("desktop monitors", "Orizon desktop monitor hints"),
        ("desktop runtime", "layer-rules"),
        ("desktop layers", "Orizon desktop layers"),
        ("desktop version", "Orizon desktop hyprctl version"),
        ("desktop devices", "Orizon desktop devices"),
        ("desktop systeminfo", "Orizon desktop systeminfo"),
        ("desktop backend", "current-backend: framebuffer-vm"),
        ("desktop protocol", "orizon-desktop-ipc-v0"),
        ("desktop layouts", "Orizon desktop layouts"),
        ("desktop layout-tree", "Orizon desktop layout tree"),
        ("desktop animations", "Orizon desktop animations"),
        ("desktop decorations", "Orizon desktop decorations"),
        ("desktop render", "Orizon desktop render"),
        ("desktop descriptions", "Orizon desktop hyprctl descriptions"),
        ("desktop instances", "Orizon desktop instances"),
        ("desktop submap", "submap:"),
        ("desktop configerrors", "Hyprland config errors"),
        ("desktop rollinglog", "Hyprland rolling log"),
        ("desktop focus-history", "focusHistoryID"),
        ("desktop client-model", "Orizon desktop client model"),
        ("desktop keyword general:gaps_in 9", "desktop keyword: applied"),
        ("desktop keyword layerrule blur, launcher", "/system/desktop-layers.conf"),
        ("desktop hyprctl getoption layerrule", "value: blur, launcher"),
        ("desktop hyprctl keyword input:repeat_rate 40", "/system/desktop-runtime.conf"),
        ("desktop hyprctl getoption input:repeat_rate", "value: 40"),
        ("desktop hyprctl getoption cursor:no_hardware_cursors", "value: true"),
        ("desktop hyprctl getoption render:direct_scanout", "value: false"),
        ("desktop hyprctl keyword decoration:shadow:range 22", "desktop keyword: applied"),
        ("desktop hyprctl getoption decoration:shadow:range", "value: 22"),
        ("desktop hyprctl keyword animations:tick_budget 24", "desktop keyword: applied"),
        ("desktop hyprctl getoption animations:tick_budget", "value: 24"),
        ("desktop hyprctl getoption decoration:blur:enabled", "value: false"),
        ("desktop hyprctl reload", "desktop config apply: applied"),
        ("desktop hyprctl version", "Orizon desktop hyprctl version"),
        ("desktop hyprctl systeminfo", "Orizon desktop systeminfo"),
        ("desktop hyprctl backend", "current-backend: framebuffer-vm"),
        ("desktop hyprctl protocol", "wayland: no"),
        ("desktop hyprctl clientmodel", "manual-drag=no"),
        ("desktop hyprctl activeworkspace", "active workspace:"),
        ("desktop hyprctl focushistory", "focusHistoryID"),
        ("desktop hyprctl layouts", "Orizon desktop layouts"),
        ("desktop hyprctl layouttree", "manual-drag=no"),
        ("desktop hyprctl animations", "Orizon desktop animations"),
        ("desktop hyprctl decorations", "Orizon desktop decorations"),
        ("desktop hyprctl render", "Orizon desktop render"),
        ("desktop hyprctl descriptions", "Orizon desktop hyprctl descriptions"),
        ("desktop hyprctl instances", "Orizon desktop instances"),
        ("desktop hyprctl submap", "submap:"),
        ("desktop hyprctl keymap", "Orizon desktop keymap"),
        ("desktop hyprctl cursorpos", "cursorpos:"),
        ("desktop hyprctl devices", "Orizon desktop devices"),
        ("desktop hyprctl splash", "Orizon desktop splash"),
        ("desktop hyprctl configerrors", "Hyprland config errors"),
        ("desktop hyprctl configtrace", "PREPARE"),
        ("desktop hyprctl rollinglog", "Hyprland rolling log"),
        ("desktop hyprctl getoption general:gaps_in", "value: 9"),
        ("desktop hyprctl keyword decoration:rounding 11", "desktop keyword: applied"),
        ("desktop hyprctl getoption decoration:rounding", "value: 11"),
        ("desktop hyprctl binds", "/system/desktop-binds.conf"),
        ("desktop hyprctl layers", "Orizon desktop layers"),
        ("desktop autostart", "Orizon desktop autostart"),
        ("desktop autostart terminal off", "desktop session: updated"),
        ("desktop autostart terminal on", "desktop session: updated"),
        ("desktop dispatch exec terminal", "exec orizon-terminal client spawned"),
        ("desktop dispatch exec terminal", "exec orizon-terminal client spawned"),
        ("desktop dispatch swapwithmaster", "swapwithmaster ok"),
        ("desktop dispatch focusmaster", "focusmaster ok"),
        ("desktop dispatch cyclenext", "cyclenext ok"),
        ("desktop dispatch swapnext", "swapnext ok"),
        ("desktop dispatch movefocus r", "movefocus"),
        ("desktop dispatch swapwindow l", "swapwindow ok"),
        ("desktop dispatch togglesplit", "togglesplit split="),
        ("desktop dispatch layoutmsg splitratio 60", "splitratio 60"),
        ("desktop dispatch layoutmsg splitratio +5", "splitratio 65"),
        ("desktop dispatch layoutmsg masterratio 65", "masterratio 65"),
        ("desktop dispatch layoutmsg mfact -5", "masterratio 60"),
        ("desktop dispatch layoutmsg orientationleft", "split=vertical"),
        ("desktop dispatch layoutmsg orientationtop", "split=horizontal"),
        ("desktop dispatch resizeactive 5 0", "resizeactive split="),
        ("desktop dispatch submap resize", "submap resize"),
        ("desktop hyprctl submap", "submap: resize"),
        ("desktop hyprctl submap reset", "submap default"),
        ("desktop dispatch fullscreen", "fullscreen on"),
        ("desktop dispatch pseudo", "pseudo on"),
        ("desktop dispatch pin", "pin on"),
        ("desktop hyprctl clients", "Orizon desktop windows"),
        ("desktop hyprctl activewindow", "activewindow:"),
        ("desktop hyprctl monitors", "Monitor 0"),
        ("desktop windows", "Orizon desktop windows"),
        ("desktop clients", "focusHistoryID"),
        ("desktop activewindow", "focusHistoryID"),
        ("desktop workspace", "Orizon desktop workspaces"),
        ("desktop dispatch movetoworkspace 2", "moved active to workspace 2"),
        ("desktop dispatch workspace 2", "workspace 2"),
        ("desktop dispatch workspace previous", "workspace 1"),
        ("desktop dispatch workspace +1", "workspace 2"),
        ("desktop workspace 2", "workspace 2 active"),
        ("desktop dispatch movetoworkspacesilent empty", "silently moved active"),
        ("desktop dispatch workspace next", "workspace 3"),
        ("desktop dispatch workspace empty", "workspace 4"),
        ("desktop workspace empty", "desktop dispatch: workspace"),
        ("desktop dispatch movefocus next", "movefocus"),
        ("desktop shortcuts", "F1"),
        ("desktop keymap", "F9 resize"),
        ("desktop doctor", "summary:"),
        ("desktop logs", "desktop log:"),
        ("desktop theme moss", "desktop session: updated"),
        ("desktop wallpaper dawn", "desktop session: updated"),
        ("desktop layout master", "desktop session: updated"),
        ("desktop focus toggle", "desktop session: updated"),
        ("desktop bar toggle", "desktop session: updated"),
        ("desktop apply", "session reloaded"),
        ("desktop launcher show", "launcher open"),
        ("desktop app settings", "class: orizon-settings"),
        ("desktop apps launcher", "surface: overlay"),
        ("desktop launch terminal", "exec orizon-terminal client spawned"),
        ("desktop launch settings", "exec orizon-settings client spawned"),
        ("desktop launch logs", "exec orizon-logs client spawned"),
        ("desktop launch launcher", "orizon-launcher overlay toggled"),
        ("desktop dispatch exec orizon-packages", "exec orizon-packages client spawned"),
        ("desktop dispatch exec update", "exec orizon-update-viewer client spawned"),
        ("desktop hyprctl clients", "orizon-update-viewer"),
        ("desktop modules", "orizon-waybar"),
        ("pkg info orizon-terminal", "sample pkg sample orizon-terminal"),
        ("pkg search orizon-terminal", "pkg install orizon-terminal"),
        ("pkg search waybar", "orizon-waybar"),
        ("pkg info orizon-desktop-hypr", "state available optional"),
        ("desktop package", "orizon-desktop-hypr.opkg"),
        ("pkg simulate /workspace/packages/orizon-desktop-hypr.opkg", "dry-run"),
        ("pkg verify /workspace/packages/orizon-desktop-hypr.opkg", "package verify: OK"),
        ("pkg install orizon-desktop-hypr", "pkg"),
        ("update status", "update:"),
        ("bootguard", "Orizon boot guard"),
        ("bootguard recover", "bootguard recover:"),
        ("rollback-status", "rollback status:"),
        ("selftest", "summary:"),
        ("storage", "selected="),
        ("storage diag", "nvme: controllers="),
        ("storage vmcheck", "storage vmcheck:"),
        ("storage vmcheck", "summary:"),
        ("persist status", "persistence:"),
        ("persist slots", "persistence slots:"),
        ("persist save", "persistence save: ok"),
        ("persist restore previous", "persistence restore: PASS"),
        ("disk identify", "disk identify:"),
        ("disk read-test", "mode=read-only"),
        ("disk read-test last", "mode=read-only"),
        ("gpt scan", "GPT partitions"),
        ("logs storage", "storage log:"),
        ("logs network", "ipv4:"),
        ("hw next", "Hardware return plan"),
        ("report next", "diagnostic-only"),
        ("report save", "hardware-report.txt"),
        ("cat /workspace/hardware-report.txt", "System State"),
        ("install-plan", "install-plan: wrote"),
        ("cat /workspace/.orizon/install-report.txt", "write-scope: none"),
        ("rollback", "rollback"),
        ("hostkey", "fingerprint-sha256"),
    ]
    if include_update:
        commands.append(("update", "update:"))
    command_lines = "\n".join(f"{cmd}\t{needle}" for cmd, needle in commands)
    remote_script = f"""#!/usr/bin/env bash
set -u
ASKPASS=/tmp/orizon_matrix_askpass.sh
PASSFILE=/tmp/orizon_matrix_password.txt
KNOWN=/tmp/orizon_matrix_known_hosts
OUT=/tmp/orizon_matrix_output.txt
printf '%s' '{encoded}' | base64 -d > "$PASSFILE"
cat > "$ASKPASS" <<'EOS'
#!/bin/sh
cat /tmp/orizon_matrix_password.txt
EOS
chmod +x "$ASKPASS"
rm -f "$KNOWN"
while IFS=$'\\t' read -r cmd needle; do
  [ -z "$cmd" ] && continue
  echo "--- $cmd ---"
  attempt=1
  while :; do
    DISPLAY=none SSH_ASKPASS="$ASKPASS" SSH_ASKPASS_REQUIRE=force timeout {timeout}s setsid ssh -n \\
      -oNumberOfPasswordPrompts=1 \\
      -oPreferredAuthentications=password \\
      -oPubkeyAuthentication=no \\
      -oStrictHostKeyChecking=no \\
      -oUserKnownHostsFile="$KNOWN" \\
      -oConnectTimeout=5 \\
      orizon@{ip} "$cmd" > "$OUT" 2>&1
    rc=$?
    cat "$OUT"
    echo "rc=$rc"
    if [ "$cmd" = "persist save" ] && [ "$rc" -eq 0 ]; then
      if grep -qi "persistence save: ok" "$OUT" || grep -qi "Orizon data persistence full" "$OUT"; then
        break
      fi
    fi
    if [ "$rc" -eq 0 ] && grep -qi "$needle" "$OUT"; then
      if [ "$cmd" = "disk read-test last" ] && grep -q "lba=0 " "$OUT"; then
        echo "last-sector read-test used LBA 0"
        rm -f "$ASKPASS" "$PASSFILE" "$OUT"
        exit 1
      fi
      break
    fi
    if [ "$attempt" -ge 6 ]; then
      [ "$rc" -eq 0 ] || echo "ssh command failed after $attempt attempts"
      grep -qi "$needle" "$OUT" || echo "missing expected output: $needle"
      rm -f "$ASKPASS" "$PASSFILE" "$OUT"
      exit 1
    fi
    attempt=$((attempt + 1))
    echo "retrying $cmd ($attempt/6)..."
    sleep 2
  done
done <<'EOC'
{command_lines}
EOC
rm -f "$ASKPASS" "$PASSFILE" "$OUT"
"""
    remote = "/tmp/orizon_matrix_ssh_checks.sh"
    sftp = client.open_sftp()
    with sftp.open(remote, "w") as handle:
        handle.write(remote_script)
    run_command(client, f"chmod +x {remote}")
    return run_command(client, f"bash {remote}", check=True)


def run_ssh_one(client, ip: str, password: str, command: str, needle: str, timeout: int) -> str:
    encoded = base64.b64encode(password.encode("utf-8")).decode("ascii")
    remote_script = f"""#!/usr/bin/env bash
set -u
ASKPASS=/tmp/orizon_one_askpass.sh
PASSFILE=/tmp/orizon_one_password.txt
KNOWN=/tmp/orizon_one_known_hosts
OUT=/tmp/orizon_one_output.txt
printf '%s' '{encoded}' | base64 -d > "$PASSFILE"
cat > "$ASKPASS" <<'EOS'
#!/bin/sh
cat /tmp/orizon_one_password.txt
EOS
chmod +x "$ASKPASS"
rm -f "$KNOWN"
DISPLAY=none SSH_ASKPASS="$ASKPASS" SSH_ASKPASS_REQUIRE=force timeout {timeout}s setsid ssh -n \\
  -oNumberOfPasswordPrompts=1 \\
  -oPreferredAuthentications=password \\
  -oPubkeyAuthentication=no \\
  -oStrictHostKeyChecking=no \\
  -oUserKnownHostsFile="$KNOWN" \\
  -oConnectTimeout=5 \\
  orizon@{ip} "{command}" > "$OUT" 2>&1
rc=$?
cat "$OUT"
if [ "$rc" -ne 0 ] || ! grep -qi "{needle}" "$OUT"; then
  echo "single ssh command failed rc=$rc expected={needle}" >&2
  rm -f "$ASKPASS" "$PASSFILE" "$OUT"
  exit 1
fi
rm -f "$ASKPASS" "$PASSFILE" "$OUT"
"""
    remote = "/tmp/orizon_matrix_ssh_one.sh"
    sftp = client.open_sftp()
    with sftp.open(remote, "w") as handle:
        handle.write(remote_script)
    run_command(client, f"chmod +x {remote}")
    return run_command(client, f"bash {remote}", check=True)


def run_lifecycle_checks(
    client,
    sudo_password: str,
    cfg: dict,
    ip: str,
    password: str,
    *,
    boot_timeout: int,
    ssh_timeout: int,
) -> str:
    vm_name = cfg["name"]
    mac = domif_mac(client, sudo_password, vm_name)
    lines: list[str] = []

    ok, size = capture_framebuffer_smoke(client, sudo_password, vm_name)
    lines.append(f"framebuffer screenshot: {'ok' if ok else 'warn'} bytes={size}")

    lines.append("--- ssh reboot ---")
    lines.append(run_ssh_one(client, ip, password, "reboot", "scheduled", ssh_timeout))
    time.sleep(8)
    state = wait_domstate(client, sudo_password, vm_name, {"running"}, boot_timeout)
    if state != "running":
        raise RuntimeError(f"VM did not return to running after reboot; state={state}")
    boot_and_start_ssh(client, sudo_password, vm_name, password)
    reboot_ip = find_guest_ip(
        client, sudo_password, cfg, vm_name, mac, boot_timeout
    )
    if not reboot_ip:
        raise RuntimeError("guest IP unavailable after reboot")
    lines.append(f"reboot ssh ip={reboot_ip}")
    lines.append(
        run_ssh_one(client, reboot_ip, password, "selftest", "summary:", ssh_timeout)
    )

    lines.append("--- ssh shutdown ---")
    lines.append(
        run_ssh_one(client, reboot_ip, password, "shutdown", "scheduled", ssh_timeout)
    )
    state = wait_domstate(
        client, sudo_password, vm_name, {"shut off", "shutoff"}, boot_timeout
    )
    if state not in {"shut off", "shutoff"}:
        raise RuntimeError(f"VM did not shut off cleanly; state={state}")
    lines.append(f"shutdown state={state}")
    return "\n".join(lines)


def parse_cases(raw: str) -> list[str]:
    if raw == "all":
        return list(MATRIX_CASES)
    return [part.strip() for part in raw.split(",") if part.strip()]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Orizon VM boot/network/SSH smoke matrix on ZimaOS.")
    parser.add_argument("--env-file", default="config/hosts/zimaos.local.env")
    parser.add_argument("--vm-config", default="config/vm/orizon-dev.example.json")
    parser.add_argument(
        "--remote-source-dir",
        default="/DATA/orizon-build/x86_64/workspace/orizon-os-x86_64/iso_root",
        help="Remote boot tree produced by build_x86_64_on_zimaos.py.",
    )
    parser.add_argument(
        "--cases",
        default="nat-e1000e,nat-virtio,nat-rtl8139",
        help="Comma-separated case names or 'all'. Bridge cases need host-reachable guest IPs.",
    )
    parser.add_argument("--password", default="testtest")
    parser.add_argument("--boot-timeout", type=int, default=60)
    parser.add_argument("--ssh-timeout", type=int, default=40)
    parser.add_argument("--include-update", action="store_true")
    parser.add_argument(
        "--disk-bus",
        choices=("sata", "virtio"),
        default="sata",
        help="Disk bus profile for storage smoke tests. Default keeps the AHCI/SATA path.",
    )
    parser.add_argument(
        "--include-lifecycle",
        action="store_true",
        help="Also verify framebuffer screenshot, SSH reboot, post-reboot SSH, and SSH shutdown.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("artifacts/vm-matrix"),
        help="Directory for per-case logs and matrix-summary.{json,md}. Use '-' to disable.",
    )
    args = parser.parse_args()
    output_dir = args.output_dir
    if output_dir and str(output_dir) == "-":
        output_dir = None
    elif output_dir and not output_dir.is_absolute():
        output_dir = Path.cwd() / output_dir

    env_config = parse_env_file(Path(args.env_file))
    base_config = load_json(Path(args.vm_config))
    sudo_password = env_config.get("ZIMAOS_SUDO_PASSWORD", read_required(env_config, "ZIMAOS_PASSWORD"))
    cases = parse_cases(args.cases)

    client = connect_ssh(env_config)
    try:
        source_ok = run_sudo_command(
            client,
            sudo_password,
            f"test -d {args.remote_source_dir} && echo ok || echo missing",
            check=False,
        ).strip()
        if source_ok != "ok":
            raise RuntimeError(
                f"Remote source tree not found: {args.remote_source_dir}. "
                "Run scripts/orizon/build_x86_64_on_zimaos.py first."
            )

        results: list[dict[str, str]] = []
        for case_name in cases:
            if case_name not in MATRIX_CASES:
                raise ValueError(f"Unknown matrix case: {case_name}")
            cfg = apply_storage_profile(
                matrix_config(base_config, case_name, MATRIX_CASES[case_name]),
                args.disk_bus,
            )
            case_log: list[str] = []
            print(
                f"=== {case_name} ({cfg['network_model']} / {cfg['network_name'] or 'bridge'} / disk={args.disk_bus}) ==="
            )
            case_log.append(
                f"case={case_name} network={cfg['network_model']}/{cfg['network_name'] or 'bridge'} disk={cfg.get('disk_bus', 'sata')}"
            )
            try:
                define_vm(client, sudo_password, cfg)
                state, _vnc = deploy_remote_tree(
                    client=client,
                    sudo_password=sudo_password,
                    vm_name=cfg["name"],
                    remote_disk=cfg["remote_disk_path"],
                    disk_size=cfg.get("disk_size", "8G"),
                    remote_tree_dir=args.remote_source_dir,
                    start_vm=True,
                )
                case_log.append(f"domstate={state.strip()}")
                if state.strip().lower() != "running":
                    detail = f"state={state.strip()}"
                    results.append(matrix_result(case_name, STATUS_FAIL, detail, cfg))
                    case_log.append(f"{STATUS_FAIL}: {detail}")
                    continue
                boot_and_start_ssh(client, sudo_password, cfg["name"], args.password)
                mac = domif_mac(client, sudo_password, cfg["name"])
                case_log.append(f"mac={mac}")
                ip = find_guest_ip(
                    client,
                    sudo_password,
                    cfg,
                    cfg["name"],
                    mac,
                    args.boot_timeout,
                )
                if not ip:
                    if cfg["network_name"]:
                        detail = "guest IP unavailable from libvirt lease/arp/neighbor probes"
                        results.append(matrix_result(case_name, STATUS_FAIL, detail, cfg))
                        case_log.append(f"{STATUS_FAIL}: {detail}")
                        print(f"{STATUS_FAIL}: {detail}")
                        continue
                    ok, size = capture_framebuffer_smoke(client, sudo_password, cfg["name"])
                    status = STATUS_WARN if ok else STATUS_FAIL
                    detail = (
                        f"framebuffer={'ok' if ok else 'missing'} bytes={size}; "
                        "ssh skipped because bridge guest IP was not discoverable "
                        "from virsh arp or host neighbor tables"
                    )
                    results.append(matrix_result(case_name, status, detail, cfg))
                    case_log.append(f"{status}: {detail}")
                    print(f"{status}: {detail}")
                    if args.include_lifecycle:
                        run_sudo_command(
                            client,
                            sudo_password,
                            f"virsh destroy {cfg['name']} || true",
                            check=False,
                        )
                    continue
                case_log.append(f"guest-ip={ip}")
                configure_ssh_console(
                    client,
                    sudo_password,
                    cfg["name"],
                    args.password,
                    rounds=1,
                )
                output = run_ssh_checks(
                    client,
                    ip,
                    args.password,
                    timeout=args.ssh_timeout,
                    include_update=args.include_update,
                )
                case_log.append(output)
                print_console(output)
                if args.include_lifecycle:
                    lifecycle = run_lifecycle_checks(
                        client,
                        sudo_password,
                        cfg,
                        ip,
                        args.password,
                        boot_timeout=args.boot_timeout,
                        ssh_timeout=args.ssh_timeout,
                    )
                    case_log.append(lifecycle)
                    print_console(lifecycle)
                results.append(
                    matrix_result(
                        case_name,
                        STATUS_PASS,
                        "ssh diagnostics passed",
                        cfg,
                        ip,
                    )
                )
            except Exception as exc:
                detail = f"{type(exc).__name__}: {exc}"
                results.append(matrix_result(case_name, STATUS_FAIL, detail, cfg))
                case_log.append(f"{STATUS_FAIL}: {detail}")
                print(f"{STATUS_FAIL}: {case_name}: {detail}")
            finally:
                write_case_log(output_dir, case_name, case_log)

        print("=== matrix summary ===")
        for row in results:
            print(f"{row['case']}: {row['status']} {row['detail']}")
        write_matrix_report(output_dir, results)
        if output_dir:
            print(f"matrix reports: {output_dir}")
        return 1 if any(row["status"] == STATUS_FAIL for row in results) else 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
