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
- Local update transaction state and package database bootstrap.
- `/workspace/.orizon/update.log` and `/workspace/.orizon/update-state`.
- Runtime system files `/system/packages`, `/system/update-state`, and
  `/system/update-source`.

Still required before GitHub downloads can happen fully inside Orizon OS:

- ARP and IPv4.
- DHCP or static IPv4 configuration.
- DNS.
- TCP.
- HTTPS/TLS, because GitHub requires HTTPS for raw artifacts.
- Verified package/manifest format.
- Safe boot/system writer for replacing kernel/system files without corrupting
  the current boot.

Until those layers exist, `update` starts the upgrade and stops safely at the
first missing kernel layer instead of pretending that the machine was upgraded.
