$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
# Build the editor target and local plugins together so their binaries stay compatible.
& 'D:\UE_5.6\Engine\Build\BatchFiles\Build.bat' OfficeHostEditor Win64 Development "-Project=$projectRoot\ShanghaiOfficeShell_OpenSteel.uproject" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw 'OfficeHost editor and local plugin build failed' }
