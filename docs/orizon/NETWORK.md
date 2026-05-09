# Orizon Network

This page documents the current wired networking path used by `update`,
diagnostics and VM testing.

## Supported VM NICs

- Intel `e1000` / `e1000e`
- Realtek `RTL8139`
- VirtIO-net modern and legacy/transitional, used by QEMU and Proxmox bridge
  setups

Orizon currently configures IPv4 through DHCP. NAT and bridge are both valid,
but the network behind the VM must provide DHCP for internet access.

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
hw
update
```

`net` should show `driver=virtio-net` or `driver=intel-e1000`, `present=yes`,
`initialized=yes` and `link=up`. `net dhcp` requests an IPv4 lease without
running a full update. If the link is up but DHCP fails, the next suspect is
VLAN, gateway, DHCP server or firewall on the LAN.

## Local Libvirt Bridge Example

The local provisioning script already supports bridge mode. Use an empty
`network_name`, set `bridge_device` to the host interface, and prefer
`network_model` `virtio` now that Orizon has a VirtIO-net driver.

Example: [config/vm/orizon-dev.bridge.example.json](../../config/vm/orizon-dev.bridge.example.json)
