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
  "security policy",
  "security audit",
  "security keys",
  "security doctor",
  "system status",
  "system health",
  "system snapshot",
  "cat /workspace/.orizon/system-snapshot.txt",
  "system backup",
  "cat /workspace/.orizon/admin-backup.txt",
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
  "net daily",
  "net tcp raw.githubusercontent.com 443",
  "net tcp raw.githubusercontent.com 443 attempts 2",
  "wifi status",
  "free",
  "ps",
  "pkg status",
  "pkg audit",
  "pkg doctor",
  "pkg cache",
  "pkg help",
  "pkg search orizon",
  "pkg search desktop",
  "pkg remote",
  "pkg remote verify",
  "pkg upgrade plan",
  "pkg sample",
  "pkg sample orizon-desktop-core",
  "pkg verify /workspace/packages/orizon-desktop-core.opkg",
  "pkg sample orizon-terminal",
  "pkg sample orizon-waybar",
  "pkg simulate /workspace/packages/orizon-hello.opkg",
  "pkg verify /workspace/packages/orizon-hello.opkg",
  "pkg install /workspace/packages/orizon-hello.opkg",
  "desktop status",
  "desktop config",
  "desktop config doctor",
  "desktop config apply",
  "desktop config trace",
  "desktop hyprctl getoption env",
  "desktop hyprctl getoption workspace",
  "desktop session",
  "desktop settings",
  "desktop settings presets",
  "desktop settings doctor",
  "desktop settings preset compact",
  "desktop settings set gaps-in 10",
  "desktop settings set border-size 3",
  "desktop settings set focus-ring no",
  "desktop settings set render-profile performance",
  "desktop hyprctl getoption render:focus_ring",
  "desktop hyprctl getoption render:profile",
  "desktop settings repair",
  "desktop input",
  "desktop input layout fr",
  "desktop input layout us",
  "desktop input pointer natural",
  "desktop input focus toggle",
  "desktop input submap launch",
  "desktop input submap reset",
  "desktop pointer",
  "desktop apps",
  "desktop app settings",
  "desktop apps launcher",
  "desktop profiles",
  "desktop preset moss",
  "desktop binds",
  "desktop rules",
  "desktop monitors",
  "desktop runtime",
  "desktop layers",
  "desktop version",
  "desktop devices",
  "desktop systeminfo",
  "desktop backend",
  "desktop protocol",
  "desktop layouts",
  "desktop layout-tree",
  "desktop layout-state",
  "desktop animations",
  "desktop decorations",
  "desktop render",
  "desktop descriptions",
  "desktop instances",
  "desktop submap",
  "desktop configerrors",
  "desktop rollinglog",
  "desktop client-model",
  "desktop workspace-stack",
  "desktop rule-matches",
  "desktop keyword general:gaps_in 9",
  "desktop hyprctl version",
  "desktop hyprctl systeminfo",
  "desktop hyprctl backend",
  "desktop hyprctl protocol",
  "desktop hyprctl clientmodel",
  "desktop hyprctl -j clientmodel",
  "desktop hyprctl rulematches",
  "desktop hyprctl -j rulematches",
  "desktop hyprctl activeworkspace",
  "desktop hyprctl -j clients",
  "desktop hyprctl -j activewindow",
  "desktop hyprctl -j workspaces",
  "desktop hyprctl -j activeworkspace",
  "desktop hyprctl -j focushistory",
  "desktop hyprctl -j workspacestack",
  "desktop hyprctl -j layoutstate",
  "desktop hyprctl -j layouttree",
  "desktop hyprctl workspacestack",
  "desktop hyprctl layouts",
  "desktop hyprctl layoutstate",
  "desktop hyprctl layouttree",
  "desktop hyprctl animations",
  "desktop hyprctl decorations",
  "desktop hyprctl render",
  "desktop hyprctl descriptions",
  "desktop hyprctl instances",
  "desktop hyprctl submap",
  "desktop hyprctl cursorpos",
  "desktop hyprctl devices",
  "desktop hyprctl splash",
  "desktop hyprctl configerrors",
  "desktop hyprctl configtrace",
  "desktop hyprctl rollinglog",
  "desktop hyprctl getoption env",
  "desktop hyprctl getoption workspace",
  "desktop hyprctl getoption general:gaps_in",
  "desktop hyprctl keyword decoration:rounding 11",
  "desktop hyprctl getoption decoration:rounding",
  "desktop hyprctl keyword decoration:shadow:range 22",
  "desktop hyprctl getoption decoration:shadow:range",
  "desktop hyprctl keyword animations:tick_budget 24",
  "desktop hyprctl getoption animations:tick_budget",
  "desktop hyprctl binds",
  "desktop hyprctl layers",
  "desktop autostart",
  "desktop autostart terminal off",
  "desktop autostart terminal on",
  "desktop dispatch exec terminal",
  "desktop dispatch exec terminal",
  "desktop dispatch focuswindow class:orizon-terminal",
  "desktop dispatch focuscurrentorlast",
  "desktop dispatch markurgent on",
  "desktop activewindow",
  "desktop dispatch focuscurrentorlast",
  "desktop focus-history",
  "desktop dispatch focusurgentorlast",
  "desktop focus-window title:Terminal",
  "desktop dispatch cyclenext",
  "desktop dispatch swapnext",
  "desktop dispatch movefocus r",
  "desktop dispatch swapwindow l",
  "desktop dispatch togglesplit",
  "desktop dispatch layoutmsg layout master",
  "desktop dispatch layoutmsg splitratio 60",
  "desktop dispatch layoutmsg nmaster 2",
  "desktop dispatch layoutmsg addmaster",
  "desktop dispatch layoutmsg removemaster",
  "desktop dispatch layoutmsg splitratio reset",
  "desktop dispatch layoutmsg masterratio reset",
  "desktop dispatch layoutmsg nmaster reset",
  "desktop dispatch layoutmsg layout monocle",
  "desktop layout-tree",
  "desktop dispatch layoutmsg reset",
  "desktop dispatch layoutmsg layout master",
  "desktop dispatch layoutmsg preselect r",
  "desktop dispatch layoutmsg preselect up",
  "desktop dispatch layoutmsg preselect reset",
  "desktop dispatch submap resize",
  "desktop hyprctl submap",
  "desktop hyprctl submap reset",
  "desktop dispatch fullscreen",
  "desktop dispatch fullscreen off",
  "desktop dispatch fullscreenstate 1",
  "desktop dispatch fullscreenstate 2 0",
  "desktop hyprctl activewindow",
  "desktop dispatch fullscreenstate -1 2",
  "desktop dispatch fullscreenstate 0 0",
  "desktop dispatch pseudo",
  "desktop dispatch pseudo off",
  "desktop dispatch pseudotile on",
  "desktop dispatch pin",
  "desktop dispatch pin off",
  "desktop dispatch pin on",
  "desktop hyprctl clients",
  "desktop hyprctl activewindow",
  "desktop hyprctl monitors",
  "desktop windows",
  "desktop workspace",
  "desktop dispatch movetoworkspace 2",
  "desktop dispatch workspace 2",
  "desktop dispatch workspace previous",
  "desktop dispatch workspace +1",
  "desktop dispatch workspace r+1",
  "desktop dispatch workspace r~2",
  "desktop dispatch workspace m~1",
  "desktop dispatch workspace m+1",
  "desktop dispatch workspace e+1",
  "desktop dispatch workspace e-1",
  "desktop dispatch renameworkspace 2 dev",
  "desktop dispatch workspace name:dev",
  "desktop dispatch movetoworkspace name:dev",
  "desktop workspace 2",
  "desktop dispatch movetoworkspacesilent empty",
  "desktop dispatch workspace next",
  "desktop dispatch workspace empty",
  "desktop workspace empty",
  "desktop dispatch movefocus next",
  "desktop shortcuts",
  "desktop doctor",
  "desktop logs",
  "desktop theme moss",
  "desktop wallpaper dawn",
  "desktop layout master",
  "desktop focus toggle",
  "desktop bar toggle",
  "desktop apply",
  "desktop launcher show",
  "desktop launch terminal",
  "desktop dispatch exec settings",
  "desktop dispatch tagwindow +settings class:orizon-settings",
  "desktop dispatch focuswindow tag:settings",
  "desktop dispatch movetoworkspacesilent r+1,tag:settings",
  "desktop dispatch movetoworkspacesilent 2,tag:settings",
  "desktop dispatch movetoworkspacesilent 2,class:orizon-settings",
  "desktop dispatch movetoworkspace active,activewindow",
  "desktop launch launcher",
  "pkg info orizon-desktop-hypr",
  "desktop package",
  "pkg simulate /workspace/packages/orizon-desktop-hypr.opkg",
  "pkg verify /workspace/packages/orizon-desktop-hypr.opkg",
  "pkg install orizon-desktop-hypr",
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
      grep -q "Orizon security status" "`$OUT" && grep -q "vfs.policy: version=2" "`$OUT" && grep -q "policy-denies:" "`$OUT" || { echo "missing security output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "security policy")
      grep -q "security policy:" "`$OUT" && grep -q "policy-version: 2" "`$OUT" && grep -q "ssh-audit-redaction" "`$OUT" || { echo "missing security policy output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "security audit")
      grep -q "security audit:" "`$OUT" && grep -q "persistent-log:" "`$OUT" && grep -q "state-files: policy=/system/security-policy" "`$OUT" || { echo "missing security audit output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "security keys")
      grep -q "security keys:" "`$OUT" && grep -q "rotation-summary: ssh-hostkey=runtime" "`$OUT" || { echo "missing security keys output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "security doctor")
      grep -q "security doctor:" "`$OUT" && grep -q "policy.state" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing security doctor output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system status")
      grep -q "Orizon system status" "`$OUT" && grep -q "boot-mode:" "`$OUT" || { echo "missing system status output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system health")
      grep -q "Orizon system health" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing system health output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system snapshot")
      grep -q "system snapshot:" "`$OUT" && grep -q "system-snapshot.txt" "`$OUT" || { echo "missing system snapshot output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "cat /workspace/.orizon/system-snapshot.txt")
      grep -q "Orizon system snapshot" "`$OUT" && grep -q "== health ==" "`$OUT" || { echo "missing system snapshot file"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "system backup")
      grep -q "system backup:" "`$OUT" && grep -q "admin-backup.txt" "`$OUT" || { echo "missing system backup output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "cat /workspace/.orizon/admin-backup.txt")
      grep -q "Orizon admin backup" "`$OUT" && grep -q "excluded: SSH private keys" "`$OUT" || { echo "missing admin backup file"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
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
    "net daily")
      grep -q "network daily:" "`$OUT" && grep -q "retry-policy:" "`$OUT" || { echo "missing network daily report"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "net tcp raw.githubusercontent.com 443")
      grep -q "tcp: PASS" "`$OUT" || { echo "missing tcp probe pass"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "net tcp raw.githubusercontent.com 443 attempts 2")
      grep -q "tcp retry summary: PASS" "`$OUT" || { echo "missing tcp retry summary pass"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
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
    "pkg doctor")
      grep -q "pkg doctor:" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing pkg doctor output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg cache")
      grep -q "pkg cache:" "`$OUT" && grep -q "package-repo-signature" "`$OUT" || { echo "missing pkg cache output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg search orizon")
      grep -q "pkg search:" "`$OUT" && grep -q "builtin orizon-core" "`$OUT" || { echo "missing pkg search output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg search desktop")
      grep -q "orizon-desktop-hypr" "`$OUT" || { echo "missing desktop package search output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg remote")
      grep -q "package remote:" "`$OUT" || { echo "missing pkg remote output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg sample")
      grep -q "Sample package written" "`$OUT" || { echo "missing pkg sample output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg sample orizon-desktop-core")
      grep -q "Desktop module package written" "`$OUT" && grep -q "orizon-desktop-core.opkg" "`$OUT" || { echo "missing desktop core sample output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg verify /workspace/packages/orizon-desktop-core.opkg")
      grep -q "package verify: OK" "`$OUT" || { echo "missing desktop core verify output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg sample orizon-terminal")
      grep -q "Desktop module package written" "`$OUT" && grep -q "orizon-terminal.opkg" "`$OUT" || { echo "missing desktop terminal sample output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg sample orizon-waybar")
      grep -q "planned later" "`$OUT" && grep -q "not generated" "`$OUT" || { echo "missing desktop waybar planned output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
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
    "desktop status")
      grep -q "Orizon desktop" "`$OUT" && grep -q "hyprland-inspired" "`$OUT" || { echo "missing desktop status"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop config")
      grep -q "Hyprland-style" "`$OUT" || { echo "missing desktop config"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop config doctor")
      grep -q "Hyprland config doctor" "`$OUT" && grep -q "apply-support:" "`$OUT" && grep -q "source-resolve: loaded=1" "`$OUT" || { echo "missing desktop config doctor"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop config apply")
      grep -q "desktop config apply: applied" "`$OUT" && grep -q "runtime-files:" "`$OUT" && grep -q "source-resolve: loaded=1" "`$OUT" || { echo "missing desktop config apply"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop config trace"|"desktop hyprctl configtrace")
      grep -q "Orizon desktop Hyprland config trace" "`$OUT" && grep -q "APPLY" "`$OUT" && grep -q "PREPARE" "`$OUT" && grep -q "SOURCE path=/home/orizon/.config/hypr/orizon-local.conf status=LOADED" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing desktop config trace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop session")
      grep -q "Orizon desktop session" "`$OUT" && grep -q "theme:" "`$OUT" || { echo "missing desktop session"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop settings")
      grep -q "Orizon desktop system settings" "`$OUT" && grep -q "gaps-in:" "`$OUT" || { echo "missing desktop settings"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop settings presets")
      grep -q "Orizon desktop settings presets" "`$OUT" && grep -q "performance" "`$OUT" || { echo "missing desktop settings presets"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop settings doctor")
      grep -q "Orizon desktop settings doctor" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing desktop settings doctor"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop settings preset compact")
      grep -q "desktop settings preset: applied" "`$OUT" && grep -q "preset: compact" "`$OUT" || { echo "missing desktop settings preset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop settings set gaps-in 10"|"desktop settings set border-size 3"|"desktop settings set focus-ring no"|"desktop settings set render-profile performance")
      grep -q "desktop settings: updated" "`$OUT" || { echo "missing desktop settings update"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop settings repair")
      grep -q "desktop settings: repaired" "`$OUT" || { echo "missing desktop settings repair"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop input")
      grep -q "Orizon desktop input" "`$OUT" && grep -q "manual-window-drag: no" "`$OUT" || { echo "missing desktop input"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop input layout fr")
      grep -q "desktop input: updated" "`$OUT" && grep -q "keyboard-layout: fr-azerty" "`$OUT" || { echo "missing desktop input layout fr"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop input layout us")
      grep -q "desktop input: updated" "`$OUT" && grep -q "keyboard-layout: us-qwerty" "`$OUT" || { echo "missing desktop input layout us"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop input pointer natural")
      grep -q "desktop input: updated" "`$OUT" && grep -q "pointer-profile: natural" "`$OUT" || { echo "missing desktop input pointer"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop input focus toggle")
      grep -q "desktop input: updated" "`$OUT" && grep -q "focus-follows-mouse:" "`$OUT" || { echo "missing desktop input focus"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop input submap launch")
      grep -q "submap launch" "`$OUT" || { echo "missing desktop input submap launch"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop input submap reset")
      grep -q "submap default" "`$OUT" || { echo "missing desktop input submap reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop pointer")
      grep -q "Orizon desktop pointer" "`$OUT" && grep -q "usb-hid:" "`$OUT" || { echo "missing desktop pointer"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop apps")
      grep -q "Orizon desktop apps" "`$OUT" && grep -q "terminal" "`$OUT" || { echo "missing desktop apps"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop app settings")
      grep -q "Orizon desktop app" "`$OUT" && grep -q "class: orizon-settings" "`$OUT" || { echo "missing desktop app settings"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop apps launcher")
      grep -q "Orizon desktop app" "`$OUT" && grep -q "surface: overlay" "`$OUT" || { echo "missing desktop launcher detail"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop profiles")
      grep -q "Orizon desktop profiles" "`$OUT" && grep -q "themes:" "`$OUT" || { echo "missing desktop profiles"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop preset moss")
      grep -q "desktop preset: applied" "`$OUT" && grep -q "preset: moss" "`$OUT" || { echo "missing desktop preset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop binds")
      grep -q "Orizon desktop binds" "`$OUT" && grep -q "/system/desktop-binds.conf" "`$OUT" && grep -q "killactive" "`$OUT" || { echo "missing desktop binds"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop rules")
      grep -q "Orizon desktop window rules" "`$OUT" && grep -q "/system/desktop-rules.conf" "`$OUT" || { echo "missing desktop rules"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop monitors")
      grep -q "Orizon desktop monitor hints" "`$OUT" && grep -q "/system/desktop-monitors.conf" "`$OUT" || { echo "missing desktop monitors"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop runtime")
      grep -q "Orizon desktop Hyprland runtime" "`$OUT" && grep -q "/system/desktop-runtime.conf" "`$OUT" || { echo "missing desktop runtime"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop layers")
      grep -q "Orizon desktop layers" "`$OUT" && grep -q "namespace=bar" "`$OUT" || { echo "missing desktop layers"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop version"|"desktop hyprctl version")
      grep -q "Orizon desktop hyprctl version" "`$OUT" && grep -q "not upstream Hyprland" "`$OUT" || { echo "missing desktop version"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop devices"|"desktop hyprctl devices")
      grep -q "Orizon desktop devices" "`$OUT" && grep -q "manual-window-drag: no" "`$OUT" || { echo "missing desktop devices"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop systeminfo"|"desktop hyprctl systeminfo")
      grep -q "Orizon desktop systeminfo" "`$OUT" && grep -q "not upstream Hyprland" "`$OUT" || { echo "missing desktop systeminfo"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop backend"|"desktop hyprctl backend")
      grep -q "Orizon desktop backend" "`$OUT" && grep -q "current-backend: framebuffer-vm" "`$OUT" || { echo "missing desktop backend"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop protocol"|"desktop hyprctl protocol")
      grep -q "Orizon desktop protocol" "`$OUT" && grep -q "wayland: no" "`$OUT" || { echo "missing desktop protocol"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop layouts"|"desktop hyprctl layouts")
      grep -q "Orizon desktop layouts" "`$OUT" && grep -q "dwindle" "`$OUT" || { echo "missing desktop layouts"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop layout-state"|"desktop hyprctl layoutstate")
      grep -q "Orizon desktop layout state" "`$OUT" && grep -q "per-workspace" "`$OUT" && grep -q "workspace 1" "`$OUT" || { echo "missing desktop layout state"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop layout-tree"|"desktop hyprctl layouttree")
      grep -q "Orizon desktop layout tree" "`$OUT" && grep -q "manual-drag=no" "`$OUT" || { echo "missing desktop layout tree"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop animations"|"desktop hyprctl animations")
      grep -q "Orizon desktop animations" "`$OUT" && grep -q "animations:enabled" "`$OUT" || { echo "missing desktop animations"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop decorations"|"desktop hyprctl decorations")
      grep -q "Orizon desktop decorations" "`$OUT" && grep -q "manual-drag=no" "`$OUT" || { echo "missing desktop decorations"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop render"|"desktop hyprctl render")
      grep -q "Orizon desktop render" "`$OUT" && grep -q "manual-drag=no" "`$OUT" || { echo "missing desktop render"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop descriptions"|"desktop hyprctl descriptions")
      grep -q "Orizon desktop hyprctl descriptions" "`$OUT" && grep -q "layoutmsg" "`$OUT" || { echo "missing desktop descriptions"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop instances"|"desktop hyprctl instances")
      grep -q "Orizon desktop instances" "`$OUT" && grep -q "orizon-framebuffer-main" "`$OUT" || { echo "missing desktop instances"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop submap")
      grep -q "submap:" "`$OUT" || { echo "missing desktop submap"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop configerrors"|"desktop hyprctl configerrors")
      grep -q "Hyprland config errors" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing desktop configerrors"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop rollinglog"|"desktop hyprctl rollinglog")
      grep -q "Hyprland rolling log" "`$OUT" && grep -q "/logs/desktop.log" "`$OUT" || { echo "missing desktop rollinglog"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop client-model"|"desktop hyprctl clientmodel")
      grep -q "Orizon desktop client model" "`$OUT" && grep -q "manual-drag=no" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing desktop client model"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j clientmodel")
      grep -q '"manualDrag":false' "`$OUT" && grep -Fq '"workspaces":[' "`$OUT" && grep -Fq '"clients":[' "`$OUT" || { echo "missing desktop json client model"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop workspace-stack"|"desktop hyprctl workspacestack")
      grep -q "Orizon desktop workspace stack" "`$OUT" && grep -q "master/stack/focus" "`$OUT" && grep -q "manual-drag=no" "`$OUT" || { echo "missing desktop workspace stack"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop rule-matches"|"desktop hyprctl rulematches")
      grep -q "Orizon desktop rule matches" "`$OUT" && grep -q "windowrulev2" "`$OUT" && grep -q "safe-actions=tile" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing desktop rule matches"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j rulematches")
      grep -Fq '"rules":[' "`$OUT" && grep -q '"safeAction":' "`$OUT" && grep -Fq '"summary":{' "`$OUT" || { echo "missing desktop json rule matches"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop keyword general:gaps_in 9"|"desktop hyprctl keyword decoration:rounding 11"|"desktop hyprctl keyword decoration:shadow:range 22"|"desktop hyprctl keyword animations:tick_budget 24")
      grep -q "desktop keyword: applied" "`$OUT" || { echo "missing desktop keyword"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl activeworkspace")
      grep -q "active workspace:" "`$OUT" && grep -q "layout:" "`$OUT" || { echo "missing hyprctl activeworkspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j clients")
      grep -q '"hyprlandStyleFacade":true' "`$OUT" && grep -q '"address":"0x' "`$OUT" && grep -q '"floating":false' "`$OUT" || { echo "missing hyprctl json clients"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j activewindow")
      grep -q '"fullscreenClient":' "`$OUT" && grep -q '"workspace":{"id":' "`$OUT" || { echo "missing hyprctl json activewindow"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j workspaces")
      grep -q '"windows":' "`$OUT" && grep -q '"monitor":"Orizon framebuffer"' "`$OUT" && grep -q '"pinnedAware":true' "`$OUT" || { echo "missing hyprctl json workspaces"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j activeworkspace")
      grep -q '"active":true' "`$OUT" && grep -q '"hyprlandStyleFacade":true' "`$OUT" || { echo "missing hyprctl json activeworkspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j focushistory")
      grep -Fq '"history":[' "`$OUT" && grep -q '"focusHistoryID":' "`$OUT" && grep -q '"scope":' "`$OUT" || { echo "missing hyprctl json focushistory"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j workspacestack")
      grep -Fq '"workspaces":[' "`$OUT" && grep -Fq '"stack":[' "`$OUT" && grep -q '"pinnedAware":true' "`$OUT" && grep -q '"manualDrag":false' "`$OUT" || { echo "missing hyprctl json workspacestack"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j layoutstate")
      grep -q '"model":"per-workspace tiling layout state"' "`$OUT" && grep -Fq '"workspaces":[' "`$OUT" && grep -q '"manualDrag":false' "`$OUT" || { echo "missing hyprctl json layoutstate"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl -j layouttree")
      grep -q '"model":"active workspace tiling tree"' "`$OUT" && grep -Fq '"nodes":[' "`$OUT" && grep -q '"floatingSceneGraph":false' "`$OUT" || { echo "missing hyprctl json layouttree"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl cursorpos")
      grep -q "cursorpos:" "`$OUT" && grep -q "profile:" "`$OUT" || { echo "missing hyprctl cursorpos"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl splash")
      grep -q "Orizon desktop splash" "`$OUT" || { echo "missing hyprctl splash"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption general:gaps_in")
      grep -q "option general:gaps_in" "`$OUT" && grep -q "value: 9" "`$OUT" || { echo "missing hyprctl getoption gaps"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption decoration:rounding")
      grep -q "option decoration:rounding" "`$OUT" && grep -q "value: 11" "`$OUT" || { echo "missing hyprctl getoption rounding"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption render:focus_ring")
      grep -q "option render:focus_ring" "`$OUT" && grep -q "value: false" "`$OUT" || { echo "missing hyprctl getoption focus ring"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption render:profile")
      grep -q "option render:profile" "`$OUT" && grep -q "value: performance" "`$OUT" || { echo "missing hyprctl getoption render profile"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption decoration:shadow:range")
      grep -q "option decoration:shadow:range" "`$OUT" && grep -q "value: 22" "`$OUT" || { echo "missing hyprctl getoption shadow range"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption animations:tick_budget")
      grep -q "option animations:tick_budget" "`$OUT" && grep -q "value: 24" "`$OUT" || { echo "missing hyprctl getoption animation budget"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption env")
      grep -q "option env" "`$OUT" && grep -q "value: ORIZON_DESKTOP_SOURCE,1" "`$OUT" || { echo "missing hyprctl getoption env"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl getoption workspace")
      grep -q "option workspace" "`$OUT" && grep -q "value: 1, default:true" "`$OUT" || { echo "missing hyprctl getoption workspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl binds")
      grep -q "Orizon desktop binds" "`$OUT" && grep -q "/system/desktop-binds.conf" "`$OUT" || { echo "missing hyprctl binds"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl layers")
      grep -q "Orizon desktop layers" "`$OUT" && grep -q "namespace=launcher" "`$OUT" || { echo "missing hyprctl layers"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop autostart")
      grep -q "Orizon desktop autostart" "`$OUT" && grep -q "terminal:" "`$OUT" || { echo "missing desktop autostart"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop autostart terminal off"|"desktop autostart terminal on")
      grep -q "desktop session: updated" "`$OUT" || { echo "missing desktop autostart update"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch exec terminal")
      grep -q "desktop dispatch: exec terminal client spawned" "`$OUT" || { echo "missing desktop dispatch exec"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch focuswindow class:orizon-terminal"|"desktop focus-window title:Terminal")
      grep -q "desktop dispatch: focuswindow ok" "`$OUT" && grep -q "tiled=yes" "`$OUT" || { echo "missing desktop dispatch focuswindow"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch focuscurrentorlast")
      grep -q "desktop dispatch: focuscurrentorlast ok" "`$OUT" || { echo "missing desktop dispatch focuscurrentorlast"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch markurgent on")
      grep -q "desktop dispatch: markurgent on" "`$OUT" && grep -q "diagnostic=vm-only" "`$OUT" || { echo "missing desktop dispatch markurgent"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop activewindow")
      grep -q "activewindow:" "`$OUT" && grep -q "urgent: true" "`$OUT" || { echo "missing urgent activewindow diagnostic"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop focus-history")
      grep -q "Orizon desktop focus history" "`$OUT" && grep -q "urgent=true" "`$OUT" || { echo "missing urgent focus history"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch focusurgentorlast")
      grep -q "desktop dispatch: focusurgentorlast ok mode=urgent" "`$OUT" || { echo "missing desktop dispatch focusurgentorlast"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch cyclenext")
      grep -q "desktop dispatch: cyclenext" "`$OUT" || { echo "missing desktop dispatch cyclenext"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch swapnext")
      grep -q "desktop dispatch: swapnext" "`$OUT" || { echo "missing desktop dispatch swapnext"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movefocus r")
      grep -q "desktop dispatch: movefocus" "`$OUT" || { echo "missing desktop dispatch movefocus directional"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch swapwindow l")
      grep -q "desktop dispatch: swapwindow" "`$OUT" || { echo "missing desktop dispatch swapwindow"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch togglesplit")
      grep -q "desktop dispatch: togglesplit" "`$OUT" && grep -q "split=" "`$OUT" || { echo "missing desktop dispatch togglesplit"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg layout master")
      grep -q "desktop dispatch: layoutmsg layout master" "`$OUT" && grep -q "workspace=" "`$OUT" || { echo "missing desktop dispatch layoutmsg layout"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg layout monocle")
      grep -q "desktop dispatch: layoutmsg layout monocle" "`$OUT" && grep -q "workspace=" "`$OUT" || { echo "missing desktop dispatch layoutmsg monocle"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg splitratio 60")
      grep -q "desktop dispatch: layoutmsg splitratio 60" "`$OUT" || { echo "missing desktop dispatch layoutmsg"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg nmaster 2")
      grep -q "desktop dispatch: layoutmsg nmaster 2" "`$OUT" && grep -q "master=" "`$OUT" || { echo "missing desktop dispatch layoutmsg nmaster"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg addmaster")
      grep -q "desktop dispatch: layoutmsg addmaster nmaster=3" "`$OUT" || { echo "missing desktop dispatch layoutmsg addmaster"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg removemaster")
      grep -q "desktop dispatch: layoutmsg removemaster nmaster=2" "`$OUT" || { echo "missing desktop dispatch layoutmsg removemaster"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg splitratio reset")
      grep -q "desktop dispatch: layoutmsg splitratio 50" "`$OUT" && grep -q "reset=yes" "`$OUT" || { echo "missing desktop dispatch layoutmsg splitratio reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg masterratio reset")
      grep -q "desktop dispatch: layoutmsg masterratio 58" "`$OUT" && grep -q "reset=yes" "`$OUT" || { echo "missing desktop dispatch layoutmsg masterratio reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg nmaster reset")
      grep -q "desktop dispatch: layoutmsg nmaster 1" "`$OUT" && grep -q "reset=yes" "`$OUT" || { echo "missing desktop dispatch layoutmsg nmaster reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg reset")
      grep -q "desktop dispatch: layoutmsg layout reset" "`$OUT" && grep -q "default-layout=" "`$OUT" || { echo "missing desktop dispatch layoutmsg reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg preselect r")
      grep -q "desktop dispatch: layoutmsg preselect r" "`$OUT" && grep -q "split=vertical" "`$OUT" || { echo "missing desktop dispatch layoutmsg preselect r"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg preselect up")
      grep -q "desktop dispatch: layoutmsg preselect up" "`$OUT" && grep -q "split=horizontal" "`$OUT" || { echo "missing desktop dispatch layoutmsg preselect up"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch layoutmsg preselect reset")
      grep -q "desktop dispatch: layoutmsg preselect reset" "`$OUT" && grep -q "split=auto" "`$OUT" || { echo "missing desktop dispatch layoutmsg preselect reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch submap resize")
      grep -q "desktop dispatch: submap resize" "`$OUT" || { echo "missing desktop dispatch submap"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl submap")
      grep -q "submap:" "`$OUT" || { echo "missing hyprctl submap"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl submap reset")
      grep -q "desktop dispatch: submap default" "`$OUT" || { echo "missing hyprctl submap reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch fullscreen")
      grep -q "desktop dispatch: fullscreen" "`$OUT" || { echo "missing desktop dispatch fullscreen"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch fullscreen off")
      grep -q "desktop dispatch: fullscreen off" "`$OUT" || { echo "missing desktop dispatch fullscreen off"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch fullscreenstate 1")
      grep -q "desktop dispatch: fullscreenstate on" "`$OUT" || { echo "missing desktop dispatch fullscreenstate"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch fullscreenstate 2 0")
      grep -q "desktop dispatch: fullscreenstate internal=2 client=0" "`$OUT" || { echo "missing desktop dispatch fullscreenstate split"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch fullscreenstate -1 2")
      grep -q "desktop dispatch: fullscreenstate internal=2 client=2" "`$OUT" || { echo "missing desktop dispatch fullscreenstate keep-current"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch fullscreenstate 0 0")
      grep -q "desktop dispatch: fullscreenstate internal=0 client=0" "`$OUT" || { echo "missing desktop dispatch fullscreenstate reset"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch pseudo")
      grep -q "desktop dispatch: pseudo" "`$OUT" || { echo "missing desktop dispatch pseudo"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch pseudo off")
      grep -q "desktop dispatch: pseudo off" "`$OUT" || { echo "missing desktop dispatch pseudo off"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch pseudotile on")
      grep -q "desktop dispatch: pseudotile on" "`$OUT" || { echo "missing desktop dispatch pseudotile on"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch pin")
      grep -q "desktop dispatch: pin" "`$OUT" || { echo "missing desktop dispatch pin"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch pin off")
      grep -q "desktop dispatch: pin off" "`$OUT" || { echo "missing desktop dispatch pin off"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch pin on")
      grep -q "desktop dispatch: pin on" "`$OUT" || { echo "missing desktop dispatch pin on"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl clients")
      grep -q "Orizon desktop windows" "`$OUT" || { echo "missing desktop hyprctl clients"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl activewindow")
      grep -q "activewindow:" "`$OUT" && grep -q "fullscreenClient:" "`$OUT" || { echo "missing desktop hyprctl activewindow"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop hyprctl monitors")
      grep -q "Monitor 0" "`$OUT" || { echo "missing desktop hyprctl monitors"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop windows")
      grep -q "Orizon desktop windows" "`$OUT" && grep -q "layout:" "`$OUT" || { echo "missing desktop windows"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop workspace")
      grep -q "Orizon desktop workspaces" "`$OUT" && grep -q "active:" "`$OUT" || { echo "missing desktop workspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop workspace 2")
      grep -q "desktop: workspace 2 active" "`$OUT" || { echo "missing desktop workspace switch"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movetoworkspacesilent empty")
      grep -q "desktop dispatch: silently moved active to workspace" "`$OUT" && grep -q "follow=no active=2" "`$OUT" || { echo "missing desktop dispatch movetoworkspacesilent"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch tagwindow +settings class:orizon-settings")
      grep -q "desktop dispatch: tagwindow set" "`$OUT" && grep -q 'tag="settings"' "`$OUT" && grep -q 'selector="class:orizon-settings"' "`$OUT" || { echo "missing desktop tagwindow"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch focuswindow tag:settings")
      grep -q "desktop dispatch: focuswindow ok" "`$OUT" && grep -q 'target="tag:settings"' "`$OUT" || { echo "missing desktop tag focuswindow"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movetoworkspacesilent 2,tag:settings")
      grep -q "desktop dispatch: silently moved selected to workspace 2" "`$OUT" && grep -q 'selector="tag:settings"' "`$OUT" && grep -q "follow=no" "`$OUT" || { echo "missing desktop tag movetoworkspacesilent"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movetoworkspacesilent 2,class:orizon-settings")
      grep -q "desktop dispatch: silently moved selected to workspace 2" "`$OUT" && grep -q 'selector="class:orizon-settings"' "`$OUT" && grep -q "follow=no" "`$OUT" || { echo "missing desktop selected movetoworkspacesilent"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movetoworkspace active,activewindow")
      grep -q "desktop dispatch: moved selected to workspace" "`$OUT" && grep -q 'selector="activewindow"' "`$OUT" && grep -q "follow=yes" "`$OUT" || { echo "missing desktop activewindow movetoworkspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch workspace next")
      grep -q "desktop dispatch: workspace 3" "`$OUT" || { echo "missing desktop dispatch workspace next"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch workspace empty")
      grep -q "desktop dispatch: workspace 4" "`$OUT" || { echo "missing desktop dispatch workspace empty"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop workspace empty")
      grep -q "desktop dispatch: workspace" "`$OUT" || { echo "missing desktop workspace empty"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movetoworkspace 2")
      grep -q "desktop dispatch: moved active to workspace 2" "`$OUT" && grep -q "follow=yes active=2" "`$OUT" || { echo "missing desktop dispatch movetoworkspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch workspace 2")
      grep -q "desktop dispatch: workspace 2" "`$OUT" || { echo "missing desktop dispatch workspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch workspace previous")
      grep -q "desktop dispatch: workspace 1" "`$OUT" || { echo "missing desktop dispatch workspace previous"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch workspace +1")
      grep -q "desktop dispatch: workspace 2" "`$OUT" || { echo "missing desktop dispatch workspace relative"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch renameworkspace 2 dev")
      grep -q 'desktop dispatch: renameworkspace 2 name="dev"' "`$OUT" || { echo "missing desktop dispatch renameworkspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch workspace name:dev")
      grep -q "desktop dispatch: workspace 2" "`$OUT" && grep -q 'name="dev"' "`$OUT" || { echo "missing desktop named workspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movetoworkspace name:dev")
      grep -q "desktop dispatch: moved active to workspace 2" "`$OUT" && grep -q 'name="dev"' "`$OUT" && grep -q "follow=yes active=2" "`$OUT" || { echo "missing desktop named movetoworkspace"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch movefocus next")
      grep -q "desktop dispatch: movefocus" "`$OUT" || { echo "missing desktop dispatch movefocus"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop shortcuts")
      grep -q "F1" "`$OUT" && grep -q "F2" "`$OUT" || { echo "missing desktop shortcuts"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop doctor")
      grep -q "desktop doctor:" "`$OUT" && grep -q "summary:" "`$OUT" || { echo "missing desktop doctor"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop logs")
      grep -q "desktop log:" "`$OUT" || { echo "missing desktop logs"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop theme moss"|"desktop wallpaper dawn"|"desktop layout master"|"desktop focus toggle"|"desktop bar toggle")
      grep -q "desktop session: updated" "`$OUT" || { echo "missing desktop session update"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop apply")
      grep -q "desktop: session reloaded" "`$OUT" || { echo "missing desktop apply output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop dispatch exec settings")
      grep -q "desktop dispatch: exec orizon-settings client spawned" "`$OUT" || { echo "missing desktop dispatch exec settings"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop launcher show")
      grep -q "desktop: launcher open" "`$OUT" || { echo "missing desktop launcher output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop launch terminal")
      grep -Eq "desktop: launched terminal|exec orizon-terminal client spawned" "`$OUT" || { echo "missing desktop launch output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop launch launcher")
      grep -q "orizon-launcher overlay toggled" "`$OUT" || { echo "missing desktop launch launcher output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg info orizon-desktop-hypr")
      grep -q "orizon-desktop-hypr" "`$OUT" && grep -Eq "available optional|state installed" "`$OUT" || { echo "missing desktop package info"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "desktop package")
      grep -q "orizon-desktop-hypr.opkg" "`$OUT" || { echo "missing desktop package output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg simulate /workspace/packages/orizon-desktop-hypr.opkg")
      grep -q "pkg simulate:" "`$OUT" && grep -q "dry-run" "`$OUT" || { echo "missing desktop pkg simulate output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg verify /workspace/packages/orizon-desktop-hypr.opkg")
      grep -q "package verify: OK" "`$OUT" || { echo "missing desktop pkg verify output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
      ;;
    "pkg install orizon-desktop-hypr")
      grep -Eq "unavailable in live boot|pkg named install: orizon-desktop-hypr" "`$OUT" || { echo "missing named desktop package install output"; rm -f "`$ASKPASS" "`$PASSFILE" "`$OUT"; exit 1; }
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
