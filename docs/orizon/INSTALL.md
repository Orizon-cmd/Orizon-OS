# Orizon OS Installer

The live ISO cannot safely rewrite itself, so durable updates begin with a real
disk installation path:

Current validation and safety limits are summarized in [STATUS.md](STATUS.md).
Do not treat VM install success as Lenovo or physical-PC validation.
For VM install failure triage, use [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
For release artifact publishing after installer changes, use
[RELEASE.md](RELEASE.md).
Installer behavior changes should also update `CHANGELOG.md`, `STATUS.md`, and
`COMMANDS.md` so the safe VM-only boundary remains visible.

1. boot the live ISO,
2. run `install`,
3. collect language, keyboard, target disk, disk strategy, optional data
   partition, hostname, and the optional desktop choice,
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

Before running the guided flow, or from SSH in a VM, this command writes a
non-destructive preflight report:

```text
install-plan
cat /workspace/.orizon/install-report.txt
```

Optional report modes are `manual`, `dual-boot-esp`, `dual-boot-data <part>`,
and `guided-full-disk`. They do not install anything; they only document the
target disk, write scope, confirmation string, partition snapshot, and any
obvious blocker.

The guided flow currently asks for:

- language: `fr_FR` or `en_US`
- keyboard: `fr-azerty` or `us-qwerty`
- target disk: detected as `disk0`, `disk1`, etc. with driver, size and model
- disk mode: `dual-boot-data`, `dual-boot-esp`, `guided-full-disk`, or
  `manual-later`
- data partition: required only for `dual-boot-data`; this must be an
  empty/prepared partition that Orizon may overwrite
- hostname, defaulting to `orizon-os`
- optional desktop: default `none`; choosing yes installs the
  `orizon-desktop-hypr` Hyprland-style profile
- explicit confirmation: `DUALDATA disk0 partN` for installed dual boot,
  `DUALBOOT disk0` for side-by-side ESP only, or `ERASE disk0` for full-disk
  installation

Storage can also be inspected outside the installer:

```text
disks
partitions
gpt scan
storage detail
storage diag
storage vmcheck
logs storage
logs pci
disk identify
disk read-test
disk read-test last
storage select 1
```

Those commands are non-destructive. If a real laptop does not show its internal
disk, do not run `install`; capture `report save`, `storage diag`,
`storage vmcheck`, `logs storage`, `logs pci`, `pci bars`, `disk identify`, and
`disk read-test last` first. In VM, AHCI and modern VirtIO-blk are both valid
storage profiles for installer preflight and persistence checks; `storage
vmcheck` verifies first/last-sector reads and GPT/protective-MBR state without
writing. VirtIO-scsi remains diagnostic-only.

It writes runtime/staging state:

```text
/workspace/.orizon/install-report.txt
/workspace/.orizon/install-plan
/workspace/.orizon/install-state
/workspace/.orizon/installed
/workspace/.orizon/keyboard
/system/install-state
/system/hostname
/system/locale
/system/keyboard
/system/desktop.conf
/system/desktop-session.conf
/system/desktop-settings.conf
/system/desktop-binds.conf
/system/desktop-autostart.conf
/system/desktop-rules.conf
/system/desktop-monitors.conf
/system/desktop-layers.conf
/system/desktop-runtime.conf
/system/desktop-state.conf
/home/orizon/.config/hypr/orizon-hypr.conf
/logs/desktop-session.log
```

The desktop prompt is optional and disabled by default. It does not install the
real upstream Hyprland compositor yet; it installs Orizon's Hyprland-style
desktop profile, which can dispatch terminal clients with F1, kill the active
tiled client with F2, open the launcher with F3, toggle fullscreen with F4,
toggle pseudo tiling with F5, cycle focus with F6, switch workspaces with F7/F8,
and persist theme/wallpaper/layout/bar/focus settings in
`/system/desktop-session.conf`. System-wide desktop settings such as gaps,
border size, rounding, animations, shadows, terminal, launcher, keyboard, and
pointer policy are created in `/system/desktop-settings.conf` at the same time.
`desktop settings paths`, `desktop settings export`, and `desktop settings sync`
keep that central `/system` hub aligned with
`/home/orizon/.config/hypr/orizon-hypr.conf` and generated runtime hints.
`desktop config apply` also generates inspectable runtime hints for binds,
autostart, rules, monitors, layer rules, env/workspace/source intent,
animation/bezier hints and input/misc/layout hints under `/system`.
`desktop start|stop|restart|reload|recover` writes session-manager state to
`/system/desktop-state.conf` and logs lifecycle events in
`/logs/desktop-session.log`.
The model is closer to Hyprland dispatchers and automatic tiling than to a
mouse-drag window desktop. The same profile can be installed later through
packages:

```text
desktop package
pkg install orizon-desktop-hypr
pkg verify /workspace/packages/orizon-desktop-hypr.opkg
pkg simulate /workspace/packages/orizon-desktop-hypr.opkg
pkg install /workspace/packages/orizon-desktop-hypr.opkg
desktop start
desktop state
desktop session
desktop settings
desktop settings paths
desktop settings export
desktop settings sync
desktop modules
desktop settings presets
desktop settings doctor
desktop settings preset compact
desktop settings set gaps-in 10
desktop apps
desktop launch settings
desktop launch logs
desktop launch packages
desktop launch update
desktop profiles
desktop preset moss
desktop binds
desktop rules
desktop monitors
desktop runtime
desktop layers
desktop version
desktop devices
desktop keymap
desktop systeminfo
desktop layouts
desktop animations
desktop decorations
desktop render
desktop descriptions
desktop instances
desktop submap
desktop configerrors
desktop rollinglog
desktop focus-history
desktop keyword general:gaps_in 9
desktop dispatch exec terminal
desktop hyprctl clients
desktop hyprctl activewindow
desktop hyprctl focushistory
desktop hyprctl version
desktop hyprctl systeminfo
desktop hyprctl activeworkspace
desktop hyprctl layouts
desktop hyprctl animations
desktop hyprctl decorations
desktop hyprctl render
desktop hyprctl descriptions
desktop hyprctl instances
desktop hyprctl submap
desktop hyprctl keymap
desktop hyprctl cursorpos
desktop hyprctl devices
desktop hyprctl splash
desktop hyprctl configerrors
desktop hyprctl rollinglog
desktop hyprctl getoption general:gaps_in
desktop hyprctl keyword decoration:rounding 11
desktop keyword layerrule blur, launcher
desktop hyprctl getoption layerrule
desktop hyprctl keyword input:repeat_rate 40
desktop hyprctl getoption input:repeat_rate
desktop hyprctl reload
desktop dispatch fullscreen
desktop dispatch pseudo
desktop dispatch cyclenext
desktop dispatch swapwithmaster
desktop dispatch focusmaster
desktop dispatch togglesplit
desktop dispatch layoutmsg splitratio 60
desktop dispatch layoutmsg masterratio 65
desktop dispatch layoutmsg orientationleft
desktop dispatch resizeactive 5 0
desktop dispatch submap resize
desktop hyprctl submap reset
desktop dispatch movetoworkspace 2
desktop dispatch movetoworkspacesilent empty
desktop dispatch workspace +1
desktop dispatch workspace next
desktop dispatch workspace empty
desktop focus toggle
desktop autostart
desktop doctor
```

After the first installed boot, use the lifecycle commands before doing
updates:

```text
system status
system health
system snapshot
cat /workspace/.orizon/system-snapshot.txt
system backup
cat /workspace/.orizon/admin-backup.txt
system services
system logs
system doctor
system firstboot
hostname
firstboot done
```

`system status` confirms whether the VM is still a live ISO or an installed
boot, shows the persisted hostname, first-boot marker, required roots, and safe
next commands. `system health` gives a concise PASS/WARN summary.
`system snapshot` writes `/workspace/.orizon/system-snapshot.txt` with status, health,
services, firstboot, doctor, and log evidence. `system backup` writes
`/workspace/.orizon/admin-backup.txt` with non-secret configuration only and
explicitly excludes SSH private keys, update private keys, package payload
secrets, and disk data. `system services` shows the simple init/service
policy, and `system logs` shows `/system/boot-state`,
`/system/service-state`, `/logs/init.log`, and `/logs/service.log`.
`system doctor` audits roots/config/init state without changing disk layout.
`system init` can be rerun safely to refresh `/system/boot-state`,
`/system/service-state`, `/logs/init.log`, and `/logs/service.log`.
`system firstboot` shows the installed first-boot checklist; `firstboot done`
marks it reviewed after you confirm the VM state. `hostname set <name>`
updates `/system/hostname`. If a default file is missing, `system repair`
recreates only missing `/system`, `/home`, `/packages`, `/logs`, and
`/workspace/.orizon` defaults and writes
`/workspace/.orizon/rescue-report.txt`; it never partitions or installs.
`rescue` prints the non-destructive recovery checklist.

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

After a boot-payload update, Orizon writes a boot guard marker:

```text
/workspace/.orizon/boot-guard
/system/boot-guard
```

The next installed boot first arms a fallback Limine default to the rollback
entry, then auto-validates the updated kernel only once it reaches the console.
After validation Orizon restores the normal Limine default. If the refreshed
kernel fails after early Orizon boot but before the console, the next default
boot selects rollback. If the rollback Limine entry is selected while an update
is pending, Orizon treats that as a recovery boot and attempts to restore the
rollback payload as the main boot slot. Inspect it with:

```text
bootguard
bootguard confirm
bootguard recover
rollback-status
```

The bootguard status includes the validation counter, detected firmware type,
the EFI system table address when Limine provides it, and the current
pseudo-A/B status. `bootguard recover` manually arms the cached fallback Limine
config for the next reboot. This prepares the future NVRAM/`BootNext` path, but
current automatic rollback is still the Limine fallback-config path after
Orizon early boot.

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

The persistent data area now uses a small two-slot snapshot format when the
data partition has enough space. Orizon accepts either the Orizon data GUID or
the exact GPT partition name `orizon-data`, loads the newest valid slot by
sequence, keeps compatibility with the older single-slot v1 snapshot, and
exposes:

```text
persist status
persist slots
persist save
persist restore previous
persist repair
```

`persist restore previous` restores the newest valid non-active slot and
promotes it as the next snapshot. `persist repair` rewrites the current
in-memory roots to the next snapshot slot; neither command repartitions,
formats, or installs anything.

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
`install-status` to review the saved state, preflight report, and install log.

The selected keyboard layout is now applied by the kernel input layer. Current
layouts are `fr-azerty` and `us-qwerty`; accent keys are mapped to ASCII-safe
fallbacks until the console grows Unicode text support.

## Safety Boundary

The installer can now partition and install to AHCI/SATA disks or NVMe
namespaces with 512-byte LBAs. It can also create an installed dual-boot flow
when a prepared partition already exists beside Windows/Linux. It is still
intentionally narrow: no automatic NVRAM/BCD entry creation, no automatic
shrink/create of an Orizon data partition yet, and no firmware-level boot-count
recovery before the Orizon kernel starts.

## Next Kernel Layers

- Add automatic UEFI NVRAM or Windows BCD entry creation for dual boot.
- Add an in-OS partition create/resize assistant after the manual/prepared
  partition path is battle-tested.
- Add rollback-safe A/B system slots for full system images.
- Add full Unicode keyboard/text rendering for accented keys.
- Replace emulator poweroff fallback with full ACPI shutdown parsing.
