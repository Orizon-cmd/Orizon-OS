# Orizon OS Installer

The live ISO cannot safely rewrite itself, so durable updates begin with a real
disk installation path:

1. boot the live ISO,
2. run `install`,
3. collect language, keyboard, hostname, and disk strategy,
4. write an installation plan under `/workspace/.orizon/`,
5. save `/workspace` before touching the disk,
6. write a GPT disk with a FAT32 ESP,
7. copy the UEFI fallback loader, kernel, and Limine config,
8. verify the installed UEFI boot files,
9. preserve the Orizon data partition used by `/workspace`,
10. mark the system as installed and shut down so installer media can be removed.

## Current In-OS Command

Run from the Orizon console:

```text
install
```

The guided flow currently asks for:

- language: `fr_FR` or `en_US`
- keyboard: `fr-azerty` or `us-qwerty`
- disk mode: `guided-full-disk` or `manual-later`
- hostname, defaulting to `orizon-os`
- explicit confirmation with `INSTALL` or `install`

It writes runtime/staging state:

```text
/workspace/.orizon/install-plan
/workspace/.orizon/install-state
/workspace/.orizon/installed
/workspace/.orizon/keyboard
/system/install-state
/system/locale
/system/keyboard
```

It also writes a bootable disk layout in `guided-full-disk` mode:

- protective MBR plus primary/backup GPT
- partition 1: FAT32 ESP from 1 MiB to 512 MiB
- partition 2: Orizon data from 512 MiB to the end of disk
- `/EFI/BOOT/BOOTX64.EFI`
- `/boot/kernel.elf`
- `/limine.conf`, `/boot/limine.conf`, and `/EFI/BOOT/limine.conf`
- `/INSTALL.TXT`

`/workspace` remains persistent through the Orizon data partition.
Files and directories created during the live boot are saved before the ESP/GPT
writer runs, so they survive the install path.

Before the disk is marked installed, the installer runs the same boot validator
exposed as:

```text
boot-check
```

It checks the protective MBR, GPT entries, FAT32 ESP, ESP volume label,
`/EFI/BOOT/BOOTX64.EFI`, `/boot/kernel.elf`, Limine configs, and
`/INSTALL.TXT`.

If the Orizon GPT layout is already present but the ESP boot files are damaged,
this command rewrites only the ESP boot files and keeps the data partition:

```text
repair-boot
```

After success the installer prints a shutdown notice:

```text
Remove/eject the ISO or USB installer before the next boot.
```

It then schedules shutdown. On the next boot, the persistent installed marker
blocks `install` to avoid accidental reinstall/destructive disk writes. Use
`install-status` to review the saved state.

The selected keyboard layout is now applied by the kernel input layer. Current
layouts are `fr-azerty` and `us-qwerty`; accent keys are mapped to ASCII-safe
fallbacks until the console grows Unicode text support.

## Safety Boundary

The installer can now partition and install to the first AHCI/SATA disk or the
first NVMe namespace with 512-byte LBAs. It is still intentionally narrow: UEFI
fallback boot only, no multi-disk picker yet, no dual-boot preservation, and no
automatic boot-count recovery yet.

## Next Kernel Layers

- Add an explicit multi-disk selector.
- Add rollback-safe A/B system slots for full system images.
- Add optional dual-boot/manual partitioning.
- Add full Unicode keyboard/text rendering for accented keys.
- Replace emulator poweroff fallback with full ACPI shutdown parsing.
