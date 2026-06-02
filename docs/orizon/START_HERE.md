# Orizon OS Start Here

This is the short operator page for resuming work without re-reading every
design note.

## Current State

- For the precise truth table, read [STATUS.md](STATUS.md). It separates
  VM-ready features, prepared hardware paths, and work that is not implemented
  yet.
- Installed updates are active: the in-OS `update` command downloads the signed
  GitHub manifest, verifies boot payload SHA-256 values, refreshes the installed
  ESP, and writes rollback metadata.
- Rollback is boot-count style through Limine today. It restores the normal
  Limine default after a successful shell boot, but true UEFI Runtime Services
  `BootNext` writing is still future work.
- Release packaging is guarded: `Orizon-OS.iso`, `updates/x86_64/kernel.elf`,
  `BOOTX64.EFI`, `limine.conf`, `manifest.txt`, `manifest.sig`, and
  `release.txt` are generated and cross-checked by
  `scripts/orizon/orizon_update.py --mode validate-release`, including
  manifest/release size and SHA-256 consistency for the current ISO.
  Use [RELEASE.md](RELEASE.md) as the release checklist before publishing a
  build or artifact change.
- Developer checks are unified in `scripts/orizon/quick_check.py`; GitHub
  Actions uses `scripts/orizon/ci_release_guard.py` to run quick checks,
  release validation, release-note generation, tracked-secret scan, and a
  machine-readable artifact sync summary before the source build.
  Use [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for VM/ZimaOS failure triage.
- Hardware capture is exportable with `report save`, which writes the
  non-destructive `/workspace/hardware-report.txt` bundle containing storage,
  PCI BARs, USB, Wi-Fi, network, SSH, bootguard, update, selftest and log tails.
  `report next` and `hw next` print the exact future-hardware capture plan
  without claiming any real-PC validation.
- Installer preflight is exportable with `install-plan`, which writes the
  non-destructive `/workspace/.orizon/install-report.txt` bundle for VM/SSH
  review before any disk write.
- The optional first desktop profile is available through `desktop status`,
  `desktop session`, `desktop settings`, `desktop settings paths`, `desktop
  settings export`, `desktop settings sync`, `desktop input`, `desktop modules`, `desktop settings preset`, `desktop settings doctor`, `desktop apps`, `desktop app <id>`, `desktop doctor`, the guided installer
  desktop prompt, `pkg sample desktop`, `pkg install orizon-desktop-hypr`,
  and split samples for `orizon-desktop-core`, `orizon-terminal`,
  `orizon-settings`, and `orizon-launcher`. `orizon-waybar` is future only.
  It is Hyprland-style Orizon compositor plumbing, disabled by default, persists
  `/system/desktop-session.conf` plus `/system/desktop-settings.conf`,
  mirrors those central settings into `/home/orizon/.config/hypr/`, writes
  `/system/desktop-modules.conf` for split module samples, and
  generated `/system/desktop-binds.conf` and related runtime hints, and starts
  with F1 exec terminal, F2 killactive, F3 launcher, F4 fullscreen, F5 pseudo,
  F6 focus cycle, F7/F8 workspace navigation, `desktop profiles`, `desktop
  preset`, `desktop focus`, `desktop binds/rules/monitors/runtime/layers`,
  `desktop version/devices/systeminfo/layouts/animations/decorations/render/configerrors/rollinglog/focus-history`,
  `desktop keyword`, `desktop dispatch` including focusmaster/swapwithmaster
  plus split/master ratio layout messages, directional movefocus/swapwindow,
  workspace next/empty targets, and
  silent move-to-workspace dispatch,
  persistent render tuning for focus ring, shadow range, render profile and
  animation tick budget,
  preserved `layerrule`/`bindm`/`bindl`/animation/input/device/decoration/cursor/render/debug hints without default
  free-drag window moving,
  `desktop start/stop/restart/reload/recover/rescue/state` session-manager commands,
  v2 state health/counters,
  `desktop hyprctl version/systeminfo/clients/workspaces/activeworkspace/activewindow/focushistory/layouts/animations/decorations/render/descriptions/instances/submap/devices/cursorpos/splash/configerrors/rollinglog/getoption/keyword/binds/layers`, `desktop autostart`,
  `desktop windows/clients/activewindow`, and runtime tiled workspace/client-state commands rather
  than real upstream Hyprland.
- Installed/live lifecycle is visible with `system status`; `system health`
  gives a PASS/WARN summary, `system snapshot` writes
  `/workspace/.orizon/system-snapshot.txt`, `system backup` exports non-secret
  config to `/workspace/.orizon/admin-backup.txt`, `system init` records
  `/system/boot-state` and `/logs/init.log`, `system services` shows the
  simple service policy, `system doctor` audits roots/config/init state,
  `system repair` recreates only missing default roots/config, `rescue` prints
  the safe recovery checklist, `hostname set <name>` persists
  `/system/hostname`, and `firstboot done` marks the installed first boot as
  reviewed.
- Base security is visible with `security`. SSH has password auth disabled until
  configured, lockout/audit enabled, a persistent host key, VFS policy v2,
  generated `/system/security-policy` and `/system/security-state`, generic
  remote file writes limited to `/workspace`, `/home`, `/logs`, and `/packages`,
  remote root deletion blocked, and signed update manifests are mandatory.
  Package repo sidecar signatures are checked when cached and reported as
  WARN/fallback when absent.
- The local framebuffer console can scroll long outputs with `z` up and `s`
  down on an empty prompt. Use `less <file>` for full-screen local paging with
  `z/s`, arrows, space, `g/G`, and `q`; SSH `cat /workspace/hardware-report.txt`
  is still preferred for copying the full report.
- Wired VM networking supports e1000/e1000e, RTL8139, and VirtIO-net. The
  ZimaOS NAT matrix cases for those NICs passed with lifecycle checks on
  2026-05-22. Bridge cases booted to framebuffer, but SSH was not reachable
  because the guest IP was not discoverable from the ZimaOS host.
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
python scripts/orizon/quick_check.py
```

## What To Capture Next

- ZimaOS VM smoke, when requested: boot, DHCP, SSH, ping, DNS, `system status`,
  `system health`, `system snapshot`, `system backup`, `rescue`, `pkg status`,
  `pkg doctor`, `update status`, `report save`, `install-plan`,
  `security`, `selftest crypto`, and `hostkey` on e1000e, VirtIO-net, and RTL8139 NAT
  first.
- ZimaOS VM lifecycle smoke, when requested: `python scripts/orizon/test_vm_matrix.py
  --cases nat-e1000e --include-lifecycle` adds framebuffer screenshot,
  SSH-triggered reboot, post-reboot SSH, and clean VM shutdown.
- ZimaOS installed update/rollback smoke, when requested:
  `python scripts/orizon/test_update_rollback_vm.py` installs a disposable VM,
  validates signed update metadata, runs rollback, reboots, and shuts down.
- Future storage capture, only when the user boots a real machine intentionally:
  `report next`, `report save`, `storage diag`, `logs storage`, `logs pci`,
  `pci bars`, `disk identify`, and `gpt scan`; do not install while
  investigating missing-disk detection.
- USB Ethernet hardware: `usb rescan`, `usb`, `logs usb`, `net status`.
- Lenovo Wi-Fi AP validation, only on the user's real hardware: `wifi validate
  <ssid> [password]`, then `logs wifi`, `net status`, `wifi wpa`, and
  `wifi data`.
- Rollback hardening: inspect `bootguard`, `rollback-status`, the Limine
  fallback config, and the captured EFI system table handoff before attempting
  future NVRAM/BootNext work.

## Files To Open First

- [README.md](../../README.md)
- [CHANGELOG.md](../../CHANGELOG.md)
- [docs/orizon/STATUS.md](STATUS.md)
- [docs/orizon/COMMANDS.md](COMMANDS.md)
- [docs/orizon/DESKTOP.md](DESKTOP.md)
- [docs/orizon/ROADMAP.md](ROADMAP.md)
- [docs/orizon/UPDATE.md](UPDATE.md)
- [docs/orizon/INSTALL.md](INSTALL.md)
- [docs/orizon/PACKAGES.md](PACKAGES.md)
- [docs/orizon/NETWORK.md](NETWORK.md)
- [docs/orizon/SECURITY.md](SECURITY.md)
- [docs/orizon/RELEASE.md](RELEASE.md)
- [docs/orizon/TROUBLESHOOTING.md](TROUBLESHOOTING.md)
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
  need to move with it. Use `python scripts/orizon/quick_check.py` before the
  commit, or `python scripts/orizon/ci_release_guard.py --output-dir artifacts`
  when you want the same summary GitHub Actions uploads.
