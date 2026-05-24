# Orizon Release Guide

This page is the release checklist for VM/ZimaOS work. It keeps the boot ISO,
update payloads, manifests, signatures, release notes, and GitHub CI story in
one place.

## Boundary

- Current release validation is VM/ZimaOS oriented.
- A successful VM build or smoke never proves Lenovo, real USB Ethernet, real
  Wi-Fi AP, or physical-PC support.
- Do not publish private keys, local env files, imported firmware blobs, host
  passwords, hotspot credentials, or captured secrets.
- If code changes the kernel or boot payloads, update the release artifacts in
  the same commit.

## Artifact Set

The tracked release bundle is:

```text
Orizon-OS.iso
updates/x86_64/kernel.elf
updates/x86_64/BOOTX64.EFI
updates/x86_64/limine.conf
updates/x86_64/manifest.txt
updates/x86_64/manifest.sig
updates/x86_64/release.txt
```

`manifest.txt` carries payload metadata and package-index pins.
`manifest.sig` is the detached signed manifest metadata. `release.txt` is the
human-readable cross-check of ISO, payload, manifest, and signature hashes.
Package repository sidecars such as `/workspace/.orizon/package-index.sig` are
runtime package-cache inputs, not OS release artifacts yet; missing sidecars
must remain a WARN/fallback, not a hidden PASS.

## Normal Release Flow

When kernel, boot, update, package, installer, shell, network, storage, or docs
that describe generated artifacts changed:

```powershell
python scripts/orizon/orizon_update.py --mode zimaos-iso
python scripts/orizon/quick_check.py
python scripts/orizon/orizon_update.py --mode validate-release
```

When the change is documentation-only and does not change generated artifacts:

```powershell
python scripts/orizon/quick_check.py
python scripts/orizon/orizon_update.py --mode validate-release
```

Run a short VM smoke when the change affects runtime behavior:

```powershell
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e --disk-bus sata --boot-timeout 90 --ssh-timeout 45
```

The matrix writes per-case logs plus:

```text
artifacts/vm-matrix/matrix-summary.md
artifacts/vm-matrix/matrix-summary.json
```

Statuses are explicit: `PASS` means SSH/runtime checks passed, `WARN` means a
limited boot/framebuffer result such as bridge IP discovery missing, `FAIL`
means the case failed, and `SKIP` is reserved for intentionally omitted checks.
Do not run the full matrix unless the task explicitly asks for it.

## GitHub CI

The GitHub release guard entrypoint is:

```powershell
python scripts/orizon/ci_release_guard.py --output-dir artifacts
```

It centralizes quick checks, tracked-secret scan, release validation, release
notes preview, artifact synchronization summaries, and a diff-aware
source/artifact check. CI should fail clearly when any tracked release artifact
is out of sync with the manifest or `release.txt`, or when runtime source under
`orizon-os-x86_64/` changes without refreshed `Orizon-OS.iso`,
`updates/x86_64/kernel.elf`, `manifest.txt`, `manifest.sig`, and `release.txt`.

The guard writes these diagnostics:

```text
artifacts/release-summary.md
artifacts/release-artifacts.json
artifacts/source-artifact-sync.md
artifacts/source-artifact-sync.json
```

## Failure Triage

- If `validate-release` fails, rebuild with `zimaos-iso` or restore the stale
  artifact from the intended commit. Do not hand-edit hashes.
- If secret scan fails, remove the tracked secret-like file or rename/split
  generated test data so it cannot be mistaken for private material.
- If the ZimaOS build fails before compile, check ZimaOS disk space and Docker
  availability first.
- If the VM smoke fails after boot, capture `logs network`, `logs ssh`,
  `system health`, `system snapshot`, `system backup`, `system logs`,
  `security audit`, `report save`, and the script output before changing
  behavior.
- If package/update network commands fail, run `net check`, `net daily`, and
  `net tcp raw.githubusercontent.com 443 attempts 2` in the VM.

## Commit Rule

For a runtime build change, commit source, docs, `Orizon-OS.iso`, and
`updates/x86_64/*` together. For docs-only work, do not touch generated
artifacts just to make a commit look larger.
