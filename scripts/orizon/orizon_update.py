from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
from pathlib import Path

from common import connect_ssh, parse_env_file


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE_DIR = "orizon-os-x86_64"
DEFAULT_ROOT_ISO = "Orizon-OS.iso"
DEFAULT_GITHUB_REPO = "https://github.com/Orizon-cmd/Orizon-OS.git"
DEFAULT_GITHUB_REF = "main"


def run(command: list[str], *, cwd: Path = REPO_ROOT) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def run_capture(command: list[str], *, cwd: Path = REPO_ROOT) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def maybe_sync_git(enabled: bool) -> None:
    if enabled:
        run(["git", "pull", "--ff-only"])


def ensure_clean_git_tree(allow_dirty: bool) -> None:
    if allow_dirty:
        return
    status = run_capture(["git", "status", "--porcelain"])
    if status:
        raise RuntimeError(
            "Refusing to sync from GitHub with local changes. "
            "Commit, stash, or rerun with --allow-dirty-github-sync."
        )


def sync_from_github(repo_url: str, ref: str, allow_dirty: bool) -> None:
    if not (REPO_ROOT / ".git").exists():
        raise RuntimeError("--from-github requires this directory to be a Git checkout.")
    ensure_clean_git_tree(allow_dirty)
    run(["git", "fetch", "--tags", repo_url, ref])
    run(["git", "merge", "--ff-only", "FETCH_HEAD"])
    current = run_capture(["git", "rev-parse", "HEAD"])
    fetched = run_capture(["git", "rev-parse", "FETCH_HEAD"])
    if current != fetched:
        raise RuntimeError(
            "Local checkout is ahead of the requested GitHub ref. "
            "Push it first, or run without --from-github for local-only builds."
        )


def github_repo_slug(repo_url: str) -> str:
    prefix = "https://github.com/"
    if not repo_url.startswith(prefix):
        raise ValueError(
            "Cannot derive a raw GitHub URL from this repo URL. "
            "Use --github-iso-url explicitly."
        )

    slug = repo_url[len(prefix) :].strip("/")
    if slug.endswith(".git"):
        slug = slug[:-4]
    if slug.count("/") < 1:
        raise ValueError(
            "Cannot derive a raw GitHub URL from this repo URL. "
            "Use --github-iso-url explicitly."
        )
    return slug


def resolve_github_ref(repo_url: str, ref: str) -> str:
    if re.fullmatch(r"[0-9a-fA-F]{40}", ref):
        return ref

    slug = github_repo_slug(repo_url)
    quoted_ref = urllib.parse.quote(ref, safe="")
    api_url = f"https://api.github.com/repos/{slug}/commits/{quoted_ref}"
    request = urllib.request.Request(
        api_url,
        headers={"Accept": "application/vnd.github+json", "User-Agent": "orizon-update"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        data = json.loads(response.read().decode("utf-8"))
    sha = data.get("sha")
    if not sha:
        raise RuntimeError(f"Could not resolve GitHub ref: {ref}")
    return sha


def github_raw_url(repo_url: str, ref: str, repo_path: str) -> str:
    slug = github_repo_slug(repo_url)
    return f"https://raw.githubusercontent.com/{slug}/{ref}/{repo_path.lstrip('/')}"


def download_file(url: str, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    hasher = hashlib.sha256()
    with urllib.request.urlopen(url, timeout=60) as response:
        total = response.headers.get("Content-Length", "unknown")
        with tempfile.NamedTemporaryFile(
            prefix=output_path.name + ".", suffix=".tmp", delete=False, dir=output_path.parent
        ) as tmp:
            temp_path = Path(tmp.name)
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                hasher.update(chunk)
                tmp.write(chunk)
    shutil.move(str(temp_path), output_path)
    print(f"Downloaded ISO: {output_path}")
    print(f"Source URL: {url}")
    print(f"Size: {output_path.stat().st_size} bytes (expected {total})")
    print(f"SHA256: {hasher.hexdigest()}")


def download_github_iso(
    *,
    repo_url: str,
    ref: str,
    repo_path: str,
    explicit_url: str,
    output_iso: Path,
) -> None:
    if explicit_url:
        url = explicit_url
    else:
        resolved_ref = resolve_github_ref(repo_url, ref)
        if resolved_ref != ref:
            print(f"GitHub ref {ref} resolved to {resolved_ref}")
        url = github_raw_url(repo_url, resolved_ref, repo_path)
    download_file(url, output_iso)


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
            "GitHub can provide either the public source or the published ISO."
        )
    )
    parser.add_argument(
        "--mode",
        choices=("github-iso", "local-iso", "zimaos-iso", "zimaos-vm"),
        default="local-iso",
        help="Update backend to run.",
    )
    parser.add_argument(
        "--sync-git",
        action="store_true",
        help="Run 'git pull --ff-only' before building.",
    )
    parser.add_argument(
        "--from-github",
        action="store_true",
        help="Fast-forward this checkout from the public GitHub repo before building.",
    )
    parser.add_argument(
        "--allow-dirty-github-sync",
        action="store_true",
        help="Allow --from-github even when the local Git tree has changes.",
    )
    parser.add_argument(
        "--github-repo",
        default=DEFAULT_GITHUB_REPO,
        help="Public Orizon OS GitHub repository URL.",
    )
    parser.add_argument(
        "--github-ref",
        default=DEFAULT_GITHUB_REF,
        help="GitHub branch, tag, or ref used for internet updates.",
    )
    parser.add_argument(
        "--github-iso-path",
        default=DEFAULT_ROOT_ISO,
        help="Path to the published ISO inside the GitHub repository.",
    )
    parser.add_argument(
        "--github-iso-url",
        default="",
        help="Explicit ISO URL. Overrides --github-repo/--github-ref/--github-iso-path.",
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
    if args.from_github and args.mode != "github-iso":
        sync_from_github(
            args.github_repo, args.github_ref, args.allow_dirty_github_sync
        )

    source_dir = REPO_ROOT / args.source_dir
    output_iso = REPO_ROOT / args.output_iso
    publish = not args.no_publish_root_iso

    if args.mode == "github-iso":
        download_github_iso(
            repo_url=args.github_repo,
            ref=args.github_ref,
            repo_path=args.github_iso_path,
            explicit_url=args.github_iso_url,
            output_iso=output_iso,
        )
    elif args.mode == "local-iso":
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
