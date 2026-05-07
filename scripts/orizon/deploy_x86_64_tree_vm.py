from __future__ import annotations

import argparse
import tarfile
import tempfile
from pathlib import Path

from common import connect_ssh, load_json, parse_env_file, read_required, run_command, run_sudo_command


def create_tarball(source_dir: Path) -> Path:
    temp_file = tempfile.NamedTemporaryFile(suffix=".tar.gz", delete=False)
    temp_path = Path(temp_file.name)
    temp_file.close()

    with tarfile.open(temp_path, "w:gz") as archive:
        for child in sorted(source_dir.iterdir()):
            archive.add(child, arcname=child.name)

    return temp_path


def deploy_remote_tree(
    *,
    client,
    sudo_password: str,
    vm_name: str,
    remote_disk: str,
    disk_size: str,
    remote_tree_dir: str,
    start_vm: bool,
) -> tuple[str, str]:
    state = run_sudo_command(
        client, sudo_password, f"virsh domstate {vm_name} || true", check=False
    ).strip().lower()
    if state == "running":
        run_sudo_command(client, sudo_password, f"virsh destroy {vm_name}")

    remote_staging = f"/tmp/{vm_name}-boot-tree"

    run_sudo_command(
        client,
        sudo_password,
        "sh -lc "
        + repr(
            f"set -e; "
            f"install -d -m 0755 /DATA/VM; "
            f"rm -rf {remote_staging}; "
            f"mkdir -p {remote_staging}; "
            f"cp -a {remote_tree_dir}/. {remote_staging}/; "
            f"truncate -s {disk_size} {remote_disk}; "
            f"parted -s {remote_disk} mklabel gpt; "
            f"parted -s {remote_disk} mkpart ESP fat32 1MiB 100%; "
            f"parted -s {remote_disk} set 1 esp on; "
            f"LOOP=$(losetup --find --show -P {remote_disk}); "
            f"mkfs.fat -F32 ${{LOOP}}p1 >/dev/null; "
            f"MNT=$(mktemp -d); "
            f"mount ${{LOOP}}p1 $MNT; "
            f"cp -a {remote_staging}/. $MNT/; "
            f"sync; "
            f"umount $MNT; "
            f"rmdir $MNT; "
            f"losetup -d $LOOP; "
            f"rm -rf {remote_staging}"
        ),
    )

    if start_vm:
        run_sudo_command(client, sudo_password, f"virsh start {vm_name}")

    vnc_display = run_sudo_command(
        client, sudo_password, f"virsh vncdisplay {vm_name} || true", check=False
    ).strip()
    final_state = run_sudo_command(
        client, sudo_password, f"virsh domstate {vm_name}", check=False
    ).strip()
    return final_state, vnc_display


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Deploy the tracked x86_64 UEFI tree to a ZimaOS VM disk."
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
        "--source-dir",
        default="orizon-os-x86_64/iso_root",
        help="Local directory containing the boot tree to deploy.",
    )
    parser.add_argument(
        "--remote-source-dir",
        default="",
        help="Remote directory on ZimaOS containing the boot tree to deploy.",
    )
    parser.add_argument(
        "--no-start",
        action="store_true",
        help="Leave the VM powered off after writing the disk.",
    )
    args = parser.parse_args()

    use_remote_source = bool(args.remote_source_dir)
    if not use_remote_source:
        source_dir = Path(args.source_dir)
        if not source_dir.exists():
            raise FileNotFoundError(f"Source directory not found: {source_dir}")
    else:
        source_dir = None

    env_config = parse_env_file(Path(args.env_file))
    vm_config = load_json(Path(args.vm_config))
    sudo_password = env_config.get("ZIMAOS_SUDO_PASSWORD", read_required(env_config, "ZIMAOS_PASSWORD"))

    archive_path = create_tarball(source_dir) if source_dir else None
    remote_archive = f"/tmp/{vm_config['name']}-boot-tree.tar.gz"
    remote_disk = vm_config["remote_disk_path"]
    vm_name = vm_config["name"]
    disk_size = vm_config.get("disk_size", "8G")

    client = connect_ssh(env_config)
    try:
        if use_remote_source:
            remote_tree_dir = args.remote_source_dir
        else:
            sftp = client.open_sftp()
            sftp.put(str(archive_path), remote_archive)
            remote_tree_dir = f"/tmp/{vm_name}-boot-tree-src"
            run_sudo_command(
                client,
                sudo_password,
                "sh -lc "
                + repr(
                    f"rm -rf {remote_tree_dir}; "
                    f"mkdir -p {remote_tree_dir}; "
                    f"tar -xzf {remote_archive} -C {remote_tree_dir}; "
                    f"rm -f {remote_archive}"
                ),
            )

        final_state, vnc_display = deploy_remote_tree(
            client=client,
            sudo_password=sudo_password,
            vm_name=vm_name,
            remote_disk=remote_disk,
            disk_size=disk_size,
            remote_tree_dir=remote_tree_dir,
            start_vm=not args.no_start,
        )

        if not use_remote_source:
            run_sudo_command(
                client,
                sudo_password,
                f"rm -rf {remote_tree_dir}",
                check=False,
            )

        print(f"Updated boot tree for {vm_name}")
        print(f"Remote disk: {remote_disk}")
        print(f"VM state: {final_state}")
        if vnc_display:
            print(f"VNC display: {vnc_display}")
        return 0
    finally:
        if archive_path:
            archive_path.unlink(missing_ok=True)
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
