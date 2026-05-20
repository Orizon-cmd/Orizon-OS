# Orizon OS Command Quick Reference

This page groups the commands that matter most during validation. It is not a
complete shell manual; it is the checklist to avoid hunting through the longer
docs.

## Release And Build

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-iso
python scripts/orizon/orizon_update.py --mode zimaos-vm
python scripts/orizon/orizon_update.py --mode github-iso
git diff --check
python -m py_compile scripts/orizon/orizon_update.py scripts/orizon/build_x86_64_on_zimaos.py scripts/orizon/test_vm_matrix.py
```

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
install-status
boot-check
dualboot-check
repair-boot
bootguard
bootguard confirm
rollback-status
rollback
logs boot
logs update
```

Known limit: rollback is currently Limine/boot-count style after Orizon early
boot. True firmware `BootNext` through UEFI Runtime Services is not implemented
yet.

## Network And Update

```text
net
net status
net dhcp
net auto
net config show
net config ip <ip> gateway <gateway> dns <dns> [subnet <mask>]
route
dns raw.githubusercontent.com
ping 8.8.8.8
update
logs network
logs update
```

`update` is installed-disk only. Live ISO boot intentionally blocks it.

## SSH

```text
ssh password <password>
ssh start
ssh status
ssh audit
ssh auth
ssh auth max <attempts>
ssh auth lockout <seconds>
ssh lockout clear
ssh hostkey
ssh hostkey reload
ssh hostkey reset
ssh algorithms
logs ssh
```

Use SSH for ZimaOS/VM diagnostics and remote admin commands. Keep long soak and
multi-client tests separate from quick build checks.

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
hw
pci
pci bars
input
storage
disks
partitions
mounts
logs all
```

For Lenovo hardware work, capture command output rather than summarizing it from
memory. The useful files are `/logs/wifi.log`, `/logs/usb.log`,
`/logs/network.log`, `/logs/ssh.log`, and `/workspace/.orizon/update.log`.
