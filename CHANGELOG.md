# Changelog

## Unreleased

- Aligned the documentation/release map after the VM/ZimaOS stabilization
  blocks: README, STATUS, RELEASE, ROADMAP, troubleshooting, and subsystem docs
  now point to the same implemented/prepared/not-validated boundary.
- Added security posture v2 diagnostics with VFS policy state files,
  policy-denial counters, key-rotation posture, and `security doctor` snapshots.
- Improved package manager v5 diagnostics with signed remote-index sidecar
  posture, cached upgrade plans, transaction state, and clearer `pkg doctor`
  output.
- Improved installed VM system administration with firstboot, service/log
  state, system health/snapshot/backup, repair, and rescue workflows.
- Strengthened the VM matrix and CI release guard with stricter PASS/WARN/FAIL
  reporting, source/artifact synchronization checks, and clearer release logs.
- Added persistent root snapshot/restore tooling for `/workspace`, `/home`, `/system`, `/packages`, and `/logs`.
- Added installed-system lifecycle commands for live vs installed state, first boot, rescue, hostname, and repair.
- Added installed-system UX v2 with service-state/logs, firstboot checklist, MOTD/fstab/rescue/admin defaults, and `system logs`.
- Added installed-system UX v3 with `system health`, `system snapshot`, `system backup`, `/system/os-release`, `/system/machine-id`, and non-secret admin backup reports.
- Extended the package manager with signed remote metadata, search/remote/status/history, dependency checks, and rollback-oriented install/remove paths.
- Improved the local console with scrollback, keyboard paging, command history, and a simple `less` pager.
- Added daily network diagnostics for DHCP renew, DNS, routes, TLS probe, and clearer update/pkg network errors.
- Improved VM daily networking with `net daily`, retrying TCP probes, explicit NAT/bridge boundary notes, and SSH/matrix coverage.
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
- Added package manager v4 diagnostics with `pkg audit`, `pkg cache`, `pkg simulate`, transaction-tagged history, and clearer detached-signature limitations.
- Improved the local framebuffer shell with `shell status`, `wc`, richer `grep`, `tee` pipelines, and searchable command history.
- Added security hardening v2 diagnostics with `security policy/audit/keys/doctor`, SSH host-key rotation, and redacted SSH audit events.
- Added release and VM/ZimaOS troubleshooting guides, plus clearer documentation labels for implemented, prepared, simulated, and non-validated work.
- Strengthened CI release guards with source/artifact sync checks and added PASS/WARN/FAIL VM matrix summaries.
