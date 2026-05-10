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
  "free",
  "ps",
  "pkg status",
  "storage",
  "timer",
  "write /workspace/ssh-regression.txt alpha",
  "append /workspace/ssh-regression.txt beta",
  "cat /workspace/ssh-regression.txt",
  "audit",
  "rm /workspace/ssh-regression.txt",
  "audit"
)
$commandBlock = ($commands -join "`n")

$remoteScript = @"
set -u
ASKPASS=/tmp/orizon_askpass_regression.sh
PASSFILE=/tmp/orizon_ssh_regression_password.txt
KNOWN=/tmp/orizon_known_hosts_regression

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
    orizon@$VmIp "`$cmd"
  rc=`$?
  echo "rc=`$rc"
  if [ "`$rc" -ne 0 ]; then
    rm -f "`$ASKPASS" "`$PASSFILE"
    exit "`$rc"
  fi
  sleep 1
}

while IFS= read -r cmd; do
  [ -z "`$cmd" ] && continue
  run_cmd "`$cmd"
done <<'EOC'
$commandBlock
EOC

rm -f "`$ASKPASS" "`$PASSFILE"
echo "Orizon SSH regression OK"
"@

$remoteScript | ssh $ZimaHost "tr -d '\r' | bash"
