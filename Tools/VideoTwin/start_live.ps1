# Start the camera -> HLS converter (MediaMTX) for the video twin.
#
#   powershell -ExecutionPolicy Bypass -File start_live.ps1
#
# Works from any folder: it uses the mediamtx.videotwin.yml next to it, and mediamtx.exe next to it
# (the deployed kit) or, on the development machine, the copy under the project's Saved folder.
#
# The camera password is taken from the environment variable OFFICE_CAM_PASSWORD and handed to MediaMTX
# through its own environment overrides (MTX_PATHS_<NAME>_SOURCE). It is never written to a file and does
# not appear on the process command line. Set it once per user:
#   [Environment]::SetEnvironmentVariable('OFFICE_CAM_PASSWORD', '<password>', 'User')
#
# HLS is served on 127.0.0.1:8888 only. The footage shows identifiable people; Unreal runs on this same
# machine, so nothing needs to listen on the network. Pass -Listen <ip>:8888 only if Unreal runs elsewhere.
#
# Pure ASCII on purpose: Windows PowerShell 5.1 reads .ps1 files as the system ANSI code page.

[CmdletBinding()]
param(
    [string]$Listen = "127.0.0.1:8888",
    [string]$Exe = "",
    [string]$CameraInterface = ""
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$template = Join-Path $here 'mediamtx.videotwin.yml'
if (-not (Test-Path $template)) { throw "mediamtx.videotwin.yml is missing next to this script ($here)." }

$run = $here
if (-not $Exe) {
    if (Test-Path (Join-Path $here 'mediamtx.exe')) {
        $Exe = Join-Path $here 'mediamtx.exe'
    } else {
        # development layout: <project>\Tools\VideoTwin\start_live.ps1, exe kept out of git under Saved
        $project = Split-Path -Parent (Split-Path -Parent $here)
        foreach ($c in @((Join-Path $project 'Saved\VideoTwinMVP\live\mediamtx.exe'), (Join-Path $project 'Saved\VideoTwinMVP\live_test\mediamtx.exe'))) {
            if (Test-Path $c) { $Exe = $c; $run = Join-Path $project 'Saved\VideoTwinMVP\live'; break }
        }
    }
}
if (-not $Exe -or -not (Test-Path $Exe)) { throw "mediamtx.exe not found. Put it next to this script or pass -Exe <path>." }
New-Item -ItemType Directory -Force $run | Out-Null

# Optional local-only configuration for machines whose VPN captures the camera subnet.
$networkConfig = Join-Path $run 'network.json'
if (-not $CameraInterface -and (Test-Path -LiteralPath $networkConfig)) {
    $CameraInterface = (Get-Content -LiteralPath $networkConfig -Raw | ConvertFrom-Json).camera_interface
}
$relayRoutes = @()
if ($CameraInterface) {
    $cameraAdapter = Get-NetIPConfiguration -InterfaceAlias $CameraInterface -ErrorAction Stop
    $cameraSourceIP = @($cameraAdapter.IPv4Address)[0].IPAddress
    if (-not $cameraSourceIP) { throw 'Camera interface has no IPv4 address.' }
}

$camPwd = [Environment]::GetEnvironmentVariable('OFFICE_CAM_PASSWORD', 'Process')
if (-not $camPwd) { $camPwd = [Environment]::GetEnvironmentVariable('OFFICE_CAM_PASSWORD', 'User') }
if (-not $camPwd) { $camPwd = [Environment]::GetEnvironmentVariable('OFFICE_CAM_PASSWORD', 'Machine') }
if (-not $camPwd) { throw "OFFICE_CAM_PASSWORD is not set. See the comment at the top of this script." }

# One override per camera path found in the template: the address comes from the file, the password from the environment.
$name = $null
$count = 0
foreach ($line in Get-Content $template) {
    if ($line -match '^\s{2}([A-Za-z0-9_]+):\s*$') { $name = $Matches[1]; continue }
    if ($name -and $line -match '^\s{4}source:\s*(\S+)\s*$') {
        $url = $Matches[1].Replace('CHANGE_ME', [Uri]::EscapeDataString($camPwd))
        if ($CameraInterface) {
            $cameraUri = [Uri]$url
            $localPort = 18554 + $count
            $relayRoutes += @{host=$cameraUri.Host; port=$cameraUri.Port; local_port=$localPort}
            $builder = [UriBuilder]$cameraUri
            $builder.Host = '127.0.0.1'
            $builder.Port = $localPort
            $url = $builder.Uri.AbsoluteUri
        }
        [Environment]::SetEnvironmentVariable(('MTX_PATHS_{0}_SOURCE' -f $name.ToUpper()), $url, 'Process')
        $count++
        $name = $null
    }
}
if ($count -eq 0) { throw "No camera paths with a source were found in $template." }
if ($CameraInterface) {
    $relayScript = Join-Path $here 'camera_tcp_relay.py'
    $routesFile = Join-Path $run 'camera_relay_routes.json'
    $relayRoutes | ConvertTo-Json | Set-Content -LiteralPath $routesFile -Encoding utf8
    $python = (Get-Command python.exe -ErrorAction Stop).Source
    Get-CimInstance Win32_Process -Filter "Name='python.exe'" | Where-Object {
        $_.CommandLine -like "*$relayScript*" -and $_.CommandLine -like "*$routesFile*"
    } | ForEach-Object {
        $oldRelay = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue
        if ($oldRelay) { Stop-Process -InputObject $oldRelay -Force; $oldRelay.WaitForExit(10000) | Out-Null }
    }
    $relayArgs = '-u "{0}" --routes "{1}" --interface-index {2} --source-ip {3}' -f $relayScript,$routesFile,$cameraAdapter.InterfaceIndex,$cameraSourceIP
    $relay = Start-Process -FilePath $python -ArgumentList $relayArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'camera_relay.log') -RedirectStandardError (Join-Path $run 'camera_relay.err')
    if ($relay.WaitForExit(1000)) { throw 'Camera relay failed to start; inspect camera_relay.err.' }
}
[Environment]::SetEnvironmentVariable('MTX_HLSADDRESS', $Listen, 'Process')
[Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS', 'file', 'Process')
[Environment]::SetEnvironmentVariable('MTX_LOGFILE', (Join-Path $run 'mediamtx.log'), 'Process')

# Replace an earlier instance started from this same template (matched by its path, nothing else).
Get-CimInstance Win32_Process -Filter "Name='mediamtx.exe'" | Where-Object { $_.CommandLine -like "*$template*" } | ForEach-Object {
    $previous = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue
    if ($previous) {
        Stop-Process -InputObject $previous -Force
        if (-not $previous.WaitForExit(10000)) { throw 'Previous MediaMTX process did not exit; refusing to start a second listener.' }
    }
}

Write-Output ("video twin live: {0} camera(s), HLS on {1}, log {2}" -f $count, $Listen, (Join-Path $run 'mediamtx.log'))
Set-Location $run
& $Exe $template
