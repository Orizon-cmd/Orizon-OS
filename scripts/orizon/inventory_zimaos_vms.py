from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Dict, List

from common import (
    connect_ssh,
    parse_env_file,
    read_required,
    run_command,
    run_sudo_command,
    write_json,
)


def parse_domain_xml(xml_text: str) -> Dict[str, Any]:
    root = ET.fromstring(xml_text)
    domain: Dict[str, Any] = {
        "name": root.findtext("name", default=""),
        "uuid": root.findtext("uuid", default=""),
        "title": root.findtext("title", default=""),
        "memory_kib": int(root.findtext("memory", default="0")),
        "vcpu_count": int(root.findtext("vcpu", default="0")),
        "firmware_loader": root.findtext("os/loader", default=""),
        "nvram_path": root.findtext("os/nvram", default=""),
        "disks": [],
        "interfaces": [],
    }

    for disk in root.findall("./devices/disk"):
        if disk.get("device") != "disk":
            continue
        source = disk.find("source")
        target = disk.find("target")
        driver = disk.find("driver")
        domain["disks"].append(
            {
                "path": source.get("file", "") if source is not None else "",
                "target": target.get("dev", "") if target is not None else "",
                "bus": target.get("bus", "") if target is not None else "",
                "format": driver.get("type", "") if driver is not None else "",
            }
        )

    for interface in root.findall("./devices/interface"):
        source = interface.find("source")
        model = interface.find("model")
        domain["interfaces"].append(
            {
                "type": interface.get("type", ""),
                "source_dev": source.get("dev", "") if source is not None else "",
                "source_mode": source.get("mode", "") if source is not None else "",
                "model": model.get("type", "") if model is not None else "",
            }
        )

    return domain


def collect_inventory(env_file: Path) -> Dict[str, Any]:
    config = parse_env_file(env_file)
    sudo_password = config.get("ZIMAOS_SUDO_PASSWORD", read_required(config, "ZIMAOS_PASSWORD"))
    client = connect_ssh(config)
    try:
        domains_text = run_sudo_command(client, sudo_password, "virsh list --all --name")
        domain_names = [line.strip() for line in domains_text.splitlines() if line.strip()]

        inventory: Dict[str, Any] = {
            "host": read_required(config, "ZIMAOS_HOST"),
            "vm_storage_root": "/DATA/VM",
            "libvirt_root": "/DATA/.libvirt",
            "casaos_virt_root": "/DATA/.casaos/virt",
            "domains": [],
        }

        for name in domain_names:
            xml_text = run_sudo_command(client, sudo_password, f"virsh dumpxml {name}")
            parsed = parse_domain_xml(xml_text)

            disk_details: List[Dict[str, Any]] = []
            for disk in parsed["disks"]:
                disk_path = disk["path"]
                info_text = run_sudo_command(
                    client,
                    sudo_password,
                    f"qemu-img info --output json {json.dumps(disk_path)}",
                )
                try:
                    disk_info = json.loads(info_text)
                except json.JSONDecodeError:
                    disk_info = {"raw_output": info_text}
                disk_details.append({**disk, "qemu_img": disk_info})

            state = run_sudo_command(client, sudo_password, f"virsh domstate {name}")
            parsed["state"] = state.strip()
            parsed["disks"] = disk_details
            inventory["domains"].append(parsed)

        inventory["vm_storage_listing"] = run_command(
            client, "ls -lah /DATA/VM 2>/dev/null"
        )
        return inventory
    finally:
        client.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Capture the ZimaOS VM inventory and save it locally."
    )
    parser.add_argument(
        "--env-file",
        default="config/hosts/zimaos.local.env",
        help="Path to the local env file with the ZimaOS connection details.",
    )
    parser.add_argument(
        "--output",
        default="config/hosts/zimaos-vms.local.json",
        help="Path to the local JSON output file.",
    )
    args = parser.parse_args()

    inventory = collect_inventory(Path(args.env_file))
    write_json(Path(args.output), inventory)
    print(f"Saved VM inventory to {args.output}")
    print(f"Domains found: {len(inventory['domains'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
