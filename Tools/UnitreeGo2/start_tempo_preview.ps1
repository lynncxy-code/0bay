$ErrorActionPreference = 'Stop'
$projectFile = Join-Path $PSScriptRoot '..\..\ShanghaiOfficeShell_OpenSteel.uproject'
$editorExe = 'D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe'
if (!(Test-Path -LiteralPath $editorExe)) { throw "Unreal Editor not found: $editorExe" }
$projectFile = (Resolve-Path -LiteralPath $projectFile).Path
# A separate game window leaves the user's current editor and unsaved scene alone.
Start-Process -FilePath $editorExe -ArgumentList @("`"$projectFile`"", '/Game/Robots/UnitreeGo2/Maps/L_Go2_TempoPreview', '-game', '-windowed', '-ResX=1440', '-ResY=900', '-NoSplash', '-NoSound')
