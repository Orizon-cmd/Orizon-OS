[CmdletBinding()]
param(
    [string]$EnvFile = "",
    [string]$RemoteCommand = "hostname; uname -a; df -h /"
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

& ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new $target $RemoteCommand
exit $LASTEXITCODE
