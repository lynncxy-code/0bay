# Start the camera -> HLS converter (MediaMTX) for the video twin.
#
#   powershell -ExecutionPolicy Bypass -File Tools\VideoTwin\start_live.ps1
#
# The camera password is taken from the environment variable OFFICE_CAM_PASSWORD and handed to MediaMTX
# through its own environment overrides (MTX_PATHS_<NAME>_SOURCE). It is never written to a file.
# Set it once per user:   [Environment]::SetEnvironmentVariable('OFFICE_CAM_PASSWORD', '<password>', 'User')
#
# HLS is served on 127.0.0.1:8888 only. The footage shows identifiable people; Unreal runs on this same
# machine, so nothing needs to listen on the network. Pass -Listen <ip>:8888 if Unreal runs elsewhere.
#
# Pure ASCII on purpose: Windows PowerShell 5.1 reads .ps1 files as the system ANSI code page.

[CmdletBinding()]
param(
    [string]$Listen = "127.0.0.1:8888",
    [string]$Exe = ""
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = (Resolve-Path (Join-Path $here '..\..')).Path
$run = Join-Path $project 'Saved\VideoTwinMVP\live'
New-Item -ItemType Directory -Force $run | Out-Null

if (-not $Exe) {
    foreach ($c in @((Join-Path $run 'mediamtx.exe'), (Join-Path $project 'Saved\VideoTwinMVP\live_test\mediamtx.exe'))) {
        if (Test-Path $c) { $Exe = $c; break }
    }
}
if (-not $Exe -or -not (Test-Path $Exe)) { throw "mediamtx.exe not found. Put it in $run or pass -Exe <path>." }

$camPwd = [Environment]::GetEnvironmentVariable('OFFICE_CAM_PASSWORD', 'Process')
if (-not $camPwd) { $camPwd = [Environment]::GetEnvironmentVariable('OFFICE_CAM_PASSWORD', 'User') }
if (-not $camPwd) { throw "OFFICE_CAM_PASSWORD is not set. See the comment at the top of this script." }

# One override per camera path found in the template: the address comes from the file, the password from the environment.
$template = Join-Path $here 'mediamtx.videotwin.yml'
$name = $null
$count = 0
foreach ($line in Get-Content $template) {
    if ($line -match '^\s{2}([A-Za-z0-9_]+):\s*$') { $name = $Matches[1]; continue }
    if ($name -and $line -match '^\s{4}source:\s*(\S+)\s*$') {
        $url = $Matches[1].Replace('CHANGE_ME', [Uri]::EscapeDataString($camPwd))
        [Environment]::SetEnvironmentVariable(('MTX_PATHS_{0}_SOURCE' -f $name.ToUpper()), $url, 'Process')
        $count++
        $name = $null
    }
}
if ($count -eq 0) { throw "No camera paths with a source were found in $template." }
[Environment]::SetEnvironmentVariable('MTX_HLSADDRESS', $Listen, 'Process')
[Environment]::SetEnvironmentVariable('MTX_LOGDESTINATIONS', 'file', 'Process')
[Environment]::SetEnvironmentVariable('MTX_LOGFILE', (Join-Path $run 'mediamtx.log'), 'Process')

# Replace an earlier instance started by this script (matched by this template's path, nothing else).
Get-CimInstance Win32_Process -Filter "Name='mediamtx.exe'" | Where-Object { $_.CommandLine -like "*mediamtx.videotwin.yml*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

Write-Output ("video twin live: {0} camera(s), HLS on {1}, log {2}" -f $count, $Listen, (Join-Path $run 'mediamtx.log'))
Set-Location $run
& $Exe $template
