from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
UPDATE_DIR = REPO_ROOT / "updates" / "x86_64"

REQUIRED_RELEASE_ARTIFACTS = [
    ("iso", REPO_ROOT / "Orizon-OS.iso", "Orizon-OS.iso"),
    ("kernel", UPDATE_DIR / "kernel.elf", "updates/x86_64/kernel.elf"),
    ("efi", UPDATE_DIR / "BOOTX64.EFI", "updates/x86_64/BOOTX64.EFI"),
    ("limine", UPDATE_DIR / "limine.conf", "updates/x86_64/limine.conf"),
    ("manifest", UPDATE_DIR / "manifest.txt", "updates/x86_64/manifest.txt"),
    (
        "manifest-signature",
        UPDATE_DIR / "manifest.sig",
        "updates/x86_64/manifest.sig",
    ),
    ("release", UPDATE_DIR / "release.txt", "updates/x86_64/release.txt"),
]

CORE_RELEASE_ARTIFACT_PATHS = {
    "Orizon-OS.iso",
    "updates/x86_64/kernel.elf",
    "updates/x86_64/manifest.txt",
    "updates/x86_64/manifest.sig",
    "updates/x86_64/release.txt",
}

RUNTIME_SOURCE_PREFIXES = (
    "orizon-os-x86_64/",
)

RUNTIME_SOURCE_EXCLUDES = (
    "orizon-os-x86_64/build/",
    "orizon-os-x86_64/iso_root/",
    "orizon-os-x86_64/orizonos-x86_64.iso",
)


class GuardError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_key_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        raise GuardError(f"missing metadata file: {path.relative_to(REPO_ROOT)}")
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or " " not in line:
            continue
        key, value = line.split(" ", 1)
        values[key] = value.strip()
    return values


def run_logged(name: str, command: list[str], log_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    header = f"== {name} ==\n$ {' '.join(command)}\n"
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    log_path.write_text(header + (result.stdout or ""), encoding="utf-8")
    if result.returncode != 0:
        raise GuardError(
            f"{name} failed with exit {result.returncode}; see "
            f"{log_path.relative_to(REPO_ROOT)}"
        )


def repo_path(path: str) -> str:
    return path.replace("\\", "/").strip()


def is_runtime_source_path(path: str) -> bool:
    normalized = repo_path(path)
    if any(normalized.startswith(prefix) for prefix in RUNTIME_SOURCE_EXCLUDES):
        return False
    return any(normalized.startswith(prefix) for prefix in RUNTIME_SOURCE_PREFIXES)


def resolve_changed_from(explicit: str | None) -> str:
    if explicit:
        return explicit.strip()
    for name in ("ORIZON_CI_CHANGED_FROM", "GITHUB_BASE_SHA", "GITHUB_EVENT_BEFORE"):
        value = os.environ.get(name, "").strip()
        if value:
            return value
    return ""


def git_changed_paths(changed_from: str) -> list[str]:
    if not changed_from or set(changed_from) == {"0"}:
        return []
    rev = changed_from.strip()
    verify = subprocess.run(
        ["git", "rev-parse", "--verify", f"{rev}^{{commit}}"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if verify.returncode != 0:
        raise GuardError(
            f"cannot resolve changed-from ref {rev!r}; use checkout fetch-depth 0 "
            "or pass --skip-source-artifact-check"
        )
    result = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=ACMRT", f"{rev}..HEAD"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        raise GuardError(f"git diff failed for source/artifact check: {result.stdout}")
    return [repo_path(line) for line in result.stdout.splitlines() if line.strip()]


def validate_source_artifact_sync(
    *,
    changed_from: str,
    output_dir: Path,
) -> dict[str, object]:
    changed = git_changed_paths(changed_from)
    runtime_changed = [path for path in changed if is_runtime_source_path(path)]
    artifact_changed = [
        path
        for path in changed
        if path in {item[2] for item in REQUIRED_RELEASE_ARTIFACTS}
    ]
    required = set(CORE_RELEASE_ARTIFACT_PATHS)
    if any(path.endswith("/limine.conf") for path in runtime_changed):
        required.add("updates/x86_64/limine.conf")
    missing = sorted(required - set(artifact_changed)) if runtime_changed else []

    report: dict[str, object] = {
        "changed_from": changed_from or "",
        "changed_paths": changed,
        "runtime_source_changed": runtime_changed,
        "release_artifacts_changed": artifact_changed,
        "required_artifacts_for_runtime_change": sorted(required)
        if runtime_changed
        else [],
        "missing_required_artifacts": missing,
        "status": "SKIP" if not changed_from else ("FAIL" if missing else "PASS"),
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "source-artifact-sync.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    lines = [
        "# Source Artifact Sync",
        "",
        f"- Status: {report['status']}",
        f"- Changed-from: `{changed_from or 'not provided'}`",
        f"- Runtime source changes: {len(runtime_changed)}",
        f"- Release artifact changes: {len(artifact_changed)}",
    ]
    if runtime_changed:
        lines.extend(["", "## Runtime Source Changes", ""])
        lines.extend(f"- `{path}`" for path in runtime_changed)
    if artifact_changed:
        lines.extend(["", "## Release Artifact Changes", ""])
        lines.extend(f"- `{path}`" for path in artifact_changed)
    if missing:
        lines.extend(["", "## Missing Required Artifacts", ""])
        lines.extend(f"- `{path}`" for path in missing)
    (output_dir / "source-artifact-sync.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )
    if missing:
        raise GuardError(
            "runtime source changed without refreshed release artifacts: "
            + ", ".join(missing)
        )
    return report


def validate_required_artifacts() -> list[dict[str, str | int]]:
    release = read_key_values(UPDATE_DIR / "release.txt")
    manifest = read_key_values(UPDATE_DIR / "manifest.txt")
    rows: list[dict[str, str | int]] = []

    for label, path, expected_repo_path in REQUIRED_RELEASE_ARTIFACTS:
        if not path.exists():
            raise GuardError(f"required artifact missing: {expected_repo_path}")
        actual_size = path.stat().st_size
        actual_sha = sha256_file(path)
        if label == "release":
            if release.get("release-report-version") != "1":
                raise GuardError("release.txt missing release-report-version 1")
        else:
            release_path = release.get(f"{label}-path")
            release_size = release.get(f"{label}-size")
            release_sha = release.get(f"{label}-sha256")
            if release_path != expected_repo_path:
                raise GuardError(
                    f"release.txt {label}-path mismatch: "
                    f"{release_path!r} != {expected_repo_path!r}"
                )
            if release_size != str(actual_size):
                raise GuardError(
                    f"release.txt {label}-size mismatch for {expected_repo_path}: "
                    f"{release_size!r} != {actual_size}"
                )
            if release_sha != actual_sha:
                raise GuardError(
                    f"release.txt {label}-sha256 mismatch for {expected_repo_path}"
                )
        rows.append(
            {
                "label": label,
                "path": expected_repo_path,
                "size": actual_size,
                "sha256": actual_sha,
            }
        )

    for label in ("iso", "kernel", "efi", "limine"):
        path = next(row["path"] for row in rows if row["label"] == label)
        size = next(row["size"] for row in rows if row["label"] == label)
        sha = next(row["sha256"] for row in rows if row["label"] == label)
        if manifest.get(f"{label}-path") != path:
            raise GuardError(f"manifest.txt {label}-path mismatch")
        if manifest.get(f"{label}-size") != str(size):
            raise GuardError(f"manifest.txt {label}-size mismatch")
        if manifest.get(f"{label}-sha256") != sha:
            raise GuardError(f"manifest.txt {label}-sha256 mismatch")

    return rows


def write_summary(
    rows: list[dict[str, str | int]],
    output_dir: Path,
    source_sync: dict[str, object],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    summary = output_dir / "release-summary.md"
    data = output_dir / "release-artifacts.json"

    lines = [
        "# Orizon CI Release Guard",
        "",
        "## Result",
        "",
        "- Quick checks: required",
        "- Secret scan: required by quick checks",
        "- Release validation: required",
        f"- Source/artifact sync: {source_sync.get('status', 'SKIP')}",
        "- Real hardware validation: not claimed",
        "",
        "## Required Artifacts",
        "",
        "| Label | Path | Size | SHA-256 |",
        "| --- | --- | ---: | --- |",
    ]
    for row in rows:
        lines.append(
            f"| {row['label']} | `{row['path']}` | {row['size']} | "
            f"`{str(row['sha256'])[:16]}...` |"
        )
    lines.append("")
    lines.append(
        "If this guard fails, rebuild with `python scripts/orizon/orizon_update.py "
        "--mode zimaos-iso` and commit the refreshed ISO, update payload, manifest, "
        "signature, and release report together."
    )
    lines.extend(
        [
            "",
            "## Source Artifact Sync",
            "",
            f"- Runtime source changes: {len(source_sync.get('runtime_source_changed', []))}",
            f"- Release artifact changes: {len(source_sync.get('release_artifacts_changed', []))}",
            "- Details: `source-artifact-sync.md` and `source-artifact-sync.json`",
        ]
    )
    summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    data.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "CI entrypoint for Orizon quick checks, release metadata, secret "
            "scan, and artifact synchronization diagnostics."
        )
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("artifacts"),
        help="Directory for CI logs, release notes, and artifact summaries.",
    )
    parser.add_argument(
        "--skip-quick-check",
        action="store_true",
        help="Only validate release metadata and write summaries.",
    )
    parser.add_argument(
        "--changed-from",
        help=(
            "Base commit/ref for enforcing source-to-artifact synchronization. "
            "Defaults to ORIZON_CI_CHANGED_FROM/GITHUB_BASE_SHA/GITHUB_EVENT_BEFORE."
        ),
    )
    parser.add_argument(
        "--skip-source-artifact-check",
        action="store_true",
        help="Do not enforce runtime source changes requiring refreshed release artifacts.",
    )
    args = parser.parse_args()

    output_dir = args.output_dir if args.output_dir.is_absolute() else REPO_ROOT / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        if not args.skip_quick_check:
            run_logged(
                "quick checks",
                [
                    sys.executable,
                    "scripts/orizon/quick_check.py",
                    "--skip-release",
                    "--fail-fast",
                    "--log",
                    str(output_dir / "quick-check.log"),
                ],
                output_dir / "quick-check-wrapper.log",
            )
        run_logged(
            "release validation",
            [sys.executable, "scripts/orizon/orizon_update.py", "--mode", "validate-release"],
            output_dir / "release-validation.log",
        )
        run_logged(
            "release notes",
            [
                sys.executable,
                "scripts/orizon/release_notes.py",
                "--output",
                str(output_dir / "release-notes.md"),
            ],
            output_dir / "release-notes.log",
        )
        source_sync = {"status": "SKIP"}
        if not args.skip_source_artifact_check:
            source_sync = validate_source_artifact_sync(
                changed_from=resolve_changed_from(args.changed_from),
                output_dir=output_dir,
            )
        rows = validate_required_artifacts()
        write_summary(rows, output_dir, source_sync)
    except GuardError as exc:
        print(f"CI release guard: FAIL: {exc}", file=sys.stderr)
        return 1

    print(f"CI release guard: PASS ({len(REQUIRED_RELEASE_ARTIFACTS)} artifacts)")
    print(f"summary: {(output_dir / 'release-summary.md').relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
