$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
& 'D:\UE_5.6\Engine\Build\BatchFiles\Build.bat' OfficeHostEditor Win64 Development "-Project=$projectRoot\ShanghaiOfficeShell_OpenSteel.uproject" -Module=OfficeHost -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw 'OfficeHost build failed' }
# External plugins already supply their editor DLLs. UBT can leave a second copy
# in the host directory; preserve those generated copies outside DLL lookup paths.
$backupRoot = Join-Path $projectRoot ('Saved\VideoTwinMVP\DuplicateBuildModules_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
foreach ($moduleName in @('OntoTwinSync','OntoTwinSyncEditor','OntoTwinIndustrialBehavior')) {
    foreach ($suffix in @('dll','pdb')) {
        $sourcePath = Join-Path $projectRoot "Binaries\Win64\UnrealEditor-$moduleName.$suffix"
        $targetPath = Join-Path $backupRoot "UnrealEditor-$moduleName.$suffix"
        if (-not [IO.Path]::GetFullPath($sourcePath).StartsWith($projectRoot + '\') -or -not [IO.Path]::GetFullPath($targetPath).StartsWith($projectRoot + '\')) { throw 'Unexpected path outside project' }
        if (Test-Path -LiteralPath $sourcePath) {
            New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
            Move-Item -LiteralPath $sourcePath -Destination $targetPath
        }
    }
}
$manifestPath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor.modules'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($moduleName in @('OntoTwinSync','OntoTwinSyncEditor','OntoTwinIndustrialBehavior')) { $manifest.Modules.PSObject.Properties.Remove($moduleName) }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
