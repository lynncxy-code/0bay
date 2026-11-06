param([int]$Duration=30)
& (Join-Path $PSScriptRoot '.venv\Scripts\python.exe') (Join-Path $PSScriptRoot 'go2_telemetry_bridge.py') --synthetic --duration $Duration
