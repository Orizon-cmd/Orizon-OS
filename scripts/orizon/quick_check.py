from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class CheckRunner:
    def __init__(self, log_path: Path | None, fail_fast: bool) -> None:
        self.log_path = log_path
        self.fail_fast = fail_fast
        self.failures: list[str] = []
        self.skipped: list[str] = []
        self._log_lines: list[str] = []

    def emit(self, line: str = "") -> None:
        print(line)
        self._log_lines.append(line)

    def skip(self, name: str, reason: str) -> None:
        self.skipped.append(name)
        self.emit(f"SKIP {name}: {reason}")

    def run(self, name: str, command: list[str]) -> bool:
        self.emit(f"== {name} ==")
        self.emit("$ " + " ".join(command))
        try:
            result = subprocess.run(
                command,
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        except FileNotFoundError as exc:
            self.failures.append(name)
            self.emit(f"FAIL {name}: {exc}")
            if self.fail_fast:
                self.write_log()
                raise SystemExit(1)
            return False

        if result.stdout:
            self.emit(result.stdout.rstrip())
        if result.returncode == 0:
            self.emit(f"PASS {name}")
            return True

        self.failures.append(name)
        self.emit(f"FAIL {name}: exit {result.returncode}")
        if self.fail_fast:
            self.write_log()
            raise SystemExit(result.returncode)
        return False

    def write_log(self) -> None:
        if not self.log_path:
            return
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path.write_text("\n".join(self._log_lines) + "\n", encoding="utf-8")

    def finish(self) -> int:
        self.emit("== summary ==")
        if self.skipped:
            self.emit("skipped: " + ", ".join(self.skipped))
        if self.failures:
            self.emit("failed: " + ", ".join(self.failures))
            self.write_log()
            return 1
        self.emit("all quick checks passed")
        self.write_log()
        return 0


def powershell_executable() -> str | None:
    return (
        shutil.which("pwsh")
        or shutil.which("powershell")
        or shutil.which("powershell.exe")
    )


def powershell_literal(value: Path) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def run_powershell_syntax(runner: CheckRunner) -> None:
    executable = powershell_executable()
    if not executable:
        runner.skip("PowerShell syntax", "pwsh/powershell not found")
        return

    scripts = sorted((REPO_ROOT / "scripts" / "orizon").glob("*.ps1"))
    if not scripts:
        runner.skip("PowerShell syntax", "no scripts/orizon/*.ps1 files")
        return

    parser_commands: list[str] = []
    for script in scripts:
        parser_commands.append(
            "$tokens=$null; $errors=$null; "
            f"[System.Management.Automation.Language.Parser]::ParseFile({powershell_literal(script)}, [ref]$tokens, [ref]$errors) | Out-Null; "
            "if ($errors.Count -gt 0) { $errors | ForEach-Object { Write-Error $_.Message }; exit 1 }; "
            f"Write-Output 'ps1 syntax ok: {script.name}'"
        )

    runner.run(
        "PowerShell syntax",
        [
            executable,
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            "; ".join(parser_commands),
        ],
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run fast local checks before committing Orizon OS changes."
    )
    parser.add_argument(
        "--skip-release",
        action="store_true",
        help="Skip the release artifact consistency validator.",
    )
    parser.add_argument(
        "--skip-powershell",
        action="store_true",
        help="Skip PowerShell syntax parsing.",
    )
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="Stop on the first failed check.",
    )
    parser.add_argument(
        "--log",
        type=Path,
        help="Optional path where the combined quick-check log is written.",
    )
    args = parser.parse_args()

    log_path = args.log
    if log_path and not log_path.is_absolute():
        log_path = REPO_ROOT / log_path

    runner = CheckRunner(log_path=log_path, fail_fast=args.fail_fast)
    runner.run("git diff whitespace", ["git", "diff", "--check"])

    python_scripts = sorted((REPO_ROOT / "scripts" / "orizon").glob("*.py"))
    runner.run(
        "Python syntax",
        [
            sys.executable,
            "-m",
            "py_compile",
            *[str(path.relative_to(REPO_ROOT)) for path in python_scripts],
        ],
    )

    if args.skip_powershell:
        runner.skip("PowerShell syntax", "--skip-powershell requested")
    else:
        run_powershell_syntax(runner)

    if args.skip_release:
        runner.skip("release validator", "--skip-release requested")
    else:
        runner.run(
            "release validator",
            [
                sys.executable,
                "scripts/orizon/orizon_update.py",
                "--mode",
                "validate-release",
            ],
        )

    return runner.finish()


if __name__ == "__main__":
    raise SystemExit(main())
