# Changelog

## Unreleased

- Added persistent root snapshot/restore tooling for `/workspace`, `/home`, `/system`, `/packages`, and `/logs`.
- Added installed-system lifecycle commands for live vs installed state, first boot, rescue, hostname, and repair.
- Extended the package manager with signed remote metadata, search/remote/status/history, dependency checks, and rollback-oriented install/remove paths.
- Improved the local console with scrollback, keyboard paging, command history, and a simple `less` pager.
- Added daily network diagnostics for DHCP renew, DNS, routes, TLS probe, and clearer update/pkg network errors.
- Hardened SSH diagnostics and security policy with host-key persistence, lockout/audit reporting, safer file access, and explicit signed-manifest posture.
- Added developer release guardrails: quick checks, release artifact validation, generated release notes, and CI log artifacts.
- Added a central status/limits page documenting VM-ready features, prepared hardware paths, known non-implemented work, and the no-real-hardware-validation boundary.
- Improved VM matrix reporting for bridge profiles by probing ARP/neigh IP discovery and marking unreachable bridge guests as boot/framebuffer-only instead of ambiguous SSH skips.
- Added a tracked-secret CI gate and release-notes preview artifact to the GitHub workflows.
