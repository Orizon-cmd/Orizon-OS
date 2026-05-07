from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import connect_ssh, load_json, parse_env_file, read_required, run_command, run_sudo_command


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Upload a fresh Orizon OS disk image to ZimaOS and restart the target VM."
    )
    parser.add_argument(
        "--env-file",
        default="config/hosts/zimaos.local.env",
        help="Path to the local env file with the ZimaOS connection details.",
    )
    parser.add_argument(
        "--vm-config",
        default="config/vm/orizon-dev.example.json",
        help="Path to the VM configuration JSON file.",
    )
    parser.add_argument(
        "--artifact",
        required=True,
        help="Path to the local bootable disk image to deploy.",
    )
    parser.add_argument(
        "--no-start",
        action="store_true",
        help="Leave the VM powered off after the update.",
    )
    args = parser.parse_args()

    local_artifact = Path(args.artifact)
    if not local_artifact.exists():
        raise FileNotFoundError(f"Artifact not found: {local_artifact}")

    env_config = parse_env_file(Path(args.env_file))
    vm_config = load_json(Path(args.vm_config))
    sudo_password = env_config.get("ZIMAOS_SUDO_PASSWORD", read_required(env_config, "ZIMAOS_PASSWORD"))
    client = connect_ssh(env_config)

    try:
        sftp = client.open_sftp()
        remote_disk = vm_config["remote_disk_path"]
        remote_upload = f"/tmp/{local_artifact.name}.upload"
        vm_name = vm_config["name"]

        state = run_sudo_command(client, sudo_password, f"virsh domstate {vm_name} || true").strip().lower()
        if state == "running":
            run_sudo_command(client, sudo_password, f"virsh destroy {vm_name}")

        sftp.put(str(local_artifact), remote_upload)
        run_sudo_command(
            client,
            sudo_password,
            f"install -D -m 0644 {json.dumps(remote_upload)} {json.dumps(remote_disk)}",
        )
        run_command(client, f"rm -f {json.dumps(remote_upload)}")

        if not args.no_start:
            run_sudo_command(client, sudo_password, f"virsh start {vm_name}")

        print(f"Updated VM disk for {vm_name}")
        print(f"Remote disk: {remote_disk}")
        print(f"Started VM: {not args.no_start}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
