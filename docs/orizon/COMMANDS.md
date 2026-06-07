# Orizon OS Command Quick Reference

This page groups the commands that matter most during validation. It is not a
complete shell manual; it is the checklist to avoid hunting through the longer
docs.

## Status And Limits

Before claiming a feature is complete, check [STATUS.md](STATUS.md). It is the
short source of truth for VM-ready features, prepared hardware paths, known
limits, and the rule that Lenovo/real-hardware validation needs fresh captures.
Desktop commands should be read through the desktop taxonomy there:
`desktop hyprctl` is a Hyprland-style Orizon facade, framebuffer VM outputs are
VM-ready only, future Wayland/wlroots and `orizon-waybar` entries are prepared
only, and no command proves physical hardware.
Use [RELEASE.md](RELEASE.md) for the artifact/CI checklist and
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) for VM/ZimaOS failure triage.

## Release And Build

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-iso
python scripts/orizon/orizon_update.py --mode zimaos-vm
python scripts/orizon/orizon_update.py --mode github-iso
python scripts/orizon/orizon_update.py --mode validate-release
python scripts/orizon/quick_check.py
python scripts/orizon/ci_release_guard.py --output-dir artifacts
python scripts/orizon/release_notes.py --output release_notes.md
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e --include-lifecycle
python scripts/orizon/test_update_rollback_vm.py
```

Human release notes live in `CHANGELOG.md`; generated CI/release previews come
from `scripts/orizon/release_notes.py` and `updates/x86_64/release.txt`.

`test_vm_matrix.py --cases all --include-lifecycle` runs NAT and bridge
profiles. NAT cases are SSH-validated. Bridge cases now try `virsh` ARP and
host neighbor-table discovery; if no IP is discoverable, they still perform a
boot/framebuffer smoke and report `WARN` instead of silently pretending that
SSH was tested. Per-case logs and `matrix-summary.{md,json}` are written under
`artifacts/vm-matrix/`.

`quick_check.py` runs `git diff --check`, Python syntax checks for all
`scripts/orizon/*.py`, the tracked-secret scan, PowerShell syntax checks when
PowerShell is available, and the strict release-artifact validator. Use
`--log artifacts/quick-check.log` when a CI or ZimaOS run should keep the
combined output.

`ci_release_guard.py` is the GitHub Actions entrypoint. It runs quick checks
without duplicating release validation, then runs the strict release validator,
generates release notes, and writes `artifacts/release-summary.md` plus
`artifacts/release-artifacts.json` so stale ISO/kernel/manifest/signature
failures are obvious in CI logs. It also writes `source-artifact-sync.{md,json}`
and fails CI when runtime source changes without refreshed release artifacts.

Expected release artifacts:

```text
Orizon-OS.iso
updates/x86_64/kernel.elf
updates/x86_64/BOOTX64.EFI
updates/x86_64/limine.conf
updates/x86_64/manifest.txt
updates/x86_64/manifest.sig
updates/x86_64/release.txt
```

## Boot, Install, And Rollback

```text
install
install-plan
install-status
system status
system health
system snapshot
cat /workspace/.orizon/system-snapshot.txt
system backup
cat /workspace/.orizon/admin-backup.txt
system init
system services
system logs
system doctor
system repair
rescue
system firstboot
firstboot done
hostname
hostname set orizon-vm
desktop status
desktop config
desktop config doctor
desktop config apply
desktop config trace
desktop start
desktop stop
desktop restart
desktop reload
desktop recover
desktop rescue
desktop state
desktop session
desktop settings
desktop settings paths
desktop settings export
desktop settings sync
desktop settings presets
desktop settings doctor
desktop settings preset compact
desktop settings set gaps-in 10
desktop settings set border-size 3
desktop settings repair
desktop input
desktop input layout fr
desktop input layout us
desktop input pointer natural
desktop input focus toggle
desktop input submap launch
desktop input submap reset
desktop pointer
desktop keymap
desktop modules
desktop apps
desktop app settings
desktop apps launcher
desktop profiles
desktop preset moss
desktop binds
desktop rules
desktop monitors
desktop runtime
desktop layers
desktop version
desktop devices
desktop systeminfo
desktop backend
desktop protocol
desktop architecture
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
desktop rollinglog
desktop focus-history
desktop workspace-stack
desktop focus-state
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
desktop hyprctl focusstate
desktop hyprctl layouts
desktop hyprctl layoutstate
desktop hyprctl layouttree
desktop hyprctl animations
desktop hyprctl decorations
desktop hyprctl render
desktop hyprctl descriptions
desktop hyprctl instances
desktop hyprctl submap
desktop hyprctl keymap
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
desktop hyprctl -j getoption env
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
desktop hyprctl keyword dwindle:pseudotile false
desktop hyprctl getoption dwindle:pseudotile
desktop hyprctl keyword master:mfact 0.60
desktop hyprctl getoption master:mfact
desktop hyprctl keyword unbind SUPER,Q
desktop hyprctl getoption unbind
desktop hyprctl reload
desktop hyprctl -j reload
desktop hyprctl binds
desktop hyprctl -j binds
desktop hyprctl layers
desktop hyprctl -j layers
desktop dispatch exec terminal
desktop hyprctl clients
desktop hyprctl activewindow
desktop hyprctl -j version
desktop hyprctl -j systeminfo
desktop hyprctl -j backend
desktop hyprctl -j protocol
desktop hyprctl -j architecture
desktop hyprctl -j clients
desktop hyprctl -j activewindow
desktop hyprctl -j workspaces
desktop hyprctl -j activeworkspace
desktop hyprctl -j focushistory
desktop hyprctl -j workspacestack
desktop hyprctl -j focusstate
desktop hyprctl -j clientmodel
desktop hyprctl -j rulematches
desktop hyprctl monitors
desktop hyprctl -j monitors
desktop hyprctl -j devices
desktop hyprctl -j keymap
desktop hyprctl -j cursorpos
desktop hyprctl -j animations
desktop hyprctl -j decorations
desktop hyprctl -j render
desktop hyprctl -j layouts
desktop hyprctl -j descriptions
desktop hyprctl -j instances
desktop hyprctl -j modules
desktop hyprctl -j shortcuts
desktop hyprctl -j submap
desktop hyprctl -j splash
desktop hyprctl -j session
desktop hyprctl -j rollinglog
desktop autostart
desktop autostart terminal off
desktop autostart terminal on
desktop dispatch fullscreen
desktop dispatch fullscreen off
desktop dispatch fullscreenstate 1
desktop dispatch fullscreenstate 2 0
desktop dispatch fullscreenstate 0 2
desktop dispatch pseudo
desktop dispatch pseudotile on
desktop dispatch pin
desktop dispatch pin off
desktop dispatch cyclenext
desktop dispatch swapnext
desktop dispatch movefocus r
desktop dispatch focuswindow class:orizon-terminal
desktop dispatch focusmwindow rank:2
desktop dispatch focusmwindow master
desktop focus-window title:Terminal
desktop dispatch swapwindow l
desktop dispatch swapmwindow next
desktop dispatch swapmwindow rank:2
desktop dispatch movewindow r
desktop dispatch movewindow master
desktop dispatch swapwithmaster
desktop dispatch focusmaster
desktop dispatch togglesplit
desktop dispatch layoutmsg layout master
desktop dispatch layoutmsg splitratio 60
desktop dispatch layoutmsg splitratio +5
desktop dispatch layoutmsg masterratio 65
desktop dispatch layoutmsg mfact -5
desktop dispatch layoutmsg nmaster 2
desktop dispatch layoutmsg addmaster
desktop dispatch layoutmsg removemaster
desktop dispatch layoutmsg splitratio reset
desktop dispatch layoutmsg masterratio reset
desktop dispatch layoutmsg nmaster reset
desktop dispatch layoutmsg reset
desktop dispatch layoutmsg orientationleft
desktop dispatch layoutmsg orientationtop
desktop dispatch layoutmsg preselect r
desktop dispatch layoutmsg preselect up
desktop dispatch layoutmsg preselect reset
desktop dispatch resizeactive 5 0
desktop dispatch submap resize
desktop hyprctl submap reset
desktop windows
desktop clients
desktop client-model
desktop rule-matches
desktop activewindow
desktop workspace
desktop workspace 2
desktop dispatch movetoworkspace 2
desktop dispatch movetoworkspacesilent empty
desktop dispatch workspace 2
desktop dispatch workspace previous
desktop dispatch workspace +1
desktop dispatch workspace r+1
desktop dispatch workspace r~2
desktop dispatch workspace m~1
desktop dispatch workspace e+1
desktop dispatch focusworkspaceoncurrentmonitor active
desktop dispatch renameworkspace 2 dev
desktop dispatch workspace name:dev
desktop dispatch movetoworkspace name:dev
desktop dispatch movetoworkspacesilent special:magic,activewindow
desktop dispatch togglespecialworkspace magic
desktop dispatch tagwindow +settings class:orizon-settings
desktop dispatch focuswindow tag:settings
desktop dispatch movetoworkspacesilent 2,tag:settings
desktop dispatch movetoworkspacesilent 2,class:orizon-settings
desktop dispatch movetoworkspace active,activewindow
desktop dispatch workspace next
desktop dispatch workspace empty
desktop workspace empty
desktop dispatch movefocus next
desktop dispatch focuswindow 0x1100
desktop shortcuts
desktop keymap
desktop doctor
desktop logs
desktop enable
desktop disable
desktop reset
desktop write-config
desktop theme moss
desktop wallpaper dawn
desktop layout master
desktop focus toggle
desktop bar toggle
desktop apply
desktop launcher show
desktop launch terminal
desktop launch settings
desktop launch logs
desktop launch packages
desktop launch update
desktop launch launcher
desktop apps
desktop app settings
desktop app logs
desktop app packages
desktop app update
desktop killactive
desktop open terminal
desktop close terminal
desktop package
boot-check
dualboot-check
repair-boot
bootguard
bootguard confirm
bootguard recover
rollback-status
rollback
logs boot
logs update
```

Known limit: rollback is currently Limine/boot-count style after Orizon early
boot. `bootguard` and `update status` show the strategy, remaining attempts,
normal/fallback Limine config cache, `bootguard-recover`,
`pseudo-ab-slots: prepared=yes`, `nvram-bootnext: prepared=no`, and
`ab-slots: prepared=no`. `bootguard recover` arms the cached fallback Limine
config for the next boot. True firmware `BootNext` through UEFI Runtime
Services is not implemented yet.

`install-plan` is non-destructive. It writes
`/workspace/.orizon/install-report.txt` so VM/SSH checks can review the selected
disk, mode, write scope, confirmation string, and GPT snapshot before any
installer write happens.

`system status` is the fast installed/live lifecycle view. It prints
`boot-mode: live` or `boot-mode: installed`, hostname, first-boot state,
persistent roots, required `/system` files, init/log state, and safe next
commands. `system health` is the concise PASS/WARN operator view.
`system snapshot` writes `/workspace/.orizon/system-snapshot.txt` with status, health,
services, doctor, firstboot, and log evidence. `system backup` writes
`/workspace/.orizon/admin-backup.txt` with non-secret configuration only:
`os-release`, `machine-id`, hostname, fstab map, network policy, services,
rescue policy, admin guide, notes, and shell profile. SSH private keys, update
private keys, package payload secrets, and disk data are excluded. `system init`
reruns the idempotent boot tasks, writes
`/system/boot-state`, `/system/service-state`, `/logs/init.log`, and
`/logs/service.log`, performs the VM-safe `desktop-restore` reload for the
Hyprland-style session state, falls back to `desktop recover` if reload returns
WARN, and persists roots when possible.
`system services` shows the small service policy (`persistence`, `bootlog`,
`network`, `ssh`, `desktop`, `desktop-restore`, `package-db`,
`update-bootguard`, `firstboot`) without
pretending to be a full service manager. `system logs` prints the boot-state,
service-state, init log, and service log in one place. `system firstboot`
prints the first installed boot checklist before `firstboot done` marks it as
reviewed. `system doctor` audits roots/config/init state without writes.
`rescue` prints the non-destructive recovery checklist. `system repair`
recreates only missing defaults under `/workspace/.orizon`, `/home/orizon`,
`/system`, `/packages`, and `/logs`, including `/system/motd`,
`/system/fstab`, `/system/os-release`, `/system/machine-id`,
`/system/rescue.conf`, `/system/admin-guide.txt`,
`/system/admin-notes.txt`, `/home/orizon/.profile`, then writes
`/workspace/.orizon/rescue-report.txt`.
`hostname set <name>` persists `/system/hostname`.

`desktop` controls the optional first desktop profile. It is Hyprland-style,
not upstream Hyprland/Wayland yet. The live ISO keeps it disabled by default.
Use `desktop start` or `pkg install orizon-desktop-hypr` to start the
compositor desktop session; `desktop enable` remains a compatibility alias.
`desktop stop`, `desktop restart`, `desktop reload`, `desktop recover`,
`desktop rescue`, `desktop state`, and `desktop hyprctl -j session
[status|start|stop|restart|reload|recover|rescue]` manage or inspect `/system/desktop-state.conf` and
`/logs/desktop-session.log`. The state file now records health,
desired/runtime/policy coherence, live/install boot mode, lifecycle counters,
and recovery commands. `desktop rescue` is read-only and prints the safe
checklist before `desktop recover` repairs/reapplies anything. Since package
`0.90.0`, `system init` also records `reload-result`, `fallback-action`, and
`fallback-result`, and attempts `desktop recover` before recommending rescue.
Since package `0.91.0`, native app diagnostics also expose read-only compositor
runtime per app: client counts, mapped/hidden state, focus/workspace state,
focused client address/title in text output, and launcher overlay visibility.
Since package `0.89.0`, `system init`, `system services`, and `system logs`
expose `desktop-restore` boot/session reload diagnostics for VM/ZimaOS recovery.
Since package
`0.88.0`, `desktop state`, `desktop rescue`, and JSON `hyprctl session` also
include a VM-ready session audit with file presence/size checks,
`recommendedAction`, and `rescueRecommended`. `desktop config doctor` validates the Hyprland-style
user config and reports VM-safe `source-resolve` status for
`/home/orizon/.config/hypr/orizon-local.conf`; `desktop config apply` imports the supported subset into the
session/settings files plus generated runtime hint files such as
`/system/desktop-binds.conf`, `/system/desktop-autostart.conf`,
`/system/desktop-rules.conf`, `/system/desktop-monitors.conf`, and
`/system/desktop-layers.conf`, `/system/desktop-runtime.conf`, and
`/system/desktop-state.conf`, and `desktop config trace` explains each config
line as `APPLY`, `PREPARE`, `IGNORE`, or `ERROR`, including `SOURCE
... status=LOADED|MISSING|SKIP`, without changing runtime
state.
`layerrule`, `bind`/`bindl`/`bindr`/`binde`/`bindm`, `unbind`, `binds:*`,
`bezier`, `animation`, and input/device/decoration/cursor/render/debug/misc/layout/dwindle/master/gestures/xwayland
hints are preserved there until the real Wayland backend exists; bind variants
are counted in diagnostics. Since package `0.86.0`, `desktop configerrors`,
`desktop hyprctl -j configerrors`, `desktop hyprctl -j binds`, and
`desktop hyprctl -j shortcuts` expose plain/keyboard/mouse/composite bind flag
counts plus `manualDragFromBindm=false`; mouse binds are parsed for
compatibility without enabling free-drag window moving by default.
Since package `0.87.0`, source resolution diagnostics also include per-source
file entries with `LOADED`, `MISSING`, `SKIP_DUP`, `SKIP_UNSAFE`,
`DEPTH_LIMIT`, or `FILE_LIMIT` status in text and JSON `configerrors` output.
Since package `0.92.0`, `desktop submap`, `desktop keymap`,
`desktop shortcuts`, and JSON `submap|keymap|shortcuts` expose the active
submap role/actions, sticky-until-reset policy, exit hints, keyboard layout,
pointer profile, and focus-follows-mouse counters. This remains VM framebuffer
input over dispatcher/tiling actions; mouse `bindm` still does not enable free
manual window drag.
Since package `0.101.0`, `desktop hyprctl -j rollinglog` mirrors both
`/logs/desktop.log` and `/logs/desktop-session.log` as structured VM admin
data. JSON includes `preview`, `sessionPreview`, `bytesSampled`,
`sessionBytesSampled`, `eventsEmpty`, and `sessionEmpty`, while still avoiding
upstream Hyprland socket-log claims.
Since package `0.100.0`, `desktop hyprctl -j reload` keeps the text `result`
and adds structured `parserSummary`, `sourceResolve`, `runtimeFiles`,
`session`, and `settings` objects. VM automation can now check parser/source
counts, generated runtime files, session policy, and render/input settings
without scraping the prose report, still inside the Orizon VM framebuffer
facade.
Since package `0.99.0`, `desktop hyprctl keyword <key> <value>` reports
`route`, `effect`, `target-path`, and `reload-recommended`; JSON keyword adds
`route`, `effect`, `targetPath`, and `reloadRecommended`. These labels separate
immediate settings, session state, prepared runtime hints, and unsupported
keys while staying inside the Orizon VM framebuffer facade.
Since package `0.98.0`, `desktop hyprctl getoption <hint>` keeps the mapped
`value:` and adds `entry-count`, while JSON getoption adds `entryCount`,
`entries`, `entriesStored`, and `entriesTruncated` for repeated Hyprland-style
runtime hints such as `env`, `workspace`, `monitor`, `bind`, `windowrulev2`,
`layerrule`, and `source`. This remains VM/ZimaOS framebuffer diagnostics, not
upstream Hyprland IPC, Wayland/wlroots, Waybar, floating/manual drag, or
physical hardware validation.
Since package `0.97.0`, `desktop focus-state`, `desktop hyprctl focusstate`,
and JSON `desktop hyprctl -j focusstate` expose a read-only active-workspace
focus/master/stack diagnostic with `focusHistoryID`, split/master ratios,
fullscreen/pseudo/pinned flags, and `lastDispatch` status/error/hint/result.
It is VM framebuffer state only, not upstream Hyprland IPC, Wayland/wlroots,
Waybar, floating/manual drag, or physical hardware validation.
Since package `0.96.0`, `desktop truth`, `desktop hyprctl truth`, and JSON
`desktop hyprctl -j truth` expose the runtime truth taxonomy used by the docs:
implemented, VM-ready, simulated facade, prepared, not implemented, and not
hardware-proven. This gives VM/ZimaOS operators a direct command for checking
the Orizon framebuffer facade versus upstream Hyprland, Wayland/wlroots,
Waybar activation, floating/manual drag, and physical hardware validation.
Since package `0.95.0`, `desktop architecture`, `desktop backend`, `desktop
protocol`, and JSON `architecture|backend|protocol` expose concrete
architecture contracts: `single-framebuffer-surface-v0`,
`software-raster-present-v0`, `internal-tiled-client-v0`, backend
capabilities/limits, protocol limits, and the future backend contract. This is
still prepared-only for Wayland/wlroots and remains on the VM framebuffer
backend.
Since package `0.94.0`, `desktop modules`, JSON `modules`, `pkg info
<desktop-module>`, and generated module `.opkg` samples expose `split-plan`,
dependency graph, module boundaries, activation roles, and `split-version 2`.
This prepares `orizon-desktop-core`, `orizon-terminal`, `orizon-settings`, and
`orizon-launcher` as separate packages while keeping `orizon-waybar`
future-only and inactive.
Since package `0.93.0`, `desktop render`, `desktop decorations`, `desktop
animations`, and JSON `render|decorations|animations` expose framebuffer
surface/reserved areas, tiling area, scale policy, frame budget, rendered
client counts, gaps/borders/rounding, focus ring, shadows, and
client/workspace animation state without enabling Waybar, floating windows, or
manual drag.
`desktop settings` manages the system-wide
`/system/desktop-settings.conf` layer created by the installer/package and
stores compositor defaults such as gaps, border size, rounding, animations,
shadows, focus ring, shadow range, animation tick budget/curve, render profile,
terminal, launcher, keyboard, and pointer policy. `desktop settings
paths` shows the central `/system` settings hub, `desktop settings export`
rewrites `/home/orizon/.config/hypr/orizon-hypr.conf` from `/system`, and
`desktop settings sync` exports then refreshes runtime hints. `desktop settings
presets`, `desktop settings preset <name>`, and `desktop settings doctor` add a
safer profile/validation layer for that system file. `desktop session`
manages theme/wallpaper/bar/focus state, `desktop preset <name>` applies a saved
symbolic session, `desktop input` is the keyboard/pointer/focus hub for
`layout fr|us`, pointer profiles, focus-follows-mouse, and submaps,
`desktop pointer` shows cursor and HID mouse/tablet diagnostics,
`desktop layout <dwindle|master|monocle>` updates the
prepared layout profile, `desktop keymap` shows active F-key/submap keyboard
runtime, active submap role/actions, sticky reset policy, and focus-follows-mouse counters, `desktop binds`, `desktop rules`, `desktop monitors`,
and `desktop runtime` show the generated Hyprland-style runtime files,
`desktop layers` shows the compositor layer model, `desktop version` identifies
the honest Orizon compatibility facade, `desktop devices` summarizes keyboard
and pointer inputs, `desktop systeminfo`, `desktop layouts`, `desktop
layout-state`, `desktop layout-tree`, `desktop animations`, `desktop decorations`, `desktop descriptions`, `desktop instances`,
`desktop render`, `desktop submap`, `desktop configerrors`,
`desktop config trace`, `desktop rollinglog`, `desktop focus-history`,
`desktop workspace-stack`, `desktop client-model`, `desktop rule-matches`, and
`desktop truth` expose more Hyprland-like inspection surfaces,
`desktop keyword <key> <value>` applies a single
Hyprland-style runtime keyword, and supported settings keywords are written
back into `/home/orizon/.config/hypr/orizon-hypr.conf` so a later
`desktop hyprctl reload` keeps the value. `desktop dispatch
<dispatcher> [args]` runs `exec`, `killactive`,
`workspace`, `focusworkspaceoncurrentmonitor`, `focusmonitor`,
`movecurrentworkspacetomonitor`, `moveworkspacetomonitor`,
`togglespecialworkspace`, `renameworkspace`, `movetoworkspace`,
`movetoworkspacesilent`, `movefocus`, `focusmwindow`, `focuswindow`,
`focuscurrentorlast`, `focusurgentorlast`, `markurgent`, `tagwindow`,
`cyclenext`, `swapnext`,
`swapwindow`, `swapmwindow`, `movewindow`, `movewindoworgroup`, `focusmaster`, `swapwithmaster`, `fullscreen`,
`fullscreenstate`, `pseudo`, `pseudotile`, `pin`,
`togglesplit`, `layoutmsg`, `resizeactive`, and `submap`,
including `layoutmsg layout <dwindle|master|monocle>`,
`layoutmsg reset`, `layoutmsg preselect <l|r|u|d|reset>`,
`layoutmsg splitratio <10-90|+/-n|reset>`, `masterratio`/`mfact`, and
`nmaster <1-8|+/-n|reset>`/`addmaster`/`removemaster`, plus explicit
`orientationleft/right/top/bottom` tiling hints and
`focusmwindow`/`swapmwindow <next|prev|master|last|+n|-n|rank:n|index:n>`
for active workspace rank-based focus/swap, `movewindow
<l|r|u|d|next|prev|master>` tiled-order movement without
floating/manual drag,
monitor dispatch aliases that report `single-framebuffer=yes` instead of
pretending to route real Wayland outputs,
idempotent client-state dispatchers
`fullscreen|pseudo|pseudotile|pin <on|off|toggle|1|0>` and
`fullscreenstate <internal 0-3|-1> <client 0-3|-1>` or the legacy
`fullscreenstate <on|off|toggle|1|0>`,
`desktop hyprctl ...` exposes
a small Hyprland-like facade for version/systeminfo/architecture/truth/backend/protocol/clients/clientmodel/rulematches/workspaces/activeworkspace/monitors/activewindow/focushistory/workspacestack/focusstate/binds/keymap/layers/layouts/layoutstate/animations/decorations/render/descriptions/instances/modules/shortcuts/autostart/apps/app/launch/submap/devices/cursorpos/splash/session/configerrors/configtrace/rollinglog/getoption/keyword/dispatch/reload,
`desktop truth`, `desktop architecture`, `desktop backend`, and `desktop protocol` expose the VM
framebuffer backend map, the `orizon-compositor-api-v0` API seam, the
`compositor-backend-v0` drawing/present seam in
`kernel/include/compositor_backend.h` and `kernel/gui/compositor_backend.c`,
the backend surface/render/client contracts and current backend limits,
the internal `orizon-desktop-ipc-v0` protocol map plus runtime
`desktop-protocol-v0` trace in `kernel/include/desktop_protocol.h` and
`kernel/system/desktop_protocol.c`, and the honest
not-yet-Wayland/wlroots boundary,
`desktop layout-tree` and `desktop hyprctl layouttree` show the active
workspace tiling tree with client roles, rectangles, focus state, and
`manual-drag=no`; monocle clients outside the active surface are reported as
`monocle-deck` with `rendered=no`,
`desktop layout-state` and `desktop hyprctl layoutstate` show per-workspace
layout, split mode, split ratio, master ratio, `nmaster`, and the
`lastDispatch` snapshot for the most recent tiling dispatcher result,
including stable `error`/`hint` fields for scriptable failure diagnostics,
`desktop hyprctl -j version|systeminfo|backend|protocol|architecture|truth|clients|workspaces|activeworkspace|activewindow|focushistory|workspacestack|focusstate|clientmodel|rulematches|layoutstate|layouttree|monitors|devices|keymap|cursorpos|animations|decorations|render|layouts|descriptions|instances|modules|shortcuts|autostart|apps|app|launch|submap|splash|session|rollinglog|configerrors|configtrace|getoption|keyword|dispatch|reload|binds|layers`
emits a compact VM-safe JSON facade for future desktop tooling. It includes
client/workspace state, focus-history, workspace-stack, client-model, and
rule-match/layout-tree/config fields such as `focusHistoryID`, `scope`, `role`,
`pinnedAware`, `summary`, `safeAction`, `nodes`, `rect`, `parserSummary`, and
`trace`, plus action/input/layer fields such as `result`, `lastDispatch`,
`error`, `hint`, `dispatcher`, `args`, `runtimeFile`,
`singleFramebuffer`, `libinput`, `activeSubmap`, `desiredState`,
`runtimeState`, `sessionLogTail`, `currentBackend`,
`futureBackend`, `protocol`, `renderer`, `focusRing`, `transition`,
`renderProfile`, `manualWindowDrag`, `mouseBindsPreparedOnly`, and
`waybarActive`; it is
Hyprland-style diagnostic data, not real Wayland/wlroots client state.
`desktop workspace-stack` and `desktop hyprctl workspacestack` show
master/stack/focus order, local vs pinned scope, focus rank, stable addresses,
urgent state, and geometry for each workspace,
`desktop focus-state` and `desktop hyprctl focusstate` show the active
workspace's active client, master, stack, ratios, flags, and last dispatcher
result in one VM-safe report,
`movetoworkspace <workspace|special[:name]>[,<window>]` follows the moved tiled client while
`movetoworkspacesilent <workspace|special[:name]>[,<window>]` keeps the current workspace
active and restores workspace-local focus; window selectors accept `id`,
`0xaddress`, `class:app`, `title:text`, `tag:name`, or `activewindow`,
workspace targets accept Hyprland-style `r+/-n`/`r~n` including empty slots and
single-monitor VM `m/e +/-n`/`m/e ~n` open-workspace selectors, and
`special[:name]` for a tiled scratchpad overlay toggled by
`togglespecialworkspace [name]`,
`desktop profiles` lists available symbolic profiles, `desktop autostart` controls startup apps,
`desktop apps` lists compositor-managed app entries, `desktop app <id>` shows
class/module/surface plus data-source/runbook/limit/runtime details, and
`desktop launch terminal|settings|logs|packages|update|launcher` opens the first
native apps as tiled clients or toggles the launcher overlay. The
settings/logs/packages/update tiles render practical source, command, and
runtime hints in the framebuffer compositor.
`desktop hyprctl -j apps`, `desktop hyprctl -j app <id>`, and `desktop hyprctl
-j launch <app>` expose the same VM-ready app model, runtime object, and launch result as JSON,
with launcher overlay boundaries and no taskbar, start menu, floating desktop,
manual drag, Wayland/wlroots, or hardware validation claim.
`desktop hyprctl -j autostart` and `desktop hyprctl -j autostart terminal
on|off|toggle` expose/update the persisted terminal autostart policy and
generated `exec-once` runtime hints. This is session plumbing for the VM
framebuffer compositor, not Waybar, a taskbar, a start menu, floating windows,
manual drag, or upstream Wayland/wlroots Hyprland.
`desktop windows`/`desktop clients`
list tiled clients with stable addresses, geometry and `focusHistoryID`;
`desktop rule-matches`/`desktop hyprctl rulematches` explain which
`windowrulev2` class/title/app/tag/initialClass/initialTitle/workspace/focus/pin/fullscreen selectors match those clients,
which safe spawn-time actions are applied (`tile`, `fullscreen`, `pseudo`,
`pin`, `tag`, `workspace N`), and which floating/free-drag style actions are ignored,
`desktop activewindow` mirrors the focused client state, and F1/F2/F3/F4/F5/F6/F7/F8 map to exec
terminal/killactive/launcher/fullscreen/pseudo/focus/workspace navigation.
F9/F10/F11 enter resize/move/launch submaps and F12/Esc returns to default,
while `desktop modules` shows the prepared package split map for
`orizon-desktop-core`, `orizon-terminal`, `orizon-settings`, `orizon-launcher`,
and future `orizon-waybar`, without enabling manual window dragging. See
[DESKTOP.md](DESKTOP.md).

Useful render tuning commands stay Hyprland-style while remaining VM-safe:
`desktop settings set focus-ring yes|no`, `desktop settings set render-profile
balanced|performance|cozy`, `desktop keyword decoration:shadow:range <0-32>`,
and `desktop keyword animations:tick_budget <4-60>`. Inspect them with
`desktop render`, `desktop decorations`, `desktop animations`, or
`desktop hyprctl getoption render:focus_ring|render:profile|decoration:shadow:range|animations:tick_budget`.

## Network And Update

```text
net
net status
net dhcp
net auto
net check
net daily
net renew
net tcp raw.githubusercontent.com 443
net tcp raw.githubusercontent.com 443 attempts 2
net tls
net diag
net config show
net config ip <ip> gateway <gateway> dns <dns> [subnet <mask>]
route
dns raw.githubusercontent.com
ping 8.8.8.8
update status
update
logs storage
logs pci
logs network
logs update
```

`net check` is safe over SSH and summarizes link, IPv4, route, gateway, DNS,
and the next TCP/TLS probes with PASS/WARN/FAIL. `net daily` adds the VM
workflow, retry policy, config/log paths, and the honest NAT/bridge boundary.
`net tcp <host> [port] [attempts <1-5>]` is the cheap reachability check before
update/pkg work; it retries by default and separates DNS/TCP/firewall issues
from TLS/root-trust issues. `net renew` reapplies saved DHCP/static config from
the local console with a retry. `net tls` runs the heavier GitHub
HTTPS/root-trust probe, and `net diag` chains daily + check + TCP + TLS for a
fuller VM report. `update` is installed-disk only. Live ISO boot intentionally
blocks it.

## Packages

```text
pkg status
pkg list
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
pkg info <name>
pkg info orizon-desktop-hypr
pkg history
pkg sample
pkg sample desktop
pkg sample orizon-desktop-core
pkg sample orizon-terminal
pkg sample orizon-settings
pkg sample orizon-launcher
pkg hash /workspace/packages/orizon-hello.opkg
pkg verify /workspace/packages/orizon-hello.opkg
pkg verify /workspace/packages/orizon-desktop-core.opkg
pkg simulate /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
pkg install orizon-desktop-hypr
pkg install orizon-terminal
pkg remove orizon-hello
pkg rollback orizon-hello
```

`pkg update`, `pkg upgrade`, `pkg install`, `pkg remove`, and `pkg rollback`
are installed-disk only. `pkg sample`, `pkg hash`, `pkg verify`, `pkg search`,
`pkg audit`, `pkg doctor`, `pkg cache`, `pkg simulate`, `pkg remote`,
`pkg remote verify`, and `pkg upgrade plan` are safe in the live ISO and over
SSH. The remote package index is authenticated through the signed system
manifest, package repository commit pin, and package-index SHA-256 pin.
Detached package repo signatures are prepared through
`/workspace/.orizon/package-index.sig`; missing sidecars are reported as WARN
and still fall back to the signed manifest pin.

`pkg sample desktop` writes `/workspace/packages/orizon-desktop-hypr.opkg`,
the optional desktop package. It installs `/system/desktop.conf`,
`/system/desktop-session.conf`, `/system/desktop-settings.conf`, generated
desktop runtime hint files, and the Hyprland-style user config under
`/home/orizon/.config/hypr/`.
It also writes `/system/desktop-modules.conf`, a non-invasive module map for
the split packages. `pkg sample orizon-desktop-core|orizon-terminal|orizon-settings|orizon-launcher`
writes separate module `.opkg` files, and installed VMs accept named installs
such as `pkg install orizon-terminal`; app modules auto-prepare
`orizon-desktop-core` first. `pkg info orizon-terminal` and
`pkg search waybar` report those modules, but `orizon-waybar` is not generated
or installed now.
`desktop settings sync` is the preferred repair command if `/system` settings
and the user Hyprland-style config drift apart.
On an installed VM, `pkg install orizon-desktop-hypr` generates and installs
that package by name, `pkg remove orizon-desktop-hypr` disables it, and
`pkg rollback orizon-desktop-hypr` restores the last removed snapshot.

## SSH

```text
ssh password <password>
ssh start
ssh status
ssh audit
ssh sessions
security
security policy
security audit
security keys
security doctor
security rotate ssh-hostkey
net check
net daily
net tcp raw.githubusercontent.com 443
net tcp raw.githubusercontent.com 443 attempts 2
net tls
ssh auth
ssh auth max <attempts>
ssh auth lockout <seconds>
ssh lockout clear
ssh hostkey
ssh hostkey reload
ssh hostkey reset
ssh algorithms
logs ssh
logs security
hw next
report next
report save
cat /workspace/hardware-report.txt
head /workspace/hardware-report.txt
tail /workspace/hardware-report.txt
selftest ssh
system status
system repair
rescue
hostname
hostname set orizon-vm
reboot
shutdown
```

Use SSH for ZimaOS/VM diagnostics and remote admin commands. Keep long soak and
multi-client tests separate from quick build checks. Unknown remote `exec`
commands return a non-zero status so automation can fail fast.

`security` summarizes base hardening: SSH auth/lockout, persistent host key,
VFS policy v2, signed manifest requirement, package index pinning, policy-deny
counters and known limits. It refreshes `/system/security-policy`,
`/system/security-state`, and `/workspace/.orizon/security-doctor.txt` as
non-secret operator evidence. `security policy` expands the active rules,
`security audit` shows the persistent security mirror plus SSH audit and denial
counters, `security keys` reports key rotation posture without dumping private
material, and `security doctor` gives a non-destructive PASS/WARN summary.
`security rotate ssh-hostkey` regenerates the local SSH host identity for future
sessions and may require clearing the client known_hosts entry. Update/package
trust-root rotation is reported as `release-required`. Generic SSH file writes
are limited to `/workspace`, `/home`, `/logs` and `/packages`, while
`/workspace/.orizon` remains internal OS state and remote root deletion is
blocked. Sensitive files such as `/system/ssh.conf`,
`/system/ssh_host_rsa.key`, `.env`, `.key`, `.pem`, `.ssh`, private, secret,
token and credential paths are not readable through `cat/head/tail`. SSH audit
redacts `ssh password`, generic write/append payloads, and Wi-Fi credentials
before mirroring events to `/logs/security.log`.

## Security

```text
security
security policy
security audit
security keys
security doctor
security rotate ssh-hostkey
ssh auth
ssh audit
ssh hostkey
logs security
update status
```

See [SECURITY.md](SECURITY.md) for the current implemented policy, generated
state files, and limits.

## USB Ethernet

```text
usb
usb rescan
logs usb
net status
net dhcp
```

Supported packet paths today: xHCI CDC-ECM raw Ethernet and Realtek RTL815x.
CDC-NCM, ASIX, SMSC/LAN95xx, RNDIS, hubs, and missing endpoints are diagnostic
families until a specific driver path is added and validated.

## Intel AX201 / CNVi Wi-Fi

```text
wifi
wifi firmware
wifi apm
wifi boot arm
wifi alive
wifi queues arm
wifi context arm
wifi scheduler arm
wifi rx poll
wifi nvm arm
wifi nvm-info arm
wifi bringup
wifi crypto
wifi scan arm
wifi scan poll
wifi connect <ssid> [password]
wifi join <ssid> [password]
wifi validate <ssid> [password]
wifi online <ssid> [password]
wifi update <ssid> [password]
wifi wpa
wifi key pairwise arm
wifi key gtk arm
wifi data
logs wifi
```

Known limit: the WPA2/CCMP path is prepared for the Lenovo AX201 but still needs
real AP validation on the user's Lenovo before it can be called hardware-proven.

## Hardware Report

```text
sysinfo
report
report next
report save
selftest
selftest network
selftest storage
selftest crypto
selftest ssh
selftest update
hw
hw next
pci
pci bars
logs pci
input
storage
disks
storage diag
storage vmcheck
logs storage
disk identify
disk read-test
disk read-test last
gpt scan
partitions
mounts
persist status
persist slots
persist save
persist repair
persist restore previous
persist restore slot 0
system status
system services
system doctor
rescue
system repair
logs all
```

For future hardware work, capture command output rather than summarizing it from
memory. Start with `report next` to see the safe capture plan, then run
`report save` and copy `/workspace/hardware-report.txt`.
If the internal disk is missing, run `storage diag`, `storage vmcheck`,
`logs storage`, `logs pci`, `pci bars`, `disk identify`, `disk read-test last`,
and `gpt scan`; they are read-only and report NVMe/AHCI candidates, Intel
RST/VMD blockers, secondary PCI bus hints, modern/legacy VirtIO-blk state,
VirtIO-scsi diagnostic-only status, NVMe CAP/CC/CSTS/admin errors,
first/last-sector readability, GPT/protective-MBR state, and SDHCI/eMMC cases
without installing or writing to disk. The useful files are
`/workspace/hardware-report.txt`, `/logs/wifi.log`, `/logs/usb.log`,
`/logs/network.log`, `/logs/ssh.log`, `/logs/security.log`, and
`/workspace/.orizon/update.log`.

`persist status` affiche l'etat detecte des racines data, le LBA de la
partition Orizon detectee par GUID Orizon ou nom GPT exact `orizon-data`, le
nombre de slots disponibles, le slot actif, la sequence du dernier snapshot, le
nombre d'entrees et le mode `persistent` ou `memory`.
`persist slots` liste les snapshots lisibles avec version, sequence, payload et
checksum. `persist save` force une sauvegarde des racines `/workspace`, `/home`,
`/system`, `/packages` et `/logs`. `persist restore previous` restaure le slot
valide non actif le plus recent puis le promeut comme nouveau snapshot; `persist
restore slot <n>` fait la meme chose pour un slot precis. `persist repair`
reecrit un snapshot propre depuis l'etat VFS courant; ces commandes restent non
destructives pour le partitionnement et ne servent pas a installer l'OS.
`system status` et `rescue` sont inclus dans `report save` pour documenter
l'etat live/installe, le hostname et les commandes de recuperation sans devoir
lire l'ecran local.

## Shell Helpers

```text
help shell
shell status
tail [-n] <file>
wc <file>
grep [-i] [-v] [-n] text <file>
history grep text
cmd1 ; cmd2
cmd > /workspace/out.txt
cmd >> /workspace/out.txt
cmd | grep [-i] [-v] [-n] text
cmd | head -20
cmd | tail -20
cmd | wc
cmd | tee [-a] /workspace/out.txt
cmd | less
```

The local framebuffer shell now has a small diagnostic command layer above the
classic command dispatcher. It can run simple grouped commands with `;`, write
or append captured command output with `>` and `>>`, and pass captured output
through lightweight pipe stages: `grep`, `head`, `tail`, `wc`, `tee`, `cat`,
`less`/`more`. `grep` accepts `-i` for case-insensitive search, `-v` for
inverted matches, and `-n` for line numbers. `shell status` prints the local
console buffers/capabilities, and `history grep <text>` searches saved command
history.
This is intentionally not a full POSIX shell yet: no quoting, variables,
background jobs, or conditional exit-code logic. Interactive commands such as
`less`, `edit`, `install`, `reboot`, and `shutdown` are blocked as pipe or
redirection sources.

## Local Console Scrolling

When a command is longer than the visible framebuffer console, use `z` on an
empty prompt to scroll up and `s` to scroll back down. Uppercase `Z` and `S`
move by a larger half-screen step. The shortcut only steals `s` while the view
is already scrolled, so commands such as `storage`, `selftest`, and `ssh` still
type normally at the bottom prompt.

For files and reports, use the local pager:

```text
less /workspace/hardware-report.txt
less /logs/boot.log
```

Inside `less`, `z` or the up arrow moves one visual line up, `s` or the down
arrow moves one visual line down, `Z`/`S` or space move by a page, `g` and `G`
jump to the top/end, and `q` returns to the shell prompt. The pager is local to
the framebuffer console; SSH should keep using `cat`, `head`, and `tail`.
