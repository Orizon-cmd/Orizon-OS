# Orizon OS Packages

Orizon packages are intentionally small for the first implementation. The goal
is to let Orizon update and install separate components without turning the
kernel updater into a giant boot-only replacement tool.

Current package-manager limits are summarized in [STATUS.md](STATUS.md).
`pkg rollback` is local package recovery, not full boot-level package rollback.

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
pkg cache
pkg search orizon
pkg remote
pkg remote verify
pkg upgrade plan
pkg update
pkg upgrade
pkg info orizon-hello
pkg history
pkg sample
pkg hash /workspace/packages/orizon-hello.opkg
pkg verify /workspace/packages/orizon-hello.opkg
pkg simulate /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
pkg remove orizon-hello
pkg rollback orizon-hello
```

`pkg update`, `pkg upgrade`, `pkg install`, `pkg remove`, and `pkg rollback`
are available only after Orizon OS has been installed to disk. Live boot can
inspect, audit, search, create, hash, verify and simulate package files, but it refuses
persistent package changes because the live ISO is not the installed system.
`pkg audit` checks package database/cache consistency, `pkg cache` prints cache
paths and counters, and `pkg simulate <file>` prints a dry-run install/upgrade
plan without writing files.
`pkg upgrade plan` remains safe in live boot: it reads the cached signed remote
index when present and prints install/upgrade/current/protected decisions
without mutating files. `pkg update` and `pkg upgrade` are intentionally thin
wrappers around the signed system `update` flow: the package index is
authenticated by the signed OS manifest, pinned package repository commit, and
pinned package-index SHA-256. `pkg remote verify` validates the cached index
shape, paths, hashes, sizes and duplicate names. Detached package repository
signatures are not implemented yet; the current fallback is the signed update
manifest pinning the package index.

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
when it is not available yet. `pkg remote verify` writes
`/workspace/.orizon/pkgdb/cache/remote.status` with the last validation result.
`pkg audit` reports invalid stored packages, orphan metadata, missing metadata,
rollback snapshots, remote-index status and a PASS/WARN/FAIL summary. `pkg cache`
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
`removed` / `rollback` events with `transaction=v4-N`, rollback and result
fields. Remove rollback is persistent across reboots through the package
database, but it is still a local package transaction guard, not a full
boot-level package rollback.
