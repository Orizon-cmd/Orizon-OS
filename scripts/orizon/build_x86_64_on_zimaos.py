from __future__ import annotations

import argparse
import subprocess
import tarfile
import tempfile
from pathlib import Path

from common import connect_ssh, parse_env_file, read_required, run_command, run_sudo_command


DOCKERFILE_CONTENT = """FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \\
    ca-certificates \\
    clang \\
    curl \\
    lld \\
    make \\
    tar \\
    xorriso \\
    xz-utils \\
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
"""


def create_project_tarball(source_dir: Path) -> Path:
    temp_file = tempfile.NamedTemporaryFile(suffix=".tar.gz", delete=False)
    temp_path = Path(temp_file.name)
    temp_file.close()

    with tarfile.open(temp_path, "w:gz") as archive:
        archive.add(source_dir, arcname=source_dir.name)

    return temp_path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build the x86_64 Orizon OS tree on ZimaOS using Docker."
    )
    parser.add_argument(
        "--env-file",
        default="config/hosts/zimaos.local.env",
        help="Path to the local env file with the ZimaOS connection details.",
    )
    parser.add_argument(
        "--source-dir",
        default="orizon-os-x86_64",
        help="Local project directory to build remotely.",
    )
    parser.add_argument(
        "--remote-root",
        default="/DATA/orizon-build/x86_64",
        help="Remote build workspace root on ZimaOS.",
    )
    parser.add_argument(
        "--image-name",
        default="orizon-os-x86_64-builder",
        help="Docker image name used for the remote build environment.",
    )
    parser.add_argument(
        "--rebuild-image",
        action="store_true",
        help="Force a rebuild of the Docker image before compiling.",
    )
    parser.add_argument(
        "--deploy-vm",
        action="store_true",
        help="Deploy the compiled iso_root tree to the Orizon VM after build.",
    )
    parser.add_argument(
        "--vm-config",
        default="config/vm/orizon-dev.example.json",
        help="VM config used when --deploy-vm is enabled.",
    )
    args = parser.parse_args()

    source_dir = Path(args.source_dir)
    if not source_dir.exists():
        raise FileNotFoundError(f"Source directory not found: {source_dir}")

    env_config = parse_env_file(Path(args.env_file))
    sudo_password = env_config.get(
        "ZIMAOS_SUDO_PASSWORD", read_required(env_config, "ZIMAOS_PASSWORD")
    )
    archive_path = create_project_tarball(source_dir)

    remote_root = args.remote_root.rstrip("/")
    remote_archive = f"{remote_root}/upload/source.tar.gz"
    remote_dockerfile = f"{remote_root}/Dockerfile"
    remote_workspace = f"{remote_root}/workspace"
    remote_project_root = f"{remote_workspace}/{source_dir.name}"
    remote_iso_root = f"{remote_project_root}/iso_root"
    remote_artifacts = f"{remote_root}/artifacts"
    remote_docker_home = f"{remote_root}/docker-home"
    remote_build_script = f"{remote_root}/build-inside.sh"
    docker_cmd = (
        f"env HOME={remote_docker_home} DOCKER_CONFIG={remote_docker_home} docker"
    )

    client = connect_ssh(env_config)
    try:
        sftp = client.open_sftp()
        run_sudo_command(
            client,
            sudo_password,
            "sh -lc "
            + repr(
                f"mkdir -p {remote_root}/upload {remote_artifacts} "
                f"{remote_docker_home} "
                f"&& chown -R {read_required(env_config, 'ZIMAOS_USER')}:samba {remote_root.rsplit('/', 1)[0]}"
            ),
        )
        with sftp.open(remote_dockerfile, "w") as handle:
            handle.write(DOCKERFILE_CONTENT)
        sftp.put(str(archive_path), remote_archive)

        build_script = (
            f"set -e; "
            f"mkdir -p {remote_workspace}; "
            f"find {remote_workspace} -mindepth 1 -maxdepth 1 -exec rm -rf {{}} + || true; "
            f"tar -xzf {remote_archive} -C {remote_workspace}; "
            f"cd {remote_project_root}; "
            f"find . -type f "
            f"\\( -name 'Makefile' -o -name '*.mk' -o -name '*.sh' -o -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' -o -name '*.conf' -o -name '*.md' \\) "
            f"-exec sed -i 's/\\r$//' {{}} +; "
            f"make clean; "
            f"make; "
            f"tar -czf {remote_artifacts}/orizonos-x86_64-built-iso-root.tar.gz -C {remote_project_root} iso_root build orizonos-x86_64.iso; "
            f"ls -lh orizonos-x86_64.iso build/kernel.elf"
        )
        with sftp.open(remote_build_script, "w") as handle:
            handle.write("#!/usr/bin/env bash\n")
            handle.write(build_script)

        image_check = run_sudo_command(
            client,
            sudo_password,
            f"sh -lc \"{docker_cmd} image inspect {args.image_name} >/dev/null 2>&1 && echo present || echo missing\"",
            check=False,
        ).strip()
        if args.rebuild_image or image_check != "present":
            run_sudo_command(
                client,
                sudo_password,
                f"{docker_cmd} build -t {args.image_name} -f {remote_dockerfile} {remote_root}",
            )

        build_output = run_sudo_command(
            client,
            sudo_password,
            "sh -lc "
            + repr(
                f"{docker_cmd} run --rm "
                f"--user 0:0 "
                f"-v {remote_workspace}:{remote_workspace} "
                f"-v {remote_root}:{remote_root} "
                f"-w {remote_project_root} "
                f"{args.image_name} "
                f"bash {remote_build_script}"
            ),
        )

        print("Remote x86_64 build completed")
        print(build_output)
        print(f"Remote project root: {remote_project_root}")
        print(f"Remote iso root: {remote_iso_root}")
        print(f"Remote artifacts: {remote_artifacts}")

        if args.deploy_vm:
            cmd = [
                "python",
                "scripts/orizon/deploy_x86_64_tree_vm.py",
                "--env-file",
                args.env_file,
                "--vm-config",
                args.vm_config,
                "--remote-source-dir",
                remote_iso_root,
            ]
            subprocess.run(cmd, check=True)

        return 0
    finally:
        archive_path.unlink(missing_ok=True)
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
