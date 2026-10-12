# Install the camera -> HLS converter on the machine that runs the packaged Unreal build.
#
#   powershell -ExecutionPolicy Bypass -File install_live.ps1
#
# Run it from the kit folder (the one holding mediamtx.exe, mediamtx.videotwin.yml and start_live.ps1),
# logged in as the user who will run Unreal. It:
#   1. asks for the camera password once if OFFICE_CAM_PASSWORD is not set, and stores it as a per-user
#      environment variable (not in any file);
#   2. registers a scheduled task "VideoTwinLive" that starts the converter at logon;
#   3. starts it now and reports whether each camera could be reached.
#
# Remove again with:  schtasks /Delete /TN VideoTwinLive /F
#
# Pure ASCII on purpose: Windows PowerShell 5.1 reads .ps1 files as the system ANSI code page.

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($f in 'mediamtx.exe', 'mediamtx.videotwin.yml', 'start_live.ps1') {
    if (-not (Test-Path (Join-Path $here $f))) { throw "$f is missing from $here." }
}

if (-not [Environment]::GetEnvironmentVariable('OFFICE_CAM_PASSWORD', 'User')) {
    $sec = Read-Host -AsSecureString "Camera password (stored as a per-user environment variable, not in a file)"
    $plain = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))
    if (-not $plain) { throw "No password entered." }
    [Environment]::SetEnvironmentVariable('OFFICE_CAM_PASSWORD', $plain, 'User')
    Write-Output "OFFICE_CAM_PASSWORD stored for user $env:USERNAME."
} else {
    Write-Output "OFFICE_CAM_PASSWORD is already set for user $env:USERNAME."
}

$tn = "VideoTwinLive"
$action = "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$here\start_live.ps1`""
schtasks /Create /TN $tn /TR $action /SC ONLOGON /RU $env:USERNAME /IT /F | Out-Null
schtasks /Run /TN $tn | Out-Null
Write-Output "Scheduled task '$tn' registered (runs at logon) and started. Waiting for the cameras ..."
Start-Sleep -Seconds 16

$log = Join-Path $here 'mediamtx.log'
$proc = Get-CimInstance Win32_Process -Filter "Name='mediamtx.exe'" | Where-Object { $_.CommandLine -like "*$here*" }
if (-not $proc) { Write-Output "PROBLEM: the converter is not running. Check $log"; exit 1 }
$listen = Get-NetTCPConnection -OwningProcess $proc.ProcessId -State Listen -ErrorAction SilentlyContinue | ForEach-Object { "{0}:{1}" -f $_.LocalAddress, $_.LocalPort }
Write-Output ("Converter running (pid {0}), listening on: {1}" -f $proc.ProcessId, ($listen -join ', '))

$names = Select-String -Path (Join-Path $here 'mediamtx.videotwin.yml') -Pattern '^\s{2}([A-Za-z0-9_]+):\s*$' | ForEach-Object { $_.Matches[0].Groups[1].Value }
foreach ($n in $names) {
    $ok = Select-String -Path $log -Pattern "\[path $n\].*(ready|is available and online)" -Quiet
    $err = (Select-String -Path $log -Pattern "\[path $n\].*ERR" | Select-Object -Last 1).Line
    if ($ok) { Write-Output ("  {0,-8} CONNECTED" -f $n) }
    elseif ($err) { Write-Output ("  {0,-8} NOT reachable: {1}" -f $n, ($err -replace '^.*\] ', '')) }
    else { Write-Output ("  {0,-8} no result yet, see {1}" -f $n, $log) }
}
Write-Output "Unreal reads http://127.0.0.1:8888/<name>/index.m3u8 (see stream_url in VideoTwin\calibration.json)."
