# Orizon OS Packages

Orizon packages are intentionally small for the first implementation. The goal
is to let Orizon update and install separate components without turning the
kernel updater into a giant boot-only replacement tool.

Current package-manager limits are summarized in [STATUS.md](STATUS.md).
`pkg rollback` is local package recovery, not full boot-level package rollback.
Package/update release rules are centralized in [RELEASE.md](RELEASE.md), and
VM package failure triage is in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
When package behavior changes, update `CHANGELOG.md`, this page, and
`STATUS.md`; regenerate release artifacts only when runtime source or generated
package/update artifacts changed.

Official repository:

```text
https://github.com/Orizon-cmd/Orizon-Packages
```

## Commands

```text
pkg help
pkg list
pkg status
pkg audit
pkg doctor
pkg cache
pkg search orizon
pkg search desktop
pkg remote
pkg remote verify
pkg upgrade plan
pkg update
pkg upgrade
pkg info orizon-hello
pkg history
pkg sample
pkg sample desktop
pkg info orizon-desktop-hypr
pkg hash /workspace/packages/orizon-hello.opkg
pkg verify /workspace/packages/orizon-hello.opkg
pkg simulate /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
pkg install orizon-desktop-hypr
pkg remove orizon-desktop-hypr
pkg rollback orizon-desktop-hypr
pkg remove orizon-hello
pkg rollback orizon-hello
```

`pkg update`, `pkg upgrade`, `pkg install`, `pkg remove`, and `pkg rollback`
are available only after Orizon OS has been installed to disk. Live boot can
inspect, audit, diagnose, search, create, hash, verify and simulate package files, but it refuses
persistent package changes because the live ISO is not the installed system.
`pkg audit` checks package database/cache consistency, `pkg doctor` adds a v5
safety summary for directories, transactions, remote index state and package
repo signature state, and `pkg cache` prints cache
paths and counters, and `pkg simulate <file>` prints a dry-run install/upgrade
plan without writing files.
`pkg upgrade plan` remains safe in live boot: it reads the cached signed remote
index when present and prints install/upgrade/current/protected decisions
without mutating files. `pkg update` and `pkg upgrade` are intentionally thin
wrappers around the signed system `update` flow: the package index is
authenticated by the signed OS manifest, pinned package repository commit, and
pinned package-index SHA-256. `pkg remote verify` validates the cached index
shape, paths, hashes, sizes and duplicate names, then checks the prepared
detached sidecar `/workspace/.orizon/package-index.sig` when it exists. If that
sidecar is absent, Orizon reports WARN and keeps the signed manifest pin as the
honest fallback.

`pkg search desktop` and `pkg info orizon-desktop-hypr` expose the optional
desktop package even before it is installed. `pkg sample desktop` creates
`/workspace/packages/orizon-desktop-hypr.opkg`. The split module samples are
also generated on demand with `pkg sample orizon-desktop-core`, `pkg sample
orizon-terminal`, `pkg sample orizon-settings`, and `pkg sample
orizon-launcher`; named installs such as `pkg install orizon-terminal`
auto-prepare `orizon-desktop-core` first on an installed VM. `orizon-waybar`
stays planned only: no Waybar/taskbar package is generated or installed now.
The all-in-one optional package installs the first Orizon desktop profile:
`/system/desktop.conf`, `/system/desktop-session.conf`,
`/system/desktop-settings.conf`,
`/system/desktop-modules.conf`,
`/system/desktop-binds.conf`, `/system/desktop-autostart.conf`,
`/system/desktop-rules.conf`, `/system/desktop-monitors.conf`,
`/system/desktop-layers.conf`,
`/system/desktop-runtime.conf`,
`/system/desktop-state.conf`,
`/home/orizon/.config/hypr/orizon-hypr.conf`, and
`/system/share/orizon-desktop-hypr.conf`. It is Hyprland-style configuration
for Orizon's compositor, not upstream Hyprland/Wayland yet. After an installed
VM boot:

```text
pkg install orizon-desktop-hypr
pkg sample orizon-desktop-core
pkg verify /workspace/packages/orizon-desktop-core.opkg
pkg sample orizon-terminal
pkg sample orizon-settings
pkg sample orizon-launcher
pkg info orizon-terminal
pkg search orizon-terminal
desktop status
desktop session
desktop settings
desktop settings paths
desktop settings export
desktop settings sync
desktop modules
desktop settings presets
desktop settings doctor
desktop settings preset compact
desktop config doctor
desktop config apply
desktop config trace
desktop input
desktop input layout fr
desktop input layout us
desktop input pointer natural
desktop input focus toggle
desktop pointer
desktop rules
desktop monitors
desktop runtime
desktop layers
desktop version
desktop devices
desktop systeminfo
desktop backend
desktop protocol
desktop layouts
desktop layout-state
desktop layout-tree
desktop animations
desktop decorations
desktop render
desktop descriptions
desktop instances
desktop submap
desktop configerrors
desktop config trace
desktop rollinglog
desktop focus-history
desktop workspace-stack
desktop client-model
desktop rule-matches
desktop keyword general:gaps_in 9
desktop hyprctl version
desktop hyprctl systeminfo
desktop hyprctl backend
desktop hyprctl protocol
desktop hyprctl clientmodel
desktop hyprctl rulematches
desktop hyprctl activeworkspace
desktop hyprctl focushistory
desktop hyprctl workspacestack
desktop hyprctl layouts
desktop hyprctl layoutstate
desktop hyprctl layouttree
desktop hyprctl animations
desktop hyprctl decorations
desktop hyprctl render
desktop hyprctl descriptions
desktop hyprctl instances
desktop hyprctl submap
desktop hyprctl cursorpos
desktop hyprctl devices
desktop hyprctl splash
desktop hyprctl configerrors
desktop hyprctl configtrace
desktop hyprctl -j configerrors
desktop hyprctl -j configtrace
desktop hyprctl rollinglog
desktop hyprctl getoption general:gaps_in
desktop hyprctl -j getoption general:gaps_in
desktop hyprctl keyword decoration:rounding 11
desktop hyprctl -j keyword decoration:rounding 11
desktop hyprctl keyword decoration:shadow:range 22
desktop hyprctl getoption decoration:shadow:range
desktop hyprctl keyword animations:tick_budget 24
desktop hyprctl getoption animations:tick_budget
desktop hyprctl getoption render:focus_ring
desktop hyprctl getoption render:profile
desktop keyword layerrule blur, launcher
desktop hyprctl getoption layerrule
desktop hyprctl keyword input:repeat_rate 40
desktop hyprctl getoption input:repeat_rate
desktop hyprctl reload
desktop hyprctl -j reload
desktop hyprctl clients
desktop hyprctl -j version
desktop hyprctl -j systeminfo
desktop hyprctl -j backend
desktop hyprctl -j protocol
desktop hyprctl -j clients
desktop hyprctl clientmodel
desktop hyprctl rulematches
desktop hyprctl activewindow
desktop hyprctl -j activewindow
desktop hyprctl -j workspaces
desktop hyprctl -j activeworkspace
desktop hyprctl -j focushistory
desktop hyprctl -j workspacestack
desktop hyprctl -j clientmodel
desktop hyprctl -j rulematches
desktop hyprctl -j layoutstate
desktop hyprctl -j layouttree
desktop hyprctl -j monitors
desktop hyprctl -j devices
desktop hyprctl -j keymap
desktop hyprctl -j cursorpos
desktop hyprctl -j animations
desktop hyprctl -j decorations
desktop hyprctl -j render
desktop hyprctl -j binds
desktop hyprctl -j layers
desktop dispatch togglesplit
desktop dispatch layoutmsg layout master
desktop dispatch layoutmsg splitratio 60
desktop dispatch layoutmsg splitratio +5
desktop dispatch layoutmsg masterratio 65
desktop dispatch layoutmsg nmaster 2
desktop dispatch layoutmsg addmaster
desktop dispatch layoutmsg removemaster
desktop dispatch layoutmsg splitratio reset
desktop dispatch layoutmsg masterratio reset
desktop dispatch layoutmsg nmaster reset
desktop dispatch layoutmsg reset
desktop dispatch layoutmsg preselect r
desktop dispatch layoutmsg preselect up
desktop dispatch layoutmsg preselect reset
desktop dispatch focusmaster
desktop dispatch swapwithmaster
desktop dispatch focusmwindow rank:2
desktop dispatch focusmwindow master
desktop dispatch movefocus r
desktop dispatch focuswindow class:orizon-terminal
desktop dispatch swapwindow l
desktop dispatch swapmwindow next
desktop dispatch swapmwindow rank:2
desktop dispatch movewindow r
desktop dispatch movewindow master
desktop dispatch resizeactive 5 0
desktop dispatch submap resize
desktop dispatch movetoworkspacesilent empty
desktop dispatch tagwindow +settings class:orizon-settings
desktop dispatch focuswindow tag:settings
desktop dispatch movetoworkspacesilent 2,tag:settings
desktop dispatch movetoworkspacesilent 2,class:orizon-settings
desktop dispatch movetoworkspace active,activewindow
desktop dispatch workspace next
desktop dispatch focusworkspaceoncurrentmonitor active
desktop dispatch workspace empty
desktop focus-window title:Terminal
desktop hyprctl submap reset
desktop keymap
desktop doctor
desktop launch settings
desktop launch logs
desktop launch packages
desktop launch update
desktop launch launcher
desktop apps
desktop app settings
```

The named install path generates the local `.opkg`, installs it, then enables
the profile with a package hook. Removing the package disables the desktop
policy, and `pkg rollback orizon-desktop-hypr` restores the last removed
desktop package snapshot. The generated desktop package is currently version
`0.73.0` because it includes policy/config files, the persisted session
settings, the system-wide desktop settings layer, settings hub paths/export/sync
commands, `/system/desktop-modules.conf`, `/system/desktop-backend.conf`,
`/system/desktop-protocol.conf`, Hyprland-style config doctor/apply/trace import diagnostics, the VM-safe
`/home/orizon/.config/hypr/orizon-local.conf` source override, generated
bind/autostart/window-rule/monitor/layer/runtime hint files, runtime inspection
commands, `desktop keyword`, input/version/systeminfo/backend/protocol/layouts/layout-state/layout-tree/animations/decorations/render/descriptions/instances/submap/configerrors/config-trace/rollinglog/focus-history/workspace-stack/client-model/rule-matches/keymap diagnostics, the
`hyprctl [-j] version/systeminfo/backend/protocol/clients/clientmodel/rulematches/workspaces/activeworkspace/activewindow/focushistory/workspacestack/monitors/layouts/layoutstate/layouttree/animations/decorations/render/descriptions/instances/submap/devices/keymap/cursorpos/splash/configerrors/configtrace/rollinglog/getoption/keyword/reload/binds/layers`
facade, pointer diagnostics, the aligned Hyprland-style key template,
preset/focus commands, dispatcher commands, split `fullscreenstate internal client`
diagnostics with `fullscreenClient`, pseudo/pseudotile/pinned/urgent client state,
move-to-workspace window selectors (`id`, `0xaddress`, `class:app`,
`title:text`, `tag:name`, `activewindow`) for dispatcher-only tiling moves,
VM-safe `tagwindow` diagnostics and safe spawn-time `windowrulev2` actions for
tile/fullscreen/pseudo/pin/tag/workspace,
stable client addresses, `focusHistoryID`, active-window/client geometry,
compact JSON for `clients`/`workspaces`/`activeworkspace`/`activewindow`/
`focushistory`/`workspacestack`/`clientmodel`/`rulematches`/`layoutstate`/
`layouttree`/`monitors`/`devices`/`keymap`/`cursorpos`/`animations`/`decorations`/`render`/`version`/`systeminfo`/`backend`/`protocol`/`configerrors`/`configtrace`/`getoption`/`keyword`/`reload`/
`binds`/`layers`,
focus-cycle/focusmwindow/focuswindow/focuscurrentorlast/focusurgentorlast/markurgent/swap/focusmaster/swapwithmaster/swapmwindow/togglesplit/layoutmsg layout plus split/master ratio/nmaster/resizeactive/submap actions, idempotent client-state dispatch, per-workspace layout state, monocle deck rendered diagnostics, a `bar no` default so Waybar/status-bar work remains future and opt-in, workspace stack diagnostics, directional movefocus/swapwindow/movewindow, rank-based active-workspace focus/swap dispatch, dynamic workspace next/empty plus `r/m/e` prefixed targets and `focusworkspaceoncurrentmonitor` VM alias, silent move-to-workspace dispatch, active F9/F10/F11 submaps,
VM-safe monitor dispatch aliases `focusmonitor`,
`movecurrentworkspacetomonitor`, and `moveworkspacetomonitor`,
Hyprland-style tiled special workspace dispatchers `togglespecialworkspace
[name]` and `movetoworkspace special[:name]` without floating/manual drag,
VM-safe tiled-order `movewindow <l|r|u|d|next|prev|master>` dispatchers,
native tiling clients for settings/logs/packages/update, the app catalog/detail
commands `desktop apps` and `desktop app <id>` with native app data-source,
runbook, VM-ready, and limit diagnostics, launcher-as-overlay dispatch,
the `desktop input` layout/pointer/focus hub with `/system/keyboard` sync,
`desktop keymap`, and commands used by `desktop theme`,
`desktop wallpaper`, `desktop layout`, `desktop autostart`, `desktop bar`, and
the launcher. Version `0.73.0` extends the JSON truth-map layer with
`desktop hyprctl -j version`, `desktop hyprctl -j systeminfo`,
`desktop hyprctl -j backend`, and `desktop hyprctl -j protocol`, including
package/compositor metadata, framebuffer backend state, internal IPC protocol
state, prepared future `wayland-wlroots` intent, path status, and explicit
`wayland=false` / `wlroots=false` / `manualDrag=false` /
`hardwareValidation=false` boundaries for VM-safe tooling without claiming
upstream Hyprland socket compatibility, Wayland clients, wlroots, Waybar,
floating windows, free mouse drag, or physical hardware validation. Version
`0.72.0` extends the JSON diagnostics layer with
`desktop hyprctl -j animations`, `desktop hyprctl -j decorations`, and
`desktop hyprctl -j render`, including software framebuffer animation state,
focus ring/border/shadow state, renderer/profile/protocol boundaries, and
explicit `manualDrag=false` / `wayland=false` / `wlroots=false` boundaries for
VM-safe tooling without enabling a taskbar, Waybar, floating windows, free
mouse drag, upstream Wayland/wlroots Hyprland behavior, or physical hardware
validation. Version `0.71.0` extends the JSON diagnostics layer with
`desktop hyprctl -j devices`, `desktop hyprctl -j keymap`, and
`desktop hyprctl -j cursorpos`, including keyboard layout/submap state,
pointer coordinates/buttons, input backend summaries, and explicit
`libinput=false` / `manualDrag=false` boundaries for VM-safe tooling without
Wayland keygrabs, free mouse drag, or physical input validation. Version
`0.70.0` extends the JSON diagnostics layer with
`desktop hyprctl -j monitors`, including the single framebuffer monitor,
active workspace, reserved edges, scale, and explicit `singleFramebuffer=true`
/ `manualDrag=false` / `wayland=false` boundaries for VM-safe tooling without
enabling multi-output Wayland routing, taskbars, Waybar, floating windows,
free mouse drag, or physical monitor validation. Version `0.69.0` extends the
JSON diagnostics layer with
`desktop hyprctl -j binds` and `desktop hyprctl -j layers`, including bind
variant counts, `bindm` prepared-only status, framebuffer layer state, and
explicit `manualDrag=false` / `waybarActive=false` / `taskbar=false`
boundaries for VM-safe tooling without enabling a taskbar, Waybar, floating
windows, free mouse drag, layer-shell, or upstream Wayland/wlroots Hyprland
behavior. Version `0.68.0` extends the JSON diagnostics/action layer with
`desktop hyprctl -j getoption`, `desktop hyprctl -j keyword`, and
`desktop hyprctl -j reload`, including mapped values, keyword result status,
runtime hint files, reload results, and explicit `manualDrag=false` /
`taskbar=false` boundaries for VM-safe tooling without enabling a taskbar,
Waybar, floating windows, free mouse drag, or upstream Wayland/wlroots
Hyprland behavior. Version `0.67.0` extends the JSON diagnostics layer with
`desktop hyprctl -j configerrors` and `desktop hyprctl -j configtrace`,
including parser summaries, source resolution, line-by-line
apply/prepare/ignore decisions, and explicit `manualDrag=false` /
`taskbar=false` boundaries for VM-safe tooling without enabling a taskbar,
Waybar, floating windows, free mouse drag, or upstream Wayland/wlroots
Hyprland behavior. Version `0.66.0` extends the JSON diagnostics layer with
`desktop hyprctl -j layoutstate` and `desktop hyprctl -j layouttree`,
including per-workspace layout state, ratios, `nmaster`, active tiling-tree
nodes, roles, rectangles, and explicit `manualDrag=false` /
`floatingSceneGraph=false` boundaries for VM-safe tooling without enabling a
taskbar, Waybar, floating windows, free mouse drag, or upstream Wayland/wlroots
Hyprland behavior. Version `0.65.0` extends the JSON diagnostics layer with
`desktop hyprctl -j clientmodel` and `desktop hyprctl -j rulematches`,
including summary, workspace/client graph, selectors, `safeAction`, and
spawn-rule diagnostics for VM-safe tooling without enabling a taskbar,
Waybar, floating windows, free mouse drag, or upstream Wayland/wlroots
Hyprland behavior. Version `0.64.0` extends the JSON diagnostics layer with
`desktop hyprctl -j focushistory` and `desktop hyprctl -j workspacestack`,
including `focusHistoryID`, `scope`, `role`, `pinnedAware`, and
`manualDrag=false` fields for VM-safe tooling without enabling a taskbar,
Waybar, floating windows, free mouse drag, or upstream Wayland/wlroots
Hyprland behavior. Version `0.63.0` expands the native app layer: the terminal,
settings, logs, packages, update, and launcher catalog entries now expose
data sources/runbooks/limits, and the generated package records app catalog,
detail, launch, source, and surface-policy metadata. The rendered
settings/logs/packages/update panels remain framebuffer tiling clients and do
not enable floating windows, free mouse drag, a Windows taskbar/start menu,
Waybar, or upstream Wayland/wlroots behavior. Version `0.62.0` expands the config bridge with generated
defaults and runtime `getoption` support for `dwindle:*`, `master:*`,
`binds:*`, `gestures:*`, `xwayland:*`, `misc:*`, `debug:*`, and `unbind`
preservation in `/system/desktop-binds.conf`. These remain VM-safe runtime
hints and do not enable real Wayland keygrab removal, floating windows, free
mouse drag, taskbar, or Waybar. Version `0.61.0` adds Hyprland-style monitor dispatch aliases:
`focusmonitor`, `movecurrentworkspacetomonitor`, and
`moveworkspacetomonitor` validate/report requested monitors with
`single-framebuffer=yes` while the VM backend remains the single
Orizon-framebuffer and does not claim Wayland/wlroots output routing. Version
`0.60.0` aligns the generated `orizon-desktop-hypr`
package payload with the live profile: package-installed
`/system/desktop-binds.conf` and `/home/orizon/.config/hypr/orizon-hypr.conf`
include the same `focusmwindow`/`swapmwindow` binds and move-submap actions.
Version `0.59.0` wires the rank-based Hyprland-style dispatchers
into the generated user config, `/system/desktop-binds.conf`, the active move
submap, supported-bind diagnostics, and VM matrix. The default shortcuts expose
`focusmwindow`/`swapmwindow` while keeping Orizon tiling-only: no free mouse
dragging, floating desktop, taskbar, Waybar, or upstream Wayland/wlroots claim
is enabled. Version `0.58.0` adds rank-based Hyprland-style tiled dispatch:
`focusmwindow` and `swapmwindow` accept `next`, `prev`, `master`, `last`,
relative `+n`/`-n`, `rank:n`, `index:n`, or a bare rank over the active
workspace. It also exposes `focusworkspaceoncurrentmonitor` as an honest
single-framebuffer VM alias for workspace focus. No floating windows, free
mouse drag, taskbar, Waybar, or upstream Wayland/wlroots routing is enabled.
Version `0.57.0` adds stricter Hyprland-style bind diagnostics:
`bind`, `bindl`, `bindr`, `binde`, and `bindm` are classified separately,
`binds:*` stays a runtime hint instead of a key binding, and the generated move
submap records `N/B/M` tiled-order `movewindow` actions. `bindm` remains a
prepared compatibility hint only and does not enable free-drag window moving.
Version `0.19.0` specifically added the desktop session manager:
`desktop start/stop/restart/reload/recover/state`, persisted
`/system/desktop-state.conf`, `/logs/desktop-session.log`, and package
metadata for session lifecycle. Version `0.20.0` adds keyboard ergonomics:
active resize/move/launch submaps, `resizeactive`, `desktop keymap`, and
focus-follows-mouse transitions in the compositor. Mouse binds are parsed for compatibility, but
the package does not enable free-drag window moving by default.
Version `0.21.0` adds the first native app clients launched with
`desktop launch settings|logs|packages|update`; they remain tiled compositor
clients and do not add a taskbar or manual window dragging.
Version `0.22.0` adds the desktop settings hub commands:
`desktop settings paths`, `desktop settings export`, and `desktop settings
sync`, keeping `/system` as the source of truth while regenerating the
Hyprland-style user config and runtime hints.
Version `0.23.0` adds the modular desktop packaging map and prepared package
metadata for `orizon-desktop-core`, `orizon-terminal`, `orizon-settings`,
`orizon-launcher`, and future `orizon-waybar`. The split packages are prepared
for discovery and documentation; the current install path remains
`pkg install orizon-desktop-hypr`.
Version `0.24.0` adds framebuffer render diagnostics and first live render UX:
software focus ring, shadow/rounding diagnostics, ticked focus/workspace/layout
transition state, `desktop render`, and `desktop hyprctl render`. It remains a
Hyprland-style Orizon compositor facade, not upstream Wayland/wlroots.
Version `0.25.0` expands the workspace model to ten dynamic Hyprland-style
slots, adds `workspace next`, `workspace empty`, relative wrap-around targets,
`movetoworkspacesilent`, and richer active/workspace diagnostics.
Version `0.26.0` adds Hyprland-style directional tiled focus and swap:
`movefocus l|r|u|d`, `swapwindow l|r|u|d`, generated arrow/H-L binds, and package
metadata for these dispatchers. It still does not enable manual window dragging.
Version `0.27.0` expands the Hyprland-style config bridge with
`input:repeat_*`, touchpad, border color, decoration blur/shadow, cursor,
render, debug, device, plugin, permission, group, gestures, xwayland, and
ecosystem runtime hints. These keys are inspectable through
`desktop config doctor`, `desktop runtime`, and `desktop hyprctl getoption`, but
remain honest runtime intent until Orizon grows the real Wayland backend.
Version `0.28.0` strengthens the installed/live desktop session manager with a
v2 state file, `desktop rescue`, health and policy coherence reporting,
lifecycle counters, recovery commands, file checks, and clearer
crash/recover-ready diagnostics for VM usage.
Version `0.29.0` adds a compositor-managed desktop app catalog with
`desktop apps`, `desktop app <id>`, class/module/backend/surface diagnostics,
and explicit `desktop launch launcher` overlay dispatch. It keeps all native
apps tiled or overlay-only and still does not add a taskbar, start menu, or
manual free-drag windows.
Version `0.30.0` adds `desktop input` as a VM-safe Hyprland-style input hub:
keyboard layout switching synchronizes desktop settings with `/system/keyboard`
and `/workspace/.orizon/keyboard`, pointer profiles are persisted for
diagnostics, focus-follows-mouse is controlled from the same surface, and submap
dispatch remains keyboard-only without manual window dragging.
Version `0.31.0` adds persistent render and animation settings:
`focus-ring`, `shadow-range`, `animation-ticks`, `animation-curve`, and
`render-profile`. These values are written by the installer/package, exported
into the Hyprland-style user config, consumed by the framebuffer compositor, and
inspectable through `desktop render`, `desktop decorations`, `desktop
animations`, and `desktop hyprctl getoption render:focus_ring|render:profile|decoration:shadow:range|animations:tick_budget`.
Version `0.32.0` turns the prepared module map into package-manager surfaces:
`pkg sample orizon-desktop-core|orizon-terminal|orizon-settings|orizon-launcher`
writes separate `.opkg` files, `pkg install <module>` is accepted by name on an
installed VM, app modules auto-prepare `orizon-desktop-core`, and
`pkg info/search` report sample/install paths. `orizon-waybar` remains a
future separate package only and is not generated or installed.
Version `0.33.0` adds the desktop architecture truth map:
`desktop backend`, `desktop protocol`, `desktop hyprctl backend`, and
`desktop hyprctl protocol`, backed by `/system/desktop-backend.conf` and
`/system/desktop-protocol.conf`. These commands document the current
`framebuffer-vm` backend and internal `orizon-desktop-ipc-v0` dispatcher
protocol while keeping Wayland/wlroots/upstream Hyprland marked prepared-only
and not implemented.
Version `0.34.0` adds active tiling tree diagnostics with
`desktop layout-tree` and `desktop hyprctl layouttree`. The output reports
workspace root geometry, client roles, rectangles, fullscreen/pseudo/pinned/urgent
state, focus status, `focusHistoryID`, and the explicit tiling-only
`manual-drag=no` boundary.
Version `0.35.0` adds read-only Hyprland config tracing with
`desktop config trace` and `desktop hyprctl configtrace`, showing line-by-line
`APPLY`, `PREPARE`, `IGNORE`, and `ERROR` decisions plus the runtime route for
each parsed key.
Version `0.36.0` adds `desktop client-model` and `desktop hyprctl clientmodel`
as read-only diagnostics for the current Hyprland-style client/workspace/focus
state graph, including fullscreen/pseudo/pinned/urgent state, rules runtime, stable
client addresses, backend truth, and the `manual-drag=no` boundary.
Version `0.37.0` adds `desktop rule-matches` and `desktop hyprctl rulematches`
as read-only diagnostics for `/system/desktop-rules.conf`, mapping
`windowrulev2` selectors to current tiled clients with a simplified matcher
and the same no-drag/no-floating boundary.
Version `0.38.0` applies a safe subset of matching `windowrulev2` actions when
clients spawn: `tile`, `fullscreen`, `pseudo`, `pin`, and `workspace N`. The
client diagnostics now show `rulesMatched`, `rulesApplied`, and `ruleActions`,
while floating/free-drag style actions remain ignored and visible.
Version `0.50.0` extends the same VM-safe model with client tags:
`desktop dispatch tagwindow <+tag|-tag|clear|tag> [target]`, `tag:name`
selectors for focus/move dispatchers, tag-aware `clients`/`activewindow`
diagnostics, and safe spawn-time `windowrulev2` `tag` actions. This remains a
Hyprland-style facade on Orizon's framebuffer VM backend, not upstream
Wayland/wlroots Hyprland.
Version `0.51.0` adds compact `desktop hyprctl -j` JSON for `clients`,
`workspaces`, `activeworkspace`, and `activewindow`, so future status tooling
and the later separate Waybar-style package can consume stable VM-safe state
without adding any bar/taskbar now. Version `0.64.0` extends that JSON surface
to `focushistory` and `workspacestack`; version `0.65.0` adds `clientmodel`
and `rulematches`; version `0.66.0` adds `layoutstate` and `layouttree`;
version `0.67.0` adds `configerrors` and `configtrace`; version `0.68.0`
adds `getoption`, `keyword`, and `reload`; version `0.69.0` adds `binds` and
`layers`; version `0.70.0` adds `monitors`; version `0.71.0` adds
`devices`, `keymap`, and `cursorpos`; version `0.72.0` adds `animations`,
`decorations`, and `render`; version `0.73.0` adds `version`, `systeminfo`,
`backend`, and `protocol`.
Version `0.54.0` adds VM-safe Hyprland-style `source` resolution for
`~/.config/hypr/orizon-local.conf`, with `source-resolve` diagnostics and
runtime `env`/`workspace` hints visible through `desktop hyprctl getoption`.
Version `0.55.0` adds a VM-safe tiled special workspace/scratchpad model:
`desktop dispatch movetoworkspacesilent special:magic,activewindow` hides a
managed client in the named special overlay and `desktop dispatch
togglespecialworkspace magic` shows or hides it over the current workspace.
Diagnostics expose `special`/`specialWorkspace` in clients, activewindow,
workspaces, workspace-stack, client-model, and compact JSON. It does not add
floating windows, mouse dragging, a taskbar, or upstream Hyprland/wlroots.
Version `0.56.0` adds VM-safe Hyprland-style
`desktop dispatch movewindow <l|r|u|d|next|prev|master>` and
`movewindoworgroup` aliases. They move the active tiled client through the
compositor order or into the master slot only; no floating, pixel drag, taskbar,
Waybar, or upstream Wayland/wlroots compositor behavior is enabled.
Version `0.53.0` adds VM-safe Hyprland-style layout reset and preselect
dispatchers: `desktop dispatch layoutmsg reset` restores active workspace
layout defaults, `splitratio reset`, `masterratio reset`, and `nmaster reset`
reset ratios/master count individually, and `preselect <l|r|u|d|reset>` maps
directional split hints onto the current framebuffer tiling state without
creating floating windows or manual drag. Version `0.52.0` adds
Hyprland-style workspace target prefixes to the same
single-monitor VM facade: `r+/-n` and `r~n` select relative or absolute
workspace slots including empty slots, while `m+/-n`, `e+/-n`, `m~n`, and
`e~n` walk open Orizon workspaces on the current framebuffer monitor. The
parser is shared by `workspace`, `movetoworkspace`, and
`movetoworkspacesilent`; real multi-monitor Wayland routing is still future
work.

## Package Format

A package is one text file:

```text
orizon-package 1
name orizon-hello
version 0.1.0
depends orizon-core core-x86_64
depends orizon-packages text-payload-v5
sha256 <sha256 of every byte after the payload line>
payload:
file /system/share/orizon-hello.txt
Hello from an Orizon package.
content-end
post-install
append /workspace/packages/history.log orizon-hello 0.1.0 installed
end-post-install
pre-remove
echo pre-remove: orizon-hello cleanup starting
end-pre-remove
post-remove
append /workspace/packages/history.log orizon-hello 0.1.0 removed
end-post-remove
```

The hash covers the raw payload bytes after `payload:`. That keeps the header
editable while still proving that the files and post-install actions were not
changed. Optional `depends <name> <version|*>` lines are checked before install;
missing dependencies block `pkg install` and show as warnings in `pkg verify`.

## Payload Features

Supported payload entries:

- `file <absolute-path>` followed by file contents and `content-end`
- `post-install` followed by script lines and `end-post-install`
- `pre-remove` followed by script lines and `end-pre-remove`
- `post-remove` followed by script lines and `end-post-remove`

Supported script commands:

- `mkdir <path>`
- `touch <path>`
- `write <path> <text>`
- `append <path> <text>`
- `echo <text>`
- `sync`

Package writes are limited to safe Orizon paths: `/system`, `/home`,
`/packages`, `/logs`, `/tmp`, and `/workspace`. Packages cannot write inside
`/workspace/.orizon`; that area belongs to the package database and installer.
Payloads and scripts also reject common secret-bearing targets such as
`/system/ssh.conf`, `/system/ssh_host_rsa.key`, `.env`, `.key`,
`.private.pem`, `.ssh`, private, secret, token, credential, `id_rsa` and
`id_ed25519` paths. This is a conservative guardrail for VM work, not a full
package sandbox.

## Installed Database

Installed package state is stored under:

```text
/workspace/.orizon/pkgdb
/workspace/.orizon/pkgdb/installed
/workspace/.orizon/pkgdb/packages
/workspace/.orizon/pkgdb/removed
/workspace/.orizon/pkgdb/cache
/workspace/.orizon/pkgdb/transaction.state
/workspace/.orizon/pkgdb/upgrade.plan
/workspace/.orizon/package-index
/workspace/.orizon/package-index.sig
```

The current VFS still stores real persistence through `/workspace`. Because of
that, installed package manifests are kept in `/workspace/.orizon/pkgdb` and
replayed during boot to restore runtime files under paths such as `/system`.
Post-install scripts are not replayed on every boot, only the package file
payload is.

Runtime package views are mirrored to:

```text
/workspace/.orizon/packages
/system/packages
/system/installed
```

## Next Steps

The first GitHub package index is now active:

```text
packages/x86_64/index.txt
packages/x86_64/<name>.opkg
```

`update` compares installed package versions, downloads only missing or changed
`.opkg` files, verifies their SHA-256 from the index, and then lets `pkg`
verify the internal payload SHA-256 before installation.

`pkg search <query>` searches builtin, installed and cached remote package
metadata. `pkg remote` prints the cached signed package index and makes clear
when it is not available yet. `pkg remote verify` writes
`/workspace/.orizon/pkgdb/cache/remote.status` and
`/workspace/.orizon/pkgdb/cache/remote.sig.status` with the last validation
results. `pkg audit` reports invalid stored packages, orphan metadata, missing
metadata, rollback snapshots, remote-index status and a PASS/WARN/FAIL summary.
`pkg doctor` adds a broader non-destructive v5 health check for database
directories, the last transaction, cached upgrade plan, script policy and
signature fallback. `pkg cache`
prints the package database/cache layout and is useful for CI logs.
`pkg simulate <file>` parses a package, validates dependencies, lists scripts/files, shows the
install/upgrade action and confirms that the run is dry-run only.
`pkg upgrade plan` compares cached remote package versions against builtin and
installed package metadata, then prints a non-destructive plan. `pkg upgrade`
prints that plan and then reuses the signed `update` flow to refresh packages.
`pkg info <name>` shows stored package metadata, dependencies, scripts and the
files owned by an installed package.

`pkg remove <name>` saves a rollback snapshot in
`/workspace/.orizon/pkgdb/removed`, runs an optional `pre-remove` script,
removes files declared by the stored manifest, runs an optional `post-remove`
script, deletes installed metadata, refreshes `/system/installed`, and persists
the package database. `pkg rollback <name>` restores the last removed snapshot
if the package is not already installed.

Package install now keeps a previous package manifest in memory while applying
an upgrade. If payload replay or metadata update fails, Orizon removes the
partial new payload and restores the previous package payload/metadata when it
exists. History entries include explicit `installed` / `upgraded` /
`removed` / `rollback` events with `transaction=v5-N`, rollback and result
fields. The latest write operation also updates
`/workspace/.orizon/pkgdb/transaction.state`. Remove rollback is persistent
across reboots through the package database, but it is still a local package
transaction guard, not a full boot-level package rollback.
