@echo off
setlocal
cd /d "%~dp0"
echo Exporting Office_Demo to OfficeWebXR/data
echo This can take many minutes. Leave this window open.
echo.
"D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0ShanghaiOfficeShell_OpenSteel.uproject" /Game/OfficeShell/Maps/Office_Demo -unattended -nopause -nosplash -log -ExecutePythonScript="F:/Office_VR/Scripts/ExportOfficeWebXR.py"
echo.
echo Exit code: %ERRORLEVEL%
if exist "OfficeWebXR\data\architecture.glb" echo architecture.glb is ready.
if exist "OfficeWebXR\data\export-report.json" echo See OfficeWebXR\data\export-report.json
pause
