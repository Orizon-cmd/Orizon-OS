# Orizon OS x86_64

This target is the active personal `x86_64` development base for `Orizon OS`.
It boots fast, stays stable, and intentionally starts from a minimal shell
with a restrained, development-first interface.

## Current Shape

- Boots on the ZimaOS VM target and on UEFI-capable `x86_64` hardware
- Uses a simple framebuffer UI with an `Orizon OS` splash and one core console
- Keeps `/workspace` persistent on the ZimaOS VM data partition
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
- On the ZimaOS VM, `/workspace` is saved to the reserved Orizon data area.
- New features should be reintroduced deliberately from this minimal base.
