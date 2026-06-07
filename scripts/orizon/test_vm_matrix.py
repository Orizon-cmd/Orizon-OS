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


def send_console_key(client, sudo_password: str, vm_name: str, key: str) -> None:
    run_sudo_command(
        client,
        sudo_password,
        f"virsh send-key --holdtime 80 {vm_name} {key}",
        check=False,
    )


def confirm_boot_menu(client, sudo_password: str, vm_name: str) -> None:
    # The VM console input is blind. Fresh OVMF NVRAM can stop at the language
    # chooser before Limine; Limine itself can also wait at its menu. Walk the
    # safe firmware path (close language picker, Continue), then confirm Limine
    # boot so later Orizon commands do not get typed into firmware/edit screens.
    for key in ("KEY_ESC", "KEY_DOWN", "KEY_DOWN", "KEY_DOWN", "KEY_DOWN", "KEY_ENTER"):
        send_console_key(client, sudo_password, vm_name, key)
        time.sleep(0.35)
    # If the previous sequence came from OVMF's first-boot menu, Limine appears
    # only after a firmware transition. On this ZimaOS/libvirt firmware path,
    # Limine receives letters reliably but Enter can leave the countdown stuck.
    # Open the selected entry and use Limine's editor-level F10 boot action
    # without changing the config.
    time.sleep(10)
    send_console_key(client, sudo_password, vm_name, "KEY_E")
    time.sleep(0.5)
    send_console_key(client, sudo_password, vm_name, "KEY_F10")
    time.sleep(20)


def configure_ssh_console(
    client,
    sudo_password: str,
    vm_name: str,
    password: str,
    *,
    rounds: int = 1,
    initial_wait: int = 0,
    boot_confirm: bool = False,
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
    if boot_confirm:
        confirm_boot_menu(client, sudo_password, vm_name)
    if initial_wait:
        time.sleep(initial_wait)
    for round_index in range(rounds):
        for cmd in commands:
            send_console_text(client, sudo_password, vm_name, cmd + "\n")
            time.sleep(1.5)
        if round_index + 1 < rounds:
            time.sleep(5)


def boot_and_start_ssh(client, sudo_password: str, vm_name: str, password: str) -> None:
    # Do not send text while Limine is still active: a blind "keyboard us" can
    # become an edit command in the bootloader and leave the entry INVALID.
    # Use the editor-level F10 boot path because it is deterministic on the
    # ZimaOS/OVMF/Limine console path used by the VM smoke tests.
    configure_ssh_console(
        client,
        sudo_password,
        vm_name,
        password,
        rounds=2,
        initial_wait=100,
        boot_confirm=True,
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
        ("system services", "desktop-restore"),
        ("system logs", "Orizon system logs"),
        ("system logs", "desktop-restore:"),
        ("system doctor", "summary:"),
        ("system init", "system init:"),
        ("system init", "desktop-restore="),
        ("system init", "fallback-action:"),
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
        ("pkg sample orizon-desktop-core", "split-plan: core-first"),
        ("pkg verify /workspace/packages/orizon-desktop-core.opkg", "package verify: OK"),
        ("cat /workspace/packages/orizon-desktop-core.opkg", "split-version 2"),
        ("cat /workspace/packages/orizon-desktop-core.opkg", "activation policy-session-settings"),
        ("pkg sample orizon-terminal", "Desktop module package written"),
        ("pkg sample orizon-terminal", "dependency: orizon-desktop-core"),
        ("pkg sample orizon-waybar", "planned later"),
        ("pkg simulate /workspace/packages/orizon-hello.opkg", "dry-run"),
        ("pkg verify /workspace/packages/orizon-hello.opkg", "package verify: OK"),
        ("pkg install /workspace/packages/orizon-hello.opkg", "install"),
        ("desktop status", "Orizon desktop"),
        ("desktop start", "desktop session-manager: start"),
        ("desktop state", "desired-state started"),
        ("desktop state", "recommended-action:"),
        ("desktop state", "file-audit:"),
        ("desktop rescue", "Orizon desktop rescue"),
        ("desktop rescue", "rescue-recommended:"),
        ("desktop reload", "desktop session-manager: reload"),
        ("desktop restart", "desktop session-manager: restart"),
        ("desktop stop", "desired-state: stopped"),
        ("desktop start", "desired-state: started"),
        ("desktop config", "Hyprland-style"),
        ("desktop config doctor", "layerrules="),
        ("desktop config doctor", "source-resolve: loaded=1"),
        ("desktop config apply", "layers=/system/desktop-layers.conf"),
        ("desktop config apply", "source-resolve: loaded=1"),
        ("desktop config trace", "APPLY"),
        ("desktop hyprctl configtrace", "SOURCE path=/home/orizon/.config/hypr/orizon-local.conf status=LOADED"),
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
        ("desktop keymap", "active-submap-role:"),
        ("desktop keymap", "submap-policy: sticky-until-reset=yes"),
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
        ("desktop backend", "contracts: surface=single-framebuffer-surface-v0"),
        ("desktop backend", "capabilities: put_pixel"),
        ("desktop backend", "future-contract: future backend must implement compositor-backend-v0"),
        ("desktop protocol", "orizon-desktop-ipc-v0"),
        ("desktop protocol", "protocol-api: desktop-protocol-v0"),
        ("desktop protocol", "client-contract: internal-tiled-client-v0"),
        ("desktop protocol", "limits: no-wayland-wire"),
        ("desktop protocol", "internal-protocol-state:"),
        ("desktop protocol", "internal-client-contract: internal-tiled-client-v0"),
        ("desktop architecture", "orizon-compositor-api-v0"),
        ("desktop architecture", "backend-api: compositor-backend-v0"),
        ("desktop architecture", "backend-contract: surface=single-framebuffer-surface-v0"),
        ("desktop architecture", "backend-capabilities: put_pixel"),
        ("desktop architecture", "protocol-client-contract: internal-tiled-client-v0"),
        ("desktop architecture", "backend-future: wayland-wlroots prepared"),
        ("desktop layouts", "Orizon desktop layouts"),
        ("desktop layout-tree", "Orizon desktop layout tree"),
        ("desktop animations", "Orizon desktop animations"),
        ("desktop animations", "frame-budget:"),
        ("desktop animations", "client-animations:"),
        ("desktop decorations", "Orizon desktop decorations"),
        ("desktop decorations", "gaps: inner="),
        ("desktop decorations", "accessibility:"),
        ("desktop render", "Orizon desktop render"),
        ("desktop render", "surface:"),
        ("desktop render", "scale-policy:"),
        ("desktop render", "frame-budget:"),
        ("desktop render", "clients: workspace="),
        ("desktop descriptions", "Orizon desktop hyprctl descriptions"),
        ("desktop instances", "Orizon desktop instances"),
        ("desktop submap", "submap:"),
        ("desktop submap", "active-role:"),
        ("desktop submap", "sticky-until-reset: yes"),
        ("desktop submap", "manual-window-drag: no"),
        ("desktop configerrors", "Hyprland config errors"),
        ("desktop configerrors", "bind-detail:"),
        ("desktop configerrors", "plain="),
        ("desktop configerrors", "composite="),
        ("desktop configerrors", "source-detail:"),
        ("desktop configerrors", "source-file:"),
        ("desktop rollinglog", "Hyprland rolling log"),
        ("desktop focus-history", "focusHistoryID"),
        ("desktop client-model", "Orizon desktop client model"),
        ("desktop rule-matches", "safe-actions=tile"),
        ("desktop keyword general:gaps_in 9", "desktop keyword: applied"),
        ("desktop keyword layerrule blur, launcher", "/system/desktop-layers.conf"),
        ("desktop hyprctl getoption layerrule", "value: blur, launcher"),
        ("desktop hyprctl keyword input:repeat_rate 40", "/system/desktop-runtime.conf"),
        ("desktop hyprctl keyword input:repeat_rate 40", "route: runtime-hint"),
        ("desktop hyprctl keyword input:repeat_rate 40", "effect: prepared-runtime-hint"),
        ("desktop hyprctl keyword input:repeat_rate 40", "reload-recommended: yes"),
        ("desktop hyprctl getoption input:repeat_rate", "value: 40"),
        ("desktop keyword binds:workspace_center_on 1", "/system/desktop-runtime.conf"),
        ("desktop hyprctl getoption binds:workspace_center_on", "value: 1"),
        ("desktop hyprctl keyword dwindle:pseudotile false", "/system/desktop-runtime.conf"),
        ("desktop hyprctl getoption dwindle:pseudotile", "value: false"),
        ("desktop hyprctl keyword master:mfact 0.60", "/system/desktop-runtime.conf"),
        ("desktop hyprctl getoption master:mfact", "value: 0.60"),
        ("desktop hyprctl getoption master:new_status", "value: master"),
        ("desktop hyprctl keyword gestures:workspace_swipe false", "/system/desktop-runtime.conf"),
        ("desktop hyprctl getoption gestures:workspace_swipe", "value: false"),
        ("desktop hyprctl keyword xwayland:force_zero_scaling false", "/system/desktop-runtime.conf"),
        ("desktop hyprctl getoption xwayland:force_zero_scaling", "value: false"),
        ("desktop hyprctl keyword unbind SUPER,Q", "/system/desktop-binds.conf"),
        ("desktop hyprctl getoption unbind", "value: SUPER,Q"),
        ("desktop hyprctl getoption env", "value: ORIZON_DESKTOP_SOURCE,1"),
        ("desktop hyprctl getoption env", "entry-count:"),
        ("desktop hyprctl getoption env", "entry[0]: ORIZON_DESKTOP_SOURCE,1"),
        ("desktop hyprctl getoption workspace", "value: 1, default:true"),
        ("desktop hyprctl getoption cursor:no_hardware_cursors", "value: true"),
        ("desktop hyprctl getoption render:direct_scanout", "value: false"),
        ("desktop hyprctl keyword decoration:shadow:range 22", "desktop keyword: applied"),
        ("desktop hyprctl getoption decoration:shadow:range", "value: 22"),
        ("desktop hyprctl keyword animations:tick_budget 24", "desktop keyword: applied"),
        ("desktop hyprctl getoption animations:tick_budget", "value: 24"),
        ("desktop hyprctl getoption decoration:blur:enabled", "value: false"),
        ("desktop hyprctl reload", "desktop config apply: applied"),
        ("desktop hyprctl -j reload", '"command":"reload"'),
        ("desktop hyprctl -j reload", '"ok":true'),
        ("desktop hyprctl -j reload", '"manualDrag":false'),
        ("desktop hyprctl -j dispatch workspace active", '"command":"dispatch"'),
        ("desktop hyprctl -j dispatch workspace active", '"dispatcher":"workspace"'),
        ("desktop hyprctl -j dispatch workspace active", '"ok":true'),
        ("desktop hyprctl -j dispatch workspace active", '"error":null'),
        ("desktop hyprctl -j dispatch workspace active", '"hint":"dispatch accepted"'),
        ("desktop hyprctl -j dispatch workspace active", '"lastDispatch":{'),
        ("desktop hyprctl -j dispatch workspace active", '"manualDrag":false'),
        ("desktop hyprctl -j dispatch layoutmsg splitratio reset", '"dispatcher":"layoutmsg"'),
        ("desktop hyprctl -j dispatch layoutmsg splitratio reset", '"args":"splitratio reset"'),
        ("desktop hyprctl -j dispatch submap move", '"submap":"move"'),
        ("desktop hyprctl -j dispatch submap reset", '"submap":"default"'),
        ("desktop hyprctl version", "Orizon desktop hyprctl version"),
        ("desktop hyprctl systeminfo", "Orizon desktop systeminfo"),
        ("desktop hyprctl backend", "current-backend: framebuffer-vm"),
        ("desktop hyprctl protocol", "wayland: no"),
        ("desktop hyprctl architecture", "backend-future: wayland-wlroots prepared"),
        ("desktop hyprctl -j version", '"command":"version"'),
        ("desktop hyprctl -j version", '"upstreamHyprland":false'),
        ("desktop hyprctl -j version", '"manualDrag":false'),
        ("desktop hyprctl -j systeminfo", '"command":"systeminfo"'),
        ("desktop hyprctl -j systeminfo", '"protocols":{"wayland":false'),
        ("desktop hyprctl -j systeminfo", '"manualDrag":false'),
        ("desktop hyprctl -j backend", '"command":"backend"'),
        ("desktop hyprctl -j backend", '"surfaceContract":"single-framebuffer-surface-v0"'),
        ("desktop hyprctl -j backend", '"backendLimits":"no-wayland'),
        ("desktop hyprctl -j backend", '"futureBackend":"wayland-wlroots"'),
        ("desktop hyprctl -j backend", '"hardwareValidation":false'),
        ("desktop hyprctl -j protocol", '"command":"protocol"'),
        ("desktop hyprctl -j protocol", '"protocol":"orizon-desktop-ipc-v0"'),
        ("desktop hyprctl -j protocol", '"protocolApi":"desktop-protocol-v0"'),
        ("desktop hyprctl -j protocol", '"clientContract":"internal-tiled-client-v0'),
        ("desktop hyprctl -j protocol", '"protocolLimits":"no-wayland-wire'),
        ("desktop hyprctl -j protocol", '"runtime"'),
        ("desktop hyprctl -j protocol", '"lastMessage"'),
        ("desktop hyprctl -j protocol", '"wayland":false'),
        ("desktop hyprctl -j architecture", '"command":"architecture"'),
        ("desktop hyprctl -j architecture", '"backendApi"'),
        ("desktop hyprctl -j architecture", '"surfaceContract":"single-framebuffer-surface-v0"'),
        ("desktop hyprctl -j architecture", '"clientContract":"internal-tiled-client-v0'),
        (
            "desktop hyprctl -j architecture",
            '"source":"kernel/gui/compositor_backend.c"',
        ),
        ("desktop hyprctl -j architecture", '"api":"orizon-compositor-api-v0"'),
        ("desktop hyprctl -j architecture", '"futureImplemented":false'),
        ("desktop hyprctl -j architecture", '"wayland":false'),
        ("desktop hyprctl -j architecture", '"hardwareValidation":false'),
        ("desktop hyprctl clientmodel", "manual-drag=no"),
        ("desktop hyprctl -j clientmodel", '"manualDrag":false'),
        ("desktop hyprctl -j clientmodel", '"summary":{'),
        ("desktop hyprctl -j clientmodel", '"workspaces":['),
        ("desktop hyprctl rulematches", "safe-actions=tile"),
        ("desktop hyprctl -j rulematches", '"safeAction":'),
        ("desktop hyprctl -j rulematches", '"summary":{'),
        ("desktop hyprctl -j rulematches", '"selectors":"class/title/app/tag'),
        ("desktop hyprctl activeworkspace", "active workspace:"),
        ("desktop hyprctl -j clients", '"hyprlandStyleFacade":true'),
        ("desktop hyprctl -j activewindow", '"fullscreenClient":'),
        ("desktop hyprctl -j workspaces", '"windows":'),
        ("desktop hyprctl -j activeworkspace", '"active":true'),
        ("desktop hyprctl -j focushistory", '"history":['),
        ("desktop hyprctl -j focushistory", '"scope":'),
        ("desktop hyprctl -j workspacestack", '"pinnedAware":true'),
        ("desktop hyprctl -j workspacestack", '"stack":['),
        ("desktop hyprctl -j workspacestack", '"manualDrag":false'),
        ("desktop focus-state", "Orizon desktop focus state"),
        ("desktop focus-state", "last-dispatch:"),
        ("desktop hyprctl focusstate", "master:"),
        ("desktop hyprctl -j focusstate", '"command":"focusstate"'),
        ("desktop hyprctl -j focusstate", '"lastDispatch":'),
        ("desktop hyprctl -j focusstate", '"manualDrag":false'),
        ("desktop hyprctl -j layoutstate", '"model":"per-workspace tiling layout state"'),
        ("desktop hyprctl -j layoutstate", '"workspaces":['),
        ("desktop hyprctl -j layoutstate", '"lastDispatch":{'),
        ("desktop hyprctl -j layoutstate", '"manualDrag":false'),
        ("desktop hyprctl -j layouttree", '"model":"active workspace tiling tree"'),
        ("desktop hyprctl -j layouttree", '"nodes":['),
        ("desktop hyprctl -j layouttree", '"lastDispatch":{'),
        ("desktop hyprctl -j layouttree", '"floatingSceneGraph":false'),
        ("desktop hyprctl -j monitors", '"command":"monitors"'),
        ("desktop hyprctl -j monitors", '"singleFramebuffer":true'),
        ("desktop hyprctl -j monitors", '"manualDrag":false'),
        ("desktop hyprctl -j devices", '"command":"devices"'),
        ("desktop hyprctl -j devices", '"libinput":false'),
        ("desktop hyprctl -j devices", '"manualWindowDrag":false'),
        ("desktop hyprctl -j keymap", '"command":"keymap"'),
        ("desktop hyprctl -j keymap", '"activeSubmap":'),
        ("desktop hyprctl -j keymap", '"activeSubmapRole":'),
        ("desktop hyprctl -j keymap", '"stickySubmaps":true'),
        ("desktop hyprctl -j keymap", '"manualDrag":false'),
        ("desktop hyprctl -j cursorpos", '"command":"cursorpos"'),
        ("desktop hyprctl -j cursorpos", '"manualDrag":false'),
        ("desktop hyprctl -j animations", '"command":"animations"'),
        ("desktop hyprctl -j animations", '"workspaceTransition":true'),
        ("desktop hyprctl -j animations", '"frameBudget":{'),
        ("desktop hyprctl -j animations", '"clientAnimations":{'),
        ("desktop hyprctl -j animations", '"manualDrag":false'),
        ("desktop hyprctl -j decorations", '"command":"decorations"'),
        ("desktop hyprctl -j decorations", '"focusRing":{'),
        ("desktop hyprctl -j decorations", '"gaps":{'),
        ("desktop hyprctl -j decorations", '"accessibility":{'),
        ("desktop hyprctl -j decorations", '"manualDrag":false'),
        ("desktop hyprctl -j render", '"command":"render"'),
        ("desktop hyprctl -j render", '"renderer":"software"'),
        ("desktop hyprctl -j render", '"surface":{'),
        ("desktop hyprctl -j render", '"scalePolicy":{'),
        ("desktop hyprctl -j render", '"frameBudget":{'),
        ("desktop hyprctl -j render", '"clientAnimations":{'),
        ("desktop hyprctl -j render", '"manualDrag":false'),
        ("desktop hyprctl -j layouts", '"command":"layouts"'),
        ("desktop hyprctl -j layouts", '"activeLayout":'),
        ("desktop hyprctl -j layouts", '"manualDrag":false'),
        ("desktop hyprctl -j descriptions", '"command":"descriptions"'),
        ("desktop hyprctl -j descriptions", '"jsonCommands":['),
        ("desktop hyprctl -j descriptions", '"floatingDesktop":false'),
        ("desktop hyprctl -j instances", '"command":"instances"'),
        ("desktop hyprctl -j instances", '"signature":"orizon-framebuffer-main"'),
        ("desktop hyprctl -j instances", '"manualDrag":false'),
        ("desktop hyprctl -j modules", '"command":"modules"'),
        ("desktop hyprctl -j modules", '"modularPackaging":true'),
        ("desktop hyprctl -j modules", '"splitPlan":{'),
        ("desktop hyprctl -j modules", '"dependencyGraph":['),
        ("desktop hyprctl -j modules", '"boundary":"policy-session-settings-logs"'),
        ("desktop hyprctl -j modules", '"name":"orizon-waybar"'),
        ("desktop hyprctl -j modules", '"plannedOnly":true'),
        ("desktop hyprctl -j modules", '"manualDrag":false'),
        ("desktop hyprctl -j shortcuts", '"command":"shortcuts"'),
        ("desktop hyprctl -j shortcuts", '"keyboardOnly":true'),
        ("desktop hyprctl -j shortcuts", '"bindmPreparedOnly":true'),
        ("desktop hyprctl -j shortcuts", '"manualDragFromBindm":false'),
        ("desktop hyprctl -j shortcuts", '"bindFlags":{'),
        ("desktop hyprctl -j shortcuts", '"submapPolicy":{'),
        ("desktop hyprctl -j shortcuts", '"stickySubmaps":true'),
        ("desktop hyprctl -j shortcuts", '"manualDrag":false'),
        ("desktop hyprctl -j autostart", '"command":"autostart"'),
        ("desktop hyprctl -j autostart", '"terminal":true'),
        ("desktop hyprctl -j autostart", '"manualDrag":false'),
        ("desktop hyprctl -j autostart terminal off", '"requestedValue":"off"'),
        ("desktop hyprctl -j autostart terminal off", '"terminal":false'),
        ("desktop hyprctl -j autostart terminal on", '"requestedValue":"on"'),
        ("desktop hyprctl -j autostart terminal on", '"terminal":true'),
        ("desktop hyprctl -j submap", '"command":"submap"'),
        ("desktop hyprctl -j submap", '"activeSubmap":'),
        ("desktop hyprctl -j submap", '"activeRole":'),
        ("desktop hyprctl -j submap", '"stickyUntilReset":true'),
        ("desktop hyprctl -j submap", '"manualDrag":false'),
        ("desktop hyprctl -j splash", '"command":"splash"'),
        ("desktop hyprctl -j splash", '"renderer":"software"'),
        ("desktop hyprctl -j splash", '"manualDrag":false'),
        ("desktop hyprctl -j session", '"command":"session"'),
        ("desktop hyprctl -j session", '"desiredState":'),
        ("desktop hyprctl -j session", '"audit":{'),
        ("desktop hyprctl -j session", '"recommendedAction":'),
        ("desktop hyprctl -j session", '"manualDrag":false'),
        ("desktop hyprctl -j session", '"hardwareValidation":false'),
        ("desktop hyprctl -j session reload", '"action":"reload"'),
        ("desktop hyprctl -j session reload", '"ok":true'),
        ("desktop hyprctl -j rollinglog", '"command":"rollinglog"'),
        ("desktop hyprctl -j rollinglog", '"paths":{"events":'),
        ("desktop hyprctl -j rollinglog", '"manualDrag":false'),
        ("desktop hyprctl -j configerrors", '"model":"Hyprland-style config parser diagnostics"'),
        ("desktop hyprctl -j configerrors", '"summary":{'),
        ("desktop hyprctl -j configerrors", '"plain":'),
        ("desktop hyprctl -j configerrors", '"compositeFlags":'),
        ("desktop hyprctl -j configerrors", '"sourceResolve":{'),
        ("desktop hyprctl -j configerrors", '"maxDepth":3'),
        ("desktop hyprctl -j configerrors", '"files":['),
        ("desktop hyprctl -j configerrors", '"manualDrag":false'),
        ("desktop hyprctl -j configtrace", '"model":"Hyprland-style config trace"'),
        ("desktop hyprctl -j configtrace", '"parserSummary":{'),
        ("desktop hyprctl -j configtrace", '"trace":['),
        ("desktop hyprctl focushistory", "focusHistoryID"),
        ("desktop hyprctl workspacestack", "master/stack/focus"),
        ("desktop hyprctl layouts", "Orizon desktop layouts"),
        ("desktop hyprctl layoutstate", "Orizon desktop layout state"),
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
        ("desktop hyprctl -j getoption general:gaps_in", '"command":"getoption"'),
        ("desktop hyprctl -j getoption general:gaps_in", '"key":"general:gaps_in"'),
        ("desktop hyprctl -j getoption general:gaps_in", '"value":"9"'),
        ("desktop hyprctl -j getoption env", '"command":"getoption"'),
        ("desktop hyprctl -j getoption env", '"key":"env"'),
        ("desktop hyprctl -j getoption env", '"entryCount":'),
        ("desktop hyprctl -j getoption env", '"entries":['),
        ("desktop hyprctl -j getoption env", '"ORIZON_DESKTOP_SOURCE,1"'),
        ("desktop hyprctl keyword decoration:rounding 11", "desktop keyword: applied"),
        ("desktop hyprctl -j keyword decoration:rounding 11", '"command":"keyword"'),
        ("desktop hyprctl -j keyword decoration:rounding 11", '"ok":true'),
        ("desktop hyprctl -j keyword decoration:rounding 11", '"route":"settings"'),
        ("desktop hyprctl -j keyword decoration:rounding 11", '"effect":"applied-setting"'),
        ("desktop hyprctl -j keyword decoration:rounding 11", '"targetPath":"/system/desktop-settings.conf"'),
        ("desktop hyprctl -j keyword decoration:rounding 11", '"reloadRecommended":false'),
        ("desktop hyprctl -j keyword decoration:rounding 11", '"manualDrag":false'),
        ("desktop hyprctl getoption decoration:rounding", "value: 11"),
        ("desktop hyprctl binds", "/system/desktop-binds.conf"),
        ("desktop hyprctl binds", "focusmwindow"),
        ("desktop hyprctl binds", "swapmwindow"),
        ("desktop hyprctl -j binds", '"command":"binds"'),
        ("desktop hyprctl -j binds", '"mouseBindsPreparedOnly":true'),
        ("desktop hyprctl -j binds", '"manualDragFromBindm":false'),
        ("desktop hyprctl -j binds", '"flagSemantics":{'),
        ("desktop hyprctl -j binds", '"manualDrag":false'),
        ("desktop hyprctl layers", "Orizon desktop layers"),
        ("desktop hyprctl -j layers", '"command":"layers"'),
        ("desktop hyprctl -j layers", '"waybarActive":false'),
        ("desktop hyprctl -j layers", '"taskbar":false'),
        ("desktop autostart", "Orizon desktop autostart"),
        ("desktop autostart terminal off", "desktop session: updated"),
        ("desktop autostart terminal on", "desktop session: updated"),
        ("desktop dispatch exec terminal", "exec orizon-terminal client spawned"),
        ("desktop dispatch exec terminal", "exec orizon-terminal client spawned"),
        ("desktop dispatch focuswindow class:orizon-terminal", "focuswindow ok"),
        ("desktop dispatch focuscurrentorlast", "focuscurrentorlast ok"),
        ("desktop dispatch markurgent on", "markurgent on"),
        ("desktop activewindow", "urgent: true"),
        ("desktop dispatch focuscurrentorlast", "focuscurrentorlast ok"),
        ("desktop focus-history", "urgent=true"),
        ("desktop dispatch focusurgentorlast", "mode=urgent"),
        ("desktop activewindow", "urgent: false"),
        ("desktop focus-window title:Terminal", "focuswindow ok"),
        ("desktop workspace-stack", "master=0x"),
        ("desktop dispatch swapwithmaster", "swapwithmaster ok"),
        ("desktop dispatch focusmaster", "focusmaster ok"),
        ("desktop dispatch focusmwindow rank:2", "focusmwindow ok"),
        ("desktop dispatch focusmwindow master", "target=master"),
        ("desktop dispatch cyclenext", "cyclenext ok"),
        ("desktop dispatch swapnext", "swapnext ok"),
        ("desktop dispatch movefocus r", "movefocus"),
        ("desktop dispatch swapwindow l", "swapwindow ok"),
        ("desktop dispatch swapmwindow next", "swapmwindow ok"),
        ("desktop dispatch swapmwindow rank:1", "rank=1/"),
        ("desktop dispatch movewindow r", "movewindow ok"),
        ("desktop dispatch movewindow master", "target=master"),
        ("desktop dispatch movewindoworgroup l", "movewindow ok"),
        ("desktop dispatch togglesplit", "togglesplit split="),
        ("desktop dispatch layoutmsg layout master", "layout master"),
        ("desktop layout-state", "per-workspace"),
        ("desktop dispatch layoutmsg splitratio 60", "splitratio 60"),
        ("desktop dispatch layoutmsg splitratio +5", "splitratio 65"),
        ("desktop dispatch layoutmsg masterratio 65", "masterratio 65"),
        ("desktop dispatch layoutmsg mfact -5", "masterratio 60"),
        ("desktop dispatch layoutmsg nmaster 2", "nmaster 2"),
        ("desktop dispatch layoutmsg addmaster", "addmaster nmaster=3"),
        ("desktop dispatch layoutmsg removemaster", "removemaster nmaster=2"),
        ("desktop dispatch layoutmsg movewindowmaster", "movewindowmaster"),
        ("desktop layout-tree", "nmaster=2"),
        ("desktop dispatch layoutmsg splitratio reset", "splitratio 50"),
        ("desktop dispatch layoutmsg masterratio reset", "masterratio 58"),
        ("desktop dispatch layoutmsg nmaster reset", "nmaster 1"),
        ("desktop layout-tree", "nmaster=1"),
        ("desktop dispatch layoutmsg layout monocle", "layout monocle"),
        ("desktop layout-tree", "monocle-visible"),
        ("desktop layout-tree", "monocle-deck"),
        ("desktop layout-tree", "rendered=no"),
        ("desktop dispatch layoutmsg reset", "layout reset"),
        ("desktop layout-state", "split=auto ratio=50 master=58 nmaster=1"),
        ("desktop dispatch layoutmsg layout master", "layout master"),
        ("desktop dispatch layoutmsg orientationleft", "split=vertical"),
        ("desktop dispatch layoutmsg orientationtop", "split=horizontal"),
        ("desktop dispatch layoutmsg preselect r", "preselect r split=vertical"),
        ("desktop dispatch layoutmsg preselect up", "preselect up split=horizontal"),
        ("desktop dispatch layoutmsg preselect reset", "preselect reset split=auto"),
        ("desktop dispatch resizeactive 5 0", "resizeactive split="),
        ("desktop dispatch submap resize", "submap resize"),
        ("desktop hyprctl submap", "submap: resize"),
        ("desktop hyprctl submap reset", "submap default"),
        ("desktop dispatch fullscreen", "fullscreen on"),
        ("desktop dispatch fullscreen off", "fullscreen off"),
        ("desktop dispatch fullscreenstate 1", "fullscreenstate on"),
        ("desktop dispatch fullscreenstate 2 0", "internal=2 client=0"),
        ("desktop hyprctl activewindow", "fullscreenClient: 0"),
        ("desktop dispatch fullscreenstate -1 2", "internal=2 client=2"),
        ("desktop dispatch fullscreenstate 0 0", "internal=0 client=0"),
        ("desktop dispatch pseudo", "pseudo on"),
        ("desktop dispatch pseudo off", "pseudo off"),
        ("desktop dispatch pseudotile on", "pseudotile on"),
        ("desktop dispatch pin", "pin on"),
        ("desktop dispatch pin off", "pin off"),
        ("desktop dispatch pin on", "pin on"),
        ("desktop hyprctl clients", "Orizon desktop windows"),
        ("desktop hyprctl activewindow", "activewindow:"),
        ("desktop hyprctl monitors", "Monitor 0"),
        ("desktop windows", "Orizon desktop windows"),
        ("desktop clients", "focusHistoryID"),
        ("desktop activewindow", "focusHistoryID"),
        (
            "desktop dispatch movetoworkspacesilent special:magic,activewindow",
            "special:magic",
        ),
        ("desktop clients", "special=yes"),
        ("desktop hyprctl -j clients", '"special":true'),
        (
            "desktop dispatch togglespecialworkspace magic",
            'visible name="special:magic"',
        ),
        ("desktop activewindow", "special: true"),
        ("desktop hyprctl -j activeworkspace", '"specialVisible":true'),
        (
            "desktop dispatch togglespecialworkspace magic",
            'hidden name="special:magic"',
        ),
        ("desktop workspace", "Orizon desktop workspaces"),
        ("desktop dispatch movetoworkspace 2", "moved active to workspace 2"),
        ("desktop hyprctl activeworkspace", "id: 2"),
        ("desktop dispatch workspace 2", "workspace 2"),
        ("desktop dispatch workspace previous", "workspace 1"),
        ("desktop dispatch workspace +1", "workspace 2"),
        ("desktop dispatch workspace r+1", "workspace 3"),
        ("desktop dispatch workspace r~2", "workspace 2"),
        ("desktop dispatch workspace m~1", "workspace 1"),
        ("desktop dispatch workspace m+1", "workspace 2"),
        ("desktop dispatch workspace e+1", "workspace 3"),
        ("desktop dispatch workspace e-1", "workspace 2"),
        (
            "desktop dispatch focusworkspaceoncurrentmonitor active",
            "focusworkspaceoncurrentmonitor 2",
        ),
        ("desktop dispatch focusmonitor current", "single-framebuffer=yes"),
        ('desktop dispatch focusmonitor DP-1', 'requested="DP-1"'),
        (
            "desktop dispatch movecurrentworkspacetomonitor current",
            "movecurrentworkspacetomonitor workspace=2",
        ),
        (
            "desktop dispatch moveworkspacetomonitor active current",
            "moveworkspacetomonitor workspace=2",
        ),
        (
            "desktop hyprctl dispatch focusmonitor current",
            "single-framebuffer=yes",
        ),
        ("desktop dispatch renameworkspace 2 dev", 'renameworkspace 2 name="dev"'),
        ("desktop dispatch workspace name:dev", "workspace 2"),
        ("desktop dispatch movetoworkspace name:dev", "moved active to workspace 2"),
        ("desktop workspaces", 'workspace 2: name="dev"'),
        ("desktop workspace 2", "workspace 2 active"),
        ("desktop dispatch movetoworkspacesilent empty", "silently moved active"),
        ("desktop hyprctl activeworkspace", "id: 2"),
        ("desktop dispatch workspace next", "workspace 3"),
        ("desktop dispatch workspace empty", "workspace 4"),
        ("desktop workspace empty", "desktop dispatch: workspace"),
        ("desktop dispatch movefocus next", "movefocus"),
        ("desktop shortcuts", "F1"),
        ("desktop shortcuts", "submap policy:"),
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
        ("desktop truth", "Orizon desktop Hyprland-style truth taxonomy"),
        ("desktop truth", "not-implemented: upstream Hyprland"),
        ("desktop hyprctl truth", "simulated-facade: desktop hyprctl"),
        ("desktop hyprctl -j truth", '"command":"truth"'),
        ("desktop hyprctl -j truth", '"label":"prepared"'),
        ("desktop hyprctl -j truth", '"hardwareValidation":false'),
        ("desktop app settings", "class: orizon-settings"),
        ("desktop app settings", "data-source: /system/desktop-settings.conf"),
        ("desktop app settings", "runtime-clients:"),
        ("desktop app settings", "runtime-focused-client:"),
        ("desktop app logs", "runbook: desktop logs"),
        ("desktop app packages", "data-source: /workspace/packages"),
        ("desktop app update", "limits: live ISO can inspect only"),
        ("desktop apps", "summary: total=6 native=4 terminal=1 overlay=1 tiling-clients=5"),
        ("desktop apps", "runtime: clients/focus/workspace"),
        ("desktop apps launcher", "surface: overlay"),
        ("desktop hyprctl -j apps", '"command":"apps"'),
        ("desktop hyprctl -j apps", '"tilingClients":5'),
        ("desktop hyprctl -j apps", '"runtime":{'),
        ("desktop hyprctl -j apps", '"manualDrag":false'),
        ("desktop hyprctl -j app settings", '"class":"orizon-settings"'),
        ("desktop hyprctl -j app settings", '"tiledClient":true'),
        ("desktop hyprctl -j app settings", '"runtime":{'),
        ("desktop hyprctl -j app launcher", '"overlay":true'),
        ("desktop hyprctl -j app launcher", '"overlayVisible":'),
        ("desktop hyprctl -j app launcher", '"taskbar":false'),
        ("desktop launch terminal", "exec orizon-terminal client spawned"),
        (
            "desktop hyprctl -j launch settings",
            '"result":"desktop dispatch: exec orizon-settings client spawned',
        ),
        (
            "desktop dispatch tagwindow +settings class:orizon-settings",
            'tagwindow set tag="settings"',
        ),
        ("desktop dispatch focuswindow tag:settings", "focuswindow ok"),
        (
            "desktop dispatch movetoworkspacesilent r+1,tag:settings",
            'selector="tag:settings"',
        ),
        (
            "desktop dispatch movetoworkspacesilent 2,tag:settings",
            'selector="tag:settings"',
        ),
        (
            "desktop dispatch movetoworkspacesilent 2,class:orizon-settings",
            "silently moved selected to workspace 2",
        ),
        (
            "desktop dispatch movetoworkspace active,activewindow",
            'selector="activewindow"',
        ),
        ("desktop launch logs", "exec orizon-logs client spawned"),
        ("desktop launch launcher", "orizon-launcher overlay toggled"),
        ("desktop dispatch exec orizon-packages", "exec orizon-packages client spawned"),
        ("desktop dispatch exec update", "exec orizon-update-viewer client spawned"),
        ("desktop hyprctl clients", "orizon-update-viewer"),
        ("desktop modules", "orizon-waybar"),
        ("desktop modules", "split-plan: core -> hypr profile"),
        ("desktop modules", "dependency-graph: orizon-desktop-core"),
        ("pkg info orizon-terminal", "sample pkg sample orizon-terminal"),
        ("pkg info orizon-terminal", "dependency orizon-desktop-core"),
        ("pkg info orizon-terminal", "activation tiled-terminal-client"),
        ("pkg search orizon-terminal", "pkg install orizon-terminal"),
        ("pkg search waybar", "orizon-waybar"),
        ("pkg info orizon-desktop-hypr", "state available optional"),
        ("pkg info orizon-desktop-hypr", "version 0.99.0"),
        ("desktop package", "orizon-desktop-hypr.opkg"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "focusmwindow"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "dispatch-focusmwindow-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "dispatch-focusmonitor-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "master:mfact = 0.58"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "config-unbind-runtime"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "app-catalog-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "app-surface-policy"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-focushistory-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-workspacestack-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-focusstate-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-focusstate-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-clientmodel-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-rulematches-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-layoutstate-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-layouttree-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-monitors-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-devices-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-keymap-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-cursorpos-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-version-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-systeminfo-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-backend-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-protocol-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-protocol-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "desktop-protocol-v0"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-architecture-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-truth-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-truth-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-architecture-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "surfaceContract"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "protocolLimits"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "backendApi=compositor-backend-v0"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "/system/desktop-architecture.conf"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-animations-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-decorations-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-render-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-layouts-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-descriptions-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-instances-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-modules-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-module-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-shortcuts-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-shortcut-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-autostart-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-autostart-action-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-apps-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-app-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-launch-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-submap-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-splash-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-session-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-rollinglog-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-configerrors-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-configtrace-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-getoption-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-getoption-entries"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-keyword-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-keyword-route"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-dispatch-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-reload-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-binds-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-layers-command"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-session-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-app-diagnostics"),
        ("cat /workspace/packages/orizon-desktop-hypr.opkg", "hyprctl-json-autostart-diagnostics"),
        ("pkg simulate /workspace/packages/orizon-desktop-hypr.opkg", "dry-run"),
        ("pkg verify /workspace/packages/orizon-desktop-hypr.opkg", "package verify: OK"),
        ("pkg install orizon-desktop-hypr", "pkg"),
        ("cat /system/desktop-binds.conf", "swapmwindow"),
        ("cat /home/orizon/.config/hypr/orizon-hypr.conf", "focusmwindow"),
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
    if [ "$rc" -eq 0 ] && grep -Fqi "$needle" "$OUT"; then
      if [ "$cmd" = "disk read-test last" ] && grep -q "lba=0 " "$OUT"; then
        echo "last-sector read-test used LBA 0"
        rm -f "$ASKPASS" "$PASSFILE" "$OUT"
        exit 1
      fi
      break
    fi
    if [ "$attempt" -ge 6 ]; then
      [ "$rc" -eq 0 ] || echo "ssh command failed after $attempt attempts"
      grep -Fqi "$needle" "$OUT" || echo "missing expected output: $needle"
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
    encoded_needle = base64.b64encode(needle.encode("utf-8")).decode("ascii")
    remote_script = f"""#!/usr/bin/env bash
set -u
ASKPASS=/tmp/orizon_one_askpass.sh
PASSFILE=/tmp/orizon_one_password.txt
KNOWN=/tmp/orizon_one_known_hosts
OUT=/tmp/orizon_one_output.txt
NEEDLE=/tmp/orizon_one_needle.txt
printf '%s' '{encoded}' | base64 -d > "$PASSFILE"
printf '%s' '{encoded_needle}' | base64 -d > "$NEEDLE"
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
if [ "$rc" -ne 0 ] || ! grep -Fqi -f "$NEEDLE" "$OUT"; then
  echo "single ssh command failed rc=$rc expected=$(cat "$NEEDLE")" >&2
  rm -f "$ASKPASS" "$PASSFILE" "$OUT" "$NEEDLE"
  exit 1
fi
rm -f "$ASKPASS" "$PASSFILE" "$OUT" "$NEEDLE"
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
                    ok, size = capture_framebuffer_smoke(client, sudo_password, cfg["name"])
                    if cfg["network_name"]:
                        detail = (
                            "guest IP unavailable from libvirt lease/arp/neighbor probes; "
                            f"framebuffer={'ok' if ok else 'missing'} bytes={size}"
                        )
                        results.append(matrix_result(case_name, STATUS_FAIL, detail, cfg))
                        case_log.append(f"{STATUS_FAIL}: {detail}")
                        print(f"{STATUS_FAIL}: {detail}")
                        continue
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
