from __future__ import annotations

import argparse
import base64
import json
import shlex
import tempfile
import time
from pathlib import Path

from common import connect_ssh, load_json, parse_env_file, read_required, run_command, run_sudo_command
from deploy_x86_64_tree_vm import deploy_remote_tree
from test_vm_matrix import (
    boot_and_start_ssh,
    define_vm,
    domif_mac,
    find_nat_ip,
    send_console_text,
    wait_domstate,
)


def dedicated_config(base: dict, name_suffix: str) -> dict:
    name = f"{base.get('name', 'orizon')}-{name_suffix}"
    cfg = dict(base)
    cfg["name"] = name
    cfg["title"] = f"Orizon OS {name_suffix}"
    cfg["remote_disk_path"] = f"/DATA/VM/{name}.img"
    cfg["network_name"] = cfg.get("network_name") or "default"
    cfg["network_model"] = cfg.get("network_model") or "e1000e"
    return cfg


def expect(output: str, needle: str, label: str) -> None:
    if needle.lower() not in output.lower():
        raise RuntimeError(f"{label}: missing expected text: {needle}")


def run_guest_ssh(
    client,
    ip: str,
    password: str,
    command: str,
    timeout: int,
) -> str:
    encoded = base64.b64encode(password.encode("utf-8")).decode("ascii")
    remote = f"/tmp/orizon_update_rollback_ssh_{int(time.time() * 1000)}.sh"
    quoted_command = shlex.quote(command)
    script = f"""#!/usr/bin/env bash
set -u
ASKPASS=/tmp/orizon_update_rollback_askpass.sh
PASSFILE=/tmp/orizon_update_rollback_password.txt
KNOWN=/tmp/orizon_update_rollback_known_hosts
OUT=/tmp/orizon_update_rollback_output.txt
printf '%s' '{encoded}' | base64 -d > "$PASSFILE"
cat > "$ASKPASS" <<'EOS'
#!/bin/sh
cat /tmp/orizon_update_rollback_password.txt
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
  orizon@{ip} {quoted_command} > "$OUT" 2>&1
rc=$?
cat "$OUT"
rm -f "$ASKPASS" "$PASSFILE" "$OUT" "$KNOWN"
exit "$rc"
"""
    sftp = client.open_sftp()
    with sftp.open(remote, "w") as handle:
        handle.write(script)
    try:
        run_command(client, f"chmod +x {remote}")
        return run_command(client, f"bash {remote}", check=True)
    finally:
        run_command(client, f"rm -f {remote}", check=False)


def send_full_disk_install(client, sudo_password: str, vm_name: str) -> None:
    steps = [
        ("keyboard us", 1.0),
        ("install", 1.5),
        ("2", 0.8),
        ("2", 0.8),
        ("1", 0.8),
        ("3", 0.8),
        ("", 0.8),
        ("erase disk0", 1.0),
    ]
    time.sleep(12)
    for text, delay in steps:
        send_console_text(client, sudo_password, vm_name, text + "\n")
        time.sleep(delay)


def wait_for_installed_ssh(
    client,
    sudo_password: str,
    cfg: dict,
    password: str,
    boot_timeout: int,
    ssh_timeout: int,
    *,
    start_vm: bool,
) -> str:
    vm_name = cfg["name"]
    last_error = "guest IP unavailable"

    if start_vm:
        state = run_sudo_command(
            client, sudo_password, f"virsh domstate {vm_name} || true", check=False
        ).strip().lower()
        if state != "running":
            run_sudo_command(client, sudo_password, f"virsh start {vm_name}")

    deadline = time.time() + boot_timeout
    while time.time() < deadline:
        boot_and_start_ssh(client, sudo_password, vm_name, password)
        mac = domif_mac(client, sudo_password, vm_name)
        ip = find_nat_ip(client, sudo_password, mac, cfg["network_name"], 8)
        if not ip:
            last_error = "guest IP unavailable from libvirt DHCP leases"
            time.sleep(2)
            continue
        try:
            status = run_guest_ssh(client, ip, password, "status", ssh_timeout)
            expect(status, "ssh: enabled=", "installed SSH status")
            return ip
        except Exception as exc:  # Keep retrying while the guest finishes booting.
            last_error = str(exc)
            time.sleep(3)

    raise RuntimeError(f"SSH unavailable after installed boot: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install a dedicated ZimaOS VM and validate update/rollback paths."
    )
    parser.add_argument("--env-file", default="config/hosts/zimaos.local.env")
    parser.add_argument("--vm-config", default="config/vm/orizon-dev.example.json")
    parser.add_argument(
        "--remote-source-dir",
        default="/DATA/orizon-build/x86_64/workspace/orizon-os-x86_64/iso_root",
        help="Remote boot tree produced by build_x86_64_on_zimaos.py.",
    )
    parser.add_argument("--password", default="testtest")
    parser.add_argument("--name-suffix", default="update-rollback")
    parser.add_argument("--boot-timeout", type=int, default=90)
    parser.add_argument("--ssh-timeout", type=int, default=60)
    parser.add_argument("--update-timeout", type=int, default=300)
    parser.add_argument(
        "--skip-negative-guard",
        action="store_true",
        help="Skip the temporary installed-marker removal guard check.",
    )
    parser.add_argument(
        "--leave-running",
        action="store_true",
        help="Leave the dedicated VM running after validation.",
    )
    args = parser.parse_args()

    env_config = parse_env_file(Path(args.env_file))
    base_config = load_json(Path(args.vm_config))
    cfg = dedicated_config(base_config, args.name_suffix)
    sudo_password = env_config.get(
        "ZIMAOS_SUDO_PASSWORD", read_required(env_config, "ZIMAOS_PASSWORD")
    )

    client = connect_ssh(env_config)
    transcript: list[str] = []
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
              "Run scripts/orizon/orizon_update.py --mode zimaos-iso first."
          )

      define_vm(client, sudo_password, cfg)
      run_sudo_command(
          client,
          sudo_password,
          "sh -lc " + json.dumps(f"rm -f {cfg['remote_disk_path']}"),
          check=False,
      )
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
          raise RuntimeError(f"VM did not start for install flow: {state}")

      print(f"=== install dedicated VM: {cfg['name']} ===")
      send_full_disk_install(client, sudo_password, cfg["name"])
      state = wait_domstate(
          client,
          sudo_password,
          cfg["name"],
          {"shut off", "shutoff"},
          args.boot_timeout,
      )
      if state not in {"shut off", "shutoff"}:
          raise RuntimeError(f"install did not power off cleanly; state={state}")
      transcript.append("install: VM powered off after full-disk install")

      print("=== boot installed VM and start SSH ===")
      ip = wait_for_installed_ssh(
          client,
          sudo_password,
          cfg,
          args.password,
          args.boot_timeout,
          args.ssh_timeout,
          start_vm=True,
      )
      transcript.append(f"installed-ssh-ip: {ip}")

      checks = [
          ("update status", "boot-mode: installed"),
          ("update status", "manifest:"),
          ("update status", "bootguard-strategy: limine-boot-count-shell-validation"),
          ("update status", "nvram-bootnext: prepared=no"),
          ("update status", "ab-slots: prepared=no"),
          ("bootguard", "Orizon boot guard"),
          ("bootguard", "boot-strategy: limine-boot-count-shell-validation"),
          ("bootguard", "firmware-pre-kernel-rollback=no"),
          ("persist status", "ready=yes"),
          ("persist save", "persistence save: ok"),
          ("selftest update", "update.status"),
      ]
      for command, needle in checks:
          print(f"--- {command} ---")
          out = run_guest_ssh(client, ip, args.password, command, args.ssh_timeout)
          print(out)
          expect(out, needle, command)

      if not args.skip_negative_guard:
          print("--- intentional installed-marker guard failure ---")
          marker = run_guest_ssh(
              client,
              ip,
              args.password,
              "cat /workspace/.orizon/installed",
              args.ssh_timeout,
          )
          expect(marker, "Orizon OS installed", "installed marker capture")
          run_guest_ssh(
              client,
              ip,
              args.password,
              "rm /workspace/.orizon/installed",
              args.ssh_timeout,
          )
          failed = run_guest_ssh(
              client, ip, args.password, "update", args.update_timeout
          )
          print(failed)
          expect(failed, "unavailable in live boot", "intentional update guard")
          run_guest_ssh(
              client,
              ip,
              args.password,
              "write /workspace/.orizon/installed Orizon OS installed restored-by-update-rollback-test",
              args.ssh_timeout,
          )
          restored = run_guest_ssh(
              client, ip, args.password, "update status", args.ssh_timeout
          )
          expect(restored, "boot-mode: installed", "installed marker restore")
          transcript.append("negative-guard: update refused without installed marker")

      print("--- update ---")
      update_out = run_guest_ssh(
          client, ip, args.password, "update", args.update_timeout
      )
      print(update_out)
      expect(update_out, "Update complete", "update")
      if "Boot payload already current" in update_out:
          transcript.append("update: boot payload already current")
      if "Rollback ready" in update_out:
          transcript.append("update: rollback slot armed")
          ip = wait_for_installed_ssh(
              client,
              sudo_password,
              cfg,
              args.password,
              args.boot_timeout,
              args.ssh_timeout,
              start_vm=False,
          )

      post_update = [
          ("update status", "manifest: present"),
          ("update status", "manifest.sig: present"),
          ("update status", "signature-manifest-match: yes"),
          ("update status", "tls-root-trust: embedded root trust"),
          ("update status", "package-index-auth: signed-manifest-sha256-pinned"),
          ("update status", "resume-cache:"),
          ("update status", "bootguard-state:"),
          ("update status", "bootguard-fallback-config:"),
          ("update status", "rollback-scope: post-orizon-early-boot"),
          ("update status", "ab-slots: prepared=no"),
          ("logs update", "Update complete"),
          ("bootguard", "Orizon boot guard"),
          ("bootguard", "limine-fallback-config:"),
          ("bootguard", "ab-slots: prepared=no"),
          ("pkg status", "remote-index-auth signed-update-manifest-sha256-pinned"),
          ("pkg remote", "cached-index=yes"),
          ("pkg remote verify", "remote-index-valid=yes"),
          ("pkg upgrade plan", "pkg upgrade plan:"),
          ("pkg search orizon", "pkg search:"),
          ("pkg info orizon-welcome", "orizon-welcome"),
      ]
      for command, needle in post_update:
          print(f"--- {command} ---")
          out = run_guest_ssh(client, ip, args.password, command, args.ssh_timeout)
          print(out)
          expect(out, needle, command)

      package_checks = [
          ("pkg sample", "Sample package written"),
          ("pkg verify /workspace/packages/orizon-hello.opkg", "package verify: OK"),
          ("pkg install /workspace/packages/orizon-hello.opkg", "Installed orizon-hello"),
          ("pkg info orizon-hello", "dependencies:"),
          ("pkg remove orizon-hello", "Removed orizon-hello"),
          ("pkg history", "removed orizon-hello"),
          ("pkg rollback orizon-hello", "Restored orizon-hello"),
          ("pkg history", "rollback orizon-hello"),
      ]
      for command, needle in package_checks:
          print(f"--- {command} ---")
          out = run_guest_ssh(client, ip, args.password, command, args.ssh_timeout)
          print(out)
          expect(out, needle, command)
      transcript.append("pkg: sample verify install info remove rollback history PASS")

      print("--- rollback ---")
      rollback_out = run_guest_ssh(
          client, ip, args.password, "rollback", args.update_timeout
      )
      print(rollback_out)
      expect(rollback_out, "Rollback complete", "rollback")
      rollback_status = run_guest_ssh(
          client, ip, args.password, "rollback-status", args.ssh_timeout
      )
      print(rollback_status)
      expect(rollback_status, "currently-booted-payload", "rollback-status")
      transcript.append("rollback: restored currently booted payload")

      print("--- reboot after rollback ---")
      reboot_out = run_guest_ssh(client, ip, args.password, "reboot", args.ssh_timeout)
      print(reboot_out)
      expect(reboot_out, "scheduled", "reboot")
      time.sleep(8)
      state = wait_domstate(
          client, sudo_password, cfg["name"], {"running"}, args.boot_timeout
      )
      if state != "running":
          raise RuntimeError(f"VM did not stay running after reboot; state={state}")
      ip = wait_for_installed_ssh(
          client,
          sudo_password,
          cfg,
          args.password,
          args.boot_timeout,
          args.ssh_timeout,
          start_vm=False,
      )
      for command, needle in (
          ("update status", "boot-mode: installed"),
          ("persist status", "mode=persistent"),
          ("selftest", "summary:"),
      ):
          print(f"--- post-reboot {command} ---")
          out = run_guest_ssh(client, ip, args.password, command, args.ssh_timeout)
          print(out)
          expect(out, needle, f"post-reboot {command}")

      print("--- post-reboot rollback-status ---")
      out = run_guest_ssh(client, ip, args.password, "rollback-status", args.ssh_timeout)
      print(out)
      if (
          "currently-booted-payload" not in out
          and "rollback-info: No such file" not in out
      ):
          raise RuntimeError("post-reboot rollback-status: unexpected state")

      if not args.leave_running:
          print("--- shutdown dedicated VM ---")
          shutdown_out = run_guest_ssh(
              client, ip, args.password, "shutdown", args.ssh_timeout
          )
          print(shutdown_out)
          wait_domstate(
              client,
              sudo_password,
              cfg["name"],
              {"shut off", "shutoff"},
              args.boot_timeout,
          )

      print("=== update/rollback VM summary ===")
      for line in transcript:
          print(line)
      print("result: PASS")
      return 0
    finally:
      client.close()


if __name__ == "__main__":
    raise SystemExit(main())
