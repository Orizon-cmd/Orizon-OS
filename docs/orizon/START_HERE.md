# Orizon OS Start Here

## What is already prepared

- The repository is organized as a standalone Orizon OS project.
- SSH key access to the ZimaOS lab server is configured and reusable.
- Local server settings and reusable scripts are in place.
- The ZimaOS VM storage area has been identified and a dedicated `orizon-dev` VM exists.
- A remote Docker-based `x86_64` build path is working on ZimaOS.
- VM updates can be applied in place without reinstalling the OS each time.

## Files to open first

- `README.md`
- `docs/orizon/ROADMAP.md`
- `docs/orizon/ZIMAOS_LAB.md`
- `scripts/orizon/build_x86_64_on_zimaos.py`
- `scripts/orizon/open_zimaos_vnc_tunnel.ps1`
- `scripts/orizon/setup_zimaos_access.py`
- `scripts/orizon/test_zimaos.ps1`

## First recommended moves

1. Run `python scripts/orizon/build_x86_64_on_zimaos.py --deploy-vm`.
2. Open the VM display with `powershell -File scripts/orizon/open_zimaos_vnc_tunnel.ps1`.
3. Use the running `orizon-dev` VM as the default loop for `x86_64` testing.
4. When needed, extend the same update flow to full disk images with `deploy_orizon_vm_update.py`.
