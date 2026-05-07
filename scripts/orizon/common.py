from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict

import paramiko


def parse_env_file(path: Path) -> Dict[str, str]:
    data: Dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key.strip()] = value.strip()
    return data


def read_required(config: Dict[str, str], key: str) -> str:
    value = config.get(key, "").strip()
    if not value:
        raise ValueError(f"Missing required setting: {key}")
    return value


def load_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def connect_ssh(config: Dict[str, str]) -> paramiko.SSHClient:
    host = read_required(config, "ZIMAOS_HOST")
    port = int(config.get("ZIMAOS_PORT", "22"))
    user = read_required(config, "ZIMAOS_USER")
    password = read_required(config, "ZIMAOS_PASSWORD")

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, port=port, username=user, password=password, timeout=20)
    return client


def run_command(client: paramiko.SSHClient, command: str) -> str:
    _stdin, stdout, stderr = client.exec_command(command)
    output = stdout.read().decode("utf-8", errors="replace").strip()
    error = stderr.read().decode("utf-8", errors="replace").strip()
    if error and not output:
        return error
    return output


def run_sudo_command(
    client: paramiko.SSHClient, sudo_password: str, command: str
) -> str:
    stdin, stdout, stderr = client.exec_command(
        f"sudo -S -p '' {command}", get_pty=True
    )
    stdin.write(sudo_password + "\n")
    stdin.flush()

    output = stdout.read().decode("utf-8", errors="replace")
    error = stderr.read().decode("utf-8", errors="replace")
    output = output.replace(sudo_password, "").strip()
    error = error.replace(sudo_password, "").strip()
    if error and not output:
        return error
    return output
