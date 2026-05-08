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
- rewrite only the installed ESP boot files
- preserve the Orizon data partition and `/workspace`
- save update logs and success metadata

After success, reboot to start the refreshed boot payload.

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

## Live Boot Behavior

Live boot is for testing and installation, not self-replacement. Because the
system boots from an ISO image, there is no writable system payload to mutate
safely. For that reason:

- `help` does not list `update` in live boot
- typing `update` manually prints an install-first message
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
- A/B boot slots or rollback metadata before replacing boot payloads
- signed release manifests in addition to SHA-256 transport verification
