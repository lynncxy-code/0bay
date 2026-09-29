@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo OfficeWebXR
echo Folder: %CD%
echo.
echo Edge WebXR needs SteamVR. Unreal still uses StartEditor.bat / PicoStreamingXR.
echo Do not close this window. Do not run Unreal VR Preview at the same time.
echo.

where node >nul 2>&1
if errorlevel 1 goto no_node

if exist "node_modules\three\build\three.module.js" goto have_three
echo Installing three@0.185.1 ...
call npm install
if errorlevel 1 goto npm_fail
:have_three

echo Starting SteamVR. Keep PICO Connect connected.
node scripts\ensure-steamvr.mjs
if errorlevel 1 echo SteamVR did not appear. 进入 VR will stay in the browser until SteamVR shows the headset.
echo.

set "OPEN_BROWSER=1"
set "OFFICEWEBXR_RUNTIME=steamvr"
echo Starting server, then a dedicated Edge window.
echo After SteamVR shows the Pico headset, click 进入 VR in that new Edge window.
echo Head-lock should replace the webpage. If you still see the browser, SteamVR did not take the headset.
echo.
node server.mjs
echo.
echo Server stopped. Exit code: %ERRORLEVEL%
pause
exit /b %ERRORLEVEL%

:no_node
echo Node.js is not on PATH. Install Node.js, then run this script again.
pause
exit /b 1

:npm_fail
echo npm install failed.
pause
exit /b 1
