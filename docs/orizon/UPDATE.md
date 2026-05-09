# Orizon OS Update Flow

Orizon OS does not depend on one host. GitHub is the public update source;
ZimaOS is only the current lab VM backend.

Status: active for installed systems. The in-OS `update` command is hidden from
`help` in live boot and refuses to run without the installed marker at
`/workspace/.orizon/installed`.

## Installed Update Command

Inside an installed Orizon OS:

```text
update
```

The command performs a kernel-owned full-upgrade transaction:

- prepare the local package database under `/system` and `/workspace/.orizon`
- probe Ethernet and configure IPv4 with DHCP
- resolve `raw.githubusercontent.com`
- open TCP/TLS to GitHub without launching host tools
- download `updates/x86_64/manifest.txt` from the public repository
- download `kernel.elf`, `BOOTX64.EFI`, and `limine.conf` by HTTPS `Range`
- verify every artifact with SHA-256 from the manifest
- download the package index from `Orizon-Packages`
- install missing or changed `.opkg` packages after verifying the index hash
- keep the currently booted kernel and UEFI loader as the ESP rollback slot
- rewrite only the installed ESP boot files
- preserve the Orizon data partition and `/workspace`
- save update logs and success metadata

Progress is streamed to the console while the transaction is running. The
terminal receives each state line immediately and redraws the framebuffer, so
long network downloads show Debian-like progress instead of leaving the OS
visually frozen until the final report.

After success, reboot to start the refreshed boot payload.

## Rollback

Each installed update writes two boot slots to the ESP:

```text
/boot/kernel.elf
/EFI/BOOT/BOOTX64.EFI
/boot/KROLLBK.ELF
/EFI/BOOT/BOOTX64.ROL
```

`kernel.elf` and `BOOTX64.EFI` are the updated payload. `KROLLBK.ELF` and
`BOOTX64.ROL` are copied from the payload that was running before the update.
The generated Limine config keeps the normal `Orizon OS` entry and adds:

```text
Orizon OS Rollback
```

If the refreshed system does not boot correctly, boot the rollback entry. Once
inside the rollback system, run:

```text
rollback
```

That command rewrites the ESP so the currently booted rollback payload becomes
the main boot slot again. Metadata is available with:

```text
rollback-status
```

This is the first recovery layer. A future boot-count guard can make failed
boot detection fully automatic before the kernel starts.

## Public Manifest

The public manifest is stored in the repository at:

```text
updates/x86_64/manifest.txt
```

Required keys:

```text
manifest-version 1
os Orizon OS
channel main
version <release-version>
commit <source-commit-or-channel>
source https://github.com/Orizon-cmd/Orizon-OS
kernel-path updates/x86_64/kernel.elf
kernel-size <bytes>
kernel-sha256 <sha256>
efi-path updates/x86_64/BOOTX64.EFI
efi-size <bytes>
efi-sha256 <sha256>
limine-path updates/x86_64/limine.conf
limine-size <bytes>
limine-sha256 <sha256>
```

The kernel accepts only non-empty payload sizes within its fixed safety caps
and verifies all hashes before writing the ESP.

## Package Manager Link

`update` now refreshes the Orizon package database through the in-kernel
package manager instead of writing a fixed package list by itself. It also
reads the official package repository:

```text
https://github.com/Orizon-cmd/Orizon-Packages
```

The public index used by the kernel is:

```text
packages/x86_64/index.txt
```

Index entries use this first format:

```text
package <name> <version> <path> <size> <sha256>
```

The index SHA-256 verifies the full `.opkg` file. The package manager then
checks the package's own payload SHA-256 before installing files. The local
package commands remain:

```text
pkg list
pkg status
pkg sample
pkg hash <file.opkg>
pkg install <file.opkg>
```

Installed package metadata lives in:

```text
/workspace/.orizon/pkgdb
/workspace/.orizon/package-index
/system/packages
/system/installed
```

The boot rollback system remains responsible for kernel and UEFI loader
changes. Package rollback will be added separately once package removal and
per-file manifests exist.

## Live Boot Behavior

Live boot is for testing and installation, not self-replacement. Because the
system boots from an ISO image, there is no writable system payload to mutate
safely. For that reason:

- `help` does not list `update` in live boot
- typing `update` manually prints an install-first message
- `rollback` is also installed-disk only
- installing the OS creates `/workspace/.orizon/installed`
- only after booting the installed disk does `update` become available

## Internet Entry Points

Download the latest public ISO from GitHub without compiling:

```powershell
python scripts/orizon/orizon_update.py --mode github-iso
```

Build from the latest public source:

```powershell
python scripts/orizon/orizon_update.py --from-github --mode local-iso
```

Update the lab VM from the latest public source:

```powershell
python scripts/orizon/orizon_update.py --from-github --mode zimaos-vm
```

All three flows refresh the root `Orizon-OS.iso` artifact unless
`--no-publish-root-iso` is used.

## Backends

- `github-iso`: download `Orizon-OS.iso` from the public GitHub repository.
- `local-iso`: local build for any machine with the toolchain installed.
- `zimaos-iso`: remote Docker build on the ZimaOS lab server, then download ISO.
- `zimaos-vm`: remote Docker build, VM deploy, and ISO refresh.

## Current Kernel Layers

- Intel `e1000/e1000e` Ethernet probe for the VM and compatible hardware.
- Raw Ethernet TX/RX rings.
- ARP handling and gateway MAC resolution.
- DHCP IPv4 configuration.
- DNS A-record resolver.
- Minimal blocking TCP client.
- TLS 1.2 GitHub path with SNI, certificate metadata, RSA leaf signature proof,
  X25519 key agreement, AES-128-GCM application data, encrypted HTTP `Range`
  requests, and decrypted response bodies.
- SHA-256 hashing for manifests and boot artifacts.
- FAT32 ESP writer shared with the disk installer.
- PIT timer at 100 Hz, idle `hlt`, and first scheduler/process accounting.

## Files Written By Update

Persistent files:

```text
/workspace/.orizon/update.log
/workspace/.orizon/update-state
/workspace/.orizon/update-manifest
/workspace/.orizon/github-https-manifest
/workspace/.orizon/github-https-manifest.sha256
/workspace/.orizon/packages
/workspace/.orizon/last-update
/workspace/.orizon/rollback-info
/workspace/.orizon/rollback-state
```

Runtime files:

```text
/system/packages
/system/update-state
/system/update-source
/system/update-manifest
/system/installed
```

## Remaining Hardening

The current updater is intentionally direct: one installed ESP is refreshed in
place after artifact verification. The next reliability steps are:

- root trust anchoring instead of proof-level certificate checks
- stronger retry/error recovery during HTTPS downloads
- signed release manifests in addition to SHA-256 transport verification
- boot-count based automatic fallback before the kernel starts
