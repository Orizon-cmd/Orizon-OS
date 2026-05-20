# Orizon OS Start Here

This is the short operator page for resuming work without re-reading every
design note.

## Current State

- Installed updates are active: the in-OS `update` command downloads the signed
  GitHub manifest, verifies boot payload SHA-256 values, refreshes the installed
  ESP, and writes rollback metadata.
- Rollback is boot-count style through Limine today. It restores the normal
  Limine default after a successful shell boot, but true UEFI Runtime Services
  `BootNext` writing is still future work.
- Release packaging is guarded: `Orizon-OS.iso`, `updates/x86_64/kernel.elf`,
  `BOOTX64.EFI`, `limine.conf`, `manifest.txt`, `manifest.sig`, and
  `release.txt` are generated and cross-checked by
  `scripts/orizon/orizon_update.py`, including manifest/release size and
  SHA-256 consistency for the current ISO.
- Hardware capture is exportable with `report save`, which writes the
  non-destructive `/workspace/hardware-report.txt` bundle containing storage,
  PCI BARs, USB, Wi-Fi, network, SSH, bootguard, update, selftest and log tails.
- Installer preflight is exportable with `install-plan`, which writes the
  non-destructive `/workspace/.orizon/install-report.txt` bundle for VM/SSH
  review before any disk write.
- The local framebuffer console can scroll long outputs with `z` up and `s`
  down on an empty prompt; SSH `cat /workspace/hardware-report.txt` is preferred
  for copying the full report.
- Wired VM networking supports e1000/e1000e, RTL8139, and VirtIO-net. The
  ZimaOS NAT smoke cases for those NICs have passed before; do not rerun the
  full matrix unless that is the explicit task.
- USB Ethernet has xHCI CDC-ECM and Realtek RTL815x packet paths, plus
  `/logs/usb.log` family/support diagnostics for CDC-NCM, ASIX, SMSC/LAN95xx,
  RNDIS, hub, and endpoint-blocker investigation.
- Intel AX201/CNVi Wi-Fi is staged up through guarded WPA2/CCMP validation.
  `wifi validate`, `wifi online`, and `wifi update` persist AP/WPA2/DHCP/DNS/TLS
  evidence, but the Lenovo real-AP path has not been validated yet.

## Day-To-Day Loop

Use the release helper for builds so the ISO and update payloads stay in sync:

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-iso
```

For the fast VM development loop:

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-vm
powershell -File scripts/orizon/open_orizon_vnc.ps1
```

Quick checks before a commit:

```powershell
git diff --check
python -m py_compile scripts/orizon/orizon_update.py scripts/orizon/build_x86_64_on_zimaos.py scripts/orizon/test_vm_matrix.py scripts/orizon/test_update_rollback_vm.py
```

## What To Capture Next

- ZimaOS VM smoke, when requested: boot, DHCP, SSH, ping, DNS, `pkg status`,
  `update status`, `report save`, `install-plan`, `selftest crypto`, and
  `hostkey` on e1000e, VirtIO-net, and RTL8139 NAT first.
- ZimaOS VM lifecycle smoke, when requested: `python scripts/orizon/test_vm_matrix.py
  --cases nat-e1000e --include-lifecycle` adds framebuffer screenshot,
  SSH-triggered reboot, post-reboot SSH, and clean VM shutdown.
- ZimaOS installed update/rollback smoke, when requested:
  `python scripts/orizon/test_update_rollback_vm.py` installs a disposable VM,
  validates signed update metadata, runs rollback, reboots, and shuts down.
- Lenovo storage capture, only when the user boots the new ISO: `report save`,
  `storage diag`, `logs storage`, `logs pci`, `pci bars`, `disk identify`, and
  `gpt scan`; do not install while investigating missing-disk detection.
- USB Ethernet hardware: `usb rescan`, `usb`, `logs usb`, `net status`.
- Lenovo Wi-Fi AP validation, only on the user's real hardware: `wifi validate
  <ssid> [password]`, then `logs wifi`, `net status`, `wifi wpa`, and
  `wifi data`.
- Rollback hardening: inspect `bootguard`, `rollback-status`, the Limine
  fallback config, and the captured EFI system table handoff before attempting
  future NVRAM/BootNext work.

## Files To Open First

- [README.md](../../README.md)
- [docs/orizon/COMMANDS.md](COMMANDS.md)
- [docs/orizon/ROADMAP.md](ROADMAP.md)
- [docs/orizon/UPDATE.md](UPDATE.md)
- [docs/orizon/NETWORK.md](NETWORK.md)
- [docs/orizon/LAPTOP_HARDWARE.md](LAPTOP_HARDWARE.md)
- [docs/orizon/ZIMAOS_LAB.md](ZIMAOS_LAB.md)

## Hard Limits

- Do not claim Lenovo, real AP, USB dongle, or physical hardware validation
  unless that hardware was actually tested by the user.
- Do not commit private keys, local env files, imported firmware blobs, or
  hotspot credentials.
- Do not run the full VM matrix unless the current task explicitly asks for it.
- Do not publish a release commit that updates `kernel.elf` without also checking
  whether `Orizon-OS.iso`, `manifest.txt`, `manifest.sig`, and `release.txt`
  need to move with it.
