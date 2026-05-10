# Orizon OS Laptop Hardware Target

This file tracks the first real laptop target tested for Orizon OS. It is a
development target, not a ZimaOS-only assumption.

## Lenovo 500w Yoga Gen 4

- Model: `LENOVO 82VR`, product version `Lenovo 500w Yoga Gen 4`
- CPU: Intel N100, 4 cores, x86_64, APIC/x2APIC capable
- Storage: Micron 2450 NVMe SSD, PCI `1344:5411`
- Display: Intel Alder Lake-N UHD Graphics, PCI `8086:46d1`
- USB: Intel Alder Lake-N xHCI controllers, PCI `8086:464e` and `8086:54ed`
- Wi-Fi: Intel Alder Lake-N CNVi, PCI `8086:54f0`, Linux driver `iwlwifi`
  - Verified from the live laptop over SSH on 2026-05-09.
  - Linux identifies it as `Intel(R) Wi-Fi 6 AX201 160MHz`.
  - PCI subsystem/revision: `8086:0074`, sysfs revision `0x00`,
    Linux dmesg revision `0x370`.
  - Firmware in use on Linux: `89.123cf747.0 so-a0-hr-b0-89.ucode`.
- Bluetooth: Intel AX201 Bluetooth, USB `8087:0026`
- Built-in keyboard: AT translated Set 2 keyboard through i8042/PS/2
- Touchpad: ELAN I2C-HID `ELAN0647:00 04F3:31B2`
- Touchscreen/stylus: Wacom I2C-HID `WCOM508E:00 056A:535D`
- Main I2C buses: Intel LPSS I2C controllers `8086:54e8` and `8086:54e9`

## Current Orizon Support

- Boot: UEFI boot reaches the Orizon shell after the LAPIC/polling splash
  fallback work.
- CPU/timer: Local APIC timer is the intended path. If the footer still says
  `timer irq fallback active`, keep `sysinfo` and `report` output for the next
  timer pass.
- Storage: NVMe is expected to be the correct disk path for this laptop.
- Keyboard: The internal keyboard should work through the existing PS/2 path.
- Display: Orizon uses the boot framebuffer only; there is no Intel graphics
  modesetting driver yet.
- Touchpad: first Intel LPSS/Synopsys DesignWare I2C probe is present for
  `8086:54e9` at I2C address `0x15`. Orizon can now try HID-over-I2C descriptor
  reads and report polling. Full multitouch/absolute-coordinate parsing is the
  next driver step.
- Touchscreen/stylus: first I2C-HID probe is present for `8086:54e8` at I2C
  address `0x0a`. Wacom pen/finger interpretation still needs a HID report
  parser.
- Wi-Fi: Stage-0 detection and Stage-1 firmware staging are present. Orizon can
  detect the Intel CNVi controller (`8086:54f0`), validate embedded or module
  `iwlwifi-*.ucode` firmware, wake the NIC APM path with `wifi apm`, run the
  PRPH CPU-release plus CPU1/CPU2 FH DMA load sequence with `wifi boot arm`,
  poll `CSR_INT` for the firmware `ALIVE` bit with `wifi alive`, and stage
  host-side command/RX/TX rings with `wifi queues arm`. `wifi bringup` now runs
  the full readiness sequence through `wifi nvm-info arm` and reports the first
  failed stage. `wifi scan` now builds a passive channel plan from NVM data and
  `wifi scan arm` sends the first experimental UMAC scan request. `wifi scan poll`
  parses UMAC scan-start, iteration-complete, and complete notifications
  so we can confirm firmware scan progress on the Lenovo. It also stores the
  first per-channel scan telemetry rows: channel, band, probe status, skipped
  probes, and dwell duration. Orizon now also watches `REPLY_RX_MPDU` packets
  during scan polling and records the first SSID/BSSID/channel rows from
  beacon/probe-response management frames, including capability/security hints
  for open, WEP, WPA vendor IE, and WPA2/RSN APs. `wifi scan poll` includes an
  `mpdu-debug` block with raw bytes and candidate frame offsets when no AP is
  parsed yet, which is the key feedback loop for fixing real-hardware RX
  layouts. `wifi connect <ssid> [password]` now matches a scanned AP, refuses
  unsupported WEP/WPA1 paths, requires a password for WPA2/RSN, and prepares
  open-system authentication plus association request frames. For WPA2 it also
  derives the 32-byte PMK with PBKDF2-HMAC-SHA1 and only reports a checksum,
  never the key itself. It accepts 8-63 character passphrases and 64-character
  hexadecimal PSKs. `wifi crypto` runs built-in SHA-1, PBKDF2, AES key unwrap,
  and AES-CCM known vector checks. The RX path already recognizes
  authentication and association response
  frames that match the prepared local STA/BSSID pair and records status
  codes/AID for diagnostics. `wifi tx [auth|assoc|m2|m4|data|all]` can now
  copy the prepared management frames into TX DMA buffers, fill the TX
  TFD/byte-count slot, and report the planned doorbell value without writing it.
  WPA2 EAPOL
  RX diagnostics now recognize AP key frames, capture ANonce, derive diagnostic
  PTK material, prepare an inspectable M2 response, parse M3, unwrap encrypted
  key data with the PTK KEK, extract GTK material, and prepare M4 through
  `wifi wpa`. `wifi tx m2` and `wifi tx m4` stage those responses in DMA.
  `wifi txcmd [auth|assoc|m2|m4|data]` builds a separate Intel `TX_CMD` v10
  diagnostic envelope and can queue it with strict ACK matching. `wifi bind`
  now builds diagnostic Intel `MAC_CONFIG`, `LINK_CONFIG`, and `STA_CONFIG`
  command buffers for the selected AP, records the MAC/link/STA IDs and
  checksums. `wifi bind arm` then queues those commands one by one and requires
  a matching firmware ACK for MAC before LINK, then LINK before STA. After the
  STA ACK, later `wifi txcmd` diagnostics report `bound=acked` with the planned
  AP station ID. `wifi txcmd <auth|assoc|m2> arm` now stages that `TX_CMD` into
  the command queue and requires a matching firmware response before marking
  the transmit path ACKed. Association state now requires both firmware TX ACKs,
  the STA binding ACK, and the matching AP authentication/association responses
  before Orizon reports `confirmed`. `wifi join <ssid> [password]` now runs the
  bringup, scan, association, binding, WPA M1/M2/M3/M4, and key-install sequence
  with short progress lines. `wifi key pairwise [arm]` now builds a guarded Intel data-path
  `SEC_KEY_CMD` plan for the WPA2 pairwise CCMP TK and can queue it only after
  association, STA binding, and M2 TX are ACKed. `wifi key gtk [arm]` builds the
  group CCMP key command after M3/GTK extraction. The WPA guarded-ready state
  now requires pairwise key ACK, GTK key ACK, and M4 TX ACK. `wifi data` builds
  a first software-encrypted CCMP protected data frame using the pairwise TK,
  and `wifi tx data` / `wifi txcmd data arm` target that frame through the
  staged TX path. The IPv4 stack can now use that guarded Wi-Fi link as an L2
  backend for ARP, DHCP, IPv4, and GitHub update probes; `net status` reports
  `link=wifi` when this path is selected. Real hardware validation still needs
  a full AP test for DHCP replies and protected CCMP RX traces.
- Bluetooth, camera, audio, sensors, battery: Not supported yet.

## Useful Orizon Commands On Real Hardware

```text
sysinfo
report
hw
pci
pci bars
input
wifi
wifi firmware
wifi apm
wifi boot arm
wifi alive
wifi queues
wifi queues arm
wifi context arm
wifi scheduler arm
wifi rx poll
wifi nvm arm
wifi nvm-info arm
wifi bringup
wifi crypto
wifi scan
wifi scan arm
wifi scan poll
wifi connect <ssid> [password]
wifi join <ssid> [password]
wifi wpa
wifi key
wifi key pairwise arm
wifi key gtk
wifi key gtk arm
wifi data
wifi bind
wifi bind arm
wifi txcmd auth
wifi txcmd auth arm
wifi tx all
wifi tx m2
wifi tx m4
wifi tx data
wifi txcmd assoc
wifi txcmd assoc arm
wifi txcmd m2
wifi txcmd m2 arm
wifi txcmd m4
wifi txcmd m4 arm
wifi txcmd data
wifi txcmd data arm
storage
disks
```

`pci` lists every PCI device Orizon can see and adds a rough driver hint. `input`
shows PS/2, USB HID, and I2C/SMBus candidates so we can verify whether the
touchpad path is visible from Orizon.

## Repeatable Linux Inventory

The laptop is reachable from the ZimaOS hotspot. Copy the template below to a
local ignored env file, fill the password locally, then run the inventory script:

```powershell
Copy-Item config/hosts/laptop-hotspot.env.example config/hosts/laptop-hotspot.local.env
python scripts/orizon/inventory_laptop_hotspot.py
```

The local file is ignored by Git through `config/hosts/*.local.env`.

To import the Intel Wi-Fi firmware from the laptop Linux install into the local
ISO tree, use:

```powershell
python scripts/orizon/import_intel_wifi_firmware.py
python scripts/orizon/orizon_update.py --mode zimaos-vm
```

The firmware lands in `orizon-os-x86_64/firmware/`, which is ignored by Git.
The Makefile copies it into the ISO and exposes it to the kernel as a Limine
module.

## Driver Plan For This Laptop

1. Keep boot stability first: timer source, keyboard, framebuffer, NVMe, and
   installer must work without fallback surprises.
2. Add ACPI namespace walking so Orizon discovers devices such as `ELAN0647`
   and `WCOM508E` dynamically instead of relying on the first Lenovo target
   table.
3. Harden Intel LPSS/Synopsys DesignWare I2C with IRQ support, reset/power
   commands, and clearer error counters.
4. Add a real HID report descriptor parser for multitouch absolute coordinates,
   pen/finger events, and click zones.
5. Expand xHCI from a single boot keyboard path to multi-device HID, so external
   USB mice and adapters become easier to test.
6. Grow the Intel Wi-Fi path in order: validate WPA2 M3/M4 plus GTK install on
   real APs, replace the CCMP diagnostic data frame with real ARP/DHCP/IPv4
   payloads, then harden protected RX.
