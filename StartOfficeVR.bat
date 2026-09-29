@echo off
cd /d "%~dp0"
py -3 "%~dp0Scripts\StartOfficeVR.py"
if errorlevel 1 (
  echo.
  echo 启动失败。窗口可关。
  pause
)
