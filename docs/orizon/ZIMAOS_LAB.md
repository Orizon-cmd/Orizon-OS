# ZimaOS Lab

## Access Summary

- Host alias: `zimaos-orizon`
- Host: `192.168.1.124`
- User: `orizon32`
- SSH key: `C:\\Users\\orizo\\.ssh\\id_ed25519`
- Password storage: local file `config/hosts/zimaos.local.env` only

## Local Files

- Tracked template: `config/hosts/zimaos.env.example`
- Local secrets: `config/hosts/zimaos.local.env`
- Local facts snapshot: `config/hosts/zimaos-facts.local.json`
- Local VM inventory snapshot: `config/hosts/zimaos-vms.local.json`
- Tracked SSH template: `config/ssh/zimaos.config`
- ZimaOS GitHub SSH alias template: `config/ssh/zimaos-github.config`
- ZimaOS GitHub public key copy: `config/ssh/zimaos-github.pub`
- VM template: `config/vm/orizon-dev.example.json`
- Active SSH config on this PC: `%USERPROFILE%\\.ssh\\config`

## Verified On `2026-05-07`

- Hostname: `ZimaOS`
- OS: `ZimaOS v1.5.4`
- Model: `ZimaCube`
- Kernel: `Linux 6.12.25`
- Remote working directory for `orizon32`: `/DATA`
- `sudo` works with the same password used for SSH
- Libvirt version: `10.2.0`
- QEMU image tooling: `qemu-img 9.2.0`

## VM Layout Found On The Host

- VM disk storage: `/DATA/VM`
- Libvirt metadata root: `/DATA/.libvirt`
- CasaOS VM helper state: `/DATA/.casaos/virt`
- Existing domains seen: `86b337c6`, `8a194dcc`, `fbcb0022`
- Existing domains use file-backed disks stored directly in `/DATA/VM`
- Dedicated Orizon test VM created: `orizon-dev`
- Current Orizon display endpoint while running: `vnc://localhost:0`

## Current Bootstrap Mode

The Windows machine does not currently have the local `x86_64` build toolchain
installed, and ZimaOS itself also lacks the compiler stack. For now, the first
VM boot path uses the tracked `orizon-os-x86_64/iso_root` UEFI tree and writes
it directly onto the VM disk with `scripts/orizon/deploy_x86_64_tree_vm.py`.

## GitHub Access On ZimaOS

- Dedicated server key path: `/DATA/.ssh/id_ed25519_github_orizon`
- Public key copy stored locally in `config/ssh/zimaos-github.pub`
- SSH alias template for the server stored in `config/ssh/zimaos-github.config`

Before ZimaOS can pull from the private GitHub repository directly, add the
public key from `config/ssh/zimaos-github.pub` to GitHub as a deploy key or as
an SSH key attached to the account that owns `Orizon-cmd/Orizon-OS`.

## Important Warning

The root filesystem on the ZimaOS box is currently at `100%` usage. Before
trying heavier VM workflows or package installs on that machine, free space
should be checked to avoid broken updates or failed provisioning.

## Useful Commands

- `ssh zimaos-orizon`
- `powershell -File scripts/orizon/test_zimaos.ps1`
- `python scripts/orizon/setup_zimaos_access.py`
- `python scripts/orizon/inventory_zimaos_vms.py`
- `python scripts/orizon/provision_orizon_vm.py --artifact image/orizonos-x86_64.img`
- `python scripts/orizon/deploy_orizon_vm_update.py --artifact image/orizonos-x86_64.img`
- `python scripts/orizon/deploy_x86_64_tree_vm.py`
