# Orizon OS Installer

The live ISO cannot safely rewrite itself, so durable updates begin with a real
disk installation path:

1. boot the live ISO,
2. run `install`,
3. collect language, keyboard, target disk, disk strategy, optional data
   partition, and hostname,
4. write an installation plan under `/workspace/.orizon/`,
5. choose `dual-boot-data`, `dual-boot-esp`, or `guided-full-disk`,
6. in `dual-boot-data`, add side-by-side files under `/EFI/Orizon` on the
   existing ESP and claim only the selected prepared partition as Orizon Data,
7. in `dual-boot-esp`, add side-by-side boot files only and leave every
   partition intact,
8. in full-disk mode, write a GPT disk with a FAT32 ESP and Orizon data
   partition,
9. copy the UEFI loader, kernel, and Limine config,
10. verify the installed or prepared UEFI boot files,
11. mark the system as installed only when an Orizon data partition exists.

## Current In-OS Command

Run from the Orizon console:

```text
install
```

The guided flow currently asks for:

- language: `fr_FR` or `en_US`
- keyboard: `fr-azerty` or `us-qwerty`
- target disk: detected as `disk0`, `disk1`, etc. with driver, size and model
- disk mode: `dual-boot-data`, `dual-boot-esp`, `guided-full-disk`, or
  `manual-later`
- data partition: required only for `dual-boot-data`; this must be an
  empty/prepared partition that Orizon may overwrite
- hostname, defaulting to `orizon-os`
- explicit confirmation: `DUALDATA disk0 partN` for installed dual boot,
  `DUALBOOT disk0` for side-by-side ESP only, or `ERASE disk0` for full-disk
  installation

Storage can also be inspected outside the installer:

```text
disks
partitions
storage detail
storage select 1
```

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

In `dual-boot-data` mode, Orizon does not repartition the disk and does not
overwrite the UEFI fallback path used by other operating systems. It scans the
existing GPT, mounts the existing FAT32 ESP, writes Orizon under `/EFI/Orizon`,
then changes only the selected partition type/name to `Orizon Data`.

This mode enables installed persistence and internet updates. `/workspace`,
`/home`, `/system`, `/packages`, and `/logs` are written to the selected data
partition, so anything created during the live boot in those roots is kept.
The selected partition is Orizon-owned after confirmation and its previous
filesystem/data should be considered overwritten.

In `dual-boot-esp` mode, Orizon performs only the safe boot-file preparation.
It scans the existing GPT, mounts the existing FAT32 ESP, and writes:

```text
/EFI/Orizon/BOOTX64.EFI
/EFI/Orizon/kernel.elf
/EFI/Orizon/limine.conf
/EFI/Orizon/INSTALL.TXT
```

This is intentionally a safe preparation step. It does not create an automatic
UEFI NVRAM entry yet, so booting may require firmware "boot from file", a
manual firmware boot entry, Windows BCD, or a Linux boot manager entry pointing
at `/EFI/Orizon/BOOTX64.EFI`. It also does not create an Orizon data partition,
so `/workspace` persistence, `update`, and package install/remove are not
enabled by this mode.

For `dual-boot-data`, `update` preserves the shared ESP and rewrites only the
Orizon side-by-side directory:

```text
/EFI/Orizon/BOOTX64.EFI
/EFI/Orizon/kernel.elf
/EFI/Orizon/limine.conf
/EFI/Orizon/KROLLBK.ELF
/EFI/Orizon/BOOTX64.ROL
```

Verify the side-by-side ESP files with:

```text
dualboot-check
```

The full-disk `guided-full-disk` mode writes a bootable disk layout:

- protective MBR plus primary/backup GPT
- partition 1: FAT32 ESP from 1 MiB to 512 MiB
- partition 2: Orizon data from 512 MiB to the end of disk
- `/EFI/BOOT/BOOTX64.EFI`
- `/boot/kernel.elf`
- `/limine.conf`, `/boot/limine.conf`, and `/EFI/BOOT/limine.conf`
- `/INSTALL.TXT`

The Orizon data partition persists the first real data roots:

```text
/workspace
/home
/system
/packages
/logs
```

Files and directories created during the live boot in those roots are saved
after the Orizon GPT/data layout exists, so they survive the full-disk install
path without touching unrelated dual-boot partitions.

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

The installer can now partition and install to AHCI/SATA disks or NVMe
namespaces with 512-byte LBAs. It can also create an installed dual-boot flow
when a prepared partition already exists beside Windows/Linux. It is still
intentionally narrow: no automatic NVRAM/BCD entry creation, no automatic
shrink/create of an Orizon data partition yet, and no automatic boot-count
recovery yet.

## Next Kernel Layers

- Add automatic UEFI NVRAM or Windows BCD entry creation for dual boot.
- Add an in-OS partition create/resize assistant after the manual/prepared
  partition path is battle-tested.
- Add rollback-safe A/B system slots for full system images.
- Add full Unicode keyboard/text rendering for accented keys.
- Replace emulator poweroff fallback with full ACPI shutdown parsing.
