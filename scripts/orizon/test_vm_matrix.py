from __future__ import annotations

import argparse
import base64
import re
import tempfile
import time
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

KEYMAP = {" ": "KEY_SPACE", "\n": "KEY_ENTER", "-": "KEY_MINUS"}
for _ch in "abcdefghijklmnopqrstuvwxyz0123456789":
    KEYMAP[_ch] = "KEY_" + _ch.upper()


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


def boot_and_start_ssh(client, sudo_password: str, vm_name: str, password: str) -> None:
    # Console input is sent blind through libvirt. Give the framebuffer shell
    # time to appear, then replay the idempotent setup once to avoid losing the
    # password/start commands during slower boots.
    commands = (
        "keyboard us",
        "net dhcp",
        f"ssh password {password}",
        "ssh auth max 3",
        "ssh auth lockout 30",
        "ssh start",
    )
    time.sleep(20)
    for round_index in range(2):
        for cmd in commands:
            send_console_text(client, sudo_password, vm_name, cmd + "\n")
            time.sleep(1.5)
        if round_index == 0:
            time.sleep(5)


def find_nat_ip(client, sudo_password: str, mac: str, network_name: str, timeout: int) -> str:
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
        return find_nat_ip(client, sudo_password, mac, cfg["network_name"], timeout)
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
        ("system services", "Orizon init/services"),
        ("system doctor", "summary:"),
        ("system init", "system init:"),
        ("security", "Orizon security status"),
        ("logs security", "security:"),
        ("rescue", "Orizon rescue mode"),
        ("hostname", "orizon"),
        ("system repair", "system repair:"),
        ("net status", "ipv4=yes"),
        ("net check", "network summary:"),
        ("timer", "source="),
        ("ping 8.8.8.8", "reply from"),
        ("dns raw.githubusercontent.com", " -> "),
        ("net tcp raw.githubusercontent.com 443", "tcp: PASS"),
        ("pkg status", "Orizon package manager"),
        ("pkg help", "pkg verify"),
        ("pkg search orizon", "pkg search:"),
        ("pkg remote", "package remote:"),
        ("pkg remote verify", "pkg remote verify:"),
        ("pkg upgrade plan", "pkg upgrade plan:"),
        ("pkg sample", "Sample package written"),
        ("pkg verify /workspace/packages/orizon-hello.opkg", "package verify: OK"),
        ("pkg install /workspace/packages/orizon-hello.opkg", "install"),
        ("update status", "update:"),
        ("selftest", "summary:"),
        ("storage", "selected="),
        ("storage diag", "nvme: controllers="),
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
    args = parser.parse_args()

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

        results: list[tuple[str, str, str]] = []
        for case_name in cases:
            if case_name not in MATRIX_CASES:
                raise ValueError(f"Unknown matrix case: {case_name}")
            cfg = apply_storage_profile(
                matrix_config(base_config, case_name, MATRIX_CASES[case_name]),
                args.disk_bus,
            )
            print(
                f"=== {case_name} ({cfg['network_model']} / {cfg['network_name'] or 'bridge'} / disk={args.disk_bus}) ==="
            )
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
            if state.strip().lower() != "running":
                results.append((case_name, "fail", f"state={state.strip()}"))
                continue
            boot_and_start_ssh(client, sudo_password, cfg["name"], args.password)
            mac = domif_mac(client, sudo_password, cfg["name"])
            ip = ""
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
                    detail = "guest IP unavailable from libvirt lease table"
                    results.append((case_name, "fail", detail))
                    print(f"FAIL: {detail}")
                    continue
                ok, size = capture_framebuffer_smoke(client, sudo_password, cfg["name"])
                status = "boot-only" if ok else "fail"
                detail = (
                    f"framebuffer={'ok' if ok else 'missing'} bytes={size}; "
                    "ssh skipped because bridge guest IP was not discoverable "
                    "from virsh arp or host neighbor tables"
                )
                results.append((case_name, status, detail))
                print(f"{status.upper()}: {detail}")
                if args.include_lifecycle:
                    run_sudo_command(
                        client,
                        sudo_password,
                        f"virsh destroy {cfg['name']} || true",
                        check=False,
                    )
                continue
            output = run_ssh_checks(
                client,
                ip,
                args.password,
                timeout=args.ssh_timeout,
                include_update=args.include_update,
            )
            print(output)
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
                print(lifecycle)
            results.append((case_name, "ok", ip))

        print("=== matrix summary ===")
        for case_name, status, detail in results:
            print(f"{case_name}: {status} {detail}")
        return 1 if any(status == "fail" for _, status, _ in results) else 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
