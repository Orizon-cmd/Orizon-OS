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
desktop layouts
desktop animations
desktop decorations
desktop render
desktop descriptions
desktop instances
desktop submap
desktop configerrors
desktop rollinglog
desktop focus-history
desktop keyword general:gaps_in 9
desktop hyprctl version
desktop hyprctl systeminfo
desktop hyprctl activeworkspace
desktop hyprctl focushistory
desktop hyprctl layouts
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
desktop dispatch pseudo
desktop dispatch pin
desktop dispatch cyclenext
desktop dispatch swapnext
desktop dispatch movefocus r
desktop dispatch swapwindow l
desktop dispatch swapwithmaster
desktop dispatch focusmaster
desktop dispatch togglesplit
desktop dispatch layoutmsg splitratio 60
desktop dispatch layoutmsg splitratio +5
desktop dispatch layoutmsg masterratio 65
desktop dispatch layoutmsg mfact -5
desktop dispatch layoutmsg orientationleft
desktop dispatch layoutmsg orientationtop
desktop dispatch resizeactive 5 0
desktop dispatch submap resize
desktop hyprctl submap reset
desktop windows
desktop clients
desktop activewindow
desktop workspace
desktop workspace 2
desktop dispatch movetoworkspace 2
desktop dispatch movetoworkspacesilent empty
desktop dispatch workspace 2
desktop dispatch workspace previous
desktop dispatch workspace +1
desktop dispatch workspace next
desktop dispatch workspace empty
desktop workspace empty
desktop dispatch movefocus next
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
Wayland implementation yet. `desktop config apply`
imports the supported subset into Orizon's runtime files: layout, gaps, border
size, rounding, shadows, animations, keyboard layout, focus-follows-mouse, and
terminal autostart. It also rewrites the generated Hyprland-style runtime files
for binds, autostart, window rules, monitor hints, env/workspace/source intent.
It also keeps `layerrule`, `bindm`/`bindl`, `bezier`, `animation`, input,
device, decoration, cursor, render, debug, misc, dwindle, and master hints as
inspectable runtime state without pretending they are already real
Wayland/wlroots features. Mouse binds are parsed for
compatibility, but Orizon does not enable free-drag window moving by default.
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
/system/share/orizon-desktop-hypr.conf
/logs/desktop.log
/logs/desktop-session.log
```

`desktop session` shows the persisted session options: theme, symbolic
wallpaper, layout, bar, launcher, terminal autostart, and focus-follows-mouse.
The default layout is `dwindle`; `master` and `monocle` are also understood.
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
In `move`, arrows/HJKL move focus, `1/2/3` move the active tiled client to a
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
input model. `desktop systeminfo`, `desktop layouts`, `desktop animations`,
`desktop render`, `desktop configerrors`, `desktop rollinglog`, `desktop focus-history`, `desktop decorations`,
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
`desktop keyword <key> <value>` applies one Hyprland-style keyword to the
persisted Orizon session/settings subset when supported, or records safe
runtime-only hints for keywords such as `windowrulev2`, `monitor`, `env`,
`workspace`, `source`, `submap`, `layerrule`, `bezier`, `animation`,
input/misc/layout hints, variables, and binds. `desktop hyprctl reload` now
re-imports the Hyprland-style config before refreshing the compositor session.
`desktop dispatch exec terminal`,
`desktop dispatch killactive`, `desktop dispatch workspace <target>`, `desktop
dispatch movetoworkspace <target>`, `desktop dispatch movetoworkspacesilent <target>`,
and `desktop dispatch movefocus l|r|u|d|next|prev`
exercise the same mental model as Hyprland dispatchers. The current client
dispatchers also include `fullscreen`, `pseudo`, `pin`, `cyclenext`, and
`swapnext`, directional `swapwindow l|r|u|d`, plus direct `focusmaster` and `swapwithmaster` aliases. Layout/submap
dispatchers now include `togglesplit`,
`layoutmsg orientationnext|orientationprev|orientationleft|orientationright|orientationtop|orientationbottom|splitratio <10-90|+/-n>|masterratio <10-90|+/-n>|mfact <10-90|+/-n>|focusmaster|swapwithmaster`,
`resizeactive <x> <y>`, and `submap <name|reset>`. Relative workspace targets such as `workspace +1`,
`workspace -1`, `workspace next`, `workspace empty`, and `workspace previous`
are understood for VM-safe desktop flow across ten dynamic workspace slots.

`desktop hyprctl
version|systeminfo|clients|workspaces|activeworkspace|activewindow|focushistory|monitors|binds|keymap|layers|layouts|animations|decorations|descriptions|instances|submap|devices|cursorpos|splash|configerrors|rollinglog|getoption|keyword|reload`
is a small compatibility facade for the commands people expect when coming
from Hyprland. `desktop hyprctl getoption <key>` reports the current Orizon
value or runtime hint for a Hyprland-style key, `desktop hyprctl keyword <key>
<value>` maps to `desktop keyword`, and `desktop hyprctl dispatch <dispatcher>
[args]` maps to Orizon's dispatcher layer.

`desktop windows`, `desktop clients`, `desktop activewindow`, and `desktop
focus-history` list the tiled clients with stable Orizon addresses, `at/size`
geometry, workspace state, fullscreen/pseudo/pinned flags, and Hyprland-style
`focusHistoryID`. The model is intentionally no-drag: clients are placed by
`dwindle`, `master`, or `monocle`, not by mouse-moving windows around like a
traditional desktop. The active client can still be controlled through
dispatcher state such as fullscreen/pseudo/pinned, which is closer to the
Hyprland mental model than manual window dragging.

`desktop workspace` shows the current Hyprland-style workspace state.
`desktop workspace <1-10|next|empty|+/-n|previous>` switches the active workspace.
Prefer `desktop dispatch movetoworkspace <target>` or
`desktop dispatch movetoworkspacesilent <target>` to move the active tiled
client between workspaces, matching Hyprland's dispatcher-style workflow.

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
desktop workspace <1-10|next|empty|+/-n|previous> switches runtime workspace
desktop dispatch movetoworkspace <target> moves the active tiled client
desktop dispatch movetoworkspacesilent <target> moves without changing workspace
desktop dispatch movefocus l|r|u|d|next|prev changes focus
desktop dispatch swapwindow l|r|u|d swaps tiled clients by direction
desktop dispatch fullscreen|pseudo|pin controls active client state
desktop dispatch resizeactive <x> <y> adjusts tiling ratios
desktop input layout fr|us syncs desktop and kernel keyboard layout
desktop input pointer flat|natural|precise|accelerated records pointer policy
desktop input focus on|off|toggle controls focus-follows-mouse
desktop keymap shows keyboard/submap runtime diagnostics
desktop autostart terminal on|off controls startup terminal
desktop preset <name> applies a saved symbolic session
desktop focus on|off|toggle controls focus-follows-mouse
desktop devices shows keyboard/pointer input state
desktop keyword <key> <value> applies a Hyprland-style runtime setting
desktop hyprctl getoption <key> inspects the current mapped value
desktop hyprctl reload reapplies /home/orizon/.config/hypr/orizon-hypr.conf
```

## Current Limits

- This is a Hyprland-style Orizon profile, not the real Hyprland compositor.
- No Wayland protocol, wlroots, GPU acceleration, file manager, wallpaper
  daemon, or real external client protocol is implemented yet.
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
