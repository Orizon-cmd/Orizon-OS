# Orizon Security

This page tracks the current base hardening posture for VM work. It is honest
about what is implemented and what is only a guardrail.

## Implemented Now

```text
security
ssh auth
ssh audit
ssh hostkey
update status
```

The `security` command prints a compact status block covering SSH auth,
lockout, host key storage, remote file policy, signed update policy, package
index authentication, protected files, and known limits.

SSH has no default password. Password auth is disabled until the console runs
`ssh password <password>`. Failed auth is counted, temporary lockout is
configurable with `ssh auth max <n>` and `ssh auth lockout <seconds>`, and
`audit` / `ssh sessions` records auth, commands, channel closes, listener
recoveries, and the most recent events.

Host identity is generated per installation when possible and persisted at:

```text
/system/ssh_host_rsa.key
```

The compiled bootstrap key is only a fallback if local host-key generation or
persistence fails. `ssh hostkey` exposes the fingerprint without dumping private
material.

## SSH File Policy

Generic SSH file commands are intentionally narrower than local console access.
This is the current simple policy:

```text
cat/head/tail: deny sensitive names such as /system/ssh.conf,
               /system/ssh_host_rsa.key, private, secret, token, password
write/append/touch/mkdir/rm: allowed only under /workspace, /home, /logs,
                             and /packages
```

Use command-scoped admin operations instead of editing protected files by hand:

```text
ssh password <password>
ssh password off
ssh auth max <n>
ssh auth lockout <seconds>
ssh auth default
ssh hostkey reload
ssh hostkey reset
hostname set <name>
net config ...
```

## Updates And Packages

`update` requires the public `manifest.txt` plus detached `manifest.sig`.
Before any boot payload is accepted, Orizon checks the manifest SHA-256 from the
signature metadata and verifies an RSA PKCS#1/SHA-256 signature against the
embedded update root key:

```text
orizon-update-root-2026-05
```

`update status` now states that this signature policy is required. The package
remote index remains pinned by fields inside the signed OS manifest.

## Known Limits

Orizon does not yet implement Unix-style users, groups, ACLs, sudo, secure boot,
TPM attestation, disk encryption, or a full MAC policy. The current SSH user
`orizon` is an authenticated remote admin with command-scoped restrictions, not
a low-privilege POSIX account.

Never commit local private signing keys, SSH private host keys, local env files,
firmware blobs, hotspot credentials, or captured passwords.
