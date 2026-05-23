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
- Release procedure: use [RELEASE.md](RELEASE.md) for artifact/CI publishing
  rules and [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for VM/ZimaOS failure
  triage.

## Meaning Of Status Labels

- Implemented: code or script support exists in the repository.
- VM-ready: implemented and usable in the ZimaOS VM workflow.
- Prepared: detection, diagnostics, or staged plumbing exists, but the real
  device/path is not proven.
- Simulated: behavior is represented by a safe dry-run, report, or VM-only
  workflow rather than a real production path.
- Not hardware-proven: do not claim Lenovo, real AP, dongle, or physical-PC
  validation without fresh user-provided evidence.
- Not implemented: documented future work, not just an untested feature.

## Implemented And VM-Ready

- Persistent roots: `/workspace`, `/home`, `/system`, `/packages`, and `/logs`
  use a two-slot snapshot format when an Orizon data partition is present.
  Operators can inspect and recover with `persist status`, `persist slots`,
  `persist save`, `persist restore previous`, and `persist repair`.
- Installed/live lifecycle: `system status`, `system init`,
  `system services`, `system doctor`, `system repair`, `rescue`,
  `hostname set <name>`, and `firstboot done` document first boot, record
  `/system/boot-state`, write `/logs/init.log`, expose a small service policy,
  and recreate missing defaults without partitioning or installing.
- Installer safety: `install-plan` writes
  `/workspace/.orizon/install-report.txt` without writing to disk. The real
  guided installer still requires explicit destructive confirmations.
- Package manager v4: `pkg status`, `pkg audit`, `pkg cache`, `pkg search`,
  `pkg remote`, `pkg remote verify`, `pkg upgrade plan`, `pkg simulate`,
  `pkg verify`, `pkg history`, installed-only
  `pkg update/upgrade/install/remove`, and `pkg rollback <name>` are present.
  Package rollback is local transaction recovery, not full boot-level rollback.
  Detached package repository signatures are not implemented yet; package index
  trust still falls back to the signed update manifest pins.
- Console usability: framebuffer scrollback with `z`/`s`, persistent command
  history, autocomplete, `less <file>`, `tail`, `help shell`, simple `;`
  command grouping, `>`/`>>` redirection, and diagnostic pipes to
  `grep/head/tail/wc/tee/less` are available locally. `grep` supports
  `-i/-v/-n`, `shell status` exposes buffer/capability limits, and
  `history grep <text>` searches saved commands.
- SSH admin: password auth is opt-in, lockout/audit are visible, host keys are
  persistent per install when storage is available, long outputs are segmented,
  diagnostics are usable through OpenSSH, `security policy/audit/keys/doctor`
  are available locally and remotely, `security rotate ssh-hostkey` rotates the
  local host identity for future sessions, and `/logs/security.log` mirrors SSH
  audit plus policy changes without recording passwords or generic write/Wi-Fi
  credentials.
- Security guardrails: generic SSH writes are path-scoped, `/workspace/.orizon`
  remains internal OS state, common secret-bearing names are blocked for SSH and
  package payloads, signed update manifests are mandatory, package indexes are
  pinned by the signed manifest, and release checks run a tracked-secret scan.
- Wired VM networking: e1000/e1000e, RTL8139, and VirtIO-net are the current
  daily VM NIC paths. NAT smoke tests are the normal quick gate. `net check`,
  `net daily`, retrying `net tcp <host> [port] [attempts <1-5>]`, `net tls`,
  and `net diag` split daily failures into link/IPv4, retry/DNS/TCP
  reachability, and HTTPS/root-trust layers. `net daily` also states the
  NAT/bridge detection boundary, retry policy, and persistent log/config paths.
  A full
  ZimaOS matrix run on 2026-05-22 passed NAT e1000e, NAT VirtIO-net, and NAT
  RTL8139 with SSH diagnostics, persistence checks, framebuffer screenshot,
  reboot, post-reboot SSH, and shutdown.
- VM storage: AHCI, NVMe diagnostics, GPT scan/read-test, `storage vmcheck`,
  and VirtIO-blk are available for VM work. A 2026-05-22 ZimaOS smoke with an
  e1000e NAT NIC and modern VirtIO-blk disk passed SSH, selftest, persistence
  save/restore, `gpt scan`, first/last sector read tests, install preflight,
  reboot, and shutdown. VirtIO-scsi is still diagnostic-only and reported as
  such in storage diagnostics.
- Installed VM UX: `system init` now records both boot-state and service-state,
  `system services` exposes the current mini service policy, `system logs`
  gathers boot/service/init evidence, `system firstboot` guides the first
  installed boot checklist, and `system repair` recreates missing admin defaults
  like MOTD, fstab map, rescue policy, admin guide and home profile without
  repartitioning or installing.
- Update/rollback: installed systems fetch a signed GitHub manifest, verify
  SHA-256 payloads, refresh the ESP, and use Limine fallback metadata for
  post-update validation. `update status` and `bootguard` now expose the
  strategy, scope, attempts, `bootguard recover`, Limine normal/fallback cache
  state, and the honest pseudo-A/B vs `BootNext`/full-A-B boundary.
- Release guardrails: `Orizon-OS.iso`, update payloads, `manifest.txt`,
  `manifest.sig`, and `release.txt` are cross-checked by
  `python scripts/orizon/orizon_update.py --mode validate-release` and by
  `python scripts/orizon/quick_check.py`. The same quick check now includes a
  tracked-secret scan, and GitHub CI uses `ci_release_guard.py` to upload
  quick-check logs, generated release-note previews, strict release validation
  logs, and artifact synchronization summaries.
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

- UEFI Runtime Services `BootNext` writing, full A/B root/ESP slots, or
  firmware-level rollback before the refreshed kernel starts. The implemented
  rollback layer is pseudo-A/B through Limine main/rollback payloads.
- Secure Boot, TPM attestation, disk encryption, Unix users/groups/ACLs, sudo,
  user/admin separation, or a full MAC policy.
- Automatic Windows BCD/UEFI boot entry creation for dual boot.
- In-OS partition shrink/create assistant for making free space beside an
  existing OS.
- Detached package repository signatures and package-key rotation separate
  from the signed OS manifest.
- Full boot-level package rollback.
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

## Documentation Map

- Start/resume: [START_HERE.md](START_HERE.md)
- Commands: [COMMANDS.md](COMMANDS.md)
- Release and CI: [RELEASE.md](RELEASE.md)
- VM/ZimaOS troubleshooting: [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
- Update/rollback: [UPDATE.md](UPDATE.md)
- Packages: [PACKAGES.md](PACKAGES.md)
- SSH/security: [SSH.md](SSH.md), [SECURITY.md](SECURITY.md)
- Hardware preparation only: [HARDWARE_BOOT.md](HARDWARE_BOOT.md),
  [LAPTOP_HARDWARE.md](LAPTOP_HARDWARE.md)
