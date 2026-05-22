# Orizon OS Update Flow

Orizon OS does not depend on one host. GitHub is the public update source;
ZimaOS is only the current lab VM backend.

For the exact implemented/prepared/not-implemented boundary, see
[STATUS.md](STATUS.md). In particular, rollback is currently Limine fallback
logic after Orizon early boot; true firmware `BootNext` is still future work.

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
- probe Ethernet and configure IPv4 with DHCP, then static fallback from
  `/system/network.conf` if DHCP is unavailable
- reuse a guarded Wi-Fi/CCMP link if `wifi join`, `wifi online`, or
  `wifi validate` has already made `net status` report `link=wifi`; `wifi
  update <ssid> [password]` runs that Wi-Fi validation, persists PASS/FAIL
  evidence with WPA/CCMP/DHCP/DNS/TLS snapshots to `/logs/wifi.log`, and then
  launches this updater directly
- resolve `raw.githubusercontent.com`
- open TCP/TLS to GitHub without launching host tools
- download `updates/x86_64/manifest.txt` from the public repository
- download `kernel.elf`, `BOOTX64.EFI`, and `limine.conf` by one HTTPS
  `Range` request per file, with chunk fallback if a full transfer fails
- verify every artifact with SHA-256 from the manifest
- download the package index from `Orizon-Packages`
- install missing or changed `.opkg` packages after verifying the index hash
- keep the currently booted kernel and UEFI loader as the ESP rollback slot
- rewrite only the installed ESP boot files
- cache both normal and fallback Limine configs for the post-update boot guard
- verify the rewritten ESP with the same boot checks used by `boot-check`
- preserve the Orizon data partition and persistent roots `/workspace`,
  `/home`, `/system`, `/packages`, and `/logs`
- save update logs and success metadata

Progress is streamed to the console while the transaction is running. The
terminal receives each state line immediately and redraws the framebuffer, so
long network downloads show Debian-like progress instead of leaving the OS
visually frozen until the final report.

The report also records rough elapsed timings for the package index, each
downloaded artifact, and the full transaction. These timings are written to
`/workspace/.orizon/update.log` and make slow DNS/TLS/download phases visible
without external tools.

Small boot payloads use a fast path: one TLS handshake and one HTTPS `Range`
request per artifact. The older chunk path remains as fallback, now with larger
64 KiB chunks.

If the booted kernel and UEFI loader already match the public manifest, `update`
skips the boot artifact download and ESP rewrite, then only checks packages.

`update status` is safe in the live ISO and on installed systems. It reports
whether the cached manifest/signature are present, whether `manifest.sig`
metadata matches the manifest SHA-256, the embedded TLS/root-trust posture,
HTTPS retry/resume cache state, bootguard state/attempts, cached normal and
fallback Limine configs, rollback readiness, and the live-ISO vs
installed-system difference. It also prints the honest boundary for recovery:
`nvram-bootnext: prepared=no`, `ab-slots: prepared=no`, and
`rollback-scope: post-orizon-early-boot`.
It also states the mandatory signature policy: `manifest.sig` must verify with
`rsa-pkcs1-sha256` and the embedded `orizon-update-root-2026-05` key before any
payload is accepted.

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

On the first boot into the refreshed kernel, the boot guard rewrites
`limine.conf` so the rollback entry becomes the default until the shell is
fully reached. When the shell is ready, Orizon restores the normal Limine
default and marks the update as validated. If the refreshed kernel reaches
Orizon early boot but fails before the shell, the next default boot selects
`Orizon OS Rollback` automatically.

`bootguard` now also reports the remaining validation attempts, the selected
strategy (`limine-boot-count-shell-validation`), the Limine normal/fallback
config cache state, the firmware type, and the EFI system table address when
booted through UEFI. That is the prepared plumbing for a future `BootNext`
writer, but Orizon still does not call UEFI Runtime Services yet. The status
therefore says `firmware-pre-kernel-rollback=no`: firmware-level failures
before the Orizon kernel starts still need future BootNext or A/B work.

If the refreshed system does not boot correctly, or if automatic fallback has
selected the rollback entry, run:

```text
rollback
```

That command rewrites the ESP so the currently booted rollback payload becomes
the main boot slot again. Metadata is available with:

```text
rollback-status
```

This is the current recovery layer. It is boot-count style and automatic after
the refreshed kernel reaches Orizon early boot. Rollback metadata now records
the selected strategy, scope, attempt budget, and whether BootNext was used
(`bootnext-used no`). True UEFI NVRAM `BootNext` or a firmware-level boot-count
path, which would also cover failures before the kernel starts at all, is still
a future hardening step even though the Limine EFI system table handoff is now
captured for diagnostics.

## Public Manifest

The public manifest and its detached signature are stored in the repository at:

```text
updates/x86_64/manifest.txt
updates/x86_64/manifest.sig
updates/x86_64/release.txt
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
package-source https://github.com/Orizon-cmd/Orizon-Packages.git
package-commit <resolved-package-repo-commit>
package-index-path packages/x86_64/index.txt
package-index-size <bytes>
package-index-sha256 <sha256>
iso-path Orizon-OS.iso
iso-size <bytes>
iso-sha256 <sha256>
```

The kernel accepts only non-empty payload sizes within its fixed safety caps
and verifies all hashes before writing the ESP. The signed manifest also pins
the exact package repository commit and package index SHA-256, so package
metadata cannot silently drift on `main`. Before parsing the manifest, Orizon
downloads `manifest.sig`, checks the manifest SHA-256, and verifies an
`rsa-pkcs1-sha256` signature against the compiled Orizon update root key
`orizon-update-root-2026-05`. A public branch without this signature is treated
as unsigned and the update is blocked before any boot payload or package is
installed.

The `iso-*` keys are release metadata for humans and tooling. Older kernels
ignore them, but because they are inside the signed manifest the release helper
can verify that the root `Orizon-OS.iso` committed to GitHub corresponds to the
same build as the published update payloads. `release.txt` records the ISO,
payload, manifest, and signature hashes so a commit review can spot a missing
artifact quickly.

The release helper signs manifests with a local private key:

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-vm
```

The private key defaults to `config/keys/update-signing.private.pem` and is
ignored by Git. Keep it only on the trusted release machine; the kernel contains
only the public modulus. `--generate-manifest-signing-key` is a key-rotation
bootstrap aid: a generated key must be embedded as the new kernel update root
before publishing, otherwise the helper refuses to sign.

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

The signed OS manifest pins the package index path, size, commit, and SHA-256.
Each index entry then verifies the full `.opkg` file, and the package manager
checks the package's own payload SHA-256 before installing files. The local
package commands remain:

```text
pkg list
pkg status
pkg search <query>
pkg remote
pkg update
pkg info <name>
pkg history
pkg sample
pkg hash <file.opkg>
pkg verify <file.opkg>
pkg install <file.opkg>
pkg remove <name>
pkg rollback <name>
```

Installed package metadata lives in:

```text
/workspace/.orizon/pkgdb
/workspace/.orizon/package-index
/system/packages
/system/installed
```

The boot rollback system remains responsible for kernel and UEFI loader
changes. Package removal now uses the stored package manifest to delete files
owned by a package and stores a persistent remove snapshot for
`pkg rollback <name>`. Local package install has a transaction guard: if
replaying the new payload or updating package metadata fails, Orizon removes the
partial new payload and restores the previous package payload/metadata when one
exists.
This is not yet a full boot-level package rollback.

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

All build/update flows refresh the root `Orizon-OS.iso` artifact unless
`--no-publish-root-iso` is used. Local and ZimaOS build modes also refresh
`updates/x86_64/release.txt` and validate that `manifest.sig` matches the
current `manifest.txt`; when the root ISO is published, its size and SHA-256
must match the signed `iso-*` fields. The release validator also checks that
`kernel.elf`, `BOOTX64.EFI`, `limine.conf`, `manifest.txt`, `manifest.sig`, and
`release.txt` all describe the current artifacts instead of a stale build.
Run the same validator without rebuilding:

```powershell
python scripts/orizon/orizon_update.py --mode validate-release
```

For the normal pre-commit gate, use:

```powershell
python scripts/orizon/quick_check.py
```

It combines whitespace checks, Python syntax checks, optional PowerShell syntax
checks, and release validation. GitHub CI stores this output as a quick-check
artifact. Release notes for GitHub are generated from `release.txt`,
`manifest.txt`, and `CHANGELOG.md`:

```powershell
python scripts/orizon/release_notes.py --output release_notes.md
```

## ZimaOS VM Validation

The dedicated update/rollback smoke test installs Orizon into a disposable
ZimaOS VM disk, then validates the installed-only update path through SSH:

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-iso
python scripts/orizon/test_update_rollback_vm.py
```

The test performs a full-disk install in the VM, checks `update status`,
temporarily removes `/workspace/.orizon/installed` to confirm the live-boot
guard refuses `update`, restores the marker, downloads and verifies the signed
GitHub manifest/signature, checks TLS/root-trust and resume-cache status, runs
`rollback`, reboots, and verifies the installed system still reports rollback
metadata. If the boot payload already matches the public manifest, the test
documents that the payload rewrite/bootguard-armed branch was not forced; using
an older boot tree will exercise that branch.

## Backends

- `github-iso`: download `Orizon-OS.iso` from the public GitHub repository.
- `local-iso`: local build for any machine with the toolchain installed.
- `zimaos-iso`: remote Docker build on the ZimaOS lab server, then download ISO.
- `zimaos-vm`: remote Docker build, VM deploy, and ISO refresh.

## Current Kernel Layers

- Intel `e1000/e1000e`, Realtek `RTL8139` and VirtIO-net Ethernet probes for
  VM NAT/bridge setups and compatible hardware.
- Raw Ethernet TX/RX rings.
- ARP handling and gateway MAC resolution.
- DHCP IPv4 configuration.
- Persistent static IPv4 fallback through `/system/network.conf`.
- DNS A-record resolver.
- ICMP `ping`, route, DNS and network log diagnostics.
- Minimal blocking TCP client.
- TLS 1.2 GitHub path with SNI, SAN host-name enforcement, certificate-chain
  link checks, RSA leaf signature verification, an embedded ISRG Root X1 trust
  anchor for the GitHub chain, X25519 key agreement, AES-128-GCM application
  data, encrypted HTTP `Range` requests, and decrypted response bodies.
- SHA-256 hashing for manifests and boot artifacts.
- Retried HTTPS manifest/index fetches and resumable boot-artifact caches for
  interrupted `kernel.elf`, `BOOTX64.EFI`, and `limine.conf` downloads.
- Post-update boot guard that arms a fallback Limine default on first refreshed
  boot and restores the normal default only after the shell is ready.
- FAT32 ESP writer shared with the disk installer.
- LAPIC timer first, PIT fallback, idle `hlt`, and first scheduler/process
  accounting.

## Files Written By Update

Persistent files:

```text
/workspace/.orizon/update.log
/workspace/.orizon/update-state
/workspace/.orizon/update-manifest
/workspace/.orizon/update-manifest.sig
/workspace/.orizon/update-kernel.part
/workspace/.orizon/update-efi.part
/workspace/.orizon/update-limine.part
/workspace/.orizon/update-limine.normal
/workspace/.orizon/update-limine.fallback
/workspace/.orizon/wifi-validation
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
/system/network.conf
/system/update-state
/system/update-source
/system/update-manifest
/system/update-manifest.sig
/system/installed
```

## Remaining Hardening

The current updater is intentionally direct: one installed ESP is refreshed in
place after artifact verification. The next reliability steps are:

- UEFI NVRAM `BootNext` writing through Runtime Services, or bootloader-level
  boot-count fallback before the refreshed kernel starts at all
- A/B system payload slots instead of the current single ESP main slot plus
  rollback files
