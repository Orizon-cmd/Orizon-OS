# Orizon OS Roadmap

## Current Foundation

- Installed-disk boot flow with live ISO guardrails.
- Persistent Orizon data roots: `/workspace`, `/home`, `/system`,
  `/packages`, `/logs`, and `/tmp`.
- In-kernel GitHub update path with SHA-256 verification, boot rollback slot,
  streamed progress, and rough elapsed timings.
- Minimal package manager with `pkg list`, `pkg status`, `pkg info`,
  `pkg sample`, `pkg hash`, `pkg install`, and `pkg remove`.
- Console basics: scrollback, persistent history, simple autocomplete, editor,
  `sysinfo`, `hw`, `mounts`, `logs`, `report`, `ps`, and `uptime`.
- Staged remote-management base: `ssh start/status/algorithms/stop`, TCP/22
  listener, SSH banner, server/client `KEXINIT` diagnostics, algorithm
  negotiation report, `/system/ssh.conf`, and `/logs/ssh.log`.
- Hardware base: PS/2 and USB HID keyboard input, AHCI/NVMe storage probes,
  Intel e1000/e1000e, RTL8139, VirtIO-net Ethernet, and staged Intel Wi-Fi
  detection, firmware discovery, APM wake, CPU-release firmware loading, FH DMA
  upload staging, alive polling diagnostics, and host-side command/RX/TX queue
  memory staging. The Intel Wi-Fi WPA2 path can now derive PMK/PTK, prepare
  M2/M4, unwrap M3 key data, extract GTK, and stage pairwise/group SEC_KEY
  installs behind strict firmware ACK checks. It also has an AES-CCM self-test
  and a software-encrypted CCMP Ethernet data path that the IPv4 stack can use
  for ARP/DHCP/IPv4 once WPA2 is guarded-ready. `wifi join` now orchestrates
  the bringup/scan/connect/WPA sequence with concise progress output.

## Next Stability Track

1. Add a boot-count guard so a failed updated boot can automatically select or
   restore the rollback slot.
2. Add package rollback metadata before package updates overwrite files.
3. Make the package repository signed, not only SHA-256 verified through the
   public manifest/index.
4. Expand network diagnostics with per-phase DNS/TCP/TLS counters and clearer
   bridge/DHCP failure messages.
5. Finish SSH transport: key exchange, encrypted packets, authentication,
   session channel, and Orizon pseudo-console.

## Next Hardware Track

1. Make the Lenovo 500w Yoga Gen 4 a concrete real-laptop target: boot,
   keyboard, NVMe, diagnostics, then I2C-HID touchpad.
2. Improve USB HID keyboard coverage for non-US layouts and laptop keypads.
3. Harden LAPIC timer calibration and add x2APIC support for newer firmware
   modes.
4. Expand the new Intel LPSS/Synopsys DesignWare I2C-HID probe into a full HID
   report parser for ELAN/Wacom multitouch and stylus events.
5. Build Intel CNVi Wi-Fi properly: validate WPA2 M3/M4 and GTK/group-key
   handling on the Lenovo, test DHCP over the new CCMP L2 bridge, then harden
   protected RX/retry diagnostics against real AP behaviour.
6. Harden NVMe and AHCI writes with more error reporting and timeout handling.
7. Add more VirtIO devices used by Proxmox/QEMU, especially block storage.
8. Build a repeatable VM test matrix: NAT, bridge, AHCI, NVMe, VirtIO-net, and
   at least one non-ZimaOS host.

## Next Userland Track

1. Split more features into packages so update can refresh components without
   replacing the whole kernel payload.
2. Add `pkg upgrade` once package rollback exists.
3. Add a small service/init registry for boot tasks that should not live in the
   terminal command path.
4. Improve the editor with save confirmation, file size warnings, and simple
   search.
