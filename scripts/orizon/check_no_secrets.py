from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]

BINARY_SUFFIXES = {
    ".bin",
    ".efi",
    ".elf",
    ".exe",
    ".iso",
    ".sys",
    ".ucode",
}

DENIED_PATH_PATTERNS = [
    re.compile(r"(^|/)config/keys/"),
    re.compile(r"(^|/)config/hosts/.*\.local\.(env|json)$"),
    re.compile(r"(^|/)[^/]+\.local\.(env|json)$"),
    re.compile(r"(^|/)[^/]+\.private\.pem$"),
    re.compile(r"(^|/)\.env$"),
]

SECRET_CONTENT_PATTERNS = [
    ("private key block", re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")),
    ("github token", re.compile(r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9_]{30,}\b")),
    ("github fine-grained token", re.compile(r"\bgithub_pat_[A-Za-z0-9_]{40,}\b")),
    ("aws access key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("slack token", re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{20,}\b")),
]


def git_tracked_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    raw = result.stdout.decode("utf-8", errors="replace")
    return [part for part in raw.split("\0") if part]


def normalized(path: str) -> str:
    return path.replace("\\", "/")


def is_binary_path(path: str) -> bool:
    suffixes = Path(path).suffixes
    if any(suffix.lower() in BINARY_SUFFIXES for suffix in suffixes):
        return True
    if ".dSYM/" in normalized(path):
        return True
    return False


def scan_path_rules(paths: list[str]) -> list[str]:
    findings: list[str] = []
    for path in paths:
        clean = normalized(path)
        for pattern in DENIED_PATH_PATTERNS:
            if pattern.search(clean):
                findings.append(f"{path}: tracked sensitive/local path")
                break
    return findings


def scan_content(paths: list[str]) -> list[str]:
    findings: list[str] = []
    for path in paths:
        clean = normalized(path)
        if is_binary_path(clean):
            continue
        full_path = REPO_ROOT / path
        try:
            data = full_path.read_bytes()
        except OSError as exc:
            findings.append(f"{path}: cannot read file: {exc}")
            continue
        if b"\0" in data[:4096]:
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        for name, pattern in SECRET_CONTENT_PATTERNS:
            match = pattern.search(text)
            if match:
                line_no = text[: match.start()].count("\n") + 1
                findings.append(f"{path}:{line_no}: possible {name}")
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail if Git tracks obvious local secrets or private material."
    )
    parser.parse_args()

    paths = git_tracked_files()
    findings = scan_path_rules(paths) + scan_content(paths)
    if findings:
        print("secret check: FAIL")
        for finding in findings:
            print(f"  {finding}")
        return 1

    print(f"secret check: PASS ({len(paths)} tracked paths scanned)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
