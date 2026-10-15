# Start the one-click kit (backend + 3D office) by itself every time this user logs on.
#
#   powershell -ExecutionPolicy Bypass -File register_autostart.ps1           register (or update)
#   powershell -ExecutionPolicy Bypass -File register_autostart.ps1 -Remove   unregister
#
# Put this file in the kit's VideoTwinLive folder: the kit is the folder above it, and the launcher is the one
# .exe at the top of the kit. Registers the scheduled task OfficeTwinAutoStart for the current user, logon trigger,
# 30 s delay (Docker Desktop starts from the user's Run key at the same moment; the kit's start script waits up to
# 5 minutes for it anyway). The task runs in the user's own desktop session: Unreal needs it, and so does the
# screen capture that feeds the tablets. If the scene is already running the launcher just says so and exits.
#
# A logon trigger only fires once someone logs on. For power-on start the machine also needs automatic logon
# (Docs/VideoTwin_Onsite_Ops.md, section 2).
#
# Pure ASCII on purpose: Windows PowerShell 5.1 reads .ps1 files as the system ANSI code page.

[CmdletBinding()]
param([switch]$Remove, [string]$TaskName = 'OfficeTwinAutoStart')
$ErrorActionPreference = 'Stop'

if ($Remove) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Output "removed $TaskName"
    return
}

$kit = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$launcher = Get-ChildItem -LiteralPath $kit -Filter *.exe -File | Select-Object -First 1
if (-not $launcher -or -not (Test-Path -LiteralPath (Join-Path $kit 'Start-Runtime.ps1'))) {
    throw "No kit around this script: expected Start-Runtime.ps1 and the launcher .exe in $kit"
}
# Over SSH USERDOMAIN reads WORKGROUP, which does not resolve; the computer name does.
$who = $env:COMPUTERNAME + '\' + $env:USERNAME
$action = New-ScheduledTaskAction -Execute $launcher.FullName -WorkingDirectory $kit
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $who
$trigger.Delay = 'PT30S'
$principal = New-ScheduledTaskPrincipal -UserId $who -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 30) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings `
    -Description 'Starts the office 3D twin kit (backend + UE) 30 s after this user logs on.' -Force | Out-Null
Write-Output "registered $TaskName for $who -> $($launcher.FullName)"
Write-Output "start it now: schtasks /Run /TN $TaskName"
