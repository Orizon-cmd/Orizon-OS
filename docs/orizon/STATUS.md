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
  `system health`, `system snapshot`, `system backup`, `system services`,
  `system doctor`, `system repair`, `rescue`, `hostname set <name>`, and
  `firstboot done` document first boot, record `/system/boot-state`, write
  `/logs/init.log`, expose a small service policy, export VM-safe state reports,
  and recreate missing defaults without partitioning or installing.
- Optional desktop profile: `desktop status/config/config doctor/config apply/doctor/logs/shortcuts/keymap`,
  `desktop session/settings/settings paths/settings export/settings sync/settings preset/settings doctor/input/pointer/devices/keymap/version/systeminfo/backend/protocol/layouts/layout-state/layout-tree/animations/decorations/render/descriptions/instances/submap/configerrors/config-trace/rollinglog/focus-history/workspace-stack/client-model/rule-matches/apps/app/profiles/preset/focus/autostart/windows/clients/activewindow/binds/rules/monitors/runtime/layers/keyword/dispatch/hyprctl/theme/wallpaper/layout/bar/launcher/workspace`,
  `desktop enable/disable/reset/package`, `pkg sample desktop`,
  `pkg install orizon-desktop-hypr`, and split module samples/install names for
  `orizon-desktop-core`, `orizon-terminal`, `orizon-settings`, and
  `orizon-launcher` provide the first installable Hyprland-style
  desktop profile. It is disabled by default, selectable during installation,
  package installable later, persists `/system/desktop-session.conf` plus
  `/system/desktop-settings.conf`, settings hub export/sync to
  `/home/orizon/.config/hypr/orizon-hypr.conf`, a
  `/system/desktop-modules.conf` split map for generated module packages,
  `/system/desktop-backend.conf` and `/system/desktop-protocol.conf` truth maps, plus generated
  `/system/desktop-binds.conf` and related runtime hint files, and
  currently supports F1 exec terminal, F2 killactive, launcher F3, runtime
  workspace switch, relative/dynamic workspace dispatch with next/empty targets,
  dispatcher-style
  movetoworkspace/movetoworkspacesilent with follow-vs-silent workspace focus
  restore, movefocus directionnel/focuswindow/swapwindow/cyclenext/swapnext/focusmaster/swapwithmaster/togglesplit/layoutmsg layout/split/master/nmaster/monocle-deck/resizeactive/submap, idempotent fullscreen/fullscreenstate/pseudo/pseudotile/pinned client
  state, autostart terminal policy, profile discovery, presets,
  native tiling apps for settings/logs/packages/update, app catalog/details with
  class/module/surface diagnostics, launcher overlay dispatch,
  `desktop input` layout/pointer/focus hub with `/system/keyboard` sync,
  focus-follows-mouse policy, Hyprland-style config import/runtime files,
  preserved `layerrule`/`bindm`/`bindl`/animation/input/device/decoration/cursor/render/debug hints
  without default free-drag window moving,
  session-manager commands `desktop start/stop/restart/reload/recover/rescue/state`
  with `/system/desktop-state.conf` v2 health/counters and `/logs/desktop-session.log`,
  runtime keyword/getoption/reload inspection, persistent render controls for
  focus ring, shadow range, animation tick budget/curve and render profile,
  split/master ratio controls, per-workspace layout-state diagnostics, workspace-stack master/focus diagnostics, active F9/F10/F11 keyboard submaps, explicit orientation hints, version/devices/keymap/systeminfo/backend/protocol/layouts/layout-state/layout-tree/animations/decorations/render/descriptions/instances/submap/configerrors/config-trace/rollinglog/focushistory/workspacestack/cursorpos diagnostics, pointer diagnostics, read-only config trace diagnostics for apply/prepare/ignore parser decisions, software focus ring, ticked focus/workspace/layout transition state, active tiling-tree diagnostics with client roles/rectangles/manual-drag boundary, tiled client diagnostics with stable addresses, geometry, activewindow and focusHistoryID, and
  `dwindle`/`master`/`monocle` placement in the
  Orizon compositor. `desktop backend` and `desktop protocol` document the
  current `framebuffer-vm` backend and internal `orizon-desktop-ipc-v0`
  protocol while Wayland/wlroots/upstream Hyprland remain prepared-only.
- Installer safety: `install-plan` writes
  `/workspace/.orizon/install-report.txt` without writing to disk. The real
  guided installer still requires explicit destructive confirmations.
- Package manager v5: `pkg status`, `pkg audit`, `pkg doctor`, `pkg cache`,
  `pkg search`, `pkg remote`, `pkg remote verify`, `pkg upgrade plan`,
  `pkg simulate`, `pkg verify`, `pkg history`, installed-only
  `pkg update/upgrade/install/remove`, and `pkg rollback <name>` are present.
  Package rollback is local transaction recovery, not full boot-level rollback.
  Detached package repository signatures are prepared through
  `/workspace/.orizon/package-index.sig`; missing sidecars are WARN and package
  index trust still falls back to the signed update manifest pins.
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
- Security guardrails: VFS policy v2 is visible through `security` and persisted
  as `/system/security-policy` plus `/system/security-state`; generic SSH writes
  are path-scoped, `/workspace/.orizon` remains internal OS state, remote root
  deletion is blocked, common secret-bearing names are blocked for SSH and
  package payloads, denial counters are mirrored to security audit, signed
  update manifests are mandatory, package indexes are pinned by the signed
  manifest, package-index sidecar signatures are checked when cached, key
  rotation posture is explicit (`ssh-hostkey=runtime`, update/package roots
  `release-required`), and release checks run a tracked-secret scan.
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
  installed boot checklist, `system health` gives a concise PASS/WARN operator
  summary, `system snapshot` writes
  `/workspace/.orizon/system-snapshot.txt`, `system backup` exports non-secret
  configuration to `/workspace/.orizon/admin-backup.txt`, and `system repair`
  recreates missing admin defaults like MOTD, os-release, machine-id, fstab map,
  rescue policy, admin guide, admin notes and home profile without
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
- Documentation/release alignment: `CHANGELOG.md`, this status page,
  `RELEASE.md`, `COMMANDS.md`, `TROUBLESHOOTING.md`, and subsystem pages are
  the intended source of truth after each completed block. Docs-only changes do
  not require regenerated ISO artifacts; runtime source changes do.
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
- Publishing detached package repository signatures for every package release
  and adding package-key rotation separate from the signed OS manifest.
- Full boot-level package rollback.
- Full ACPI shutdown parsing and complete ACPI namespace walking.
- Real upstream Hyprland/Wayland/wlroots integration, GPU acceleration,
  launcher/status bar, and full tiling window management. The current desktop
  is an Orizon Hyprland-style profile, not upstream Hyprland.

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
- Changelog: [../../CHANGELOG.md](../../CHANGELOG.md)
- Commands: [COMMANDS.md](COMMANDS.md)
- Release and CI: [RELEASE.md](RELEASE.md)
- VM/ZimaOS troubleshooting: [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
- Update/rollback: [UPDATE.md](UPDATE.md)
- Packages: [PACKAGES.md](PACKAGES.md)
- SSH/security: [SSH.md](SSH.md), [SECURITY.md](SECURITY.md)
- Hardware preparation only: [HARDWARE_BOOT.md](HARDWARE_BOOT.md),
  [LAPTOP_HARDWARE.md](LAPTOP_HARDWARE.md)
