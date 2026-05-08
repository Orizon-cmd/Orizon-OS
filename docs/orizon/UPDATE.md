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

The in-OS `update` command is informational for now. A real self-updater will
need networking, signature checks, and a safe boot-slot strategy. Until then,
updates are applied by host-side tools while `/workspace` persists through the
Orizon data area.
