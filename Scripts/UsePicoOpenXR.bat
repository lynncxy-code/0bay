@echo off
setlocal
set JSON=D:\New folder (2)\PICO Connect\openvr_driver\resources\ps_xrt\picostreaming-openxr.json
if not exist "%JSON%" (
  echo Missing Pico OpenXR runtime:
  echo %JSON%
  pause
  exit /b 1
)

echo Stopping stale SteamVR...
taskkill /F /IM steamvr_room_setup.exe >nul 2>&1
taskkill /F /IM vrmonitor.exe >nul 2>&1
taskkill /F /IM vrserver.exe >nul 2>&1
taskkill /F /IM vrcompositor.exe >nul 2>&1
taskkill /F /IM vrdashboard.exe >nul 2>&1
taskkill /F /IM vrwebhelper.exe >nul 2>&1

echo Setting OpenXR runtime to PICO Connect...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime /t REG_SZ /d "%JSON%" /f
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes" /v "%JSON%" /t REG_DWORD /d 0 /f
reg add "HKCU\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime /t REG_SZ /d "%JSON%" /f
setx XR_RUNTIME_JSON "%JSON%" >nul

echo.
echo Done. Close Unreal Editor completely, keep PICO Connect connected,
echo then reopen F:\Office_VR\ShanghaiOfficeShell_OpenSteel.uproject
echo and use VR Preview. Do not start SteamVR.
echo.
pause
