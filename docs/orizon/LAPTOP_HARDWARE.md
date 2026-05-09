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
- Touchpad: Not supported yet. This needs ACPI child-device enumeration,
  Intel LPSS/Synopsys DesignWare I2C, then HID-over-I2C event parsing.
- Touchscreen/stylus: Same I2C-HID stack as the touchpad, with Wacom input
  interpretation later.
- Wi-Fi: Not supported yet. Intel CNVi/iwlwifi is a large driver family, so use
  supported wired Ethernet in VMs or a future USB/Ethernet path for early
  internet tests on real laptops.
- Bluetooth, camera, audio, sensors, battery: Not supported yet.

## Useful Orizon Commands On Real Hardware

```text
sysinfo
report
hw
pci
pci bars
input
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

## Driver Plan For This Laptop

1. Keep boot stability first: timer source, keyboard, framebuffer, NVMe, and
   installer must work without fallback surprises.
2. Add better ACPI namespace walking to discover devices such as `ELAN0647` and
   `WCOM508E` from Orizon itself.
3. Add Intel LPSS/Synopsys DesignWare I2C MMIO support for PCI `8086:54e8` and
   `8086:54e9`.
4. Add HID-over-I2C descriptor/read support and convert basic relative/absolute
   pointer events into the compositor cursor.
5. Expand xHCI from a single boot keyboard path to multi-device HID, so external
   USB mice and adapters become easier to test.
6. Decide the real-network strategy for laptops: USB Ethernet first, then Wi-Fi
   only after the kernel has a stronger driver/runtime foundation.
