# Orizon Security

This page tracks the current base hardening posture for VM work. It is honest
about what is implemented and what is only a guardrail.
For the full implemented/prepared/not-validated boundary, see
[STATUS.md](STATUS.md). For release secret-scan behavior, see
[RELEASE.md](RELEASE.md).
Security behavior changes should also update `CHANGELOG.md`, this page, and
the relevant command docs before publishing.

## Implemented Now

```text
security
security policy
security audit
security keys
security doctor
security rotate ssh-hostkey
ssh auth
ssh audit
ssh hostkey
logs security
update status
```

The `security` command prints a compact status block covering SSH auth,
lockout, host key storage, remote file policy, signed update policy, package
index authentication, protected files, key-rotation posture, and known limits.
It also refreshes the non-secret state files `/system/security-policy` and
`/system/security-state` so VM/SSH reports can copy the active policy without
reading private material. `security policy` expands the active path,
update/package, audit-redaction and admin-command rules. `security audit`
combines the persistent security mirror with SSH audit plus policy-denial
counters. `security keys` reports host-key/update/package key posture without
dumping private material. `security doctor` is a non-destructive PASS/WARN
checklist for the current VM/live state and writes
`/workspace/.orizon/security-doctor.txt`.

SSH has no default password. Password auth is disabled until the console runs
`ssh password <password>`. Failed auth is counted, temporary lockout is
configurable with `ssh auth max <n>` and `ssh auth lockout <seconds>`, and
`audit` / `ssh sessions` records auth, commands, channel closes, listener
recoveries, and the most recent events. SSH audit events are persisted in
`/logs/ssh.log` and mirrored to `/logs/security.log`; policy changes such as
auth policy updates, lockout clears, host-key reloads and host-key resets also
append a non-secret security event. Audit storage redacts `ssh password`,
generic `write`/`append` payloads, and Wi-Fi credentials before they reach the
recent-event buffer or persistent logs.

Host identity is generated per installation when possible and persisted at:

```text
/system/ssh_host_rsa.key
```

The compiled bootstrap key is only a fallback if local host-key generation or
persistence fails. `ssh hostkey` exposes the fingerprint without dumping private
material.

Host-key rotation is explicit:

```text
security rotate ssh-hostkey
```

It regenerates `/system/ssh_host_rsa.key` for future SSH sessions. Existing
clients may need their known_hosts entry updated because the host fingerprint
changes by design.

Update-root and package-root rotation are intentionally reported as
`release-required`: the public trust roots are compiled into the release
artifacts and must move through a signed release rather than an in-VM command.

## VFS Security Policy V2

Generic SSH file commands are intentionally narrower than local console access.
This is the current structured path policy:

```text
policy-version: 2
cat/head/tail: deny sensitive names such as /system/ssh.conf,
               /system/ssh_host_rsa.key, private, secret, token, password,
               passwd, credential, api_key, id_rsa, id_ed25519, .ssh,
               .env, .key, .pem, .p12 and .pfx
write/append/touch/mkdir/rm: allowed only under /workspace, /home, /logs,
                             and /packages, with /workspace/.orizon blocked
                             as internal OS state
rm: remote roots /workspace, /home, /logs and /packages cannot be deleted
denials: counted by class in security/security audit and mirrored to audit
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
security rotate ssh-hostkey
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
Detached package-repository signatures are now prepared through the cached
sidecar `/workspace/.orizon/package-index.sig`; `pkg remote verify` and
`pkg doctor` check it when present and report WARN when it is absent. Package
authenticity still falls back to the signed update manifest pinning the package
repository commit, index path, index size, and index SHA-256 until the package
repo publishes that sidecar for every release.

Package payloads and package scripts are limited to `/system`, `/home`,
`/packages`, `/logs`, `/tmp`, and `/workspace`, with `/workspace/.orizon` and
common secret-bearing names blocked. This is still a path policy, not a full
MAC or container sandbox.

Release checks run the tracked-secret scanner from `scripts/orizon/check_no_secrets.py`.
It fails on tracked private-key paths, `.ssh`, local env/host files, credential
filenames, common API tokens, and private key blocks before release validation
can pass.

## Known Limits

Orizon does not yet implement Unix-style users, groups, ACLs, sudo, secure boot,
TPM attestation, disk encryption, or a full MAC policy. The current SSH user
`orizon` is an authenticated remote admin with command-scoped restrictions, not
a low-privilege POSIX account. VFS permissions are structured path allow/deny
rules plus persistent-root handling, not ownership-enforced isolation.

Never commit local private signing keys, SSH private host keys, local env files,
firmware blobs, hotspot credentials, or captured passwords.
