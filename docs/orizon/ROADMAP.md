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
- Hardware base: PS/2 and USB HID keyboard input, AHCI/NVMe storage probes,
  Intel e1000/e1000e, RTL8139, and VirtIO-net Ethernet.

## Next Stability Track

1. Add a boot-count guard so a failed updated boot can automatically select or
   restore the rollback slot.
2. Add package rollback metadata before package updates overwrite files.
3. Make the package repository signed, not only SHA-256 verified through the
   public manifest/index.
4. Expand network diagnostics with per-phase DNS/TCP/TLS counters and clearer
   bridge/DHCP failure messages.

## Next Hardware Track

1. Improve USB HID keyboard coverage for non-US layouts and laptop keypads.
2. Add APIC/LAPIC timer support so real laptops do not need the PIT/PIC polling
   fallback.
3. Harden NVMe and AHCI writes with more error reporting and timeout handling.
4. Add more VirtIO devices used by Proxmox/QEMU, especially block storage.
5. Build a repeatable VM test matrix: NAT, bridge, AHCI, NVMe, VirtIO-net, and
   at least one non-ZimaOS host.

## Next Userland Track

1. Split more features into packages so update can refresh components without
   replacing the whole kernel payload.
2. Add `pkg upgrade` once package rollback exists.
3. Add a small service/init registry for boot tasks that should not live in the
   terminal command path.
4. Improve the editor with save confirmation, file size warnings, and simple
   search.
