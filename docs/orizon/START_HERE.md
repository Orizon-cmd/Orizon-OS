# Orizon OS Start Here

## What is already prepared

- The repository is organized as a standalone Orizon OS project.
- SSH key access to the ZimaOS lab server is configured and reusable.
- Local server settings and reusable scripts are in place.
- The ZimaOS VM storage area has been identified for future Orizon test VMs.

## Files to open first

- `README.md`
- `docs/orizon/ROADMAP.md`
- `docs/orizon/ZIMAOS_LAB.md`
- `scripts/orizon/setup_zimaos_access.py`
- `scripts/orizon/test_zimaos.ps1`

## First recommended moves

1. Set `origin` to `https://github.com/Orizon-cmd/Orizon-OS.git`.
2. Decide the first supported target: `x86_64` UEFI or `ARM64` QEMU/Raspberry Pi.
3. Build the first Orizon OS boot artifact locally, or use the tracked
   `orizon-os-x86_64/iso_root` tree for immediate VM bootstrapping.
4. Provision a dedicated Orizon VM on the ZimaOS host.
5. Use the update scripts to refresh the VM artifact instead of reinstalling.
