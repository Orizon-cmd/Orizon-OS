[CmdletBinding()]
param(
    [string]$EnvFile = "",
    [int]$LocalPort = 5900,
    [int]$RemotePort = 5900,
    [switch]$UseTunnel,
    [switch]$Balanced
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

function Find-TigerVncViewer {
    $candidates = @(
        "C:\Program Files\TigerVNC\vncviewer.exe",
        "C:\Program Files (x86)\TigerVNC\vncviewer.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "TigerVNC viewer was not found."
}

function Get-TunnelProcess {
    param([int]$Port)

    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -eq "ssh.exe" -and $_.CommandLine -match [regex]::Escape("${Port}:localhost:${Port}")
        } |
        Select-Object -First 1
}

function Ensure-Tunnel {
    param(
        [string]$Target,
        [int]$Port
    )

    $listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
    if ($listener) {
        return
    }

    $existing = Get-TunnelProcess -Port $Port
    if (-not $existing) {
        Start-Process -FilePath "ssh" `
            -ArgumentList @(
                "-o", "BatchMode=yes",
                "-o", "ExitOnForwardFailure=yes",
                "-L", "${Port}:localhost:${Port}",
                $Target,
                "-N"
            ) `
            -WindowStyle Hidden | Out-Null
    }

    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 300
        $listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
        if ($listener) {
            return
        }
    }

    throw "The SSH tunnel did not start on local port $Port."
}

function Test-VncReachability {
    param(
        [string]$HostName,
        [int]$Port
    )

    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $async = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne(1500)) {
            $client.Close()
            return $false
        }

        $client.EndConnect($async)
        $client.Close()
        return $true
    } catch {
        return $false
    }
}

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $EnvFile) {
    $EnvFile = Join-Path $scriptRoot "..\\..\\config\\hosts\\zimaos.local.env"
}

$config = Get-DotEnvMap -Path $EnvFile
$viewer = Find-TigerVncViewer

$viewerArgs = @()
if (-not $Balanced) {
    $viewerArgs += @(
        "-AutoSelect=0",
        "-PreferredEncoding=Raw",
        "-FullColor=1",
        "-Shared=1"
    )
}

$canUseDirectLan = Test-VncReachability -HostName $config["ZIMAOS_HOST"] -Port $RemotePort
if (-not $UseTunnel -and $canUseDirectLan) {
    $targetAddress = "$($config['ZIMAOS_HOST'])::${RemotePort}"
} else {
    $sshTarget = if ($config.ContainsKey("ZIMAOS_SSH_ALIAS") -and $config["ZIMAOS_SSH_ALIAS"]) {
        $config["ZIMAOS_SSH_ALIAS"]
    } else {
        "$($config['ZIMAOS_USER'])@$($config['ZIMAOS_HOST'])"
    }
    Ensure-Tunnel -Target $sshTarget -Port $LocalPort
    $targetAddress = "127.0.0.1::${LocalPort}"
}

$viewerArgs += $targetAddress
Start-Process -FilePath $viewer -ArgumentList $viewerArgs
