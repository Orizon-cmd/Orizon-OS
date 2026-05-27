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
`/workspace/packages/orizon-desktop-hypr.opkg`. That optional package installs
the first Orizon desktop profile:
`/system/desktop.conf`, `/system/desktop-session.conf`,
`/system/desktop-settings.conf`,
`/system/desktop-binds.conf`, `/system/desktop-autostart.conf`,
`/system/desktop-rules.conf`, `/system/desktop-monitors.conf`,
`/system/desktop-runtime.conf`,
`/home/orizon/.config/hypr/orizon-hypr.conf`, and
`/system/share/orizon-desktop-hypr.conf`. It is Hyprland-style configuration
for Orizon's compositor, not upstream Hyprland/Wayland yet. After an installed
VM boot:

```text
pkg install orizon-desktop-hypr
desktop status
desktop session
desktop settings
desktop settings presets
desktop settings doctor
desktop settings preset compact
desktop config doctor
desktop config apply
desktop pointer
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
desktop descriptions
desktop instances
desktop submap
desktop configerrors
desktop rollinglog
desktop keyword general:gaps_in 9
desktop hyprctl version
desktop hyprctl systeminfo
desktop hyprctl activeworkspace
desktop hyprctl layouts
desktop hyprctl animations
desktop hyprctl decorations
desktop hyprctl descriptions
desktop hyprctl instances
desktop hyprctl submap
desktop hyprctl cursorpos
desktop hyprctl devices
desktop hyprctl splash
desktop hyprctl configerrors
desktop hyprctl rollinglog
desktop hyprctl getoption general:gaps_in
desktop hyprctl keyword decoration:rounding 11
desktop dispatch togglesplit
desktop dispatch layoutmsg splitratio 60
desktop dispatch submap resize
desktop hyprctl submap reset
desktop doctor
```

The named install path generates the local `.opkg`, installs it, then enables
the profile with a package hook. Removing the package disables the desktop
policy, and `pkg rollback orizon-desktop-hypr` restores the last removed
desktop package snapshot. The generated desktop package is currently version
`0.15.0` because it includes policy/config files, the persisted session
settings, the system-wide desktop settings layer, settings presets/doctor
commands, Hyprland-style config doctor/apply import, generated
bind/autostart/window-rule/monitor/runtime hint files, runtime inspection
commands, `desktop keyword`, input/version/systeminfo/layouts/animations/decorations/descriptions/instances/submap/configerrors/rollinglog diagnostics, the
`hyprctl version/systeminfo/activeworkspace/layouts/animations/decorations/descriptions/instances/submap/devices/cursorpos/splash/configerrors/rollinglog/getoption/keyword/binds/layers`
facade, pointer diagnostics, the aligned Hyprland-style key template,
preset/focus commands, dispatcher commands, fullscreen/pseudo/pinned client
state, focus-cycle/swap/togglesplit/layoutmsg/submap actions, and commands used by `desktop theme`,
`desktop wallpaper`, `desktop layout`, `desktop autostart`, `desktop bar`, and
the launcher.

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
