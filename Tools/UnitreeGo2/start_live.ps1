param(
    [Parameter(Mandatory=$true)][string]$InterfaceIP,
    [string]$RobotIP='192.168.123.18',
    [switch]$Multicast
)
$ErrorActionPreference='Stop'
$python=Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
$bridge=Join-Path $PSScriptRoot 'go2_telemetry_bridge.py'
$bridgeArgs=@($bridge,'--robot-ip',$RobotIP,'--interface-ip',$InterfaceIP)
if ($Multicast) { $bridgeArgs+='--multicast' }
& $python @bridgeArgs
