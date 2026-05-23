param(
  [string]$ZimaHost = $env:ORIZON_ZIMA_HOST,
  [string]$VmIp = $env:ORIZON_VM_IP,
  [string]$Password = $env:ORIZON_SSH_PASSWORD,
  [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = "Stop"

if (-not $ZimaHost) {
  $ZimaHost = "zimaos-orizon"
}
if (-not $VmIp) {
  $VmIp = "192.168.122.138"
}
if (-not $Password) {
  throw "Set ORIZON_SSH_PASSWORD or pass -Password. Example: `$env:ORIZON_SSH_PASSWORD='orizonpw'"
}

$encodedPassword = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Password))
$commands = @(
  "help",
  "status",
  "auth",
  "hostkey",
  "ssh algorithms",
  "security",
  "system status",
  "system services",
  "system logs",
  "system doctor",
  "system init",
  "system firstboot",
  "rescue",
  "hostname",
  "system repair",
  "net status",
  "net check",
  "net tcp raw.githubusercontent.com 443",
  "wifi status",
  "free",
  "ps",
  "pkg status",
  "pkg audit",
  "pkg cache",
  "pkg help",
  "pkg search orizon",
  "pkg remote",
  "pkg remote verify",
  "pkg upgrade plan",
  "pkg sample",
  "pkg simulate /workspace/packages/orizon-hello.opkg",
  "pkg verify /workspace/packages/orizon-hello.opkg",
  "pkg install /workspace/packages/orizon-hello.opkg",
  "update status",
  "storage",
  "storage diag",
  "storage vmcheck",
  "persist status",
  "persist slots",
  "persist save",
  "persist restore previous",
  "logs storage",
  "logs pci",
  "disk identify",
  "disk read-test",
  "disk read-test last",
  "gpt scan",
  "selftest crypto",
  "selftest ssh",
  "ssh sessions",
  "pci bars",
  "hw next",
  "report next",
  "report save",
  "install-plan",
  "head /workspace/hardware-report.txt",
  "tail /workspace/hardware-report.txt",
  "cat /workspace/.orizon/install-report.txt",
  "logs install",
  "rollback",
  "timer",
  "usb",
  "usb rescan",
  "bootguard",
  "rollback-status",
  "write /workspace/ssh-regression.txt alpha",
  "append /workspace/ssh-regression.txt beta",
  "cat /workspace/ssh-regression.txt",
  "audit",
  "logs network",
  "logs usb",
  "logs wifi",
  "logs ssh",
  "rm /workspace/ssh-regression.txt",
  "audit"
)
$commandBlock = ($commands -join "`n")

$remoteScript = @"
set -u
ASKPASS=/tmp/orizon_askpass_regression.sh
PASSFILE=/tmp/orizon_ssh_regression_password.txt
KNOWN=/tmp/orizon_known_hosts_regression
OUT=/tmp/orizon_ssh_regression_output.txt

printf '%s' '$encodedPassword' | base64 -d > "`$PASSFILE"
cat > "`$ASKPASS" <<'EOS'
#!/bin/sh
cat /tmp/orizon_ssh_regression_password.txt
EOS
chmod +x "`$ASKPASS"
rm -f "`$KNOWN"

run_cmd() {
  cmd="`$1"
  echo "--- `$cmd ---"
  DISPLAY=none SSH_ASKPASS="`$ASKPASS" SSH_ASKPASS_REQUIRE=force timeout ${TimeoutSeconds}s setsid ssh -n \
    -oNumberOfPasswordPrompts=1 \
    -oPreferredAuthentications=password \
    -oPubkeyAuthentication=no \
    -oStrictHostKeyChecking=no \
    -oUserKnownHostsFile="`$KNOWN" \
    -oConnectTimeout=5 \
    orizon@$VmIp "`$cmd" > "`$OUT" 2>&1
  rc=`$?
  cat "`$OUT"
  echo "rc=`$rc"
  if [ "`$rc" -ne 0 ]; then
    rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"
    exit "`$rc"
  fi
  case "`$cmd" in
    "help")
      grep -q "Remote Orizon commands" "`$OUT" || { echo "missing help output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "status")
      grep -q "ssh: enabled=" "`$OUT" || { echo "missing ssh status output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "auth")
      grep -q "ssh auth:" "`$OUT" || { echo "missing ssh auth output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "hostkey")
      grep -q "ssh hostkey:" "`$OUT" && grep -q "fingerprint-sha256" "`$OUT" || { echo "missing hostkey output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "ssh algorithms")
      grep -q "ssh algorithms:" "`$OUT" || { echo "missing algorithms output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "security")
      grep -q "Orizon security status" "`$OUT" && grep -q "ssh.file-policy:" "`$OUT" || { echo "missing security output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system status")
      grep -q "Orizon system status" "`$OUT" && grep -q "boot-mode:" "`$OUT" || { echo "missing system status output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system services")
      grep -q "Orizon init/services" "`$OUT" && grep -q "services:" "`$OUT" || { echo "missing system services output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system logs")
      grep -q "Orizon system logs" "`$OUT" && grep -q "service-state" "`$OUT" || { echo "missing system logs output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system doctor")
      grep -q "Orizon system doctor" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing system doctor output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system init")
      grep -q "system init:" "`$OUT" && grep -q "init-log=" "`$OUT" || { echo "missing system init output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system firstboot")
      grep -q "Orizon first boot" "`$OUT" && grep -q "checklist:" "`$OUT" || { echo "missing firstboot checklist"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "rescue")
      grep -q "Orizon rescue mode" "`$OUT" || { echo "missing rescue checklist"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "hostname")
      grep -q "orizon" "`$OUT" || { echo "missing hostname output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system repair")
      grep -q "system repair:" "`$OUT" && grep -q "rescue-report.txt" "`$OUT" || { echo "missing system repair output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "net status")
      grep -q "ipv4=" "`$OUT" || { echo "net status was not dispatched to the network command"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "net check")
      grep -q "network summary:" "`$OUT" || { echo "missing network check summary"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "net tcp raw.githubusercontent.com 443")
      grep -q "tcp: PASS" "`$OUT" || { echo "missing tcp probe pass"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "wifi status")
      grep -q "driver=" "`$OUT" || { echo "wifi status was not dispatched to the wifi command"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "cat /workspace/ssh-regression.txt")
      grep -q "alpha" "`$OUT" && grep -q "beta" "`$OUT" || { echo "missing cat output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "audit")
      grep -q "ssh audit:" "`$OUT" && grep -q "recent:" "`$OUT" || { echo "missing audit output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "bootguard")
      grep -q "Orizon boot guard" "`$OUT" || { echo "missing bootguard output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "storage diag")
      grep -q "storage diagnostics:" "`$OUT" && grep -q "nvme: controllers=" "`$OUT" || { echo "missing storage diagnostics"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "storage vmcheck")
      grep -q "storage vmcheck:" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing storage vmcheck"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "persist status")
      grep -q "persistence:" "`$OUT" && grep -q "roots=/workspace,/home,/system,/packages,/logs" "`$OUT" || { echo "missing persistence status"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "persist slots")
      grep -q "persistence slots:" "`$OUT" || { echo "missing persistence slots"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "persist save")
      grep -q "persistence save:" "`$OUT" || { echo "missing persist save output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "persist restore previous")
      grep -Eq "persistence restore: (PASS|FAIL)" "`$OUT" || { echo "missing persist restore output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "logs storage")
      grep -q "storage log:" "`$OUT" || { echo "missing storage log"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "logs pci")
      grep -q "pci diagnostics:" "`$OUT" || { echo "missing pci log"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "disk identify")
      grep -q "disk identify:" "`$OUT" || { echo "missing disk identify"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "disk read-test")
      grep -Eq "disk read-test: (PASS|WARN|FAIL)" "`$OUT" || { echo "missing disk read-test"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "disk read-test last")
      grep -Eq "disk read-test: PASS .*mode=read-only" "`$OUT" || { echo "missing last-sector read-test"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "gpt scan")
      ! grep -q "command not found" "`$OUT" || { echo "gpt scan is not exposed over SSH"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "selftest crypto")
      grep -q "summary: PASS" "`$OUT" || { echo "crypto selftest did not pass"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "selftest ssh"|"ssh sessions")
      grep -Eq "ssh\\.(listener|identity)|ssh audit:" "`$OUT" || { echo "missing ssh selftest/session output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "update status")
      grep -q "update status:" "`$OUT" || { echo "missing update status"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg help")
      grep -q "pkg simulate" "`$OUT" || { echo "missing pkg help output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg audit")
      grep -q "pkg audit:" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing pkg audit output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg cache")
      grep -q "pkg cache:" "`$OUT" && grep -q "package-repo-signature" "`$OUT" || { echo "missing pkg cache output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg search orizon")
      grep -q "pkg search:" "`$OUT" && grep -q "builtin orizon-core" "`$OUT" || { echo "missing pkg search output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg remote")
      grep -q "package remote:" "`$OUT" || { echo "missing pkg remote output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg sample")
      grep -q "Sample package written" "`$OUT" || { echo "missing pkg sample output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg simulate /workspace/packages/orizon-hello.opkg")
      grep -q "pkg simulate:" "`$OUT" && grep -q "dry-run" "`$OUT" || { echo "missing pkg simulate output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg verify /workspace/packages/orizon-hello.opkg")
      grep -q "package verify: OK" "`$OUT" || { echo "missing pkg verify output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg install /workspace/packages/orizon-hello.opkg")
      grep -q "unavailable in live boot" "`$OUT" || { echo "pkg install did not return live-boot guard"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pci bars")
      grep -q "PCI devices:" "`$OUT" || { echo "missing PCI diagnostics"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "hw next"|"report next")
      grep -q "Hardware return plan" "`$OUT" && grep -q "diagnostic-only" "`$OUT" || { echo "missing hardware return plan"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "report save")
      grep -q "/workspace/hardware-report.txt" "`$OUT" || { echo "missing report save output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "install-plan")
      grep -q "install-plan: wrote /workspace/.orizon/install-report.txt" "`$OUT" || { echo "missing install-plan output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "head /workspace/hardware-report.txt")
      grep -q "Orizon hardware report" "`$OUT" || { echo "missing saved hardware report"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "tail /workspace/hardware-report.txt")
      grep -q "\\[tail\\]" "`$OUT" && grep -q "ssh: CHANNEL" "`$OUT" || { echo "missing hardware report tail"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "cat /workspace/.orizon/install-report.txt"|"logs install")
      grep -q "Orizon install preflight" "`$OUT" && grep -q "write-scope: none" "`$OUT" || { echo "missing installer preflight report"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "rollback")
      grep -q "unavailable in live boot" "`$OUT" || { echo "rollback command did not return the live-boot guard"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "rollback-status")
      ! grep -q "command not found" "`$OUT" || { echo "rollback-status is not exposed over SSH"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "logs network"|"logs usb"|"logs wifi")
      ! grep -q "command not found" "`$OUT" || { echo "`$cmd is not exposed over SSH"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "logs ssh")
      grep -q "audit:" "`$OUT" || { echo "missing ssh log audit lines"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      bytes=`$(wc -c < "`$OUT")
      if [ "`$bytes" -lt 900 ]; then
        echo "ssh log output too short for chunk regression: `$bytes bytes"
        rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"
        exit 1
      fi
      ;;
  esac
  sleep 1
}

while IFS= read -r cmd; do
  [ -z "`$cmd" ] && continue
  run_cmd "`$cmd"
done <<'EOC'
$commandBlock
EOC

echo "--- exec command-not-found status ---"
DISPLAY=none SSH_ASKPASS="`$ASKPASS" SSH_ASKPASS_REQUIRE=force timeout ${TimeoutSeconds}s setsid ssh -n \
  -oNumberOfPasswordPrompts=1 \
  -oPreferredAuthentications=password \
  -oPubkeyAuthentication=no \
  -oStrictHostKeyChecking=no \
  -oUserKnownHostsFile="`$KNOWN" \
  -oConnectTimeout=5 \
  orizon@$VmIp "definitely-not-a-command" > "`$OUT" 2>&1
rc=`$?
cat "`$OUT"
echo "rc=`$rc"
if [ "`$rc" -eq 0 ]; then
  echo "unknown command returned success"
  rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"
  exit 1
fi
grep -q "command not found" "`$OUT" || { echo "missing command-not-found output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }

echo "--- shell pty multi-command ---"
printf 'status\rnet status\rwifi status\rexit\r' | DISPLAY=none SSH_ASKPASS="`$ASKPASS" SSH_ASKPASS_REQUIRE=force timeout ${TimeoutSeconds}s setsid ssh -tt \
  -oNumberOfPasswordPrompts=1 \
  -oPreferredAuthentications=password \
  -oPubkeyAuthentication=no \
  -oStrictHostKeyChecking=no \
  -oUserKnownHostsFile="`$KNOWN" \
  -oConnectTimeout=5 \
  orizon@$VmIp > "`$OUT" 2>&1
rc=`$?
cat "`$OUT"
echo "rc=`$rc"
if [ "`$rc" -ne 0 ]; then
  rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"
  exit "`$rc"
fi
grep -q "Orizon OS remote shell" "`$OUT" || { echo "missing remote shell banner"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
grep -q "ssh: enabled=" "`$OUT" || { echo "missing pty ssh status output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
grep -q "ipv4=" "`$OUT" || { echo "missing pty net status output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
grep -q "driver=" "`$OUT" || { echo "missing pty wifi status output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }

rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"
echo "Orizon SSH regression OK"
"@

$remoteScript | ssh $ZimaHost "tr -d '\r' | bash"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
