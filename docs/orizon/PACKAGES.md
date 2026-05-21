# Orizon OS Packages

Orizon packages are intentionally small for the first implementation. The goal
is to let Orizon update and install separate components without turning the
kernel updater into a giant boot-only replacement tool.

Official repository:

```text
https://github.com/Orizon-cmd/Orizon-Packages
```

## Commands

```text
pkg help
pkg list
pkg status
pkg search orizon
pkg remote
pkg update
pkg info orizon-hello
pkg history
pkg sample
pkg hash /workspace/packages/orizon-hello.opkg
pkg verify /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
pkg remove orizon-hello
pkg rollback orizon-hello
```

`pkg update`, `pkg install`, `pkg remove`, and `pkg rollback` are available only
after Orizon OS has been installed to disk. Live boot can inspect, search,
create, hash and verify package files, but it refuses persistent package
changes because the live ISO is not the installed system. `pkg update` is
intentionally a thin wrapper around the signed system `update` flow: the package
index is authenticated by the signed OS manifest, pinned package repository
commit, and pinned package-index SHA-256. `pkg remote` shows the cached signed
index once an installed system has refreshed it.

## Package Format

A package is one text file:

```text
orizon-package 1
name orizon-hello
version 0.1.0
depends orizon-core core-x86_64
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

## Installed Database

Installed package state is stored under:

```text
/workspace/.orizon/pkgdb
/workspace/.orizon/pkgdb/installed
/workspace/.orizon/pkgdb/packages
/workspace/.orizon/pkgdb/removed
/workspace/.orizon/package-index
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
when it is not available yet. `pkg info <name>` shows stored package metadata,
dependencies, scripts and the files owned by an installed package.

`pkg remove <name>` saves a rollback snapshot in
`/workspace/.orizon/pkgdb/removed`, runs an optional `pre-remove` script,
removes files declared by the stored manifest, runs an optional `post-remove`
script, deletes installed metadata, refreshes `/system/installed`, and persists
the package database. `pkg rollback <name>` restores the last removed snapshot
if the package is not already installed.

Package install now keeps a previous package manifest in memory while applying
an upgrade. If payload replay or metadata update fails, Orizon removes the
partial new payload and restores the previous package payload/metadata when it
exists. Remove rollback is persistent across reboots through the package
database, but it is still a local package transaction guard, not a full
boot-level package rollback.
