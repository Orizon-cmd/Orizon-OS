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
pkg sample
pkg hash /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
```

`pkg install` is available only after Orizon OS has been installed to disk.
Live boot can create and hash package files, but it refuses persistent package
installation because the live ISO is not the installed system.

## Package Format

A package is one text file:

```text
orizon-package 1
name orizon-hello
version 0.1.0
sha256 <sha256 of every byte after the payload line>
payload:
file /system/share/orizon-hello.txt
Hello from an Orizon package.
content-end
post-install
append /workspace/packages/history.log orizon-hello 0.1.0 installed
end-post-install
```

The hash covers the raw payload bytes after `payload:`. That keeps the header
editable while still proving that the files and post-install actions were not
changed.

## Payload Features

Supported payload entries:

- `file <absolute-path>` followed by file contents and `content-end`
- `post-install` followed by script lines and `end-post-install`

Supported post-install commands:

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

The next package-manager upgrades are `pkg info`, `pkg remove`, and package
rollback metadata.
