# Orizon VM/ZimaOS Troubleshooting

This page is for debugging the VM/ZimaOS workflow without touching Lenovo or
other real hardware.

## First Rules

- Do not install on real hardware while investigating a VM or repository issue.
- Do not claim physical validation from VM evidence.
- Prefer non-destructive captures first: `system status`, `selftest`,
  `report save`, `logs`, and script output.
- If a build changes runtime code, follow [RELEASE.md](RELEASE.md) before
  committing.

## Quick Triage Order

```powershell
git status -sb
python scripts/orizon/quick_check.py
python scripts/orizon/orizon_update.py --mode validate-release
```

If the issue only appears in a running VM:

```powershell
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e --disk-bus sata --boot-timeout 90 --ssh-timeout 45
```

Read `artifacts/vm-matrix/matrix-summary.md` first. Use the per-case `.log`
file when the summary says `FAIL` or `WARN`. `WARN` is useful evidence, not a
pass: it usually means the VM reached framebuffer but the SSH portion could not
be proven, for example on bridge networking without discoverable guest IP.

Inside Orizon over SSH or console:

```text
system status
system logs
selftest
security doctor
net daily
storage vmcheck
report save
tail /workspace/hardware-report.txt
```

## Symptom Table

| Symptom | First checks | Likely next action |
| --- | --- | --- |
| ISO boots but SSH is unreachable | `net status`, `ssh status`, `logs network`, host VM IP inventory | Restart DHCP/SSH from console; check NAT vs bridge IP discovery |
| DHCP is missing | `net status`, `net daily`, `logs network` | Use NAT e1000e first; avoid bridge until host IP discovery is clear |
| DNS/TCP fails | `dns raw.githubusercontent.com`, `net tcp raw.githubusercontent.com 443 attempts 2` | Re-run DHCP, inspect gateway/DNS, then retry update/pkg |
| Release validation fails | `release.txt`, `manifest.txt`, `manifest.sig`, `Orizon-OS.iso` hashes | Rebuild with `orizon_update.py --mode zimaos-iso`; do not edit hashes by hand |
| Secret scan fails | `python scripts/orizon/check_no_secrets.py` | Remove or ignore local-only secrets; never commit private key material |
| Package install fails | `pkg audit`, `pkg cache`, `pkg history`, `logs security` | Check dependency/version/path policy; use `pkg simulate` before real install |
| Update/rollback state is confusing | `update status`, `bootguard`, `rollback-status` | Remember current rollback is Limine pseudo-A/B, not true BootNext/full A/B |
| Persistence looks wrong | `persist status`, `persist slots`, `system doctor` | Use `persist restore previous` only if current state is broken |
| Storage VM read test fails | `storage diag`, `storage vmcheck`, `disk identify`, `disk read-test last` | Keep test non-destructive; note AHCI/NVMe/VirtIO path in the report |
| Long output is hard to read | SSH `cat/head/tail`, local `less <file>`, framebuffer `z/s` scroll | Prefer SSH for copyable reports |

## ZimaOS Host Checks

Use these from the Windows workspace when the host itself may be the problem:

```powershell
powershell -File scripts/orizon/test_zimaos.ps1
python scripts/orizon/inventory_zimaos_vms.py
python scripts/orizon/setup_zimaos_access.py
```

The ZimaOS root filesystem has previously been seen at 100% usage. If builds
or VM provisioning fail before Orizon boots, check free space on the host before
changing Orizon code.

## Captures To Keep

- Terminal output from the failing script.
- `/workspace/hardware-report.txt` from `report save`.
- `logs ssh`, `logs security`, `logs network`, `logs storage`, and
  `system logs`.
- The exact VM profile: NIC, disk bus, NAT/bridge, ISO commit, and whether it
  was live ISO or installed VM.

## What This Page Does Not Prove

This guide does not validate Lenovo storage, Intel AX201 real AP Wi-Fi, a real
USB Ethernet dongle, Secure Boot, TPM, or physical dual boot. Those require a
separate user-provided hardware boot and fresh captures.
