# Orizon OS Firmware

Orizon OS vendors a small Intel `iwlwifi` firmware set so the bootable ISO can
carry Wi-Fi firmware modules for supported Intel CNVi/PCIe devices.

Source:

- Repository: https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware.git
- Snapshot used locally: `b965e85c15a94974d4a6390972fc1cbc1bb109cc`
- License file included in-tree: `orizon-os-x86_64/firmware/LICENCE.iwlwifi_firmware`
- Upstream metadata included in-tree: `orizon-os-x86_64/firmware/WHENCE.linux-firmware`

These firmware blobs are not Orizon OS kernel code. They are distributed under
the upstream Intel firmware terms so the OS can boot with the required Wi-Fi
firmware available as Limine modules.
