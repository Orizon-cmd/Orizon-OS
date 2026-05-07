[CmdletBinding()]
param(
    [string]$EnvFile = "",
    [int]$LocalPort = 5900,
    [int]$RemotePort = 5900
)

function Get-DotEnvMap {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "Env file not found: $Path"
    }

    $map = @{}
    foreach ($line in Get-Content $Path) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            continue
        }

        $parts = $trimmed -split "=", 2
        if ($parts.Count -ne 2) {
            continue
        }

        $map[$parts[0].Trim()] = $parts[1].Trim()
    }

    return $map
}

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $EnvFile) {
    $EnvFile = Join-Path $scriptRoot "..\\..\\config\\hosts\\zimaos.local.env"
}

$config = Get-DotEnvMap -Path $EnvFile
$target = if ($config.ContainsKey("ZIMAOS_SSH_ALIAS") -and $config["ZIMAOS_SSH_ALIAS"]) {
    $config["ZIMAOS_SSH_ALIAS"]
} else {
    "$($config['ZIMAOS_USER'])@$($config['ZIMAOS_HOST'])"
}

Write-Host "Opening SSH tunnel to $target on local port $LocalPort ..."
Write-Host "Leave this window open, then connect your VNC client to 127.0.0.1:$LocalPort"

& ssh -L "${LocalPort}:localhost:${RemotePort}" $target -N
exit $LASTEXITCODE
