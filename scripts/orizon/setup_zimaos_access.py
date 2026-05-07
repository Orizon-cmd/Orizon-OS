from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict

import paramiko


def parse_env_file(path: Path) -> Dict[str, str]:
    data: Dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key.strip()] = value.strip()
    return data


def read_required(config: Dict[str, str], key: str) -> str:
    value = config.get(key, "").strip()
    if not value:
        raise ValueError(f"Missing required setting: {key}")
    return value


def run_command(client: paramiko.SSHClient, command: str) -> str:
    _stdin, stdout, stderr = client.exec_command(command)
    output = stdout.read().decode("utf-8", errors="replace").strip()
    error = stderr.read().decode("utf-8", errors="replace").strip()
    return output if output else error


def install_key_and_capture(
    config: Dict[str, str], facts_path: Path, sudo_check: bool
) -> Dict[str, str]:
    host = read_required(config, "ZIMAOS_HOST")
    port = int(config.get("ZIMAOS_PORT", "22"))
    user = read_required(config, "ZIMAOS_USER")
    password = read_required(config, "ZIMAOS_PASSWORD")
    sudo_password = config.get("ZIMAOS_SUDO_PASSWORD", password)
    pubkey_path = Path(read_required(config, "ZIMAOS_PUBKEY_PATH")).expanduser()
    public_key = pubkey_path.read_text(encoding="utf-8").strip()

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, port=port, username=user, password=password, timeout=15)

    facts: Dict[str, str] = {}
    try:
        sftp = client.open_sftp()
        remote_home = sftp.normalize(".")
        ssh_dir = f"{remote_home}/.ssh"
        authorized_keys = f"{ssh_dir}/authorized_keys"

        try:
            sftp.stat(ssh_dir)
        except FileNotFoundError:
            sftp.mkdir(ssh_dir, 0o700)

        try:
            with sftp.open(authorized_keys, "r") as handle:
                existing = handle.read().decode("utf-8")
        except FileNotFoundError:
            existing = ""

        if public_key not in existing:
            updated = existing
            if updated and not updated.endswith("\n"):
                updated += "\n"
            updated += public_key + "\n"
            with sftp.open(authorized_keys, "w") as handle:
                handle.write(updated)

        sftp.chmod(ssh_dir, 0o700)
        sftp.chmod(authorized_keys, 0o600)

        commands = {
            "hostname": "hostname",
            "kernel": "uname -a",
            "os_release": "cat /etc/os-release",
            "whoami": "whoami",
            "id": "id",
            "pwd": "pwd",
            "disk_root": "df -h /",
            "ip_brief": "ip -brief address || ip addr",
        }
        for name, command in commands.items():
            facts[name] = run_command(client, command)

        facts["remote_home"] = remote_home

        if sudo_check:
            stdin, stdout, stderr = client.exec_command(
                "sudo -S -p '' whoami", get_pty=True
            )
            stdin.write(sudo_password + "\n")
            stdin.flush()
            sudo_output = stdout.read().decode("utf-8", errors="replace")
            sudo_output = sudo_output.replace(sudo_password, "")
            sudo_lines = [line.strip() for line in sudo_output.splitlines() if line.strip()]
            facts["sudo_whoami"] = sudo_lines[-1] if sudo_lines else ""
            facts["sudo_stderr"] = stderr.read().decode(
                "utf-8", errors="replace"
            ).strip()
    finally:
        client.close()

    facts_path.parent.mkdir(parents=True, exist_ok=True)
    facts_path.write_text(json.dumps(facts, indent=2), encoding="utf-8")
    return facts


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install the local SSH key on ZimaOS and save a facts snapshot."
    )
    parser.add_argument(
        "--env-file",
        default="config/hosts/zimaos.local.env",
        help="Path to the local env file with the ZimaOS connection details.",
    )
    parser.add_argument(
        "--facts-file",
        default="config/hosts/zimaos-facts.local.json",
        help="Path to the JSON facts snapshot to write.",
    )
    parser.add_argument(
        "--skip-sudo-check",
        action="store_true",
        help="Skip the sudo validation command.",
    )
    args = parser.parse_args()

    env_path = Path(args.env_file)
    if not env_path.exists():
        raise FileNotFoundError(f"Env file not found: {env_path}")

    config = parse_env_file(env_path)
    facts = install_key_and_capture(
        config=config,
        facts_path=Path(args.facts_file),
        sudo_check=not args.skip_sudo_check,
    )

    print(f"SSH access verified for {config.get('ZIMAOS_SSH_ALIAS', config['ZIMAOS_HOST'])}")
    print(f"Facts written to {args.facts_file}")
    print(f"Hostname: {facts.get('hostname', 'unknown')}")
    print(f"Kernel: {facts.get('kernel', 'unknown')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
