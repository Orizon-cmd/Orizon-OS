# Changelog

## Unreleased

- Added persistent root snapshot/restore tooling for `/workspace`, `/home`, `/system`, `/packages`, and `/logs`.
- Added installed-system lifecycle commands for live vs installed state, first boot, rescue, hostname, and repair.
- Added installed-system UX v2 with service-state/logs, firstboot checklist, MOTD/fstab/rescue/admin defaults, and `system logs`.
- Extended the package manager with signed remote metadata, search/remote/status/history, dependency checks, and rollback-oriented install/remove paths.
- Improved the local console with scrollback, keyboard paging, command history, and a simple `less` pager.
- Added daily network diagnostics for DHCP renew, DNS, routes, TLS probe, and clearer update/pkg network errors.
- Hardened SSH diagnostics and security policy with host-key persistence, lockout/audit reporting, safer file access, and explicit signed-manifest posture.
- Added developer release guardrails: quick checks, release artifact validation, generated release notes, and CI log artifacts.
- Added a CI release guard entrypoint that centralizes quick checks, secret scan, release validation, release notes, and artifact synchronization summaries for GitHub Actions.
- Added a central status/limits page documenting VM-ready features, prepared hardware paths, known non-implemented work, and the no-real-hardware-validation boundary.
- Improved VM matrix reporting for bridge profiles by probing ARP/neigh IP discovery and marking unreachable bridge guests as boot/framebuffer-only instead of ambiguous SSH skips.
- Added a tracked-secret CI gate and release-notes preview artifact to the GitHub workflows.
- Clarified update rollback strategy/status with explicit Limine boot-count scope, BootNext/A-B not-prepared markers, and rollback metadata.
- Added `bootguard recover`, richer `rollback-status`, and explicit pseudo-A/B metadata for Limine main/rollback recovery.
- Fixed installed-state detection so the package database at `/system/installed` no longer disables the live installer.
- Added `storage vmcheck` / `storage repair` read-only VM storage verification across detected disks with GPT and first/last-sector probes.
- Added modern VirtIO-blk VM storage support, VirtIO disk smoke coverage, and clearer storage diagnostics for VirtIO-blk vs VirtIO-scsi.
- Added a small installed/live init layer with `system init`, `system services`, `system doctor`, `/system/boot-state`, and `/logs/init.log`.
