from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
UPDATE_DIR = REPO_ROOT / "updates" / "x86_64"

REQUIRED_RELEASE_ARTIFACTS = [
    ("iso", REPO_ROOT / "Orizon-OS.iso", "Orizon-OS.iso"),
    ("kernel", UPDATE_DIR / "kernel.elf", "updates/x86_64/kernel.elf"),
    ("manifest", UPDATE_DIR / "manifest.txt", "updates/x86_64/manifest.txt"),
    (
        "manifest-signature",
        UPDATE_DIR / "manifest.sig",
        "updates/x86_64/manifest.sig",
    ),
    ("release", UPDATE_DIR / "release.txt", "updates/x86_64/release.txt"),
]


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

    for label in ("iso", "kernel"):
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


def write_summary(rows: list[dict[str, str | int]], output_dir: Path) -> None:
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
        rows = validate_required_artifacts()
        write_summary(rows, output_dir)
    except GuardError as exc:
        print(f"CI release guard: FAIL: {exc}", file=sys.stderr)
        return 1

    print(f"CI release guard: PASS ({len(REQUIRED_RELEASE_ARTIFACTS)} artifacts)")
    print(f"summary: {(output_dir / 'release-summary.md').relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
