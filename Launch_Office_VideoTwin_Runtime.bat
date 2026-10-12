@echo off
setlocal
set "UE_EDITOR=D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%UE_EDITOR%" (
  echo UE 5.6 was not found at %UE_EDITOR%
  pause
  exit /b 1
)
start "Office Video Twin Runtime" "%UE_EDITOR%" "%~dp0ShanghaiOfficeShell_OpenSteel.uproject" /Game/OfficeShell/Maps/Office_Demo -game -windowed -ResX=1600 -ResY=900 -NoSplash
