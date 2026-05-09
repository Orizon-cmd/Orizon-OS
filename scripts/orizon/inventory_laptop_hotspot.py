from __future__ import annotations

import argparse
from pathlib import Path

import paramiko

from common import connect_ssh, parse_env_file, read_required


INVENTORY_SCRIPT = r"""
set +e
echo "===== OS ====="
cat /etc/os-release 2>/dev/null | sed -n '1,20p'
printf 'KERNEL='
uname -a 2>/dev/null

echo
echo "===== COMPUTER ====="
cat /sys/devices/virtual/dmi/id/sys_vendor 2>/dev/null | sed 's/^/vendor=/'
cat /sys/devices/virtual/dmi/id/product_name 2>/dev/null | sed 's/^/product=/'
cat /sys/devices/virtual/dmi/id/product_version 2>/dev/null | sed 's/^/version=/'
cat /sys/devices/virtual/dmi/id/board_name 2>/dev/null | sed 's/^/board=/'

echo
echo "===== CPU ====="
lscpu 2>/dev/null | sed -n '1,32p'

echo
echo "===== PCI ====="
lspci -nnk 2>/dev/null || lspci -nn 2>/dev/null || \
  find /sys/bus/pci/devices -maxdepth 1 -mindepth 1 -printf '%f\n' 2>/dev/null

echo
echo "===== USB ====="
lsusb 2>/dev/null || true

echo
echo "===== INPUT ====="
cat /proc/bus/input/devices 2>/dev/null || true

echo
echo "===== I2C ====="
for f in /sys/bus/i2c/devices/*/name; do
  [ -f "$f" ] && printf '%s: ' "$f" && cat "$f"
done 2>/dev/null

echo
echo "===== NET ====="
ip -br link 2>/dev/null || true
ip -br addr 2>/dev/null || true

echo
echo "===== STORAGE ====="
lsblk -o NAME,MODEL,SIZE,TYPE,TRAN,ROTA,MOUNTPOINTS 2>/dev/null || true

echo
echo "===== DMESG_TARGET ====="
(dmesg 2>/dev/null || echo 'dmesg-permission-denied') | \
  grep -Ei 'touch|track|i2c|hid|elan|synapt|precision|psmouse|i8042|nvme|ahci|wifi|wireless|bluetooth|xhci|keyboard|mouse|ethernet|r816|e1000|igc|realtek|intel|wacom|battery|acpi' | \
  tail -260
"""


def run_script(client: paramiko.SSHClient, script: str, timeout: int) -> int:
    stdin, stdout, stderr = client.exec_command("bash -s", timeout=timeout)
    stdin.write(script)
    stdin.channel.shutdown_write()
    exit_status = stdout.channel.recv_exit_status()
    output = stdout.read().decode("utf-8", errors="replace")
    error = stderr.read().decode("utf-8", errors="replace")
    print(output, end="")
    if error.strip():
        print("===== STDERR =====")
        print(error, end="")
    return exit_status


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inventory a laptop reachable through the ZimaOS hotspot."
    )
    parser.add_argument(
        "--zima-env",
        default="config/hosts/zimaos.local.env",
        help="Local ZimaOS env file used as the SSH jump host.",
    )
    parser.add_argument(
        "--laptop-env",
        default="config/hosts/laptop-hotspot.local.env",
        help="Local laptop env file. See config/hosts/laptop-hotspot.env.example.",
    )
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()

    zima_config = parse_env_file(Path(args.zima_env))
    laptop_config = parse_env_file(Path(args.laptop_env))
    laptop_host = read_required(laptop_config, "LAPTOP_HOST")
    laptop_port = int(laptop_config.get("LAPTOP_PORT", "22"))
    laptop_user = read_required(laptop_config, "LAPTOP_USER")
    laptop_password = read_required(laptop_config, "LAPTOP_PASSWORD")

    zima = connect_ssh(zima_config)
    laptop = paramiko.SSHClient()
    laptop.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        channel = zima.get_transport().open_channel(
            "direct-tcpip", (laptop_host, laptop_port), ("127.0.0.1", 0)
        )
        laptop.connect(
            laptop_host,
            port=laptop_port,
            username=laptop_user,
            password=laptop_password,
            timeout=20,
            sock=channel,
            look_for_keys=False,
            allow_agent=False,
        )
        return run_script(laptop, INVENTORY_SCRIPT, args.timeout)
    finally:
        laptop.close()
        zima.close()


if __name__ == "__main__":
    raise SystemExit(main())
