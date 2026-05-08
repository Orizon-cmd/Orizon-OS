# Orizon OS Update Flow

Orizon OS should not depend on one host. GitHub is the public update source;
ZimaOS is only the current lab VM backend.

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

## GitHub Options

- `--github-repo`: public repository URL.
- `--github-ref`: branch or tag, currently `main` by default.
- `--github-iso-path`: ISO path inside the repository, currently `Orizon-OS.iso`.
- `--github-iso-url`: explicit ISO URL if the artifact moves later.
- `--from-github`: requires a clean checkout and refuses local commits that are
  not published to the requested GitHub ref, so release builds really come from
  the public source.

## Current Kernel Behavior

The in-OS `update` command is now the real full-upgrade entrypoint. It starts a
kernel-owned transaction, probes the Ethernet adapter, writes state under
`/workspace/.orizon/`, and refuses to launch external host tools.

Current kernel-owned layers:

- Intel `e1000/e1000e` Ethernet probe for the VM and compatible hardware.
- Raw Ethernet TX/RX rings.
- ARP handling and gateway MAC resolution.
- DHCP IPv4 configuration.
- DNS A-record resolver.
- Minimal blocking TCP client and HTTP GET probe.
- GitHub edge contact saved to `/workspace/.orizon/github-http-response`.
- TLS ClientHello/SNI probe to GitHub `443`, with server handshake parsing,
  certificate-chain metadata, leaf certificate SHA-256, ServerKeyExchange
  metadata, leaf DNS SAN identity matching for `raw.githubusercontent.com`, and
  issuer/subject chain-link checks saved to
  `/workspace/.orizon/github-tls-response`.
- SHA-256 hashing for downloaded update proofs.
- Local manifest and staging plan:
  `/workspace/.orizon/update-manifest`, `/workspace/.orizon/update-plan`,
  `/system/update-manifest`, `/system/installed`.
- PIT timer at 100 Hz with real uptime counters.
- Idle `hlt` loop to avoid burning a full CPU core while waiting.
- First scheduler/process table with CPU tick accounting.
- Local update transaction state and package database bootstrap.
- `/workspace/.orizon/update.log` and `/workspace/.orizon/update-state`.
- Runtime system files `/system/packages`, `/system/update-state`, and
  `/system/update-source`.

Still required before GitHub package downloads can happen fully inside Orizon OS:

- TLS cryptography: certificate signature/root validation, key agreement,
  traffic keys, AEAD, and HTTP inside the encrypted tunnel.
- Verified package/manifest format.
- Safe ESP/FAT32 writer or A/B boot slot for replacing kernel/system files
  without corrupting the current boot.

Until TLS crypto and the boot writer exist, `update` starts the upgrade, reaches
GitHub over the kernel network stack, saves the GitHub HTTP redirect/proof,
performs a TLS server-handshake, leaf identity, and chain-link probe, hashes the
proofs, writes a staging plan, and stops safely instead of pretending that the
machine was upgraded.
