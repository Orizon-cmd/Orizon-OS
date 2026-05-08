# Orizon OS Update Flow

Orizon OS should not depend on one host. ZimaOS is the current lab backend, not
the update architecture.

## Portable Entry Point

Run from the repository root:

```powershell
python scripts/orizon/orizon_update.py --mode local-iso
```

This builds the active `x86_64` target on the current machine and refreshes the
root `Orizon-OS.iso` artifact.

## Backends

- `local-iso`: local build for any machine with the toolchain installed.
- `zimaos-iso`: remote Docker build on the ZimaOS lab server, then download ISO.
- `zimaos-vm`: remote Docker build, VM deploy, and ISO refresh.

## Current Kernel Behavior

The in-OS `update` command is informational for now. A real self-updater needs
networking, signature checks, and a safe boot-slot strategy. Until then, updates
are applied by host-side tools while `/workspace` persists through the Orizon
data area.
