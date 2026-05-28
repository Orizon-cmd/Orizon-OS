# Orizon OS Roadmap

## Current Foundation

- Installed-disk boot flow with live ISO guardrails.
- Non-destructive dual-boot ESP preparation: existing GPT/FAT32 ESP detection,
  side-by-side `/EFI/Orizon` boot files, and `dualboot-check` verification.
- Installed dual-boot data path: the installer can reuse an existing prepared
  partition as `Orizon Data`, preserve the shared ESP, keep live-created data,
  and enable `update` without requiring a new ISO. `install-plan` now exports a
  non-destructive preflight report for VM/SSH review before any disk write.
- Persistent Orizon data roots: `/workspace`, `/home`, `/system`,
  `/packages`, `/logs`, and `/tmp`, now guarded so persistence writes only
  activate when an Orizon data partition is actually present, including
  non-fixed LBAs on dual-boot disks.
- In-kernel GitHub update path with SHA-256 verification, boot rollback slot,
  streamed progress, rough elapsed timings, retried manifest/package-index
  fetches, resumable boot-artifact caches, full-disk ESP rewrite, and
  side-by-side `/EFI/Orizon` refresh for dual-boot data installs.
- Update authenticity guard: detached RSA PKCS#1/SHA-256 manifest signatures,
  an embedded Orizon update root key, package-index commit/SHA pinning, and
  TLS SAN/chain/signature validation anchored to ISRG Root X1 for GitHub
  downloads.
- Release packaging guard: the signed manifest records root ISO size/SHA-256,
  the helper emits `updates/x86_64/release.txt`, and build modes validate that
  ISO, boot payloads, `manifest.txt`, `manifest.sig`, and `release.txt` match
  the current artifact sizes/SHA-256 values before publication.
- Post-update boot guard: pending/testing markers, a boot-count-style Limine
  fallback default while the refreshed kernel is proving itself, automatic
  validation once the refreshed kernel reaches the shell, `bootguard`
  diagnostics, and automatic main-slot restore when the rollback boot entry is
  selected during a pending update.
- Package manager v5 foundation with `pkg list`, `pkg status`, `pkg audit`,
  `pkg doctor`, `pkg cache`, `pkg search`, `pkg remote`,
  `pkg remote verify`, `pkg upgrade plan`, `pkg simulate`, `pkg info`,
  `pkg sample`, `pkg hash`, `pkg verify`, installed-only
  `pkg update/upgrade/install/remove`, signed package-index cache validation,
  prepared package-index sidecar signature checks, cached upgrade plans,
  `pre-remove`/`post-remove` scripts, explicit transaction history/state
  events, and local `pkg rollback <name>` for the last removed package
  snapshot.
- Optional desktop profile foundation: `desktop status/config/config doctor/config apply/doctor/logs/keymap`,
  `desktop session/settings/settings preset/settings doctor/pointer/devices/keymap/version/systeminfo/layouts/animations/decorations/descriptions/instances/submap/configerrors/rollinglog/focus-history/apps/profiles/preset/focus/autostart/theme/wallpaper/layout/bar/launcher/binds/rules/monitors/runtime/layers/keyword/dispatch/hyprctl`,
  `desktop enable/disable/reset`, installer prompt wiring, `pkg sample desktop`,
  and `pkg install orizon-desktop-hypr` provide a Hyprland-style Orizon desktop
  profile. It is disabled by default, persists session/autostart settings plus
  a system-wide `/system/desktop-settings.conf` layer plus generated
  `/system/desktop-binds.conf` runtime hints, and
  currently focuses on dispatcher-driven terminal clients, relative
  workspaces, fullscreen/pseudo/pinned client state, `cyclenext`/`swapnext`/`focusmaster`/`swapwithmaster`/`togglesplit`/`layoutmsg`/`resizeactive`/`submap`,
  a first launcher/status-bar layer, symbolic profile/preset discovery,
  focus-follows-mouse policy, Hyprland-style config import/runtime files,
  preserved `layerrule`/`bindm`/`bindl`/animation/input hints without default
  free-drag window moving,
  session-manager commands `desktop start/stop/restart/reload/recover/state`,
  keyword/getoption/reload runtime inspection, split/master ratio controls, active F9/F10/F11 keyboard submaps, explicit orientation hints, version/devices/keymap/systeminfo/layouts/animations/decorations/descriptions/instances/submap/configerrors/rollinglog/focushistory/cursorpos diagnostics,
  pointer diagnostics, native tiling apps for settings/logs/packages/update,
  `dwindle`/`master`/`monocle` placement, and
  diagnostic tiled client/workspace plumbing with stable client addresses,
  activewindow geometry and Hyprland-style focusHistoryID, not the real upstream
  Hyprland/Wayland stack yet.
- Console basics: scrollback, `z`/`s` scrolling, full-screen `less <file>`
  pager, `tail`, `help shell`, simple `;` command grouping, `>`/`>>`
  redirection, diagnostic pipes to `grep/head/tail/wc/tee/less`, persistent
  history with `history grep`, `shell status`, simple autocomplete, editor,
  `sysinfo`, `hw`, `mounts`, `logs`, `report`, `report save`, `selftest`,
  `ps`, and `uptime`.
- Installed/live lifecycle commands: `system status`, `system init`,
  `system services`, `system doctor`, `system repair`, `rescue`,
  `hostname set <name>`, and `firstboot done` clarify first boot, record
  boot/init state, recreate missing default roots/config non-destructively, and
  export rescue state through the hardware report.
- Documentation cleanup: `START_HERE.md` now summarizes the current state,
  guarded limits, quick checks, and next captures, while `COMMANDS.md` keeps the
  operator command checklist separate from the long-form subsystem notes.
  `STATUS.md` is the compact truth table for implemented, VM-ready, prepared,
  not-yet-implemented, and not-yet-hardware-validated work. `RELEASE.md`,
  `TROUBLESHOOTING.md`, and `CHANGELOG.md` now define the docs/release loop so
  every block records what changed, how to validate it, and which artifacts
  should or should not move.
- Staged remote-management base: `ssh start/status/algorithms/stop`, TCP/22
  listener, SSH banner, server/client `KEXINIT`, X25519, RSA host-key
  signature, `ECDH_REPLY`, key derivation, `NEWKEYS`, encrypted
  `SERVICE_REQUEST` parsing, encrypted `SERVICE_ACCEPT`, explicit password
  authentication for user `orizon`, `session` channel open, `pty-req`, `shell`,
  `exec`, a remote diagnostic/admin shell with VFS/log/network/process/package/
  storage/network/Wi-Fi/report/selftest commands, remote
  auth/password/lockout/hostkey
  administration, direct remote file edits, heap diagnostics, audit
  counters/recent events, multi-packet channel output for longer logs, graceful
  listener recovery, anti-bruteforce lockout, config reload, per-install RSA
  host-key generation and persistent host-key file management in
  `/system/ssh_host_rsa.key`, `/system/ssh.conf`, and `/logs/ssh.log`, plus
  `security policy/audit/keys/doctor` diagnostics, VFS policy v2 state files
  under `/system/security-policy` and `/system/security-state`, explicit SSH
  host-key rotation, release-required update/package root rotation posture, and
  audit redaction for password/write/Wi-Fi credential commands.
- Hardware base: PS/2 keyboard/mouse, USB HID keyboard plus selected USB HID
  boot mouse/QEMU-tablet input, USB root-port rescans,
  last-device inventory, USB Ethernet descriptor diagnostics for common dongle
  families, persistent `/logs/usb.log` capture with family/support/blocker
  fields, xHCI CDC-ECM raw Ethernet and Realtek RTL815x packet paths,
  AHCI/NVMe/VirtIO-blk storage probes, read-only `disk identify` /
  `disk read-test` / `gpt scan`, Intel VMD/RST/eMMC blocker diagnostics with PCI BAR and
  secondary-bus capture, Intel e1000/e1000e, RTL8139,
  VirtIO-net Ethernet, and staged Intel Wi-Fi
  detection, firmware discovery, APM wake, CPU-release firmware loading, FH DMA
  upload staging, alive polling diagnostics, and host-side command/RX/TX queue
  memory staging. The Intel Wi-Fi WPA2 path can now derive PMK/PTK, prepare
  M2/M4, unwrap M3 key data, extract GTK, and stage pairwise/group SEC_KEY
  installs behind strict firmware ACK checks. It also has an AES-CCM self-test
  plus a protected CCMP RX self-test, and a software-encrypted CCMP Ethernet
  data path that the IPv4 stack can use for ARP/DHCP/IPv4 once WPA2 is
  guarded-ready. `wifi join` now orchestrates
  the bringup/scan/connect/WPA sequence with concise progress output, and
  `wifi online` / `wifi validate` extend that into DHCP, DNS, and GitHub TLS
  readiness for update-over-Wi-Fi validation. `wifi update` reuses the same
  guarded path and launches the installed updater once GitHub is reachable over
  Wi-Fi. The validation path persists PASS/FAIL evidence plus WPA/CCMP and
  network snapshots in `/logs/wifi.log` and
  `/workspace/.orizon/wifi-validation` for Lenovo AP testing.
- Repeatable ZimaOS VM smoke matrix: dedicated libvirt VMs can validate boot,
  DHCP, SSH, ping, DNS, TCP 443 reachability, package status, update status,
  storage persistence, and host-key state across NAT NIC models. Recent NAT
  smoke passes cover e1000e, VirtIO-net, RTL8139, and a targeted modern
  VirtIO-blk disk profile; this does not imply validation on Lenovo or any
  other physical PC.

## Next Stability Track

1. Turn the captured Limine EFI system table handoff into true UEFI NVRAM
   `BootNext` writing, or add bootloader-native boot-count integration, so
   firmware can automatically select rollback even when the refreshed kernel
   never reaches Orizon early boot.
2. Publish detached package repository signatures in the package repo and add
   key rotation; Orizon already detects `/workspace/.orizon/package-index.sig`
   when cached and still falls back to the signed OS manifest pin when absent.
3. Continue network hardening beyond the current `net check` / `net daily` /
   retrying `net tcp` / `net tls` / `net diag` split with deeper per-phase
   counters, packet-loss stats, and host-side bridge discovery when the ZimaOS
   lab can expose it.
4. Finish SSH remote login hardening: safer config permissions, key rotation,
   fuller PTY integration with the local Orizon terminal, and longer
   multi-client soak tests.
5. Finish dual boot: automatic UEFI NVRAM/BCD entry creation, boot-count
   recovery, and eventually an in-OS partition create/resize workflow beside
   existing operating systems.

## Next Hardware Track

1. Make the Lenovo 500w Yoga Gen 4 a concrete real-laptop target: boot,
   keyboard, capture `report save` plus storage/PCI logs for the missing-disk
   case, then validate NVMe/VMD behaviour and I2C-HID touchpad.
2. Improve USB HID keyboard coverage for non-US layouts and laptop keypads.
3. Harden LAPIC timer calibration and add x2APIC support for newer firmware
   modes.
4. Expand the new Intel LPSS/Synopsys DesignWare I2C-HID probe into a full HID
   report parser for ELAN/Wacom multitouch and stylus events.
5. Build Intel CNVi Wi-Fi properly: validate `wifi online` and `wifi update`
   on the Lenovo against a real WPA2 AP using the saved `wifi validate`
   evidence, then harden protected RX/retry diagnostics against AP-specific
   behaviour.
6. Implement USB hub downstream enumeration if the Lenovo adapter appears
   behind a dock or multi-port hub.
7. Extend USB Ethernet beyond the first xHCI CDC-ECM/RTL815x path: add CDC-NCM,
   ASIX AX88xxx, SMSC/LAN95xx, RNDIS if needed, using the captured
   `/logs/usb.log` VID/PID/endpoint evidence first, then hardware validation on
   the Lenovo adapter's actual VID/PID.
8. Implement true Intel VMD remapping if the Lenovo capture confirms the NVMe is
   hidden behind VMD/RST, then harden NVMe and AHCI writes with more error
   reporting and timeout handling.
9. Extend VirtIO storage beyond VirtIO-blk by adding VirtIO-scsi and more
   install/write stress coverage in VM.
10. Extend the VM test matrix beyond the current NAT smoke path: bridge cases,
   AHCI/NVMe storage permutations, USB Ethernet cases, and at least one
   non-ZimaOS host.

## Next Userland Track

1. Split more features into packages so update can refresh components without
   replacing the whole kernel payload.
2. Grow the small service/init registry into real configurable boot services
   once the installed VM path has more long-running daemons.
3. Improve the editor with save confirmation, file size warnings, and simple
   search.
