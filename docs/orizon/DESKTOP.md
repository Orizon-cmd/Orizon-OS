# Orizon Desktop

Orizon now has an optional first desktop profile inspired by Hyprland. It is
not upstream Hyprland or a full Wayland stack yet; it is a VM-safe Orizon
desktop session that records Hyprland-style configuration and gives the
compositor a simple terminal window workflow.

The desktop is not enabled by default in the live ISO. It can be selected in
the guided installer, enabled later from the shell, or installed through an
`.opkg` package after Orizon is installed to disk.

## Commands

```text
desktop status
desktop config
desktop session
desktop apps
desktop windows
desktop workspace
desktop workspace 2
desktop move terminal 2
desktop shortcuts
desktop doctor
desktop logs
desktop enable
desktop disable
desktop reset
desktop write-config
desktop theme moss
desktop wallpaper dawn
desktop layout tiling
desktop bar toggle
desktop apply
desktop launcher show
desktop launch terminal
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

The package writes:

```text
/system/desktop.conf
/system/desktop-session.conf
/home/orizon/.config/hypr/orizon-hypr.conf
/system/share/orizon-desktop-hypr.conf
/logs/desktop.log
```

`desktop session` shows the persisted session options: theme, symbolic
wallpaper, layout, bar, launcher, and terminal autostart. `desktop theme
<name>`, `desktop wallpaper <name>`, `desktop layout <name>`, and `desktop bar
on|off|toggle` update `/system/desktop-session.conf`; `desktop apply` reloads
that session into the compositor. Current built-in theme/wallpaper/layout names
are simple symbolic profiles such as `graphite`, `moss`, `ember`, `frost`,
`aurora`, `dawn`, `noir`, `floating`, `tiling`, and `monocle`.

`desktop apps` lists the first launcher entries. `desktop launcher show` opens
the launcher overlay, `F3` toggles it locally, and `desktop launch terminal`
opens the first real app: the Orizon terminal window.

`desktop windows` lists the current compositor layers and the managed terminal
window. It is meant as the future seam for real window management.

`desktop workspace` shows the current Hyprland-style workspace state.
`desktop workspace <n>` switches the active workspace and `desktop move
terminal <n>` moves the terminal window. This is still compositor-side
workspace plumbing, not a complete tiling window manager yet.

`desktop doctor` checks the policy file, session file, template, user config,
optional package metadata, and reports PASS/WARN/FAIL without validating real
hardware or pretending upstream Hyprland is embedded.

## Shortcuts

```text
F1 opens the terminal window
F2 closes the terminal window
F3 toggles the launcher overlay
click/t/Enter/Space opens the terminal when it is closed
desktop workspace <n> switches runtime workspace
desktop move terminal <n> moves the terminal window
```

## Current Limits

- This is a Hyprland-style Orizon profile, not the real Hyprland compositor.
- No Wayland protocol, wlroots, GPU acceleration, file manager, wallpaper
  daemon, or multi-window tiling engine is implemented yet.
- The launcher/status bar/theme are Orizon compositor primitives, not upstream
  Hyprland plugins.
- Workspaces are a runtime Orizon compositor primitive; no real tiling protocol
  or multi-client Wayland window management exists yet.
- The first useful app is still the terminal.
- Real hardware validation is not claimed; this is VM/ZimaOS-ready plumbing.
