# Orizon OS x86_64

This target is the active personal `x86_64` development base for `Orizon OS`.
It boots fast, stays stable, and intentionally starts from a minimal shell
with a restrained, development-first interface.

## Current Shape

- Boots on UEFI-capable `x86_64` hardware; ZimaOS is only the current lab VM
- Uses a framebuffer UI with an `Orizon OS` splash and one core console with
  persistent history, Tab completion, and a small line editor
- Keeps `/workspace` and `/logs` persistent when an Orizon data area is
  available
- Probes AHCI/SATA and 512-byte LBA NVMe storage, plus Intel e1000/e1000e and
  Realtek RTL8139 Ethernet controllers
- Stays intentionally small so new features can be added deliberately

## Why it is minimal

The goal is to give `Orizon OS` a clean foundation that is fully yours:

- fewer moving parts while iterating on the kernel
- less inherited UX to unwind later
- a stable place to rebuild features on your own terms

## Building

### Prerequisites

**macOS (Homebrew):**
```bash
brew install llvm xorriso qemu
```

**Linux (apt):**
```bash
sudo apt install clang lld xorriso qemu-system-x86
```

### Build

Using Make:
```bash
make
```

Or the shell script:
```bash
./build.sh
```

This creates `orizonos-x86_64.iso`.

## Portable Update Flow

From the repository root:

```bash
python scripts/orizon/orizon_update.py --mode github-iso
```

This downloads the latest public `Orizon-OS.iso` from GitHub without requiring
a compiler. To rebuild from the latest public source on the current machine:

```bash
python scripts/orizon/orizon_update.py --from-github --mode local-iso
```

The ZimaOS backend remains available only for the lab VM with:

```bash
python scripts/orizon/orizon_update.py --from-github --mode zimaos-vm
```

## Running

### QEMU (UEFI mode)

```bash
make run
```

Or manually:
```bash
qemu-system-x86_64 -M q35 -m 512M -cdrom orizonos-x86_64.iso \
    -bios /opt/homebrew/share/qemu/edk2-x86_64-code.fd -serial stdio
```

Linux OVMF path is usually `/usr/share/OVMF/OVMF_CODE.fd`.

## Project Structure

```text
kernel/
|- boot/         # Limine entry point
|- drivers/      # framebuffer, ps2, usb, storage, acpi, pci
|- fs/           # minimal workspace filesystem with persistence
|- gui/          # console compositor and terminal
|- mm/           # memory allocation
`- include/      # headers

limine.conf      # bootloader config
Makefile         # main build
build.sh         # alternative build script
```

## Notes

- The active VM build is intentionally console-first.
- The console supports `Up/Down` history, `Tab` command/path completion, and
  `edit <file>` with `.show`, `.insert`, `.replace`, `.del`, `.write`, `.save`.
- `hw` prints a compact hardware report: CPU, heap, disk, network, USB/PS2,
  install/update state, and the first PCI devices.
- `dmesg`, `logs`, and `report` expose boot diagnostics; installed systems
  persist the current boot transcript at `/logs/boot.log`.
- `/workspace` is saved to the reserved Orizon data area when the boot disk has
  been prepared for persistence.
- New features should be reintroduced deliberately from this minimal base.
