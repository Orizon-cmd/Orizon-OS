# Orizon OS Current Status And Limits

This page is the short truth table for the project. Use it before deciding
whether a feature is implemented, VM-tested, only prepared, or still future
work.

## Validation Boundary

- Current priority: ZimaOS VM work. VM tests may be run when useful.
- Lenovo and other real hardware are out of scope unless the user explicitly
  boots them and provides fresh captures.
- Do not claim Lenovo, USB dongle, real AP, or physical-PC validation from VM
  evidence alone.
- The full VM matrix should not be run unless the current task explicitly asks
  for it. Prefer build, syntax, diff-check, release validation, and a short VM
  smoke when needed.

## Implemented And VM-Ready

- Persistent roots: `/workspace`, `/home`, `/system`, `/packages`, and `/logs`
  use a two-slot snapshot format when an Orizon data partition is present.
  Operators can inspect and recover with `persist status`, `persist slots`,
  `persist save`, `persist restore previous`, and `persist repair`.
- Installed/live lifecycle: `system status`, `system repair`, `rescue`,
  `hostname set <name>`, and `firstboot done` document first boot and recreate
  missing defaults without partitioning or installing.
- Installer safety: `install-plan` writes
  `/workspace/.orizon/install-report.txt` without writing to disk. The real
  guided installer still requires explicit destructive confirmations.
- Package manager v2: `pkg status`, `pkg search`, `pkg remote`, `pkg verify`,
  `pkg history`, installed-only `pkg update/install/remove`, and
  `pkg rollback <name>` are present. Package rollback is local transaction
  recovery, not full boot-level rollback.
- Console usability: framebuffer scrollback with `z`/`s`, persistent command
  history, autocomplete, and `less <file>` are available locally.
- SSH admin: password auth is opt-in, lockout/audit are visible, host keys are
  persistent per install when storage is available, long outputs are segmented,
  and diagnostic commands are usable through OpenSSH.
- Wired VM networking: e1000/e1000e, RTL8139, and VirtIO-net are the current
  daily VM NIC paths. NAT smoke tests are the normal quick gate. A full
  ZimaOS matrix run on 2026-05-22 passed NAT e1000e, NAT VirtIO-net, and NAT
  RTL8139 with SSH diagnostics, persistence checks, framebuffer screenshot,
  reboot, post-reboot SSH, and shutdown.
- Update/rollback: installed systems fetch a signed GitHub manifest, verify
  SHA-256 payloads, refresh the ESP, and use Limine fallback metadata for
  post-update validation.
- Release guardrails: `Orizon-OS.iso`, update payloads, `manifest.txt`,
  `manifest.sig`, and `release.txt` are cross-checked by
  `python scripts/orizon/orizon_update.py --mode validate-release` and by
  `python scripts/orizon/quick_check.py`.
- Hardware reports: `report next`, `hw next`, and `report save` produce a
  non-destructive capture plan and `/workspace/hardware-report.txt`.

## Prepared But Not Hardware-Proven

- Intel VMD/RST and missing-disk analysis: PCI/storage diagnostics can report
  VMD/RST blockers, NVMe CAP/CC/CSTS, admin status, BARs, and secondary bus
  hints. A true VMD remap driver is not implemented yet.
- USB Ethernet: xHCI CDC-ECM raw Ethernet and Realtek RTL815x packet paths are
  present. CDC-NCM, ASIX, SMSC/LAN95xx, RNDIS, and hub downstream enumeration
  remain diagnostic/future paths until a specific device is validated.
- Intel AX201/CNVi Wi-Fi: firmware bringup, scan, WPA2/CCMP staging,
  `wifi validate`, `wifi online`, and `wifi update` are prepared. A real AP on
  the Lenovo has not been validated in the current workflow.
- I2C-HID laptop input: first probes exist for the documented Lenovo paths, but
  multitouch/stylus report parsing is still future work.
- Bridge networking: bridge mode is supported by configuration and scripts. In
  the 2026-05-22 ZimaOS matrix, bridge e1000e, bridge VirtIO-net, and bridge
  RTL8139 reached boot/framebuffer smoke, but SSH checks were skipped because
  the guest IP was not discoverable from `virsh` ARP or host neighbor tables.
  NAT remains the default SSH-capable gate.

## Not Implemented Yet

- UEFI Runtime Services `BootNext` writing or firmware-level rollback before
  the refreshed kernel starts.
- Secure Boot, TPM attestation, disk encryption, Unix users/groups/ACLs, sudo,
  or a full MAC policy.
- Automatic Windows BCD/UEFI boot entry creation for dual boot.
- In-OS partition shrink/create assistant for making free space beside an
  existing OS.
- Detached package repository signatures and package-key rotation separate
  from the signed OS manifest.
- Full boot-level package rollback or A/B system slots.
- Full ACPI shutdown parsing and complete ACPI namespace walking.

## Safe Operator Loop

For normal code or doc changes:

```powershell
python scripts/orizon/quick_check.py
```

When a build changes boot artifacts:

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-iso
python scripts/orizon/quick_check.py
```

When a short VM smoke is useful:

```powershell
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e
```

Do not commit private keys, local env files, imported firmware blobs, hotspot
credentials, or captured passwords.
