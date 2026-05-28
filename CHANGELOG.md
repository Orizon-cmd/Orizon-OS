# Changelog

## Unreleased

- Added the first optional Hyprland-style desktop profile with installer
  choice, `desktop` commands, `orizon-desktop-hypr` package generation, and
  F1/F2 terminal open/close support in the compositor.
- Extended the desktop package flow with named `pkg install
  orizon-desktop-hypr`, package enable/disable hooks, `desktop doctor`,
  `desktop logs`, `desktop shortcuts`, `desktop reset`, and package search/info
  discovery.
- Added a persisted desktop session layer with `desktop session`, theme,
  wallpaper, bar, launcher controls, F3 launcher toggle, and package payload
  version `0.2.0` for `/system/desktop-session.conf`.
- Added initial Hyprland-style workspace commands for the Orizon compositor:
  `desktop workspace`, `desktop workspace <n>`, and
  `desktop move terminal <n>`.
- Added `desktop windows` and `desktop layout <name>` as the first diagnostic
  seam for future compositor window management.
- Added `desktop profiles` and `desktop autostart terminal on|off|toggle`, and
  aligned the generated desktop package template as `orizon-desktop-hypr` 0.3.0.
- Added desktop presets plus `desktop focus on|off|toggle`, expanded the
  Hyprland-style key template, and bumped `orizon-desktop-hypr` to 0.4.0.
- Reworked the desktop direction toward Hyprland's tiling model with
  `dwindle`/`master`/`monocle`, tiled client focus, `desktop binds`, dispatcher
  commands, no mouse-drag window workflow, and package payload version 0.5.0.
- Expanded the Hyprland-style dispatcher model with fullscreen, pseudo,
  pinned, `cyclenext`, `swapnext`, relative workspaces, richer `hyprctl`
  active-window state, and package payload version 0.6.0.
- Added the system-wide desktop settings layer at
  `/system/desktop-settings.conf`, `desktop settings` commands, package/install
  generation hooks, compositor consumption of gaps/borders/shadows, and package
  payload version 0.7.0.
- Added desktop settings presets and validation with `desktop settings
  presets`, `desktop settings preset <name>`, `desktop settings doctor`, and
  package payload version 0.8.0.
- Added VM pointer plumbing for the desktop with USB HID boot mouse and
  QEMU-style `usb-tablet` reports routed into the compositor cursor,
  `desktop pointer` diagnostics, and package payload version 0.9.0.
- Added `desktop config doctor` and `desktop config apply` as a Hyprland-style
  config bridge: Orizon now parses variables, sections, binds, monitors,
  exec-once/env/windowrule/workspace/source keywords, applies the supported
  layout/gaps/borders/rounding/animation/shadow/input subset, and bumps the
  desktop package payload to version 0.10.0.
- Persisted generated Hyprland-style runtime files for config-applied binds,
  autostart, window rules, monitor hints, and env/workspace/source intent under
  `/system/desktop-*.conf`, exposed them through `desktop binds` and
  `desktop autostart`, and bumped `orizon-desktop-hypr` to 0.11.0.
- Added Hyprland-style runtime inspection and mutation with `desktop
  rules/monitors/runtime/layers`, `desktop keyword`, `desktop hyprctl
  getoption/keyword/binds/layers`, and bumped `orizon-desktop-hypr` to 0.12.0.
- Extended the Hyprland-like facade with `desktop version/devices` plus
  `desktop hyprctl version/activeworkspace/devices/cursorpos/splash`, and
  bumped `orizon-desktop-hypr` to 0.13.0.
- Added more Hyprland-style inspection surfaces with `desktop
  systeminfo/layouts/animations/configerrors/rollinglog`, matching `hyprctl`
  facade commands, and bumped `orizon-desktop-hypr` to 0.14.0.
- Added Hyprland-style layout control surfaces: `desktop dispatch
  togglesplit/layoutmsg/submap`, `desktop decorations/descriptions/instances/submap`,
  matching `hyprctl` facade commands, submap runtime hints, and bumped
  `orizon-desktop-hypr` to 0.15.0.
- Strengthened the Hyprland-style client model with stable client addresses,
  richer `desktop windows/clients/activewindow`, `focusHistoryID`,
  `desktop focus-history`, `desktop hyprctl focushistory`, per-client geometry,
  workspace last-window diagnostics, and bumped `orizon-desktop-hypr` to 0.16.0.
- Improved Hyprland-style tiling controls with direct
  `focusmaster`/`swapwithmaster` dispatchers, relative split ratios,
  master ratio/`mfact`, explicit orientation layout messages, richer layout
  diagnostics, and bumped `orizon-desktop-hypr` to 0.17.0.
- Extended the Hyprland-style config bridge with preserved `layerrule`,
  `bindm`/`bindl`, `bezier`/`animation`, input/misc/layout runtime hints,
  runtime-backed `getoption`, reload applying config, `/system/desktop-layers.conf`,
  and bumped `orizon-desktop-hypr` to 0.18.0.
- Added a Hyprland-style desktop session manager with
  `desktop start/stop/restart/reload/recover/state`, persistent
  `/system/desktop-state.conf`, `/logs/desktop-session.log`, lifecycle state in
  `desktop status`, and bumped `orizon-desktop-hypr` to 0.19.0.
- Added Hyprland-style keyboard ergonomics with active F9/F10/F11
  resize/move/launch submaps, F12/Esc reset, `desktop keymap`,
  `desktop dispatch resizeactive`, real focus-follows-mouse transitions in the
  VM compositor, and bumped `orizon-desktop-hypr` to 0.20.0.
- Added the first native Hyprland-style desktop apps as tiled clients:
  `desktop launch settings|logs|packages|update`, launcher/submap shortcuts,
  compositor app surfaces, and bumped `orizon-desktop-hypr` to 0.21.0.
- Added the desktop settings hub with `desktop settings paths`,
  `desktop settings export`, and `desktop settings sync`, keeping `/system` as
  the source of truth while regenerating the Hyprland-style user config/runtime
  hints, and bumped `orizon-desktop-hypr` to 0.22.0.
- Aligned the documentation/release map after the VM/ZimaOS stabilization
  blocks: README, STATUS, RELEASE, ROADMAP, troubleshooting, and subsystem docs
  now point to the same implemented/prepared/not-validated boundary.
- Added security posture v2 diagnostics with VFS policy state files,
  policy-denial counters, key-rotation posture, and `security doctor` snapshots.
- Improved package manager v5 diagnostics with signed remote-index sidecar
  posture, cached upgrade plans, transaction state, and clearer `pkg doctor`
  output.
- Improved installed VM system administration with firstboot, service/log
  state, system health/snapshot/backup, repair, and rescue workflows.
- Strengthened the VM matrix and CI release guard with stricter PASS/WARN/FAIL
  reporting, source/artifact synchronization checks, and clearer release logs.
- Added persistent root snapshot/restore tooling for `/workspace`, `/home`, `/system`, `/packages`, and `/logs`.
- Added installed-system lifecycle commands for live vs installed state, first boot, rescue, hostname, and repair.
- Added installed-system UX v2 with service-state/logs, firstboot checklist, MOTD/fstab/rescue/admin defaults, and `system logs`.
- Added installed-system UX v3 with `system health`, `system snapshot`, `system backup`, `/system/os-release`, `/system/machine-id`, and non-secret admin backup reports.
- Extended the package manager with signed remote metadata, search/remote/status/history, dependency checks, and rollback-oriented install/remove paths.
- Improved the local console with scrollback, keyboard paging, command history, and a simple `less` pager.
- Added daily network diagnostics for DHCP renew, DNS, routes, TLS probe, and clearer update/pkg network errors.
- Improved VM daily networking with `net daily`, retrying TCP probes, explicit NAT/bridge boundary notes, and SSH/matrix coverage.
- Hardened SSH diagnostics and security policy with host-key persistence, lockout/audit reporting, safer file access, and explicit signed-manifest posture.
- Added developer release guardrails: quick checks, release artifact validation, generated release notes, and CI log artifacts.
- Added a CI release guard entrypoint that centralizes quick checks, secret scan, release validation, release notes, and artifact synchronization summaries for GitHub Actions.
- Added a central status/limits page documenting VM-ready features, prepared hardware paths, known non-implemented work, and the no-real-hardware-validation boundary.
- Improved VM matrix reporting for bridge profiles by probing ARP/neigh IP discovery and marking unreachable bridge guests as boot/framebuffer-only instead of ambiguous SSH skips.
- Added a tracked-secret CI gate and release-notes preview artifact to the GitHub workflows.
- Clarified update rollback strategy/status with explicit Limine boot-count scope, BootNext/A-B not-prepared markers, and rollback metadata.
- Added `bootguard recover`, richer `rollback-status`, and explicit pseudo-A/B metadata for Limine main/rollback recovery.
- Fixed installed-state detection so the package database at `/system/installed` no longer disables the live installer.
- Added `storage vmcheck` / `storage repair` read-only VM storage verification across detected disks with GPT and first/last-sector probes.
- Added modern VirtIO-blk VM storage support, VirtIO disk smoke coverage, and clearer storage diagnostics for VirtIO-blk vs VirtIO-scsi.
- Added a small installed/live init layer with `system init`, `system services`, `system doctor`, `/system/boot-state`, and `/logs/init.log`.
- Added package manager v4 diagnostics with `pkg audit`, `pkg cache`, `pkg simulate`, transaction-tagged history, and clearer detached-signature limitations.
- Improved the local framebuffer shell with `shell status`, `wc`, richer `grep`, `tee` pipelines, and searchable command history.
- Added security hardening v2 diagnostics with `security policy/audit/keys/doctor`, SSH host-key rotation, and redacted SSH audit events.
- Added release and VM/ZimaOS troubleshooting guides, plus clearer documentation labels for implemented, prepared, simulated, and non-validated work.
- Strengthened CI release guards with source/artifact sync checks and added PASS/WARN/FAIL VM matrix summaries.
