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
    time.sleep(12)
    for cmd in (
        "keyboard us",
        "net dhcp",
        f"ssh password {password}",
        "ssh auth max 3",
        "ssh auth lockout 30",
        "ssh start",
    ):
        send_console_text(client, sudo_password, vm_name, cmd + "\n")
        time.sleep(2)


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
        ("net status", "ipv4=yes"),
        ("ping 8.8.8.8", "reply from"),
        ("dns raw.githubusercontent.com", " -> "),
        ("pkg status", "Orizon package manager"),
        ("update status", "update:"),
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
            cfg = matrix_config(base_config, case_name, MATRIX_CASES[case_name])
            print(f"=== {case_name} ({cfg['network_model']} / {cfg['network_name'] or 'bridge'}) ===")
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
            if cfg["network_name"]:
                ip = find_nat_ip(client, sudo_password, mac, cfg["network_name"], args.boot_timeout)
            if not ip:
                detail = "guest IP unavailable from libvirt lease table"
                status = "fail" if cfg["network_name"] else "skip-ssh"
                results.append((case_name, status, detail))
                print(f"{status.upper()}: {detail}")
                continue
            output = run_ssh_checks(
                client,
                ip,
                args.password,
                timeout=args.ssh_timeout,
                include_update=args.include_update,
            )
            print(output)
            results.append((case_name, "ok", ip))

        print("=== matrix summary ===")
        for case_name, status, detail in results:
            print(f"{case_name}: {status} {detail}")
        return 1 if any(status == "fail" for _, status, _ in results) else 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
