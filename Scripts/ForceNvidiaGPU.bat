@echo off
setlocal
set PREF=HKCU\Software\Microsoft\DirectX\UserGpuPreferences
echo Forcing NVIDIA (High performance) GPU for Unreal / SteamVR / PICO Connect...

reg add "%PREF%" /v "D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" /t REG_SZ /d "GpuPreference=2;" /f
reg add "%PREF%" /v "D:\Steam\steamapps\common\SteamVR\bin\win64\vrcompositor.exe" /t REG_SZ /d "GpuPreference=2;" /f
reg add "%PREF%" /v "D:\Steam\steamapps\common\SteamVR\bin\win64\vrserver.exe" /t REG_SZ /d "GpuPreference=2;" /f
reg add "%PREF%" /v "D:\Steam\steamapps\common\SteamVR\bin\win64\vrmonitor.exe" /t REG_SZ /d "GpuPreference=2;" /f
reg add "%PREF%" /v "D:\New folder (2)\PICO Connect\PICO Connect.exe" /t REG_SZ /d "GpuPreference=2;" /f

echo.
echo Done. Close Unreal and SteamVR, then open F:\Office_VR\StartEditor.bat
echo If the OpenXR GPU dialog appears, click Cancel. Do not restart from that dialog.
echo.
pause
