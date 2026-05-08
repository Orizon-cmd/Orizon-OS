from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from common import connect_ssh, parse_env_file


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE_DIR = "orizon-os-x86_64"
DEFAULT_ROOT_ISO = "Orizon-OS.iso"


def run(command: list[str], *, cwd: Path = REPO_ROOT) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def maybe_sync_git(enabled: bool) -> None:
    if enabled:
        run(["git", "pull", "--ff-only"])


def publish_local_iso(source_dir: Path, output_iso: Path) -> None:
    built_iso = source_dir / "orizonos-x86_64.iso"
    if not built_iso.exists():
        raise FileNotFoundError(f"Built ISO not found: {built_iso}")
    shutil.copy2(built_iso, output_iso)
    print(f"Published ISO: {output_iso}")


def build_local_iso(source_dir: Path, output_iso: Path, publish: bool) -> None:
    run(["make"], cwd=source_dir)
    if publish:
        publish_local_iso(source_dir, output_iso)


def build_on_zimaos(
    *,
    env_file: str,
    vm_config: str,
    source_dir: str,
    remote_root: str,
    deploy_vm: bool,
    publish: bool,
    output_iso: Path,
) -> None:
    cmd = [
        sys.executable,
        "scripts/orizon/build_x86_64_on_zimaos.py",
        "--env-file",
        env_file,
        "--vm-config",
        vm_config,
        "--source-dir",
        source_dir,
        "--remote-root",
        remote_root,
    ]
    if deploy_vm:
        cmd.append("--deploy-vm")
    run(cmd)

    if publish:
        env_config = parse_env_file(REPO_ROOT / env_file)
        client = connect_ssh(env_config)
        try:
            remote_iso = (
                f"{remote_root.rstrip('/')}/workspace/"
                f"{Path(source_dir).name}/orizonos-x86_64.iso"
            )
            sftp = client.open_sftp()
            sftp.get(remote_iso, str(output_iso))
            sftp.close()
            print(f"Published ISO: {output_iso}")
        finally:
            client.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Portable Orizon OS update entrypoint. ZimaOS is only one backend; "
            "local-iso works on any machine with the build toolchain installed."
        )
    )
    parser.add_argument(
        "--mode",
        choices=("local-iso", "zimaos-iso", "zimaos-vm"),
        default="local-iso",
        help="Update backend to run.",
    )
    parser.add_argument(
        "--sync-git",
        action="store_true",
        help="Run 'git pull --ff-only' before building.",
    )
    parser.add_argument(
        "--source-dir",
        default=DEFAULT_SOURCE_DIR,
        help="Orizon OS source target directory.",
    )
    parser.add_argument(
        "--output-iso",
        default=DEFAULT_ROOT_ISO,
        help="Root-level ISO artifact to refresh.",
    )
    parser.add_argument(
        "--no-publish-root-iso",
        action="store_true",
        help="Build/update without refreshing the root ISO artifact.",
    )
    parser.add_argument(
        "--env-file",
        default="config/hosts/zimaos.local.env",
        help="ZimaOS backend env file.",
    )
    parser.add_argument(
        "--vm-config",
        default="config/vm/orizon-dev.example.json",
        help="ZimaOS backend VM config.",
    )
    parser.add_argument(
        "--remote-root",
        default="/DATA/orizon-build/x86_64",
        help="ZimaOS backend remote workspace root.",
    )
    args = parser.parse_args()

    maybe_sync_git(args.sync_git)

    source_dir = REPO_ROOT / args.source_dir
    output_iso = REPO_ROOT / args.output_iso
    publish = not args.no_publish_root_iso

    if args.mode == "local-iso":
        build_local_iso(source_dir, output_iso, publish)
    elif args.mode == "zimaos-iso":
        build_on_zimaos(
            env_file=args.env_file,
            vm_config=args.vm_config,
            source_dir=args.source_dir,
            remote_root=args.remote_root,
            deploy_vm=False,
            publish=publish,
            output_iso=output_iso,
        )
    else:
        build_on_zimaos(
            env_file=args.env_file,
            vm_config=args.vm_config,
            source_dir=args.source_dir,
            remote_root=args.remote_root,
            deploy_vm=True,
            publish=publish,
            output_iso=output_iso,
        )

    print("Orizon update complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
