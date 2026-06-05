# Changelog

## Unreleased

- Added desktop package version `0.65.0` with structured Hyprland-style JSON
  diagnostics for `desktop hyprctl -j clientmodel` and
  `desktop hyprctl -j rulematches`. The VM-safe output now exposes client and
  workspace summaries, rule selectors, `safeAction`, spawn-rule counters, and
  explicit `manualDrag=false`/`hyprlandStyleFacade=true` boundaries without
  enabling Waybar, a taskbar/start menu, floating windows, free mouse drag, or
  upstream Wayland/wlroots Hyprland behavior.
- Added desktop package version `0.64.0` with structured Hyprland-style JSON
  diagnostics for `desktop hyprctl -j focushistory` and
  `desktop hyprctl -j workspacestack`. The VM-safe output now exposes
  focus-history entries, workspace stack roles, `scope`, `pinnedAware`,
  `manualDrag=false`, and `hyprlandStyleFacade=true` so future tooling can read
  the tiling model without adding Waybar, a taskbar/start menu, floating
  windows, free mouse drag, or upstream Wayland/wlroots Hyprland behavior.
- Added desktop package version `0.63.0` with stronger native desktop app
  diagnostics: `desktop apps` now reports a summary of terminal/native/overlay
  clients, `desktop app <id>` exposes data sources, runbooks, VM-ready status,
  and limits for terminal/settings/logs/packages/update/launcher, and the
  framebuffer-native settings/logs/packages/update panels show practical
  command/source hints inside tiled compositor clients. This remains tiling-only:
  no Windows taskbar, no start menu, no floating/free-drag desktop, no Waybar,
  and no upstream Wayland/wlroots Hyprland behavior is enabled.
- Added desktop package version `0.62.0` with a broader Hyprland-style config
  bridge: generated defaults and `desktop hyprctl getoption` now cover
  `dwindle:*`, `master:*`, `binds:*`, `gestures:*`, `xwayland:*`,
  `misc:*`, and `debug:*` runtime hints, and `unbind` is preserved in the
  VM-safe bind runtime file. This is still config/runtime intent only: no real
  Wayland keygrab removal, floating desktop, free mouse drag, taskbar, or
  Waybar behavior is enabled.
- Added desktop package version `0.61.0` with VM-safe Hyprland-style monitor
  dispatcher aliases: `focusmonitor`, `movecurrentworkspacetomonitor`, and
  `moveworkspacetomonitor`. They validate and report requested monitor
  targets with `single-framebuffer=yes` while keeping Orizon on its current
  framebuffer VM backend; no real multi-output Wayland/wlroots routing,
  floating desktop, free mouse drag, taskbar, or Waybar behavior is enabled.
- Added desktop package version `0.60.0` with the generated
  `orizon-desktop-hypr` package payload aligned to the live Hyprland-style
  config: package-installed `/system/desktop-binds.conf` and
  `/home/orizon/.config/hypr/orizon-hypr.conf` now include the same
  `focusmwindow`/`swapmwindow` rank binds and move-submap actions as the
  built-in profile. Package metadata also documents the rank dispatchers.
- Added desktop package version `0.59.0` with Hyprland-style rank dispatcher
  binds wired into the default user config, generated `/system/desktop-binds.conf`,
  move submap, shortcuts help, supported-bind diagnostics, and VM matrix. The
  exposed shortcuts cover `focusmwindow` and `swapmwindow` without introducing
  free mouse dragging, floating windows, a taskbar, Waybar, or upstream
  Wayland/wlroots Hyprland behavior.
- Added desktop package version `0.58.0` with VM-safe Hyprland-style
  rank-based tiled dispatchers: `focusmwindow` and `swapmwindow` now accept
  `next`, `prev`, `master`, `last`, relative `+n`/`-n`, `rank:n`,
  `index:n`, or a bare rank over the active workspace. The dispatcher facade
  also accepts `focusworkspaceoncurrentmonitor` as an honest single-framebuffer
  VM alias. This remains tiling-only: no floating windows, no manual mouse
  drag, no taskbar, no Waybar, and no upstream Wayland/wlroots routing.
- Added desktop package version `0.57.0` with stricter Hyprland-style bind
  parsing and diagnostics. `bind`, `bindl`, `bindr`, `binde`, and `bindm`
  are classified separately, `binds:*` options are no longer mistaken for key
  bindings, and the generated move submap now records `N/B/M` tiled-order
  `movewindow` actions. `bindm` remains a prepared compatibility hint only:
  no manual mouse drag, floating desktop, taskbar, Waybar, or upstream
  Wayland/wlroots Hyprland behavior is enabled.
- Added desktop package version `0.56.0` with VM-safe Hyprland-style
  `movewindow <l|r|u|d|next|prev|master>` and `movewindoworgroup` dispatcher
  aliases. They reorder tiled clients in the compositor stack and expose
  matching generated binds/package metadata without enabling floating windows,
  pixel dragging, a taskbar, Waybar, or upstream Wayland/wlroots Hyprland.
- Added desktop package version `0.55.0` with a VM-safe Hyprland-style tiled
  special workspace/scratchpad: `movetoworkspace special[:name]` and
  `movetoworkspacesilent special[:name]` can move a managed client into a
  named special overlay, while `togglespecialworkspace [name]` shows or hides
  it over the current workspace. Client/workspace diagnostics and compact
  `hyprctl -j` output now expose `special` and `specialWorkspace`. This keeps
  the compositor tiling-only: no floating windows, no manual drag, no taskbar,
  no Waybar, and no upstream Wayland/wlroots Hyprland claim.
- Added desktop package version `0.54.0` with VM-safe Hyprland-style `source`
  resolution. `source = ~/.config/hypr/orizon-local.conf` now creates and
  loads a local override file, applies its safe `env`/`workspace` hints during
  `desktop config apply`, exposes them through `desktop hyprctl getoption`,
  and reports `source-resolve` plus `SOURCE ... status=LOADED|MISSING|SKIP`
  diagnostics in `doctor/configerrors/configtrace`. The parser now handles
  in-place line trimming correctly, and supported `desktop keyword` settings
  are synced back into `/home/orizon/.config/hypr/orizon-hypr.conf` so
  `desktop hyprctl reload` keeps the updated values. This does not enable
  Wayland, floating windows, manual drag, a taskbar, or Waybar.
- Added desktop package version `0.53.0` with VM-safe Hyprland-style layout
  reset/preselect dispatchers: `layoutmsg reset`, `splitratio reset`,
  `masterratio reset`, `nmaster reset`, and `preselect <l|r|u|d|reset>` now
  make active workspace tiling state easier to recover and script without
  adding floating windows, manual drag, a taskbar, or Waybar.
- Added desktop package version `0.52.0` with Hyprland-style workspace target
  prefixes for the VM compositor facade: `r+/-n` and `r~n` select relative or
  absolute workspace slots including empty ones, while `m+/-n`, `e+/-n`,
  `m~n`, and `e~n` walk open Orizon workspaces on the current single
  framebuffer monitor. The same parser backs `workspace`, `movetoworkspace`,
  and `movetoworkspacesilent`; real multi-monitor Wayland/wlroots workspace
  routing remains prepared-only.
- Added desktop package version `0.51.0` with a VM-safe `desktop hyprctl -j`
  facade for `clients`, `workspaces`, `activeworkspace`, and `activewindow`.
  The JSON exposes Hyprland-style fields such as `address`, `workspace`,
  `fullscreenClient`, `tags`, `windows`, and `lastwindow`, while clearly
  marking `hyprlandStyleFacade=true` and keeping Wayland/wlroots/upstream
  Hyprland unimplemented.
- Added desktop package version `0.50.0` with VM-safe Hyprland-style client
  tags: `desktop dispatch tagwindow <+tag|-tag|clear|tag> [target]`, `tag:`
  selectors for `focuswindow` and move-to-workspace dispatchers, tag fields in
  `clients`, `activewindow`, `focus-history`, `workspace-stack`, and
  `client-model`, and broader simplified `windowrulev2` matching for
  `tag`, `initialClass`, `initialTitle`, `workspace`, `focus`, `pin`, and
  `fullscreen`. No floating, free-drag, or taskbar behavior is enabled.
- Added desktop package version `0.49.0` with closer Hyprland-style
  `movetoworkspace <workspace>,<window>` and
  `movetoworkspacesilent <workspace>,<window>` support: window selectors can
  target `id`, `0xaddress`, `class:app`, `title:text`, or `activewindow`, while
  the default path still moves the active tiled client and no manual drag or
  floating desktop behavior is enabled.
- Added desktop package version `0.48.0` with Hyprland-style focus-last and
  urgent diagnostics: `focuscurrentorlast`, `focusurgentorlast`, and the
  VM-only `markurgent` diagnostic dispatcher now exercise most-recent focus
  history, urgent client selection, and `urgent` fields in `clients`,
  `activewindow`, `focus-history`, `workspace-stack`, and `client-model`.
- Added desktop package version `0.47.0` with a closer Hyprland-style
  `fullscreenstate internal client` dispatcher: Orizon now tracks compositor
  fullscreen state separately from the future client-visible fullscreen state,
  accepts `-1..3` plus the legacy `on/off/toggle` form, and reports
  `fullscreenClient`/`fullscreenState` in client diagnostics.
- Added desktop package version `0.46.0` with stricter monocle/fullscreen deck
  rendering diagnostics: only the focused tiled client is rendered in monocle
  while other clients remain focusable as `monocle-deck`, and `layout-tree`,
  `clients`, and `activewindow` now report `rendered=yes/no`. The optional
  bar layer now defaults to `bar no` in the VM desktop profile and package
  payload, keeping Waybar/status-bar work planned but not enabled by default.
- Added desktop package version `0.45.0` with Hyprland-style master layout
  `nmaster` support: `layoutmsg nmaster`, `addmaster`, and `removemaster`
  update per-workspace master/stack tiling state and diagnostics without
  enabling floating windows or manual drag.
- Added desktop package version `0.44.0` with per-workspace focus restore and
  closer Hyprland-style move semantics: `movetoworkspace` follows the moved
  tiled client, while `movetoworkspacesilent` keeps the current workspace active.
- Added desktop package version `0.43.0` with Hyprland-style workspace naming:
  `desktop dispatch renameworkspace <target> <safe-name>`, `workspace
  name:<name>`, and `movetoworkspace name:<name>` now work in the VM
  compositor while keeping the tiling-only/no-manual-drag model.
- Fixed a ZimaOS VM boot/reset loop seen after AHCI storage discovery by
  keeping the libvirt iTCO watchdog explicitly non-resetting, skipping automatic
  persistence auto-load during `orizon.safe=1`, and reading persistence slots
  header-first with small boot I/O chunks.
- Allowed explicit `persist save` to prepare persistence metadata lazily after a
  safe boot skipped auto-load, preserving the safer boot path while keeping VM
  persistence smoke checks usable.
- Fixed desktop first-state creation so missing `/system/desktop-state.conf`
  no longer recurses through session loading during boot.
- Fixed the SSH `desktop focus-window <target>` wrapper so dispatcher success
  output is actually sent back to the remote shell.
- Hardened VM matrix boot automation to select the Limine entry before sending
  Orizon console commands, preventing accidental red `Configuration is INVALID`
  edits in Limine.
- Added safe default `hyprctl getoption` values for prepared runtime hints such
  as `cursor:no_hardware_cursors`, `render:direct_scanout`, and
  `decoration:blur:enabled`.
- Added desktop package version `0.42.0` with idempotent Hyprland-style client
  state dispatchers: `desktop dispatch fullscreen|pseudo|pseudotile|pin
  <on|off|toggle|1|0>` plus the simplified `fullscreenstate` alias. Existing
  no-argument dispatchers still toggle state, while scripted VM flows can now
  set fullscreen, pseudo and pinned state without ambiguity.
- Hardened the ZimaOS VM smoke boot path so first-boot OVMF/Limine screens are
  handled before console DHCP/SSH commands are injected, and documented the
  red `Configuration is INVALID` Limine symptom.
- Added desktop package version `0.41.0` with `desktop workspace-stack` and
  `desktop hyprctl workspacestack`, a VM-safe Hyprland-style diagnostic for
  per-workspace master/stack/focus order, pinned/local client scope, stable
  client addresses, focus ranks, and geometry while preserving
  `manual-drag=no`.
- Added desktop package version `0.40.0` with per-workspace tiling layout
  state. `desktop dispatch layoutmsg layout <dwindle|master|monocle>` now
  changes the active workspace layout without enabling floating/free-drag
  behavior, and `desktop layout-state` / `desktop hyprctl layoutstate` expose
  each workspace's layout, split mode, split ratio, and master ratio.
- Added desktop package version `0.39.0` with Hyprland-style
  `focuswindow` targeting for tiled clients. `desktop dispatch focuswindow`
  and `desktop focus-window` can now focus by id, stable `0x...` address,
  class/app, or title while preserving the no-free-drag tiling boundary.
- Added desktop package version `0.38.0` with safe spawn-time
  `windowrulev2` application for tiled clients: `tile`, `fullscreen`,
  `pseudo`, `pin`, and `workspace N` can now set initial client state, while
  floating/free-drag style actions stay ignored and visible in diagnostics.
- Added desktop package version `0.37.0` with `desktop rule-matches` and
  `desktop hyprctl rulematches`, a VM-safe diagnostic that reads
  `/system/desktop-rules.conf`, maps `windowrulev2` selectors to current tiled
  clients by class/title/app, and keeps the explicit `manual-drag=no`
  Hyprland-style boundary.
- Added desktop package version `0.36.0` with `desktop client-model` and
  `desktop hyprctl clientmodel`, a VM-safe diagnostic graph for clients,
  workspaces, focus history, fullscreen/pseudo/pinned state, rules, and the
  explicit `manual-drag=no` Hyprland-style boundary.
- Added desktop package version `0.35.0` with read-only Hyprland config
  tracing: `desktop config trace` and `desktop hyprctl configtrace` explain
  line-by-line `APPLY`, `PREPARE`, `IGNORE`, and `ERROR` parser decisions
  without changing runtime state.
- Added desktop package version `0.34.0` with active tiling tree diagnostics:
  `desktop layout-tree` and `desktop hyprctl layouttree` show workspace root
  geometry, client roles, rectangles, focus state, `focusHistoryID`, and the
  explicit `manual-drag=no` boundary.
- Added desktop package version `0.33.0` with architecture truth-map commands
  `desktop backend`, `desktop protocol`, `desktop hyprctl backend`, and
  `desktop hyprctl protocol`, backed by `/system/desktop-backend.conf` and
  `/system/desktop-protocol.conf`; the current backend remains VM framebuffer
  and Wayland/wlroots/upstream Hyprland stay documented as prepared-only.
- Added desktop package version `0.32.0` with split desktop module package
  samples for `orizon-desktop-core`, `orizon-terminal`, `orizon-settings`, and
  `orizon-launcher`; named installs auto-prepare the core for app modules while
  `orizon-waybar` remains explicitly planned and not installed.
- Added desktop package version `0.31.0` with persistent render/animation
  settings: `focus-ring`, `shadow-range`, `animation-ticks`,
  `animation-curve`, and `render-profile`, plus Hyprland-style
  `getoption/keyword` support for render focus rings, shadow range, and
  animation tick budget.
- Added desktop package version `0.30.0` with `desktop input` as a VM-safe
  keyboard/pointer/focus hub, including FR/US layout sync to `/system/keyboard`
  and `/workspace/.orizon/keyboard`.
- Added desktop package version `0.29.0` with a compositor-managed app catalog:
  `desktop apps`, `desktop app <id>`, class/module/backend/surface diagnostics,
  and explicit launcher overlay dispatch via `desktop launch launcher`.
- Added desktop package version `0.28.0` with a stronger installed/live session
  manager: v2 state health, lifecycle counters, `desktop rescue`, recovery
  commands, and clearer file checks before `desktop recover`.
- Added desktop package version `0.27.0` with broader Hyprland-style config
  hint preservation for input repeat/touchpad, decoration blur/shadow, cursor,
  render, debug, device, plugin, permission, group, gestures, xwayland, and
  ecosystem keys.
- Added desktop package version `0.26.0` with Hyprland-style directional
  tiled focus/swap dispatchers: `movefocus l|r|u|d` and
  `swapwindow l|r|u|d`, plus generated arrow/H-L binds and package metadata.
- Added desktop package version `0.25.0` with ten dynamic Hyprland-style
  workspace slots, `workspace next`, `workspace empty`, wrap-around relative
  workspace targets, and `movetoworkspacesilent`.
- Expanded active/workspace diagnostics with workspace names, local client
  counts, visit sequence data, and pinned-aware dynamic state.
- Added desktop package version `0.24.0` with Hyprland-style framebuffer render
  diagnostics through `desktop render` and `desktop hyprctl render`.
- Added software focus ring drawing, shadow/rounding diagnostics, and ticked
  focus/workspace/layout transition state for the Orizon desktop compositor.
- Documented that these render improvements remain VM-safe Orizon framebuffer
  UX, not upstream Wayland/wlroots/Hyprland validation.
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
- Added the prepared modular desktop packaging map with `desktop modules`,
  `/system/desktop-modules.conf`, discoverable `orizon-desktop-core`,
  `orizon-terminal`, `orizon-settings`, `orizon-launcher`, and future
  `orizon-waybar` package metadata, and bumped `orizon-desktop-hypr` to 0.23.0.
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
