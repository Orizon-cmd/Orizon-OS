from __future__ import annotations

import argparse
import datetime
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
DEFAULT_UPDATE_DIR = "updates/x86_64"
DEFAULT_RELEASE_REPORT = "release.txt"
DEFAULT_GITHUB_REPO = "https://github.com/Orizon-cmd/Orizon-OS.git"
DEFAULT_GITHUB_REF = "main"
DEFAULT_PACKAGE_REPO = "https://github.com/Orizon-cmd/Orizon-Packages.git"
DEFAULT_PACKAGE_REF = "main"
DEFAULT_PACKAGE_INDEX_PATH = "packages/x86_64/index.txt"
DEFAULT_MANIFEST_SIGNING_KEY = "config/keys/update-signing.private.pem"
MANIFEST_SIGNING_KEY_ID = "orizon-update-root-2026-05"
MANIFEST_SIGNING_MODULUS_HEX = (
    "a7a007e2312f120311859724f6ca3b7713dd75dc23ca0945cae6c4718384ae63"
    "3198e98c32946cccb6597eab0f0dfdc3509f7953ef665d1b581fde1c1d5f4ed"
    "cadda1fc65c6611362d033ad9d8b5959ce925157ed3849a6ab72c5eb85581b9"
    "c5c54763638ad98dcf4f1135eeeb9d73f83773224ffc4ea4bbc79cd36e66f312"
    "601dcd6614301b37c82ce1e27b789feac9f2818e5d15cf3b6a78f21afc060a5"
    "34803356c4379febaadaaf06c89612c4644ab965a5b5ff13715290799ef1aaba"
    "fb26bf7707b0da9d27d8115622c53770fb5095cd81001ee91ae7d2cdfe656031"
    "9484693adef94bbfc2b36d40e4d945a25ce61761611475e97a085bf9da81087"
    "7e87"
)


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

    try:
        result = subprocess.run(
            [
                "git",
                "ls-remote",
                repo_url,
                f"refs/heads/{ref}",
                f"refs/tags/{ref}",
                f"refs/tags/{ref}^{{}}",
                ref,
            ],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode == 0:
            first_sha = ""
            peeled_tag_sha = ""
            for line in result.stdout.splitlines():
                parts = line.split()
                if len(parts) < 2:
                    continue
                sha, remote_ref = parts[0], parts[1]
                if not first_sha:
                    first_sha = sha
                if remote_ref.endswith("^{}"):
                    peeled_tag_sha = sha
                if remote_ref == f"refs/heads/{ref}":
                    return sha
            if peeled_tag_sha:
                return peeled_tag_sha
            if first_sha:
                return first_sha
    except FileNotFoundError:
        pass

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


def download_bytes(url: str, *, max_bytes: int) -> bytes:
    chunks: list[bytes] = []
    used = 0
    with urllib.request.urlopen(url, timeout=60) as response:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            used += len(chunk)
            if used > max_bytes:
                raise RuntimeError(f"Downloaded object exceeds safety cap: {url}")
            chunks.append(chunk)
    return b"".join(chunks)


def sha256_file(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def repo_relative_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO_ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def manifest_value(text: str, key: str) -> str | None:
    prefix = key + " "
    for line in text.splitlines():
        if line.startswith(prefix):
            return line[len(prefix) :].strip()
    return None


def load_manifest_signing_key(key_path: Path, *, generate: bool):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import rsa

    if not key_path.exists() and generate:
        key_path.parent.mkdir(parents=True, exist_ok=True)
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        key_path.write_bytes(
            key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.PKCS8,
                encryption_algorithm=serialization.NoEncryption(),
            )
        )
        print(f"Generated local manifest signing key: {key_path}")
    if not key_path.exists():
        raise FileNotFoundError(
            f"Missing manifest signing key: {key_path}. "
            "Restore the trusted release key, or rotate the embedded update root key."
        )
    key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    if not isinstance(key, rsa.RSAPrivateKey):
        raise ValueError(f"Manifest signing key {key_path} is not an RSA key.")
    public_numbers = key.public_key().public_numbers()
    if f"{public_numbers.n:0512x}" != MANIFEST_SIGNING_MODULUS_HEX:
        raise ValueError(
            f"Manifest signing key {key_path} does not match "
            f"{MANIFEST_SIGNING_KEY_ID}. Restore the release key or rotate the "
            "embedded kernel update root before publishing."
        )
    return key


def write_manifest_signature(
    *, manifest: Path, signing_key_path: Path, generate_key: bool
) -> None:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import padding

    key = load_manifest_signing_key(signing_key_path, generate=generate_key)
    data = manifest.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    signature = key.sign(data, padding.PKCS1v15(), hashes.SHA256())
    sig_path = manifest.with_suffix(".sig")
    sig_path.write_text(
        "\n".join(
            [
                "signature-version 1",
                "algorithm rsa-pkcs1-sha256",
                f"key-id {MANIFEST_SIGNING_KEY_ID}",
                f"manifest-sha256 {digest}",
                f"signature {signature.hex()}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"Published signed update manifest: {sig_path}")


def update_version() -> str:
    return datetime.datetime.now().strftime("%Y.%m.%d-%H%M")


def package_index_metadata(
    *, package_repo: str, package_ref: str, package_index_path: str
) -> tuple[str, int, str, str]:
    resolved_ref = resolve_github_ref(package_repo, package_ref)
    if resolved_ref != package_ref:
        print(f"Package ref {package_ref} resolved to {resolved_ref}")
    url = github_raw_url(package_repo, resolved_ref, package_index_path)
    data = download_bytes(url, max_bytes=8192)
    if not data:
        raise RuntimeError(f"Package index is empty: {url}")
    digest = hashlib.sha256(data).hexdigest()
    print(f"Package index: {package_index_path}")
    print(f"Package index commit: {resolved_ref}")
    print(f"Package index size: {len(data)} bytes")
    print(f"Package index SHA256: {digest}")
    return resolved_ref, len(data), digest, url


def write_update_manifest(
    *,
    update_dir: Path,
    repo_url: str,
    version: str,
    signing_key_path: Path,
    generate_signing_key: bool,
    package_repo: str,
    package_ref: str,
    package_index_path: str,
    root_iso_path: Path | None,
    root_iso_repo_path: str,
) -> None:
    kernel = update_dir / "kernel.elf"
    efi = update_dir / "BOOTX64.EFI"
    limine = update_dir / "limine.conf"
    manifest = update_dir / "manifest.txt"

    for artifact in (kernel, efi, limine):
        if not artifact.exists():
            raise FileNotFoundError(f"Missing update artifact: {artifact}")

    package_commit, package_index_size, package_index_sha256, _package_url = (
        package_index_metadata(
            package_repo=package_repo,
            package_ref=package_ref,
            package_index_path=package_index_path,
        )
    )

    lines = [
        "manifest-version 1",
        "os Orizon OS",
        "channel main",
        f"version {version}",
        "commit public-main",
        f"source {repo_url}",
        "kernel-path updates/x86_64/kernel.elf",
        f"kernel-size {kernel.stat().st_size}",
        f"kernel-sha256 {sha256_file(kernel)}",
        "efi-path updates/x86_64/BOOTX64.EFI",
        f"efi-size {efi.stat().st_size}",
        f"efi-sha256 {sha256_file(efi)}",
        "limine-path updates/x86_64/limine.conf",
        f"limine-size {limine.stat().st_size}",
        f"limine-sha256 {sha256_file(limine)}",
        f"package-source {package_repo}",
        f"package-commit {package_commit}",
        f"package-index-path {package_index_path}",
        f"package-index-size {package_index_size}",
        f"package-index-sha256 {package_index_sha256}",
    ]
    if root_iso_path is not None:
        if not root_iso_path.exists():
            raise FileNotFoundError(f"Published root ISO not found: {root_iso_path}")
        lines.extend(
            [
                f"iso-path {root_iso_repo_path}",
                f"iso-size {root_iso_path.stat().st_size}",
                f"iso-sha256 {sha256_file(root_iso_path)}",
            ]
        )
    lines.append("")
    manifest.write_text("\n".join(lines), encoding="utf-8")
    print(f"Published update manifest: {manifest}")
    write_manifest_signature(
        manifest=manifest,
        signing_key_path=signing_key_path,
        generate_key=generate_signing_key,
    )


def validate_manifest_signature_metadata(manifest: Path, sig_path: Path) -> None:
    if not manifest.exists():
        raise FileNotFoundError(f"Missing signed manifest: {manifest}")
    if not sig_path.exists():
        raise FileNotFoundError(f"Missing manifest signature: {sig_path}")
    sig_text = sig_path.read_text(encoding="utf-8")
    algorithm = manifest_value(sig_text, "algorithm")
    key_id = manifest_value(sig_text, "key-id")
    manifest_sha = manifest_value(sig_text, "manifest-sha256")
    signature_hex = manifest_value(sig_text, "signature")
    if algorithm != "rsa-pkcs1-sha256":
        raise RuntimeError(f"Unexpected manifest signature algorithm: {algorithm}")
    if key_id != MANIFEST_SIGNING_KEY_ID:
        raise RuntimeError(f"Unexpected manifest signing key id: {key_id}")
    if manifest_sha != sha256_file(manifest):
        raise RuntimeError("manifest.sig does not match the current manifest.txt")
    if not signature_hex or not re.fullmatch(r"[0-9a-f]+", signature_hex):
        raise RuntimeError("manifest.sig does not contain a valid hex signature")


def validate_manifest_artifact(
    manifest_text: str, *, key: str, path: Path, expected_repo_path: str
) -> None:
    manifest_path = manifest_value(manifest_text, f"{key}-path")
    manifest_size = manifest_value(manifest_text, f"{key}-size")
    manifest_sha = manifest_value(manifest_text, f"{key}-sha256")
    if manifest_path != expected_repo_path:
        raise RuntimeError(
            f"Signed manifest {key}-path mismatch: {manifest_path} != {expected_repo_path}"
        )
    if manifest_size != str(path.stat().st_size):
        raise RuntimeError(f"Signed manifest {key}-size does not match {path}")
    if manifest_sha != sha256_file(path):
        raise RuntimeError(f"Signed manifest {key}-sha256 does not match {path}")


def validate_release_report_artifact(
    report_text: str, *, key: str, path: Path, expected_repo_path: str
) -> None:
    report_path = manifest_value(report_text, f"{key}-path")
    report_size = manifest_value(report_text, f"{key}-size")
    report_sha = manifest_value(report_text, f"{key}-sha256")
    if report_path != expected_repo_path:
        raise RuntimeError(
            f"release.txt {key}-path mismatch: {report_path} != {expected_repo_path}"
        )
    if report_size != str(path.stat().st_size):
        raise RuntimeError(f"release.txt {key}-size does not match {path}")
    if report_sha != sha256_file(path):
        raise RuntimeError(f"release.txt {key}-sha256 does not match {path}")


def validate_release_bundle(
    *,
    update_dir: Path,
    root_iso_path: Path | None,
    root_iso_repo_path: str,
) -> None:
    manifest = update_dir / "manifest.txt"
    sig_path = update_dir / "manifest.sig"
    release = update_dir / DEFAULT_RELEASE_REPORT
    required = [
        update_dir / "kernel.elf",
        update_dir / "BOOTX64.EFI",
        update_dir / "limine.conf",
        manifest,
        sig_path,
        release,
    ]
    for artifact in required:
        if not artifact.exists():
            raise FileNotFoundError(f"Release artifact missing: {artifact}")
    validate_manifest_signature_metadata(manifest, sig_path)
    manifest_text = manifest.read_text(encoding="utf-8")
    release_text = release.read_text(encoding="utf-8")

    validate_manifest_artifact(
        manifest_text,
        key="kernel",
        path=update_dir / "kernel.elf",
        expected_repo_path="updates/x86_64/kernel.elf",
    )
    validate_manifest_artifact(
        manifest_text,
        key="efi",
        path=update_dir / "BOOTX64.EFI",
        expected_repo_path="updates/x86_64/BOOTX64.EFI",
    )
    validate_manifest_artifact(
        manifest_text,
        key="limine",
        path=update_dir / "limine.conf",
        expected_repo_path="updates/x86_64/limine.conf",
    )
    for label, path in (
        ("kernel", update_dir / "kernel.elf"),
        ("efi", update_dir / "BOOTX64.EFI"),
        ("limine", update_dir / "limine.conf"),
        ("manifest", manifest),
        ("manifest-signature", sig_path),
    ):
        validate_release_report_artifact(
            release_text,
            key=label,
            path=path,
            expected_repo_path=repo_relative_path(path),
        )

    if root_iso_path is not None:
        if not root_iso_path.exists():
            raise FileNotFoundError(f"Release root ISO missing: {root_iso_path}")
        expected_iso_path = manifest_value(manifest_text, "iso-path")
        expected_iso_size = manifest_value(manifest_text, "iso-size")
        expected_iso_sha = manifest_value(manifest_text, "iso-sha256")
        actual_iso_size = str(root_iso_path.stat().st_size)
        actual_iso_sha = sha256_file(root_iso_path)
        if expected_iso_path != root_iso_repo_path:
            raise RuntimeError("Signed manifest iso-path does not match output ISO")
        if expected_iso_size != actual_iso_size:
            raise RuntimeError("Signed manifest iso-size does not match output ISO")
        if expected_iso_sha != actual_iso_sha:
            raise RuntimeError("Signed manifest iso-sha256 does not match output ISO")
        if manifest_value(release_text, "iso-path") != root_iso_repo_path:
            raise RuntimeError("release.txt iso-path does not match output ISO")
        if manifest_value(release_text, "iso-size") != actual_iso_size:
            raise RuntimeError("release.txt iso-size does not match output ISO")
        if manifest_value(release_text, "iso-sha256") != actual_iso_sha:
            raise RuntimeError("release.txt iso-sha256 does not match output ISO")

    print("Release bundle validation: OK")


def write_release_report(
    *,
    update_dir: Path,
    root_iso_path: Path | None,
    root_iso_repo_path: str,
    repo_url: str,
) -> None:
    report = update_dir / DEFAULT_RELEASE_REPORT
    manifest = update_dir / "manifest.txt"
    sig_path = update_dir / "manifest.sig"
    lines = [
        "release-report-version 1",
        f"created-utc {datetime.datetime.now(datetime.timezone.utc).isoformat()}",
        f"source {repo_url}",
    ]

    if root_iso_path is not None and root_iso_path.exists():
        lines.extend(
            [
                f"iso-path {root_iso_repo_path}",
                f"iso-size {root_iso_path.stat().st_size}",
                f"iso-sha256 {sha256_file(root_iso_path)}",
            ]
        )
    else:
        lines.append("iso-path not-published")

    for label, path in (
        ("kernel", update_dir / "kernel.elf"),
        ("efi", update_dir / "BOOTX64.EFI"),
        ("limine", update_dir / "limine.conf"),
        ("manifest", manifest),
        ("manifest-signature", sig_path),
    ):
        if path.exists():
            lines.extend(
                [
                    f"{label}-path {repo_relative_path(path)}",
                    f"{label}-size {path.stat().st_size}",
                    f"{label}-sha256 {sha256_file(path)}",
                ]
            )
        else:
            lines.append(f"{label}-path missing")
    lines.append("")
    report.write_text("\n".join(lines), encoding="utf-8")
    print(f"Published release report: {report}")


def publish_update_payloads_from_local_tree(
    *,
    source_dir: Path,
    update_dir: Path,
    repo_url: str,
    signing_key_path: Path,
    generate_signing_key: bool,
    package_repo: str,
    package_ref: str,
    package_index_path: str,
    root_iso_path: Path | None,
    root_iso_repo_path: str,
) -> None:
    update_dir.mkdir(parents=True, exist_ok=True)
    payloads = [
        (source_dir / "build" / "kernel.elf", update_dir / "kernel.elf"),
        (source_dir / "iso_root" / "EFI" / "BOOT" / "BOOTX64.EFI", update_dir / "BOOTX64.EFI"),
        (source_dir / "limine.conf", update_dir / "limine.conf"),
    ]
    for src, dst in payloads:
        if not src.exists():
            raise FileNotFoundError(f"Built payload not found: {src}")
        shutil.copy2(src, dst)
        print(f"Published update artifact: {dst}")
    write_update_manifest(
        update_dir=update_dir,
        repo_url=repo_url,
        version=update_version(),
        signing_key_path=signing_key_path,
        generate_signing_key=generate_signing_key,
        package_repo=package_repo,
        package_ref=package_ref,
        package_index_path=package_index_path,
        root_iso_path=root_iso_path,
        root_iso_repo_path=root_iso_repo_path,
    )


def publish_update_payloads_from_zimaos(
    *,
    client,
    remote_project_root: str,
    update_dir: Path,
    repo_url: str,
    signing_key_path: Path,
    generate_signing_key: bool,
    package_repo: str,
    package_ref: str,
    package_index_path: str,
    root_iso_path: Path | None,
    root_iso_repo_path: str,
) -> None:
    update_dir.mkdir(parents=True, exist_ok=True)
    payloads = [
        (f"{remote_project_root}/build/kernel.elf", update_dir / "kernel.elf"),
        (f"{remote_project_root}/iso_root/EFI/BOOT/BOOTX64.EFI", update_dir / "BOOTX64.EFI"),
        (f"{remote_project_root}/limine.conf", update_dir / "limine.conf"),
    ]
    sftp = client.open_sftp()
    try:
        for remote_src, local_dst in payloads:
            sftp.get(remote_src, str(local_dst))
            print(f"Published update artifact: {local_dst}")
    finally:
        sftp.close()
    write_update_manifest(
        update_dir=update_dir,
        repo_url=repo_url,
        version=update_version(),
        signing_key_path=signing_key_path,
        generate_signing_key=generate_signing_key,
        package_repo=package_repo,
        package_ref=package_ref,
        package_index_path=package_index_path,
        root_iso_path=root_iso_path,
        root_iso_repo_path=root_iso_repo_path,
    )


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
    publish_payloads: bool,
    update_dir: Path,
    github_repo: str,
    signing_key_path: Path,
    generate_signing_key: bool,
    package_repo: str,
    package_ref: str,
    package_index_path: str,
    root_iso_repo_path: str,
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
        "--no-publish-root-iso",
    ]
    if deploy_vm:
        cmd.append("--deploy-vm")
    run(cmd)

    if publish or publish_payloads:
        env_config = parse_env_file(REPO_ROOT / env_file)
        client = connect_ssh(env_config)
        try:
            remote_iso = (
                f"{remote_root.rstrip('/')}/workspace/"
                f"{Path(source_dir).name}/orizonos-x86_64.iso"
            )
            remote_project_root = (
                f"{remote_root.rstrip('/')}/workspace/{Path(source_dir).name}"
            )
            if publish:
                sftp = client.open_sftp()
                try:
                    sftp.get(remote_iso, str(output_iso))
                finally:
                    sftp.close()
                print(f"Published ISO: {output_iso}")
            if publish_payloads:
                publish_update_payloads_from_zimaos(
                    client=client,
                    remote_project_root=remote_project_root,
                    update_dir=update_dir,
                    repo_url=github_repo,
                    signing_key_path=signing_key_path,
                    generate_signing_key=generate_signing_key,
                    package_repo=package_repo,
                    package_ref=package_ref,
                    package_index_path=package_index_path,
                    root_iso_path=output_iso if publish else None,
                    root_iso_repo_path=root_iso_repo_path,
                )
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
        "--package-repo",
        default=DEFAULT_PACKAGE_REPO,
        help="Public Orizon package repository URL pinned into the signed manifest.",
    )
    parser.add_argument(
        "--package-ref",
        default=DEFAULT_PACKAGE_REF,
        help="Package repository branch, tag, or commit pinned into the signed manifest.",
    )
    parser.add_argument(
        "--package-index-path",
        default=DEFAULT_PACKAGE_INDEX_PATH,
        help="Package index path inside the package repository.",
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
        "--no-publish-update-payloads",
        action="store_true",
        help="Build/update without refreshing updates/x86_64 artifacts.",
    )
    parser.add_argument(
        "--update-dir",
        default=DEFAULT_UPDATE_DIR,
        help="Directory where update payloads and manifest are published.",
    )
    parser.add_argument(
        "--manifest-signing-key",
        default=DEFAULT_MANIFEST_SIGNING_KEY,
        help="Local private key used to sign updates/x86_64/manifest.sig.",
    )
    parser.add_argument(
        "--generate-manifest-signing-key",
        action="store_true",
        help=(
            "Bootstrap a local key file if missing; the generated key must be "
            "embedded as a rotated kernel update root before publishing."
        ),
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
    publish_payloads = not args.no_publish_update_payloads
    update_dir = REPO_ROOT / args.update_dir
    signing_key_path = REPO_ROOT / args.manifest_signing_key
    root_iso_repo_path = repo_relative_path(output_iso)

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
        if publish_payloads:
            publish_update_payloads_from_local_tree(
                source_dir=source_dir,
                update_dir=update_dir,
                repo_url=args.github_repo,
                signing_key_path=signing_key_path,
                generate_signing_key=args.generate_manifest_signing_key,
                package_repo=args.package_repo,
                package_ref=args.package_ref,
                package_index_path=args.package_index_path,
                root_iso_path=output_iso if publish else None,
                root_iso_repo_path=root_iso_repo_path,
            )
    elif args.mode == "zimaos-iso":
        build_on_zimaos(
            env_file=args.env_file,
            vm_config=args.vm_config,
            source_dir=args.source_dir,
            remote_root=args.remote_root,
            deploy_vm=False,
            publish=publish,
            output_iso=output_iso,
            publish_payloads=publish_payloads,
            update_dir=update_dir,
            github_repo=args.github_repo,
            signing_key_path=signing_key_path,
            generate_signing_key=args.generate_manifest_signing_key,
            package_repo=args.package_repo,
            package_ref=args.package_ref,
            package_index_path=args.package_index_path,
            root_iso_repo_path=root_iso_repo_path,
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
            publish_payloads=publish_payloads,
            update_dir=update_dir,
            github_repo=args.github_repo,
            signing_key_path=signing_key_path,
            generate_signing_key=args.generate_manifest_signing_key,
            package_repo=args.package_repo,
            package_ref=args.package_ref,
            package_index_path=args.package_index_path,
            root_iso_repo_path=root_iso_repo_path,
        )

    if args.mode != "github-iso" and publish_payloads:
        write_release_report(
            update_dir=update_dir,
            root_iso_path=output_iso if publish else None,
            root_iso_repo_path=root_iso_repo_path,
            repo_url=args.github_repo,
        )
        validate_release_bundle(
            update_dir=update_dir,
            root_iso_path=output_iso if publish else None,
            root_iso_repo_path=root_iso_repo_path,
        )

    print("Orizon update complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
