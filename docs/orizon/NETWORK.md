# Orizon Network

This page documents the current wired networking path used by `update`,
diagnostics and VM testing.

## Supported VM NICs

- Intel `e1000` / `e1000e`
- Realtek `RTL8139`
- VirtIO-net modern and legacy/transitional, used by QEMU and Proxmox bridge
  setups

## USB Ethernet Adapters

USB-to-Ethernet adapters are not PCI Ethernet cards, so they do not use the
same driver path as `e1000`, `RTL8139`, or VirtIO-net. Orizon records USB
network descriptors during xHCI/EHCI enumeration and exposes them through:

```text
usb
usb rescan
net status
hw
report
```

The first real packet path is active on xHCI for CDC-ECM style raw Ethernet
adapters and Realtek `RTL8152/RTL8153/RTL8155/RTL8156` devices. CDC-ECM uses
the adapter's bulk endpoints as raw Ethernet pipes. RTL815x uses the Realtek
vendor control registers to read the hardware MAC, disable RX aggregation for
simple frames, enable TX/RX, and add/remove the small Realtek TX/RX USB frame
descriptors before handing packets to the IPv4 stack.

Known diagnostic families also include ASIX `AX88xxx`, SMSC/LAN95xx, CDC-NCM,
and RNDIS-style adapters. These are identified by VID/PID/class today, but
packet-format support still needs a family-specific driver.

`usb` also prints xHCI/EHCI root-port diagnostics. If an adapter was plugged in
after boot, run `usb rescan` first. The useful cases are:

- `controllers=... selected=... cand0=...`: Orizon found multiple xHCI
  controllers and selected the best candidate by connected/root-port count.
- `usb-net present=yes ready=yes raw=yes`: the packet driver is active; run
  `net dhcp` or `net auto`.
- `usb-net present=yes ready=no`: the adapter was identified, but this family
  still needs a packet driver or setup stage.
- `usb-device ... hint=usb-hub`: the adapter is probably behind a USB hub or
  USB-C dock; Orizon needs hub downstream enumeration first.
- `xhci-ports ... conn ... usb-device count=0`: the root port sees something,
  but descriptor fetch still fails; capture the port line for driver work.

When the Lenovo has no built-in Ethernet port, run `usb rescan`, then `usb`.
If the status says `ready=yes raw=yes`, `net dhcp` can transmit through the USB
adapter. If it remains pending, capture the VID/PID and root-port line; that
tells us whether the next driver should be CDC-NCM, ASIX, SMSC/LAN95xx, RNDIS,
or USB hub downstream enumeration.

Orizon configures IPv4 with DHCP first, then falls back to a persistent static
configuration from `/system/network.conf` if DHCP is not available. NAT and
bridge are both valid. A bridge without DHCP can still reach GitHub if static
IP, gateway and DNS are configured.

## Proxmox Bridge Setup

Recommended VM hardware:

```text
Bridge: vmbr0
Model: VirtIO (paravirtualized)
Firewall: off while testing, unless your Proxmox rules allow DHCP/DNS/HTTPS
VLAN Tag: only if your LAN needs one
```

If a Proxmox host still exposes a VirtIO variant Orizon cannot initialize,
switch the VM network model to `Intel E1000`. Bridge mode will still work; only
the virtual card model changes.

Inside Orizon:

```text
net
net dhcp
net auto
net config ip 192.168.1.50 gateway 192.168.1.1 dns 192.168.1.1
ping 8.8.8.8
dns raw.githubusercontent.com
route
logs network
hw
update
```

`net` should show `driver=virtio-net` or `driver=intel-e1000`, `present=yes`,
`initialized=yes` and `link=up`. `net dhcp` requests an IPv4 lease without
running a full update. `net auto` tries DHCP, then static fallback. If the link
is up but DHCP fails, the next suspect is VLAN, gateway, DHCP server or firewall
on the LAN.

## Static IPv4

Use this when a VM is in bridge mode on a LAN without DHCP, or when you want a
stable address:

```text
net config ip <ip> gateway <gateway> dns <dns>
net config ip <ip> gateway <gateway> dns <dns> subnet <mask>
net config show
net config dhcp
```

The subnet defaults to `255.255.255.0` if it is omitted. `net config dhcp`
returns the machine to DHCP mode. The saved file is:

```text
/system/network.conf
```

Example file:

```text
mode static
ip 192.168.1.50
subnet 255.255.255.0
gateway 192.168.1.1
dns 192.168.1.1
```

Network events are appended to:

```text
/logs/network.log
```

Useful diagnostics:

```text
net status
route
dns raw.githubusercontent.com
ping 8.8.8.8
logs network
```

## Local Libvirt Bridge Example

The local provisioning script already supports bridge mode. Use an empty
`network_name`, set `bridge_device` to the host interface, and prefer
`network_model` `virtio` now that Orizon has a VirtIO-net driver.

Example: [config/vm/orizon-dev.bridge.example.json](../../config/vm/orizon-dev.bridge.example.json)

## VM Network Matrix

The ZimaOS lab can run a repeatable smoke matrix across libvirt NIC models:

```powershell
python scripts/orizon/build_x86_64_on_zimaos.py
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e,nat-virtio,nat-rtl8139
```

Each case provisions a dedicated VM/disk, boots the current remote `iso_root`,
runs `net dhcp`, starts SSH, then checks `status`, `net status`, `ping`, `dns`,
`pkg status`, `update status`, and `hostkey` through OpenSSH. Bridge cases are
available with `--cases all`, but host reachability depends on the lab bridge or
macvtap mode, so NAT cases are the default automated gate. The NAT gate covers
e1000e, VirtIO-net, and RTL8139.
