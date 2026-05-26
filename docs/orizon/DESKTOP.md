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
desktop session
desktop settings
desktop settings presets
desktop settings doctor
desktop settings preset compact
desktop settings set gaps-in 10
desktop settings set border-size 3
desktop settings repair
desktop pointer
desktop apps
desktop profiles
desktop preset moss
desktop binds
desktop dispatch exec terminal
desktop hyprctl clients
desktop hyprctl activewindow
desktop hyprctl monitors
desktop autostart
desktop autostart terminal off
desktop autostart terminal on
desktop dispatch fullscreen
desktop dispatch pseudo
desktop dispatch pin
desktop dispatch cyclenext
desktop dispatch swapnext
desktop windows
desktop workspace
desktop workspace 2
desktop dispatch movetoworkspace 2
desktop dispatch workspace 2
desktop dispatch workspace previous
desktop dispatch workspace +1
desktop dispatch movefocus next
desktop shortcuts
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
desktop killactive
desktop open terminal
desktop close terminal
desktop package
pkg sample desktop
pkg info orizon-desktop-hypr
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

On an installed VM, `pkg install orizon-desktop-hypr` is the shortcut path: it
generates the local package, installs it, writes the Hyprland-style config, and
enables the desktop profile. `pkg remove orizon-desktop-hypr` disables the
profile through the package remove hook, and `pkg rollback orizon-desktop-hypr`
restores the last removed desktop package snapshot.

`desktop config doctor` parses the Hyprland-style user config at
`/home/orizon/.config/hypr/orizon-hypr.conf`. It understands common Hyprland
shape such as variables, section blocks, `monitor`, `bind`, `exec-once`, `env`,
`workspace`, `windowrule`, and `source`, and reports what is supported today,
persisted as runtime hints, ignored, or malformed. `desktop config apply`
imports the supported subset into Orizon's runtime files: layout, gaps, border
size, rounding, shadows, animations, keyboard layout, focus-follows-mouse, and
terminal autostart. It also rewrites the generated Hyprland-style runtime files
for binds, autostart, window rules, monitor hints, env/workspace/source intent.
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
/system/desktop-runtime.conf
/home/orizon/.config/hypr/orizon-hypr.conf
/system/share/orizon-desktop-hypr.conf
/logs/desktop.log
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

`desktop settings` is the system-wide settings layer for the desktop
environment. It is written to `/system/desktop-settings.conf` when the desktop
is selected during installation, when `desktop enable` runs, or when
`pkg install orizon-desktop-hypr` installs the package. It currently stores
compositor-wide defaults such as `scale`, `gaps-in`, `gaps-out`,
`border-size`, `rounding`, `animations`, `shadows`, idle/lock policy, default
terminal, launcher provider, bar position, keyboard layout, and pointer
profile. Use `desktop settings set <key> <value>` to update it and
`desktop settings repair` to rewrite safe defaults. `desktop settings presets`
lists system profiles for common use cases, `desktop settings preset
<default|compact|cozy|performance|accessibility|locked>` rewrites the settings
file atomically, and `desktop settings doctor` checks that the file is present,
schema-compatible, and runtime-clamped before the compositor consumes it.

`desktop pointer` shows the compositor cursor position plus PS/2, USB HID, and
I2C-HID input diagnostics. VM profiles that expose a QEMU `usb-tablet` or a
boot-protocol USB mouse are routed into the same pointer state used by the
desktop compositor. This is pointer support for focus/cursor diagnostics, not
manual window dragging.

`desktop apps` lists the first launcher entries. `desktop launcher show` opens
the launcher overlay, `F3` toggles it locally, and `desktop launch terminal`
opens the first real app: the Orizon terminal client.

`desktop profiles` lists the symbolic presets, themes, wallpapers and layouts
currently understood by the Orizon compositor. `desktop preset
<graphite|moss|ember|frost|focus>` applies a full session combination.
`desktop autostart` shows startup policy, and `desktop autostart terminal
on|off|toggle` controls whether the terminal opens automatically when the
desktop starts.

`desktop binds` lists the generated Hyprland-style bind runtime at
`/system/desktop-binds.conf` plus the dispatcher vocabulary currently
understood by Orizon. `desktop dispatch exec terminal`,
`desktop dispatch killactive`, `desktop dispatch workspace <n>`, `desktop
dispatch movetoworkspace <n>`, and `desktop dispatch movefocus next|prev`
exercise the same mental model as Hyprland dispatchers. The current client
dispatchers also include `fullscreen`, `pseudo`, `pin`, `cyclenext`, and
`swapnext`. Relative workspace targets such as `workspace +1`, `workspace -1`,
and `workspace previous` are understood for VM-safe desktop flow.

`desktop hyprctl clients|workspaces|activewindow|monitors|reload` is a small
compatibility facade for the commands people expect when coming from Hyprland.
`desktop hyprctl dispatch <dispatcher> [args]` maps to Orizon's dispatcher
layer.

`desktop windows` lists the current compositor layers and tiled clients. The
model is intentionally no-drag: clients are placed by `dwindle`, `master`, or
`monocle`, not by mouse-moving windows around like a traditional desktop.
The active client can still be controlled through dispatcher state such as
fullscreen/pseudo/pinned, which is closer to the Hyprland mental model than
manual window dragging.

`desktop workspace` shows the current Hyprland-style workspace state.
`desktop workspace <n>` switches the active workspace. Prefer
`desktop dispatch movetoworkspace <n>` to move the active tiled client between
workspaces, matching Hyprland's dispatcher-style workflow.

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
Enter/Space on an empty focus dispatches exec terminal
desktop workspace <n> switches runtime workspace
desktop dispatch movetoworkspace <n> moves the active tiled client
desktop dispatch movefocus next|prev changes focus
desktop dispatch fullscreen|pseudo|pin controls active client state
desktop autostart terminal on|off controls startup terminal
desktop preset <name> applies a saved symbolic session
desktop focus on|off|toggle controls focus-follows-mouse
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
  layouts rather than mouse drag/resize.
- The first useful app is still the terminal.
- Real hardware validation is not claimed; this is VM/ZimaOS-ready plumbing.
