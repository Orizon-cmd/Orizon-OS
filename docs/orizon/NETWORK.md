# Orizon Network

This page documents the current wired networking path used by `update`,
diagnostics and VM testing.

## Supported VM NICs

- Intel `e1000` / `e1000e`
- Realtek `RTL8139`
- VirtIO-net modern and legacy/transitional, used by QEMU and Proxmox bridge
  setups

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
