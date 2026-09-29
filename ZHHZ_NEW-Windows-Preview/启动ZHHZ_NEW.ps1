$ErrorActionPreference = "Stop"
$PackageDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WebDir = Join-Path $PackageDir "web"
$DataDir = Join-Path $PackageDir "data"
$Port = if ($env:ZHHZ_PORT) { $env:ZHHZ_PORT } else { "4192" }
$Backend = if ($env:TWIN_BACKEND) { $env:TWIN_BACKEND } else { "http://localhost:5000" }
$env:HOST = "127.0.0.1"
$env:PORT = $Port
$env:DATA_ROOT = $DataDir
$env:TWIN_BACKEND = $Backend
$server = Start-Process -FilePath "node" -ArgumentList @((Join-Path $WebDir "server.mjs")) -WindowStyle Hidden -PassThru
try {
  Start-Sleep -Seconds 1
  $Url = "http://127.0.0.1:$Port/zhhz-new.html?lightmaps=full&twinDetail=all&roam=1&collision=0&show=xinchuang-v71"
  Start-Process $Url
  Write-Host "ZHHZ_NEW 信创全高清包已启动：$Url"
  Wait-Process -Id $server.Id
} finally {
  Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
}
