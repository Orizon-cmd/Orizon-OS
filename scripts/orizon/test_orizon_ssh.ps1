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
  "net status",
  "wifi status",
  "free",
  "ps",
  "pkg status",
  "storage",
  "timer",
  "usb",
  "usb rescan",
  "bootguard",
  "write /workspace/ssh-regression.txt alpha",
  "append /workspace/ssh-regression.txt beta",
  "cat /workspace/ssh-regression.txt",
  "audit",
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
    "net status")
      grep -q "ipv4=" "`$OUT" || { echo "net status was not dispatched to the network command"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
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
