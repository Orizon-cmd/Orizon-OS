from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

from common import (
    connect_ssh,
    load_json,
    parse_env_file,
    read_required,
    run_command,
    run_sudo_command,
)


def build_domain_xml(config: dict) -> str:
    name = config["name"]
    title = config.get("title", name)
    disk_path = config["remote_disk_path"]
    disk_format = config.get("disk_format", "raw")
    memory_kib = int(config.get("memory_mib", 4096)) * 1024
    vcpu_count = int(config.get("vcpu_count", 4))
    bridge_device = config.get("bridge_device", "eth0")
    network_model = config.get("network_model", "e1000e")
    video_model = config.get("video_model", "virtio")
    video_width = int(config.get("video_width", 1280))
    video_height = int(config.get("video_height", 800))
    loader = config.get("firmware_loader", "/usr/share/qemu/edk2-x86_64-code.fd")
    nvram_template = config.get(
        "firmware_nvram_template", "/usr/share/qemu/edk2-i386-vars.fd"
    )
    nvram_path = f"/var/lib/libvirt/qemu/nvram/{name}_VARS.fd"

    return f"""<domain type='kvm'>
  <name>{name}</name>
  <title>{title}</title>
  <memory unit='KiB'>{memory_kib}</memory>
  <currentMemory unit='KiB'>{memory_kib}</currentMemory>
  <vcpu placement='static'>{vcpu_count}</vcpu>
  <os firmware='efi'>
    <type arch='x86_64' machine='pc-q35-9.2'>hvm</type>
    <firmware>
      <feature enabled='no' name='enrolled-keys'/>
      <feature enabled='no' name='secure-boot'/>
    </firmware>
    <loader readonly='yes' type='pflash'>{loader}</loader>
    <nvram template='{nvram_template}'>{nvram_path}</nvram>
    <bootmenu enable='yes'/>
  </os>
  <features>
    <acpi/>
    <apic/>
  </features>
  <cpu mode='host-passthrough' check='none' migratable='on'/>
  <clock offset='localtime'>
    <timer name='rtc' tickpolicy='catchup'/>
    <timer name='pit' tickpolicy='delay'/>
    <timer name='hpet' present='no'/>
  </clock>
  <on_poweroff>destroy</on_poweroff>
  <on_reboot>restart</on_reboot>
  <on_crash>restart</on_crash>
  <pm>
    <suspend-to-mem enabled='no'/>
    <suspend-to-disk enabled='no'/>
  </pm>
  <devices>
    <emulator>/usr/bin/qemu-system-x86_64</emulator>
    <disk type='file' device='disk'>
      <driver name='qemu' type='{disk_format}'/>
      <source file='{disk_path}'/>
      <target dev='sda' bus='sata'/>
      <boot order='1'/>
    </disk>
    <controller type='usb' index='0' model='qemu-xhci'/>
    <controller type='sata' index='0'/>
    <controller type='pci' index='0' model='pcie-root'/>
    <controller type='pci' index='1' model='pcie-root-port'/>
    <controller type='pci' index='2' model='pcie-root-port'/>
    <controller type='pci' index='3' model='pcie-root-port'/>
    <interface type='direct'>
      <source dev='{bridge_device}' mode='bridge'/>
      <model type='{network_model}'/>
    </interface>
    <serial type='pty'>
      <target type='isa-serial' port='0'>
        <model name='isa-serial'/>
      </target>
    </serial>
    <console type='pty'>
      <target type='serial' port='0'/>
    </console>
    <input type='tablet' bus='usb'/>
    <input type='mouse' bus='ps2'/>
    <input type='keyboard' bus='ps2'/>
    <graphics type='vnc' port='-1' autoport='yes' websocket='-1' listen='::'>
      <listen type='address' address='::'/>
    </graphics>
    <video>
      <model type='{video_model}' heads='1' primary='yes'>
        <resolution x='{video_width}' y='{video_height}'/>
      </model>
    </video>
    <watchdog model='itco' action='reset'/>
    <memballoon model='virtio'/>
  </devices>
</domain>
"""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Provision a dedicated Orizon OS VM on the ZimaOS libvirt host."
    )
    parser.add_argument(
        "--env-file",
        default="config/hosts/zimaos.local.env",
        help="Path to the local env file with the ZimaOS connection details.",
    )
    parser.add_argument(
        "--vm-config",
        default="config/vm/orizon-dev.example.json",
        help="Path to the VM configuration JSON file.",
    )
    parser.add_argument(
        "--artifact",
        default="",
        help="Optional local raw disk image to upload before defining the VM.",
    )
    parser.add_argument(
        "--start",
        action="store_true",
        help="Start the VM after it is defined.",
    )
    args = parser.parse_args()

    env_config = parse_env_file(Path(args.env_file))
    vm_config = load_json(Path(args.vm_config))
    sudo_password = env_config.get("ZIMAOS_SUDO_PASSWORD", read_required(env_config, "ZIMAOS_PASSWORD"))
    client = connect_ssh(env_config)

    try:
        sftp = client.open_sftp()
        remote_upload_dir = "/tmp"
        remote_disk = vm_config["remote_disk_path"]
        remote_temp_xml = f"{remote_upload_dir}/{vm_config['name']}.xml"

        if args.artifact:
            local_artifact = Path(args.artifact)
            if not local_artifact.exists():
                raise FileNotFoundError(f"Artifact not found: {local_artifact}")
            remote_upload = f"{remote_upload_dir}/{local_artifact.name}"
            sftp.put(str(local_artifact), remote_upload)
            run_sudo_command(
                client,
                sudo_password,
                f"install -D -m 0644 {json.dumps(remote_upload)} {json.dumps(remote_disk)}",
            )
        else:
            run_sudo_command(
                client,
                sudo_password,
                "sh -lc "
                + json.dumps(
                    f"install -d -m 0755 /DATA/VM && "
                    f"if [ ! -f {remote_disk} ]; then truncate -s {vm_config.get('disk_size', '8G')} {remote_disk}; fi"
                ),
            )

        xml_text = build_domain_xml(vm_config)
        with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8") as handle:
            handle.write(xml_text)
            temp_xml = Path(handle.name)

        try:
            sftp.put(str(temp_xml), remote_temp_xml)
        finally:
            temp_xml.unlink(missing_ok=True)

        existing = run_sudo_command(client, sudo_password, "virsh list --all --name")
        if vm_config["name"] in existing.splitlines():
            run_sudo_command(client, sudo_password, f"virsh undefine {vm_config['name']} --nvram || true")

        run_sudo_command(client, sudo_password, f"virsh define {json.dumps(remote_temp_xml)}")
        run_command(client, f"rm -f {json.dumps(remote_temp_xml)}")
        if args.start:
            run_sudo_command(client, sudo_password, f"virsh start {vm_config['name']}")

        print(f"VM provisioned: {vm_config['name']}")
        print(f"Remote disk: {remote_disk}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
