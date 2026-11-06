# Independent monitor service. Does not replace the existing VideoTwin fusion gateway.
[CmdletBinding()]
param(
    [string]$Listen = '127.0.0.1:8898',
    [string]$FFmpeg = '',
    [string]$MediaMTX = '',
    [ValidateSet('h264','copy')][string]$VideoMode = 'h264',
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$python = (Get-Command python.exe -ErrorAction Stop).Source
if (-not $env:OFFICE_CAM_PASSWORD -and -not $ValidateOnly) {
    $env:OFFICE_CAM_PASSWORD = [Environment]::GetEnvironmentVariable('OFFICE_CAM_PASSWORD', 'User')
}
$arguments = @((Join-Path $PSScriptRoot 'monitor_gateway.py'), '--listen', $Listen, '--video-mode', $VideoMode)
if ($FFmpeg) { $arguments += @('--ffmpeg', $FFmpeg) }
if ($MediaMTX) { $arguments += @('--mediamtx', $MediaMTX) }
if ($ValidateOnly) { $arguments += '--validate' }
& $python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Monitor service failed. Check its filtered gateway.log.' }
