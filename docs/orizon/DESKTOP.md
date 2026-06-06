# Orizon Desktop

Orizon now has an optional first desktop profile inspired by Hyprland. It is
not upstream Hyprland or a full Wayland stack yet; it is a VM-safe Orizon
desktop session that records Hyprland-style configuration and gives the
compositor a tiled terminal-client workflow.

The desktop is not enabled by default in the live ISO. It can be selected in
the guided installer, enabled later from the shell, or installed through an
`.opkg` package after Orizon is installed to disk.

## Commands

```text
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
desktop client-model
desktop rule-matches
desktop keyword general:gaps_in 9
desktop hyprctl version
desktop hyprctl systeminfo
desktop hyprctl -j version
desktop hyprctl -j systeminfo
desktop hyprctl -j backend
desktop hyprctl -j protocol
desktop hyprctl -j clients
desktop hyprctl -j activewindow
desktop hyprctl -j workspaces
desktop hyprctl -j activeworkspace
desktop hyprctl -j focushistory
desktop hyprctl -j workspacestack
desktop hyprctl -j clientmodel
desktop hyprctl -j rulematches
desktop hyprctl -j monitors
desktop hyprctl -j devices
desktop hyprctl -j keymap
desktop hyprctl -j cursorpos
desktop hyprctl -j animations
desktop hyprctl -j decorations
desktop hyprctl -j render
desktop hyprctl -j binds
desktop hyprctl -j layers
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
desktop hyprctl keymap
desktop hyprctl cursorpos
desktop hyprctl devices
desktop hyprctl splash
desktop hyprctl configerrors
desktop hyprctl configtrace
desktop hyprctl -j configerrors
desktop hyprctl -j configtrace
desktop hyprctl rollinglog
desktop dispatch exec terminal
desktop hyprctl clients
desktop hyprctl activewindow
desktop hyprctl monitors
desktop hyprctl binds
desktop hyprctl layers
desktop hyprctl getoption general:gaps_in
desktop hyprctl keyword decoration:rounding 11
desktop keyword layerrule blur, launcher
desktop hyprctl getoption layerrule
desktop hyprctl keyword input:repeat_rate 40
desktop hyprctl getoption input:repeat_rate
desktop hyprctl reload
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
desktop dispatch focuscurrentorlast
desktop dispatch markurgent on
desktop dispatch focusurgentorlast
desktop focus-window title:Terminal
desktop dispatch swapwindow l
desktop dispatch focusmwindow rank:2
desktop dispatch focusmwindow master
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
desktop dispatch workspace m~1
desktop dispatch workspace e+1
desktop dispatch focusworkspaceoncurrentmonitor active
desktop dispatch renameworkspace 2 dev
desktop dispatch workspace name:dev
desktop dispatch movetoworkspace name:dev
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
desktop app settings
desktop apps launcher
desktop launch terminal
desktop launch settings
desktop launch logs
desktop launch packages
desktop launch update
desktop launch launcher
desktop killactive
desktop open terminal
desktop close terminal
desktop package
pkg sample desktop
pkg sample orizon-desktop-core
pkg sample orizon-terminal
pkg sample orizon-settings
pkg sample orizon-launcher
pkg info orizon-desktop-hypr
pkg info orizon-terminal
pkg search orizon-terminal
pkg search waybar
pkg search desktop
pkg verify /workspace/packages/orizon-desktop-hypr.opkg
pkg simulate /workspace/packages/orizon-desktop-hypr.opkg
pkg install /workspace/packages/orizon-desktop-hypr.opkg
pkg install orizon-desktop-hypr
pkg remove orizon-desktop-hypr
pkg rollback orizon-desktop-hypr
```

`desktop package` and `pkg sample desktop` create:

```text
/workspace/packages/orizon-desktop-hypr.opkg
```

The modular samples create:

```text
/workspace/packages/orizon-desktop-core.opkg
/workspace/packages/orizon-terminal.opkg
/workspace/packages/orizon-settings.opkg
/workspace/packages/orizon-launcher.opkg
```

On an installed VM, `pkg install orizon-desktop-hypr` is the shortcut path: it
generates the local package, installs it, writes the Hyprland-style config, and
enables the desktop profile. `pkg remove orizon-desktop-hypr` disables the
profile through the package remove hook, and `pkg rollback orizon-desktop-hypr`
restores the last removed desktop package snapshot.

`desktop modules` shows the modular packaging map at
`/system/desktop-modules.conf`. The compatible all-in-one
`orizon-desktop-hypr` package still exists, while `orizon-desktop-core`,
`orizon-terminal`, `orizon-settings`, and `orizon-launcher` now have generated
module `.opkg` samples and named install commands. App modules depend on
`orizon-desktop-core`, which is auto-prepared for named installs. `orizon-waybar`
is listed only as a future separate package; it is not generated or installed
now and no Windows-style taskbar is added.

`desktop config doctor` parses the Hyprland-style user config at
`/home/orizon/.config/hypr/orizon-hypr.conf`. It understands common Hyprland
shape such as variables, section blocks, `monitor`, `bind`, `exec-once`, `env`,
`workspace`, `windowrule`, `source`, and `submap`, and reports what is supported today,
persisted as runtime hints, ignored, or malformed. It now keeps additional
Hyprland-style families such as `device`, `decoration`, `cursor`, `render`,
`debug`, `group`, `plugin`, `permission`, `gestures`, `xwayland`, and
`ecosystem` as inspectable runtime hints when Orizon does not have the real
Wayland implementation yet. `source = ~/.config/hypr/orizon-local.conf` is now
resolved in a VM-safe way, creates `/home/orizon/.config/hypr/orizon-local.conf`
when defaults are installed, and reports loaded/missing/skipped source state.
`desktop config trace` is read-only and explains each line as `APPLY`,
`PREPARE`, `IGNORE`, or `ERROR`, including the route to session/settings,
generated runtime files, and `SOURCE ... status=LOADED|MISSING|SKIP`.
`desktop config apply`
imports the supported subset into Orizon's runtime files: layout, gaps, border
size, rounding, shadows, animations, keyboard layout, focus-follows-mouse, and
terminal autostart. It also rewrites the generated Hyprland-style runtime files
for binds, autostart, window rules, monitor hints, env/workspace/source intent.
It also keeps `layerrule`, `bind`/`bindl`/`bindr`/`binde`/`bindm`, `unbind`,
`binds:*`, `bezier`, `animation`, input, device, decoration, cursor, render,
debug, misc, dwindle, master, gestures, and xwayland hints as inspectable
runtime state without pretending they are already real Wayland/wlroots
features. Bind variants are
classified in `desktop config doctor` and `desktop configerrors`; mouse binds
are parsed for compatibility, but `bindm` remains a prepared hint and Orizon
does not enable free-drag window moving by default.
Supported `desktop keyword` settings such as `general:gaps_in` and
`decoration:rounding` are synced back into
`/home/orizon/.config/hypr/orizon-hypr.conf`, so `desktop hyprctl reload`
keeps the new value instead of reverting to the previous generated template.
Those files make the config inspectable over SSH while the compositor grows the
corresponding real Wayland features.

The package writes:

```text
/system/desktop.conf
/system/desktop-session.conf
/system/desktop-settings.conf
/system/desktop-binds.conf
/system/desktop-autostart.conf
/system/desktop-rules.conf
/system/desktop-monitors.conf
/system/desktop-layers.conf
/system/desktop-runtime.conf
/system/desktop-state.conf
/home/orizon/.config/hypr/orizon-hypr.conf
/home/orizon/.config/hypr/orizon-local.conf
/system/share/orizon-desktop-hypr.conf
/logs/desktop.log
/logs/desktop-session.log
```

`desktop session` shows the persisted session options: theme, symbolic
wallpaper, layout, bar, launcher, terminal autostart, and focus-follows-mouse.
The default layout is `dwindle`; `master` and `monocle` are also understood.
The optional bar layer defaults to `bar no` so the Hyprland-style profile stays
tiling-first and does not enable a Waybar/taskbar-like surface until a future
separate package or an explicit local choice enables it.
`desktop theme <name>`, `desktop wallpaper <name>`, `desktop layout <name>`,
`desktop focus on|off|toggle`, and `desktop bar on|off|toggle` update
`/system/desktop-session.conf`; `desktop apply` reloads that session into the
compositor. Current built-in theme/wallpaper/layout names are simple symbolic
profiles such as `graphite`, `moss`, `ember`, `frost`, `aurora`, `dawn`,
`noir`, `dwindle`, `master`, and `monocle`.

`desktop start`, `desktop stop`, `desktop restart`, `desktop reload`,
`desktop recover`, and `desktop rescue` provide a small session manager. It
writes `/system/desktop-state.conf`, appends `/logs/desktop-session.log`,
records live/install boot mode, desired/runtime/policy health, lifecycle
counters, recovery commands, terminal autostart, and never enables free-drag
window moving. `desktop state` dumps the state and session log over console or
SSH. `desktop rescue` is read-only: it prints a recovery checklist, file
health, and the safe next commands before `desktop recover` rewrites anything.

`desktop settings` is the system-wide settings layer for the desktop
environment. It is written to `/system/desktop-settings.conf` when the desktop
is selected during installation, when `desktop enable` runs, or when
`pkg install orizon-desktop-hypr` installs the package. It currently stores
compositor-wide defaults such as `scale`, `gaps-in`, `gaps-out`,
`border-size`, `rounding`, `animations`, `shadows`, `focus-ring`,
`shadow-range`, `animation-ticks`, `animation-curve`, `render-profile`,
idle/lock policy, default terminal, launcher provider, bar position, keyboard
layout, and pointer profile. Use `desktop settings set <key> <value>` to update it and
`desktop settings repair` to rewrite safe defaults. `desktop settings presets`
lists system profiles for common use cases, `desktop settings preset
<default|compact|cozy|performance|accessibility|locked>` rewrites the settings
file atomically, and `desktop settings doctor` checks that the file is present,
schema-compatible, and runtime-clamped before the compositor consumes it.
`desktop settings paths` shows the full settings hub: `/system` policy,
session, settings, generated runtime files, the user Hyprland-style config, and
logs. `desktop settings export` rewrites
`/home/orizon/.config/hypr/orizon-hypr.conf` from `/system`, and `desktop
settings sync` performs that export then refreshes the generated runtime hints.
This keeps the source of truth in `/system` while still giving users a familiar
Hyprland-shaped config file.

`desktop input` is the VM-safe Hyprland-style input hub. `desktop input layout
fr|us` updates the desktop settings and synchronizes `/system/keyboard` plus
`/workspace/.orizon/keyboard` so the kernel keyboard mapper follows the desktop
choice. `desktop input pointer <flat|natural|precise|accelerated>` records the
pointer profile used by diagnostics, and `desktop input focus <on|off|toggle>`
controls focus-follows-mouse without enabling manual window dragging.
`desktop input submap <name|reset>` is a convenience alias for the dispatcher
submap command.

`desktop pointer` shows the compositor cursor position plus PS/2, USB HID, and
I2C-HID input diagnostics. VM profiles that expose a QEMU `usb-tablet` or a
boot-protocol USB mouse are routed into the same pointer state used by the
desktop compositor. This is pointer support for focus/cursor diagnostics, not
manual window dragging.

`desktop keymap` shows the active VM keyboard/runtime map: direct F-key
shortcuts, the last key seen by the compositor, the active submap, and the
focus-follows-mouse transition counter. F9 enters the `resize` submap, F10
enters `move`, F11 enters `launch`, and F12/Esc returns to `default`. In
`resize`, arrows/HJKL adjust split/master tiling ratios and `R` resets them.
In `move`, arrows/HJKL move focus, `N/B` reorder the active tiled client,
`M` moves it to the master slot, `1/2/3` move the active tiled client to a
workspace, and `P` toggles pin. In `launch`, `T` opens a terminal, `S` opens
Settings, `L` opens Logs, `P` opens Packages, `U` opens Update, `D` toggles the
launcher, and `Q` kills the active client. This is keyboard dispatcher control,
not free window dragging.

`desktop apps` lists the compositor-managed app catalog and `desktop app <id>`
shows each app's class, module, backend, surface and launch command.
`desktop launcher show` opens the launcher overlay, `F3` toggles it locally,
and `desktop launch terminal|settings|logs|packages|update|launcher` opens the
first native Orizon apps or toggles the launcher overlay. They are
compositor-managed Hyprland-style surfaces, not floating windows, and there is
still no Windows-like taskbar or permanent start menu.

`desktop profiles` lists the symbolic presets, themes, wallpapers and layouts
currently understood by the Orizon compositor. `desktop preset
<graphite|moss|ember|frost|focus>` applies a full session combination.
`desktop autostart` shows startup policy, and `desktop autostart terminal
on|off|toggle` controls whether the terminal opens automatically when the
desktop starts.

`desktop binds` lists the generated Hyprland-style bind runtime at
`/system/desktop-binds.conf` plus the dispatcher vocabulary currently
understood by Orizon. `desktop rules`, `desktop monitors`, and `desktop
runtime` expose the generated window-rule, monitor-hint, and aggregated
runtime files; `desktop layers` shows the compositor's layer-shell-like model.
`desktop version` reports the Orizon compatibility facade without pretending to
be upstream Hyprland, and `desktop devices` summarizes the keyboard/pointer
input model. `desktop systeminfo`, `desktop backend`, `desktop protocol`,
`desktop layouts`, `desktop layout-state`, `desktop layout-tree`, `desktop animations`,
`desktop render`, `desktop configerrors`, `desktop config trace`,
`desktop rollinglog`, `desktop focus-history`, `desktop workspace-stack`, `desktop client-model`, `desktop rule-matches`, `desktop decorations`,
`desktop descriptions`, `desktop instances`, and `desktop submap` mirror common
Hyprland inspection habits while staying honest about Orizon's framebuffer backend.
`desktop render` is the most direct VM-safe renderer diagnostic: it reports the
software focus ring policy, shadow range, render profile, configured rounding,
animation curve/tick budget, transition reason and render serial. `desktop
keyword decoration:shadow:range <0-32>`, `desktop keyword
animations:tick_budget <4-60>`, `desktop keyword render:focus_ring
<true|false>`, and `desktop settings set render-profile <name>` persist those
values without enabling floating windows or manual dragging. It is
Hyprland-style compositor UX, not upstream Wayland/wlroots.
`desktop layout-tree` and `desktop hyprctl layouttree` are the active tiling
diagnostic: they print the current workspace root area, split/master ratios,
`nmaster`, client roles, rectangles, focused client, fullscreen/pseudo/pinned/urgent flags, and
`focusHistoryID`. The output explicitly reports `manual-drag=no` and
`floating-tree=no`; it is not a floating scene graph and it is not a Wayland
scene graph yet. In `monocle`, only the active tiled client is rendered; other
clients remain managed and focusable as `monocle-deck` nodes with
`rendered=no`.
`desktop layout-state` and `desktop hyprctl layoutstate` expose the
per-workspace tiling state: layout engine, split mode, split ratio and master
ratio plus `nmaster` for each workspace. `desktop dispatch layoutmsg layout
<dwindle|master|monocle>` changes only the active workspace layout; it does not
enable floating windows or manual drag.
`desktop workspace-stack` and `desktop hyprctl workspacestack` expose a
per-workspace stack view: master candidate, stack/dwindle order, focused client
for that workspace, local vs pinned scope, focus rank, stable address, and
geometry. This is a diagnostic over Orizon's framebuffer compositor, not a
wlroots scene graph.
`desktop backend` and `desktop protocol` expose the architecture seam for the
future real compositor path. Today they report `current-backend:
framebuffer-vm`, `protocol: orizon-desktop-ipc-v0`, software backbuffer
rendering, tiled internal clients only, no manual free-drag windows, no taskbar,
and no Waybar package installed. They persist the truth files
`/system/desktop-backend.conf` and `/system/desktop-protocol.conf`; the future
target is documented as `wayland-wlroots`, but Wayland, wlroots, xdg-shell,
real layer-shell clients, XWayland, GPU acceleration, and upstream Hyprland are
not implemented yet.
`desktop keyword <key> <value>` applies one Hyprland-style keyword to the
persisted Orizon session/settings subset when supported, or records safe
runtime-only hints for keywords such as `windowrulev2`, `monitor`, `env`,
`workspace`, `source`, `submap`, `layerrule`, `bezier`, `animation`,
input/misc/layout hints, variables, and binds. `desktop hyprctl reload` now
re-imports the Hyprland-style config before refreshing the compositor session.
`desktop dispatch exec terminal`,
`desktop dispatch killactive`, `desktop dispatch workspace <target>`,
`desktop dispatch togglespecialworkspace [name]`,
`desktop dispatch renameworkspace <target> <name>`, `desktop dispatch
workspace name:<name>`, `desktop dispatch movetoworkspace <target|special[:name]>[,<window>]`, `desktop dispatch movetoworkspacesilent <target|special[:name]>[,<window>]`,
`desktop dispatch movefocus l|r|u|d|next|prev`,
`desktop dispatch focusmwindow <next|prev|master|last|+n|-n|rank:n|index:n>`, and
`desktop dispatch focuswindow <id|0xaddr|class:app|title:text|tag:name|activewindow>`,
`desktop dispatch focuscurrentorlast`, `desktop dispatch focusurgentorlast`, and
`desktop dispatch tagwindow <+tag|-tag|clear|tag> [target]`
exercise the same mental model as Hyprland dispatchers. The current client
dispatchers also include `fullscreen`, `fullscreenstate`, `pseudo`,
`pseudotile`, `pin`, `markurgent`, `cyclenext`, and `swapnext`, directional
`swapwindow l|r|u|d`, rank-based `swapmwindow
<next|prev|master|last|+n|-n|rank:n|index:n>`, directional
`movewindow l|r|u|d|next|prev|master`/`movewindoworgroup`, plus direct
`focusmaster` and `swapwithmaster` aliases. `movewindow` is a tiled-order
operation: it pushes the active client through the compositor stack or to the
master slot without enabling floating geometry, pixel dragging, or free manual
movement.
`fullscreen|pseudo|pseudotile|pin <on|off|toggle|1|0>` and
`fullscreenstate <internal 0-3|-1> <client 0-3|-1>` set or toggle the active
client state without ambiguous script-side guessing. The old
`fullscreenstate on|off|toggle|1|0` form remains accepted and maps both
states together; the two-value form mirrors Hyprland's split between the
compositor state and the state sent to the client. In Orizon today the
`client` side is diagnostic/prepared only because the backend is still the VM
framebuffer facade, not Wayland/wlroots. Layout/submap
dispatchers now include `togglesplit`,
`layoutmsg layout <dwindle|master|monocle>|reset|orientationnext|orientationprev|orientationleft|orientationright|orientationtop|orientationbottom|preselect <l|r|u|d|reset>|splitratio <10-90|+/-n|reset>|masterratio <10-90|+/-n|reset>|mfact <10-90|+/-n|reset>|nmaster <1-8|+/-n|reset>|addmaster|removemaster|focusmaster|swapwithmaster`,
`layoutmsg movewindowmaster`,
`resizeactive <x> <y>`, and `submap <name|reset>`. Relative workspace targets such as `workspace +1`,
`workspace -1`, `workspace next`, `workspace empty`, and `workspace previous`
are understood for VM-safe desktop flow across ten dynamic workspace slots.
`focusworkspaceoncurrentmonitor <target>` is also accepted as the
single-framebuffer VM equivalent of Hyprland's current-monitor workspace
focus dispatcher; it does not claim real multi-monitor Wayland routing.
`focusmonitor <monitor|direction>`,
`movecurrentworkspacetomonitor <monitor>`, and
`moveworkspacetomonitor <workspace> <monitor>` are exposed for Hyprland-style
config compatibility, but today they are honest Orizon-framebuffer aliases:
they validate and report the requested monitor while keeping the VM on its
single framebuffer and do not claim real Wayland output routing.
Hyprland-style prefixed targets are also accepted by the shared workspace
parser: `r+1`/`r-1` and `r~3` walk or select numeric slots including empty
ones, while `m+1`/`m-1`, `e+1`/`e-1`, `m~1`, and `e~1` walk open Orizon
workspaces on the current framebuffer monitor. In today's VM backend `m` and
`e` are honest single-monitor aliases, not real Wayland multi-output routing.
Special workspaces are implemented as a VM-safe tiled scratchpad overlay:
`movetoworkspace special[:name]` or `movetoworkspacesilent special[:name]`
marks the selected client as special, and `togglespecialworkspace [name]`
shows or hides matching special clients over the current workspace. This is not
floating mode and does not enable mouse dragging; clients still use the active
tiling layout.

`desktop hyprctl
[-j] version|systeminfo|backend|protocol|clients|clientmodel|rulematches|workspaces|activeworkspace|activewindow|focushistory|workspacestack|monitors|binds|keymap|layers|layouts|layoutstate|layouttree|animations|decorations|render|descriptions|instances|submap|devices|cursorpos|splash|configerrors|configtrace|rollinglog|getoption|keyword|reload`
is a small compatibility facade for the commands people expect when coming
from Hyprland. `desktop hyprctl getoption <key>` reports the current Orizon
value or runtime hint for a Hyprland-style key, `desktop hyprctl keyword <key>
<value>` maps to `desktop keyword`, and `desktop hyprctl dispatch <dispatcher>
[args]` maps to Orizon's dispatcher layer.
`desktop hyprctl -j version|systeminfo|backend|protocol|clients|workspaces|activeworkspace|activewindow|focushistory|workspacestack|clientmodel|rulematches|layoutstate|layouttree|monitors|devices|keymap|cursorpos|animations|decorations|render|configerrors|configtrace|getoption|keyword|reload|binds|layers`
provides compact JSON for status tooling and future separate bar packages. It
mirrors Hyprland-style fields such as `address`, `workspace`,
`fullscreenClient`, `tags`, `windows`, `lastwindow`, `focusHistoryID`, `scope`,
`role`, `pinnedAware`, `summary`, `safeAction`, `nodes`, `rect`,
`parserSummary`, `trace`, `result`, `runtimeFile`, `singleFramebuffer`,
`libinput`, `activeSubmap`, `currentBackend`, `futureBackend`, `protocol`,
`renderer`, `focusRing`, `transition`, `renderProfile`, `manualWindowDrag`,
`mouseBindsPreparedOnly`, and `waybarActive`, but every object also carries
`hyprlandStyleFacade=true` because the backend is still Orizon framebuffer VM,
not upstream Wayland/wlroots Hyprland. The config JSON diagnostics are
read-only: they explain parser errors and apply/prepare/ignore decisions
without changing the session. JSON `getoption` is read-only, while JSON
`keyword` and `reload` are explicit VM-safe actions with `manualDrag=false`,
`floatingSceneGraph=false`, and `taskbar=false` boundaries.

`desktop apps` and `desktop app <id>` now form the first native-app control
surface for the desktop. The terminal/settings/logs/packages/update entries
declare their compositor class, module, data source, runbook, and remaining
limits; settings/logs/packages/update also render practical source/command
hints inside their framebuffer tiles. They are VM-ready tiling clients, not
floating windows, not a Windows-style shell, and not Wayland/wlroots native
applications yet. The launcher remains a transient overlay only; Waybar remains
a future separate package.

`desktop windows`, `desktop clients`, `desktop activewindow`, `desktop
focus-history`, `desktop workspace-stack`, `desktop client-model`, and `desktop rule-matches` list the tiled clients with stable
Orizon addresses, `at/size` geometry, workspace graph, rules runtime, backend
boundary, fullscreen/pseudo/pinned/urgent flags, `fullscreenClient`, current
`rendered=yes/no` state, and
Hyprland-style `focusHistoryID`.
`desktop hyprctl clientmodel` mirrors this read-only state graph.
`desktop hyprctl rulematches` reads `/system/desktop-rules.conf` and explains
which `windowrulev2` selectors match current clients by class/title/app/tag,
initialClass, initialTitle, workspace, focus, pin, and fullscreen with a
simplified matcher. Safe spawn-time actions `tile`, `fullscreen`, `pseudo`,
`pin`, `tag`, and `workspace N` can set initial tiled client state; floating/free-drag
style actions are ignored and reported, not applied. This is still not the
upstream Hyprland regex/Wayland engine. The model is intentionally no-drag: clients are placed by
`dwindle`, `master`, or `monocle`, not by mouse-moving windows around like a
traditional desktop. The active client can still be controlled through
dispatcher state such as fullscreen/pseudo/pinned. Move-to-workspace selectors
can target `id`, `0xaddress`, `class:app`, `title:text`, `tag:name`, or
`activewindow`.
`markurgent` is provided as
a VM-only diagnostic so `focusurgentorlast` can be tested before real
Wayland/client urgency exists; it does not claim upstream Hyprland or wlroots
client signaling is implemented. `tagwindow` is similarly VM-safe diagnostic
state for exercising Hyprland-style tags before real XDG tags exist. This is
closer to the Hyprland mental model than manual window dragging.

`desktop workspace` shows the current Hyprland-style workspace state.
`desktop workspace <1-10|name:<name>|next|empty|+/-n|r+/-n|m+/-n|e+/-n|r~n|m~n|e~n|previous>` switches the active workspace.
Prefer `desktop dispatch movetoworkspace <target>[,<window>]` or
`desktop dispatch movetoworkspacesilent <target>[,<window>]` to move the active
or selected tiled client between workspaces, matching Hyprland's
dispatcher-style workflow. Window selectors accept `id`, `0xaddress`,
`class:app`, `title:text`, `tag:name`, or `activewindow`. The non-silent
dispatcher follows the moved client to the target workspace; the silent variant
keeps the current workspace active and restores the local workspace focus when
another client is available.
Use `desktop dispatch movetoworkspacesilent special:magic,activewindow` plus
`desktop dispatch togglespecialworkspace magic` for the Hyprland-style
scratchpad path.

`desktop doctor` checks the policy file, session file, template, user config,
optional package metadata, and reports PASS/WARN/FAIL without validating real
hardware or pretending upstream Hyprland is embedded.

## Shortcuts

```text
F1 dispatches exec terminal
F2 dispatches killactive
F3 toggles the launcher overlay
F4 dispatches fullscreen
F5 dispatches pseudo
F6 dispatches cyclenext
F7/F8 dispatch workspace +1/-1
F9/F10/F11 enter resize/move/launch submaps
F12 or Esc returns to the default submap
Enter/Space on an empty focus dispatches exec terminal
desktop workspace <1-10|name:<name>|next|empty|+/-n|r+/-n|m+/-n|e+/-n|r~n|m~n|e~n|previous> switches runtime workspace
desktop dispatch togglespecialworkspace [name] shows/hides a tiled special scratchpad
desktop dispatch renameworkspace <target> <safe-name> names a workspace
desktop dispatch movetoworkspace <target|special[:name]>[,<window>] moves and follows the active/selected tiled client
desktop dispatch movetoworkspacesilent <target|special[:name]>[,<window>] moves without changing workspace
desktop dispatch movefocus l|r|u|d|next|prev changes focus
desktop dispatch focusmwindow next|prev|master|rank:n|index:n focuses by tiled rank
desktop dispatch focuswindow <target> focuses by id/address/class/title/tag/activewindow
desktop dispatch focuscurrentorlast focuses the previous focus-history client
desktop dispatch focusurgentorlast focuses an urgent client, then falls back to last
desktop dispatch markurgent [on|off|toggle] [target] marks VM diagnostic urgency
desktop dispatch tagwindow <+tag|-tag|clear|tag> [target] sets/clears VM diagnostic tags
desktop dispatch layoutmsg layout <dwindle|master|monocle> changes active workspace layout
desktop dispatch layoutmsg reset resets active workspace layout ratios to defaults
desktop dispatch layoutmsg preselect <l|r|u|d|reset> sets a VM-safe directional split hint
desktop dispatch swapwindow l|r|u|d swaps tiled clients by direction
desktop dispatch swapmwindow next|prev|master|rank:n|index:n swaps by tiled rank
desktop dispatch movewindow l|r|u|d|next|prev|master reorders tiled clients without free-drag
desktop dispatch fullscreen|pseudo|pseudotile|pin [on|off|toggle] controls active client state
desktop dispatch fullscreenstate <internal 0-3|-1> <client 0-3|-1> sets split fullscreen state
desktop dispatch resizeactive <x> <y> adjusts tiling ratios
desktop input layout fr|us syncs desktop and kernel keyboard layout
desktop input pointer flat|natural|precise|accelerated records pointer policy
desktop input focus on|off|toggle controls focus-follows-mouse
desktop keymap shows keyboard/submap runtime diagnostics
desktop autostart terminal on|off controls startup terminal
desktop preset <name> applies a saved symbolic session
desktop focus on|off|toggle controls focus-follows-mouse
desktop devices shows keyboard/pointer input state
desktop backend shows the current framebuffer-vm backend map
desktop protocol shows the internal orizon-desktop-ipc-v0 protocol map
desktop layout-tree shows the active tiling tree and rectangles
desktop config trace explains apply/prepare/ignore parser decisions
desktop keyword <key> <value> applies a Hyprland-style runtime setting
desktop hyprctl getoption <key> inspects the current mapped value
desktop hyprctl -j getoption <key> emits the mapped value as VM-safe JSON
desktop hyprctl -j keyword <key> <value> applies one supported key as JSON
desktop hyprctl backend mirrors desktop backend
desktop hyprctl protocol mirrors desktop protocol
desktop hyprctl -j version emits package/compositor truth-map metadata
desktop hyprctl -j systeminfo emits VM-safe compositor/session state
desktop hyprctl -j backend emits framebuffer-vs-future-wayland boundaries
desktop hyprctl -j protocol emits internal IPC vs not-Wayland boundaries
desktop hyprctl layouttree mirrors desktop layout-tree
desktop hyprctl configtrace mirrors desktop config trace
desktop hyprctl reload reapplies /home/orizon/.config/hypr/orizon-hypr.conf
desktop hyprctl -j reload reapplies the config and emits VM-safe JSON status
desktop hyprctl -j monitors emits the single framebuffer monitor map
desktop hyprctl -j devices emits VM-safe input backend summaries
desktop hyprctl -j keymap emits keyboard layout/submap state
desktop hyprctl -j cursorpos emits pointer position/buttons
desktop hyprctl -j animations emits software transition/curve state
desktop hyprctl -j decorations emits focus ring/border/shadow state
desktop hyprctl -j render emits framebuffer renderer/protocol boundaries
desktop hyprctl -j binds emits bind counts and prepared-only bindm status
desktop hyprctl -j layers emits the framebuffer layer graph without Waybar
```

## Current Limits

- This is a Hyprland-style Orizon profile, not the real Hyprland compositor.
- No Wayland protocol, wlroots, GPU acceleration, file manager, wallpaper
  daemon, or real external client protocol is implemented yet.
- `desktop backend` and `desktop protocol` are truthful architecture maps, not
  a real Wayland/wlroots backend.
- The launcher/status bar/theme are Orizon compositor primitives, not upstream
  Hyprland plugins.
- Workspaces, focus, clients, and `dwindle`/`master`/`monocle` placement are
  runtime Orizon compositor primitives; no real Wayland client management
  exists yet.
- VM pointer input supports PS/2 mouse/touchpad and selected USB HID
  mouse/tablet reports, but windows are still managed by dispatchers and tiling
  layouts rather than mouse drag/resize. Keyboard resize submaps adjust tiling
  ratios only; they do not create floating/manual window movement.
- The first useful app is still the terminal.
- Real hardware validation is not claimed; this is VM/ZimaOS-ready plumbing.
