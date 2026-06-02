# Orizon OS Command Quick Reference

This page groups the commands that matter most during validation. It is not a
complete shell manual; it is the checklist to avoid hunting through the longer
docs.

## Status And Limits

Before claiming a feature is complete, check [STATUS.md](STATUS.md). It is the
short source of truth for VM-ready features, prepared hardware paths, known
limits, and the rule that Lenovo/real-hardware validation needs fresh captures.
Use [RELEASE.md](RELEASE.md) for the artifact/CI checklist and
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) for VM/ZimaOS failure triage.

## Release And Build

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-iso
python scripts/orizon/orizon_update.py --mode zimaos-vm
python scripts/orizon/orizon_update.py --mode github-iso
python scripts/orizon/orizon_update.py --mode validate-release
python scripts/orizon/quick_check.py
python scripts/orizon/ci_release_guard.py --output-dir artifacts
python scripts/orizon/release_notes.py --output release_notes.md
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e --include-lifecycle
python scripts/orizon/test_update_rollback_vm.py
```

Human release notes live in `CHANGELOG.md`; generated CI/release previews come
from `scripts/orizon/release_notes.py` and `updates/x86_64/release.txt`.

`test_vm_matrix.py --cases all --include-lifecycle` runs NAT and bridge
profiles. NAT cases are SSH-validated. Bridge cases now try `virsh` ARP and
host neighbor-table discovery; if no IP is discoverable, they still perform a
boot/framebuffer smoke and report `WARN` instead of silently pretending that
SSH was tested. Per-case logs and `matrix-summary.{md,json}` are written under
`artifacts/vm-matrix/`.

`quick_check.py` runs `git diff --check`, Python syntax checks for all
`scripts/orizon/*.py`, the tracked-secret scan, PowerShell syntax checks when
PowerShell is available, and the strict release-artifact validator. Use
`--log artifacts/quick-check.log` when a CI or ZimaOS run should keep the
combined output.

`ci_release_guard.py` is the GitHub Actions entrypoint. It runs quick checks
without duplicating release validation, then runs the strict release validator,
generates release notes, and writes `artifacts/release-summary.md` plus
`artifacts/release-artifacts.json` so stale ISO/kernel/manifest/signature
failures are obvious in CI logs. It also writes `source-artifact-sync.{md,json}`
and fails CI when runtime source changes without refreshed release artifacts.

Expected release artifacts:

```text
Orizon-OS.iso
updates/x86_64/kernel.elf
updates/x86_64/BOOTX64.EFI
updates/x86_64/limine.conf
updates/x86_64/manifest.txt
updates/x86_64/manifest.sig
updates/x86_64/release.txt
```

## Boot, Install, And Rollback

```text
install
install-plan
install-status
system status
system health
system snapshot
cat /workspace/.orizon/system-snapshot.txt
system backup
cat /workspace/.orizon/admin-backup.txt
system init
system services
system logs
system doctor
system repair
rescue
system firstboot
firstboot done
hostname
hostname set orizon-vm
desktop status
desktop config
desktop config doctor
desktop config apply
desktop start
desktop stop
desktop restart
desktop reload
desktop recover
desktop rescue
desktop state
desktop session
desktop settings
desktop settings paths
desktop settings export
desktop settings sync
desktop settings presets
desktop settings doctor
desktop settings preset compact
desktop settings set gaps-in 10
desktop settings set border-size 3
desktop settings repair
desktop input
desktop input layout fr
desktop input layout us
desktop input pointer natural
desktop input focus toggle
desktop input submap launch
desktop input submap reset
desktop pointer
desktop keymap
desktop modules
desktop apps
desktop app settings
desktop apps launcher
desktop profiles
desktop preset moss
desktop binds
desktop rules
desktop monitors
desktop runtime
desktop layers
desktop version
desktop devices
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
desktop hyprctl version
desktop hyprctl systeminfo
desktop hyprctl activeworkspace
desktop hyprctl focushistory
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
desktop hyprctl keyword decoration:shadow:range 22
desktop hyprctl getoption decoration:shadow:range
desktop hyprctl keyword animations:tick_budget 24
desktop hyprctl getoption animations:tick_budget
desktop hyprctl getoption render:focus_ring
desktop hyprctl getoption render:profile
desktop keyword layerrule blur, launcher
desktop hyprctl getoption layerrule
desktop hyprctl keyword input:repeat_rate 40
desktop hyprctl getoption input:repeat_rate
desktop hyprctl reload
desktop hyprctl binds
desktop hyprctl layers
desktop dispatch exec terminal
desktop hyprctl clients
desktop hyprctl activewindow
desktop hyprctl monitors
desktop autostart
desktop autostart terminal off
desktop autostart terminal on
desktop dispatch fullscreen
desktop dispatch pseudo
desktop dispatch pin
desktop dispatch cyclenext
desktop dispatch swapnext
desktop dispatch movefocus r
desktop dispatch swapwindow l
desktop dispatch swapwithmaster
desktop dispatch focusmaster
desktop dispatch togglesplit
desktop dispatch layoutmsg splitratio 60
desktop dispatch layoutmsg splitratio +5
desktop dispatch layoutmsg masterratio 65
desktop dispatch layoutmsg mfact -5
desktop dispatch layoutmsg orientationleft
desktop dispatch layoutmsg orientationtop
desktop dispatch resizeactive 5 0
desktop dispatch submap resize
desktop hyprctl submap reset
desktop windows
desktop clients
desktop activewindow
desktop workspace
desktop workspace 2
desktop dispatch movetoworkspace 2
desktop dispatch movetoworkspacesilent empty
desktop dispatch workspace 2
desktop dispatch workspace previous
desktop dispatch workspace +1
desktop dispatch workspace next
desktop dispatch workspace empty
desktop workspace empty
desktop dispatch movefocus next
desktop shortcuts
desktop keymap
desktop doctor
desktop logs
desktop enable
desktop disable
desktop reset
desktop write-config
desktop theme moss
desktop wallpaper dawn
desktop layout master
desktop focus toggle
desktop bar toggle
desktop apply
desktop launcher show
desktop launch terminal
desktop launch settings
desktop launch logs
desktop launch packages
desktop launch update
desktop launch launcher
desktop killactive
desktop open terminal
desktop close terminal
desktop package
boot-check
dualboot-check
repair-boot
bootguard
bootguard confirm
bootguard recover
rollback-status
rollback
logs boot
logs update
```

Known limit: rollback is currently Limine/boot-count style after Orizon early
boot. `bootguard` and `update status` show the strategy, remaining attempts,
normal/fallback Limine config cache, `bootguard-recover`,
`pseudo-ab-slots: prepared=yes`, `nvram-bootnext: prepared=no`, and
`ab-slots: prepared=no`. `bootguard recover` arms the cached fallback Limine
config for the next boot. True firmware `BootNext` through UEFI Runtime
Services is not implemented yet.

`install-plan` is non-destructive. It writes
`/workspace/.orizon/install-report.txt` so VM/SSH checks can review the selected
disk, mode, write scope, confirmation string, and GPT snapshot before any
installer write happens.

`system status` is the fast installed/live lifecycle view. It prints
`boot-mode: live` or `boot-mode: installed`, hostname, first-boot state,
persistent roots, required `/system` files, init/log state, and safe next
commands. `system health` is the concise PASS/WARN operator view.
`system snapshot` writes `/workspace/.orizon/system-snapshot.txt` with status, health,
services, doctor, firstboot, and log evidence. `system backup` writes
`/workspace/.orizon/admin-backup.txt` with non-secret configuration only:
`os-release`, `machine-id`, hostname, fstab map, network policy, services,
rescue policy, admin guide, notes, and shell profile. SSH private keys, update
private keys, package payload secrets, and disk data are excluded. `system init`
reruns the idempotent boot tasks, writes
`/system/boot-state`, `/system/service-state`, `/logs/init.log`, and
`/logs/service.log`, and persists roots when possible.
`system services` shows the small service policy (`persistence`, `bootlog`,
`network`, `ssh`, `desktop`, `package-db`, `update-bootguard`, `firstboot`) without
pretending to be a full service manager. `system logs` prints the boot-state,
service-state, init log, and service log in one place. `system firstboot`
prints the first installed boot checklist before `firstboot done` marks it as
reviewed. `system doctor` audits roots/config/init state without writes.
`rescue` prints the non-destructive recovery checklist. `system repair`
recreates only missing defaults under `/workspace/.orizon`, `/home/orizon`,
`/system`, `/packages`, and `/logs`, including `/system/motd`,
`/system/fstab`, `/system/os-release`, `/system/machine-id`,
`/system/rescue.conf`, `/system/admin-guide.txt`,
`/system/admin-notes.txt`, `/home/orizon/.profile`, then writes
`/workspace/.orizon/rescue-report.txt`.
`hostname set <name>` persists `/system/hostname`.

`desktop` controls the optional first desktop profile. It is Hyprland-style,
not upstream Hyprland/Wayland yet. The live ISO keeps it disabled by default.
Use `desktop start` or `pkg install orizon-desktop-hypr` to start the
compositor desktop session; `desktop enable` remains a compatibility alias.
`desktop stop`, `desktop restart`, `desktop reload`, `desktop recover`,
`desktop rescue`, and `desktop state` manage `/system/desktop-state.conf` and
`/logs/desktop-session.log`. The state file now records health,
desired/runtime/policy coherence, live/install boot mode, lifecycle counters,
and recovery commands. `desktop rescue` is read-only and prints the safe
checklist before `desktop recover` repairs/reapplies anything. `desktop config doctor` validates the Hyprland-style
user config, and `desktop config apply` imports the supported subset into the
session/settings files plus generated runtime hint files such as
`/system/desktop-binds.conf`, `/system/desktop-autostart.conf`,
`/system/desktop-rules.conf`, `/system/desktop-monitors.conf`, and
`/system/desktop-layers.conf`, `/system/desktop-runtime.conf`, and
`/system/desktop-state.conf`.
`layerrule`, `bindm`/`bindl`, `bezier`, `animation`, and
input/device/decoration/cursor/render/debug/misc/layout hints are preserved
there until the real Wayland backend exists; mouse binds are parsed for
compatibility without enabling free-drag window moving by default.
`desktop settings` manages the system-wide
`/system/desktop-settings.conf` layer created by the installer/package and
stores compositor defaults such as gaps, border size, rounding, animations,
shadows, focus ring, shadow range, animation tick budget/curve, render profile,
terminal, launcher, keyboard, and pointer policy. `desktop settings
paths` shows the central `/system` settings hub, `desktop settings export`
rewrites `/home/orizon/.config/hypr/orizon-hypr.conf` from `/system`, and
`desktop settings sync` exports then refreshes runtime hints. `desktop settings
presets`, `desktop settings preset <name>`, and `desktop settings doctor` add a
safer profile/validation layer for that system file. `desktop session`
manages theme/wallpaper/bar/focus state, `desktop preset <name>` applies a saved
symbolic session, `desktop input` is the keyboard/pointer/focus hub for
`layout fr|us`, pointer profiles, focus-follows-mouse, and submaps,
`desktop pointer` shows cursor and HID mouse/tablet diagnostics,
`desktop layout <dwindle|master|monocle>` updates the
prepared layout profile, `desktop keymap` shows active F-key/submap keyboard
runtime and focus-follows-mouse counters, `desktop binds`, `desktop rules`, `desktop monitors`,
and `desktop runtime` show the generated Hyprland-style runtime files,
`desktop layers` shows the compositor layer model, `desktop version` identifies
the honest Orizon compatibility facade, `desktop devices` summarizes keyboard
and pointer inputs, `desktop systeminfo`, `desktop layouts`, `desktop
animations`, `desktop decorations`, `desktop descriptions`, `desktop instances`,
`desktop render`, `desktop submap`, `desktop configerrors`, `desktop rollinglog`, and
`desktop focus-history` expose more Hyprland-like inspection surfaces,
`desktop keyword <key> <value>` applies a single
Hyprland-style runtime keyword, `desktop dispatch
<dispatcher> [args]` runs `exec`, `killactive`,
`workspace`, `movetoworkspace`, `movetoworkspacesilent`, `movefocus`, `cyclenext`, `swapnext`,
`swapwindow`, `focusmaster`, `swapwithmaster`, `fullscreen`, `pseudo`, `pin`,
`togglesplit`, `layoutmsg`, `resizeactive`, and `submap`,
including `layoutmsg splitratio <10-90|+/-n>`, `masterratio`/`mfact`, and
explicit `orientationleft/right/top/bottom` tiling hints,
`desktop hyprctl ...` exposes
a small Hyprland-like facade for version/systeminfo/clients/workspaces/activeworkspace/monitors/activewindow/focushistory/binds/keymap/layers/layouts/animations/decorations/render/descriptions/instances/submap/devices/cursorpos/splash/configerrors/rollinglog/getoption/keyword/dispatch/reload,
`desktop profiles` lists available symbolic profiles, `desktop autostart` controls startup apps,
`desktop apps` lists compositor-managed app entries, `desktop app <id>` shows
class/module/surface details, and `desktop launch
terminal|settings|logs|packages|update|launcher` opens the first native apps as
tiled clients or toggles the launcher overlay. `desktop windows`/`desktop clients`
list tiled clients with stable addresses, geometry and `focusHistoryID`,
`desktop activewindow` mirrors the focused client state, and F1/F2/F3/F4/F5/F6/F7/F8 map to exec
terminal/killactive/launcher/fullscreen/pseudo/focus/workspace navigation.
F9/F10/F11 enter resize/move/launch submaps and F12/Esc returns to default,
while `desktop modules` shows the prepared package split map for
`orizon-desktop-core`, `orizon-terminal`, `orizon-settings`, `orizon-launcher`,
and future `orizon-waybar`, without enabling manual window dragging. See
[DESKTOP.md](DESKTOP.md).

Useful render tuning commands stay Hyprland-style while remaining VM-safe:
`desktop settings set focus-ring yes|no`, `desktop settings set render-profile
balanced|performance|cozy`, `desktop keyword decoration:shadow:range <0-32>`,
and `desktop keyword animations:tick_budget <4-60>`. Inspect them with
`desktop render`, `desktop decorations`, `desktop animations`, or
`desktop hyprctl getoption render:focus_ring|render:profile|decoration:shadow:range|animations:tick_budget`.

## Network And Update

```text
net
net status
net dhcp
net auto
net check
net daily
net renew
net tcp raw.githubusercontent.com 443
net tcp raw.githubusercontent.com 443 attempts 2
net tls
net diag
net config show
net config ip <ip> gateway <gateway> dns <dns> [subnet <mask>]
route
dns raw.githubusercontent.com
ping 8.8.8.8
update status
update
logs storage
logs pci
logs network
logs update
```

`net check` is safe over SSH and summarizes link, IPv4, route, gateway, DNS,
and the next TCP/TLS probes with PASS/WARN/FAIL. `net daily` adds the VM
workflow, retry policy, config/log paths, and the honest NAT/bridge boundary.
`net tcp <host> [port] [attempts <1-5>]` is the cheap reachability check before
update/pkg work; it retries by default and separates DNS/TCP/firewall issues
from TLS/root-trust issues. `net renew` reapplies saved DHCP/static config from
the local console with a retry. `net tls` runs the heavier GitHub
HTTPS/root-trust probe, and `net diag` chains daily + check + TCP + TLS for a
fuller VM report. `update` is installed-disk only. Live ISO boot intentionally
blocks it.

## Packages

```text
pkg status
pkg list
pkg audit
pkg doctor
pkg cache
pkg search orizon
pkg search desktop
pkg remote
pkg remote verify
pkg upgrade plan
pkg update
pkg upgrade
pkg info <name>
pkg info orizon-desktop-hypr
pkg history
pkg sample
pkg sample desktop
pkg hash /workspace/packages/orizon-hello.opkg
pkg verify /workspace/packages/orizon-hello.opkg
pkg simulate /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
pkg install orizon-desktop-hypr
pkg remove orizon-hello
pkg rollback orizon-hello
```

`pkg update`, `pkg upgrade`, `pkg install`, `pkg remove`, and `pkg rollback`
are installed-disk only. `pkg sample`, `pkg hash`, `pkg verify`, `pkg search`,
`pkg audit`, `pkg doctor`, `pkg cache`, `pkg simulate`, `pkg remote`,
`pkg remote verify`, and `pkg upgrade plan` are safe in the live ISO and over
SSH. The remote package index is authenticated through the signed system
manifest, package repository commit pin, and package-index SHA-256 pin.
Detached package repo signatures are prepared through
`/workspace/.orizon/package-index.sig`; missing sidecars are reported as WARN
and still fall back to the signed manifest pin.

`pkg sample desktop` writes `/workspace/packages/orizon-desktop-hypr.opkg`,
the optional desktop package. It installs `/system/desktop.conf`,
`/system/desktop-session.conf`, `/system/desktop-settings.conf`, generated
desktop runtime hint files, and the Hyprland-style user config under
`/home/orizon/.config/hypr/`.
It also writes `/system/desktop-modules.conf`, a non-invasive module map for
the future split packages. `pkg info orizon-terminal` and `pkg search waybar`
report those prepared modules, but `orizon-waybar` is not installed now.
`desktop settings sync` is the preferred repair command if `/system` settings
and the user Hyprland-style config drift apart.
On an installed VM, `pkg install orizon-desktop-hypr` generates and installs
that package by name, `pkg remove orizon-desktop-hypr` disables it, and
`pkg rollback orizon-desktop-hypr` restores the last removed snapshot.

## SSH

```text
ssh password <password>
ssh start
ssh status
ssh audit
ssh sessions
security
security policy
security audit
security keys
security doctor
security rotate ssh-hostkey
net check
net daily
net tcp raw.githubusercontent.com 443
net tcp raw.githubusercontent.com 443 attempts 2
net tls
ssh auth
ssh auth max <attempts>
ssh auth lockout <seconds>
ssh lockout clear
ssh hostkey
ssh hostkey reload
ssh hostkey reset
ssh algorithms
logs ssh
logs security
hw next
report next
report save
cat /workspace/hardware-report.txt
head /workspace/hardware-report.txt
tail /workspace/hardware-report.txt
selftest ssh
system status
system repair
rescue
hostname
hostname set orizon-vm
reboot
shutdown
```

Use SSH for ZimaOS/VM diagnostics and remote admin commands. Keep long soak and
multi-client tests separate from quick build checks. Unknown remote `exec`
commands return a non-zero status so automation can fail fast.

`security` summarizes base hardening: SSH auth/lockout, persistent host key,
VFS policy v2, signed manifest requirement, package index pinning, policy-deny
counters and known limits. It refreshes `/system/security-policy`,
`/system/security-state`, and `/workspace/.orizon/security-doctor.txt` as
non-secret operator evidence. `security policy` expands the active rules,
`security audit` shows the persistent security mirror plus SSH audit and denial
counters, `security keys` reports key rotation posture without dumping private
material, and `security doctor` gives a non-destructive PASS/WARN summary.
`security rotate ssh-hostkey` regenerates the local SSH host identity for future
sessions and may require clearing the client known_hosts entry. Update/package
trust-root rotation is reported as `release-required`. Generic SSH file writes
are limited to `/workspace`, `/home`, `/logs` and `/packages`, while
`/workspace/.orizon` remains internal OS state and remote root deletion is
blocked. Sensitive files such as `/system/ssh.conf`,
`/system/ssh_host_rsa.key`, `.env`, `.key`, `.pem`, `.ssh`, private, secret,
token and credential paths are not readable through `cat/head/tail`. SSH audit
redacts `ssh password`, generic write/append payloads, and Wi-Fi credentials
before mirroring events to `/logs/security.log`.

## Security

```text
security
security policy
security audit
security keys
security doctor
security rotate ssh-hostkey
ssh auth
ssh audit
ssh hostkey
logs security
update status
```

See [SECURITY.md](SECURITY.md) for the current implemented policy, generated
state files, and limits.

## USB Ethernet

```text
usb
usb rescan
logs usb
net status
net dhcp
```

Supported packet paths today: xHCI CDC-ECM raw Ethernet and Realtek RTL815x.
CDC-NCM, ASIX, SMSC/LAN95xx, RNDIS, hubs, and missing endpoints are diagnostic
families until a specific driver path is added and validated.

## Intel AX201 / CNVi Wi-Fi

```text
wifi
wifi firmware
wifi apm
wifi boot arm
wifi alive
wifi queues arm
wifi context arm
wifi scheduler arm
wifi rx poll
wifi nvm arm
wifi nvm-info arm
wifi bringup
wifi crypto
wifi scan arm
wifi scan poll
wifi connect <ssid> [password]
wifi join <ssid> [password]
wifi validate <ssid> [password]
wifi online <ssid> [password]
wifi update <ssid> [password]
wifi wpa
wifi key pairwise arm
wifi key gtk arm
wifi data
logs wifi
```

Known limit: the WPA2/CCMP path is prepared for the Lenovo AX201 but still needs
real AP validation on the user's Lenovo before it can be called hardware-proven.

## Hardware Report

```text
sysinfo
report
report next
report save
selftest
selftest network
selftest storage
selftest crypto
selftest ssh
selftest update
hw
hw next
pci
pci bars
logs pci
input
storage
disks
storage diag
storage vmcheck
logs storage
disk identify
disk read-test
disk read-test last
gpt scan
partitions
mounts
persist status
persist slots
persist save
persist repair
persist restore previous
persist restore slot 0
system status
system services
system doctor
rescue
system repair
logs all
```

For future hardware work, capture command output rather than summarizing it from
memory. Start with `report next` to see the safe capture plan, then run
`report save` and copy `/workspace/hardware-report.txt`.
If the internal disk is missing, run `storage diag`, `storage vmcheck`,
`logs storage`, `logs pci`, `pci bars`, `disk identify`, `disk read-test last`,
and `gpt scan`; they are read-only and report NVMe/AHCI candidates, Intel
RST/VMD blockers, secondary PCI bus hints, modern/legacy VirtIO-blk state,
VirtIO-scsi diagnostic-only status, NVMe CAP/CC/CSTS/admin errors,
first/last-sector readability, GPT/protective-MBR state, and SDHCI/eMMC cases
without installing or writing to disk. The useful files are
`/workspace/hardware-report.txt`, `/logs/wifi.log`, `/logs/usb.log`,
`/logs/network.log`, `/logs/ssh.log`, `/logs/security.log`, and
`/workspace/.orizon/update.log`.

`persist status` affiche l'etat detecte des racines data, le LBA de la
partition Orizon detectee par GUID Orizon ou nom GPT exact `orizon-data`, le
nombre de slots disponibles, le slot actif, la sequence du dernier snapshot, le
nombre d'entrees et le mode `persistent` ou `memory`.
`persist slots` liste les snapshots lisibles avec version, sequence, payload et
checksum. `persist save` force une sauvegarde des racines `/workspace`, `/home`,
`/system`, `/packages` et `/logs`. `persist restore previous` restaure le slot
valide non actif le plus recent puis le promeut comme nouveau snapshot; `persist
restore slot <n>` fait la meme chose pour un slot precis. `persist repair`
reecrit un snapshot propre depuis l'etat VFS courant; ces commandes restent non
destructives pour le partitionnement et ne servent pas a installer l'OS.
`system status` et `rescue` sont inclus dans `report save` pour documenter
l'etat live/installe, le hostname et les commandes de recuperation sans devoir
lire l'ecran local.

## Shell Helpers

```text
help shell
shell status
tail [-n] <file>
wc <file>
grep [-i] [-v] [-n] text <file>
history grep text
cmd1 ; cmd2
cmd > /workspace/out.txt
cmd >> /workspace/out.txt
cmd | grep [-i] [-v] [-n] text
cmd | head -20
cmd | tail -20
cmd | wc
cmd | tee [-a] /workspace/out.txt
cmd | less
```

The local framebuffer shell now has a small diagnostic command layer above the
classic command dispatcher. It can run simple grouped commands with `;`, write
or append captured command output with `>` and `>>`, and pass captured output
through lightweight pipe stages: `grep`, `head`, `tail`, `wc`, `tee`, `cat`,
`less`/`more`. `grep` accepts `-i` for case-insensitive search, `-v` for
inverted matches, and `-n` for line numbers. `shell status` prints the local
console buffers/capabilities, and `history grep <text>` searches saved command
history.
This is intentionally not a full POSIX shell yet: no quoting, variables,
background jobs, or conditional exit-code logic. Interactive commands such as
`less`, `edit`, `install`, `reboot`, and `shutdown` are blocked as pipe or
redirection sources.

## Local Console Scrolling

When a command is longer than the visible framebuffer console, use `z` on an
empty prompt to scroll up and `s` to scroll back down. Uppercase `Z` and `S`
move by a larger half-screen step. The shortcut only steals `s` while the view
is already scrolled, so commands such as `storage`, `selftest`, and `ssh` still
type normally at the bottom prompt.

For files and reports, use the local pager:

```text
less /workspace/hardware-report.txt
less /logs/boot.log
```

Inside `less`, `z` or the up arrow moves one visual line up, `s` or the down
arrow moves one visual line down, `Z`/`S` or space move by a page, `g` and `G`
jump to the top/end, and `q` returns to the shell prompt. The pager is local to
the framebuffer console; SSH should keep using `cat`, `head`, and `tail`.
