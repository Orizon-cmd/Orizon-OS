from __future__ import annotations

import argparse
import posixpath
from pathlib import Path

import paramiko

from common import connect_ssh, parse_env_file, read_required


FIRMWARE_CANDIDATES = [
    "iwlwifi-so-a0-hr-b0-89.ucode",
    "iwlwifi-so-a0-hr-b0-86.ucode",
    "iwlwifi-so-a0-hr-b0-83.ucode",
    "iwlwifi-so-a0-hr-b0-77.ucode",
    "iwlwifi-so-a0-hr-b0-74.ucode",
    "iwlwifi-so-a0-gf-a0-89.ucode",
    "iwlwifi-so-a0-gf-a0-86.ucode",
    "iwlwifi-so-a0-gf-a0-83.ucode",
    "iwlwifi-QuZ-a0-hr-b0-77.ucode",
    "iwlwifi-QuZ-a0-hr-b0-74.ucode",
]


def ssh_output(client: paramiko.SSHClient, command: str) -> str:
    _stdin, stdout, stderr = client.exec_command(command)
    status = stdout.channel.recv_exit_status()
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    if status != 0:
        raise RuntimeError(err.strip() or out.strip() or command)
    return out


def remote_firmware_list(client: paramiko.SSHClient) -> list[str]:
    script = r"""
set -e
for dir in /usr/lib/firmware /lib/firmware; do
  [ -d "$dir" ] || continue
  find "$dir" -maxdepth 1 -type f -name 'iwlwifi-*.ucode' -printf '%f\t%p\n'
done
"""
    lines = ssh_output(client, "sh -lc " + repr(script)).splitlines()
    paths: dict[str, str] = {}
    for line in lines:
        if "\t" not in line:
            continue
        name, path = line.split("\t", 1)
        paths[name] = path
    ordered: list[str] = []
    for candidate in FIRMWARE_CANDIDATES:
        if candidate in paths:
            ordered.append(paths[candidate])
    for name in sorted(paths):
        if paths[name] not in ordered:
            ordered.append(paths[name])
    return ordered


def connect_laptop(
    args: argparse.Namespace,
) -> tuple[paramiko.SSHClient | None, paramiko.SSHClient]:
    zima_config = parse_env_file(Path(args.zima_env))
    laptop_config = parse_env_file(Path(args.laptop_env))
    laptop_host = read_required(laptop_config, "LAPTOP_HOST")
    laptop_port = int(laptop_config.get("LAPTOP_PORT", "22"))
    laptop_user = read_required(laptop_config, "LAPTOP_USER")
    laptop_password = read_required(laptop_config, "LAPTOP_PASSWORD")

    laptop = paramiko.SSHClient()
    laptop.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    jump = laptop_config.get("LAPTOP_JUMP", "zima").strip().lower()

    if jump in ("", "none", "direct"):
        laptop.connect(
            laptop_host,
            port=laptop_port,
            username=laptop_user,
            password=laptop_password,
            timeout=20,
            look_for_keys=False,
            allow_agent=False,
        )
        return None, laptop

    zima = connect_ssh(zima_config)
    try:
        channel = zima.get_transport().open_channel(
            "direct-tcpip", (laptop_host, laptop_port), ("127.0.0.1", 0)
        )
    except Exception:
        zima.close()
        laptop.connect(
            laptop_host,
            port=laptop_port,
            username=laptop_user,
            password=laptop_password,
            timeout=20,
            look_for_keys=False,
            allow_agent=False,
        )
        return None, laptop

    laptop.connect(
        laptop_host,
        port=laptop_port,
        username=laptop_user,
        password=laptop_password,
        timeout=20,
        sock=channel,
        look_for_keys=False,
        allow_agent=False,
    )
    return zima, laptop


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Import Intel iwlwifi firmware from the Lenovo Linux install into the local ISO tree."
    )
    parser.add_argument("--zima-env", default="config/hosts/zimaos.local.env")
    parser.add_argument(
        "--laptop-env", default="config/hosts/laptop-hotspot.local.env"
    )
    parser.add_argument(
        "--dest-dir", default="orizon-os-x86_64/firmware",
        help="Local ignored directory copied into Orizon-OS.iso during build.",
    )
    parser.add_argument(
        "--firmware",
        default="",
        help="Optional exact remote firmware path to import instead of auto-selecting.",
    )
    args = parser.parse_args()

    zima, laptop = connect_laptop(args)
    try:
        if args.firmware:
            remote_path = args.firmware
        else:
            candidates = remote_firmware_list(laptop)
            if not candidates:
                raise RuntimeError("No iwlwifi-*.ucode firmware found on laptop")
            remote_path = candidates[0]

        name = posixpath.basename(remote_path)
        dest_dir = Path(args.dest_dir)
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest = dest_dir / name

        with laptop.open_sftp() as sftp:
            sftp.get(remote_path, str(dest))

        print(f"Imported {remote_path}")
        print(f"Local firmware: {dest}")
        print("Rebuild the ISO; the Makefile will add it as a Limine module.")
        return 0
    finally:
        laptop.close()
        if zima is not None:
            zima.close()


if __name__ == "__main__":
    raise SystemExit(main())
