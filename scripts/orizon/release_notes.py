from __future__ import annotations

import argparse
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def read_key_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or " " not in line:
            continue
        key, value = line.split(" ", 1)
        values[key] = value.strip()
    return values


def pretty_size(raw_value: str | None) -> str:
    if not raw_value:
        return "unknown"
    try:
        size = int(raw_value)
    except ValueError:
        return raw_value
    mib = size / (1024 * 1024)
    if mib >= 1:
        return f"{size} bytes ({mib:.2f} MiB)"
    kib = size / 1024
    return f"{size} bytes ({kib:.2f} KiB)"


def short_sha(raw_value: str | None) -> str:
    if not raw_value:
        return "unknown"
    return raw_value[:12]


def unreleased_changelog(path: Path) -> list[str]:
    if not path.exists():
        return []
    lines = path.read_text(encoding="utf-8").splitlines()
    captured: list[str] = []
    inside = False
    for line in lines:
        if line.startswith("## "):
            if inside:
                break
            inside = line.strip().lower() == "## unreleased"
            continue
        if inside:
            captured.append(line)
    while captured and not captured[0].strip():
        captured.pop(0)
    while captured and not captured[-1].strip():
        captured.pop()
    return captured


def build_notes(release: dict[str, str], manifest: dict[str, str]) -> str:
    lines = [
        "# Orizon OS",
        "",
        "Minimal x86_64 development release.",
        "",
        "## Artifacts",
        "",
        f"- ISO: `{release.get('iso-path', manifest.get('iso-path', 'Orizon-OS.iso'))}` size {pretty_size(release.get('iso-size', manifest.get('iso-size')))} sha256 `{short_sha(release.get('iso-sha256', manifest.get('iso-sha256')))}...`",
        f"- Kernel: `{release.get('kernel-path', manifest.get('kernel-path', 'updates/x86_64/kernel.elf'))}` size {pretty_size(release.get('kernel-size', manifest.get('kernel-size')))} sha256 `{short_sha(release.get('kernel-sha256', manifest.get('kernel-sha256')))}...`",
        f"- UEFI loader: `{release.get('efi-path', manifest.get('efi-path', 'updates/x86_64/BOOTX64.EFI'))}` size {pretty_size(release.get('efi-size', manifest.get('efi-size')))} sha256 `{short_sha(release.get('efi-sha256', manifest.get('efi-sha256')))}...`",
        f"- Manifest: `{release.get('manifest-path', 'updates/x86_64/manifest.txt')}` sha256 `{short_sha(release.get('manifest-sha256'))}...`",
        f"- Manifest signature: `{release.get('manifest-signature-path', 'updates/x86_64/manifest.sig')}` sha256 `{short_sha(release.get('manifest-signature-sha256'))}...`",
        "",
        "## Update Metadata",
        "",
        f"- Channel: `{manifest.get('channel', 'unknown')}`",
        f"- Version: `{manifest.get('version', 'unknown')}`",
        f"- Source: `{manifest.get('source', release.get('source', 'unknown'))}`",
        f"- Package index: `{manifest.get('package-index-path', 'unknown')}` at commit `{manifest.get('package-commit', 'unknown')}`",
        "",
        "## Validation Scope",
        "",
        "- These notes are generated from committed release metadata.",
        "- This release does not claim Lenovo or other real-PC hardware validation.",
        "- Use the VM smoke/matrix tests separately when a release needs runtime validation.",
    ]

    changelog_lines = unreleased_changelog(REPO_ROOT / "CHANGELOG.md")
    if changelog_lines:
        lines.extend(["", "## Changelog", ""])
        lines.extend(changelog_lines)

    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate concise GitHub release notes from Orizon metadata."
    )
    parser.add_argument(
        "--release",
        type=Path,
        default=Path("updates/x86_64/release.txt"),
        help="Path to updates/x86_64/release.txt.",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("updates/x86_64/manifest.txt"),
        help="Path to updates/x86_64/manifest.txt.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write release notes to this path instead of stdout.",
    )
    args = parser.parse_args()

    release_path = args.release if args.release.is_absolute() else REPO_ROOT / args.release
    manifest_path = (
        args.manifest if args.manifest.is_absolute() else REPO_ROOT / args.manifest
    )

    notes = build_notes(
        release=read_key_values(release_path),
        manifest=read_key_values(manifest_path),
    )

    if args.output:
        output_path = args.output if args.output.is_absolute() else REPO_ROOT / args.output
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(notes, encoding="utf-8")
    else:
        print(notes, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
