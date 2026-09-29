@echo off
setlocal
cd /d "%~dp0"

rem Pico Connect OpenXR, not SteamVR. Stale SteamVR compositor reports a fake GPU mismatch.
set "XR_RUNTIME_JSON=D:\New folder (2)\PICO Connect\openvr_driver\resources\ps_xrt\picostreaming-openxr.json"
reg add "HKCU\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime /t REG_SZ /d "%XR_RUNTIME_JSON%" /f >nul 2>&1

taskkill /F /IM steamvr_room_setup.exe >nul 2>&1
taskkill /F /IM vrmonitor.exe >nul 2>&1
taskkill /F /IM vrserver.exe >nul 2>&1
taskkill /F /IM vrcompositor.exe >nul 2>&1
taskkill /F /IM vrdashboard.exe >nul 2>&1
taskkill /F /IM vrwebhelper.exe >nul 2>&1

start "" "D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0ShanghaiOfficeShell_OpenSteel.uproject" -graphicsadapter=0
