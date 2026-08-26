$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$server = Join-Path $projectRoot 'build\Debug\homecloud_api.exe'
$webRoot = Join-Path $projectRoot 'web'
$storageRoot = if ($env:HOMECLOUD_STORAGE_ROOT) { $env:HOMECLOUD_STORAGE_ROOT } else { Join-Path $projectRoot 'data' }

$apiRunning = $false
try {
    $health = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:8080/api/v1/health' -TimeoutSec 2
    if ($health.StatusCode -eq 200) { $apiRunning = $true }
} catch {}

if (-not $apiRunning) {
    if (-not (Test-Path -LiteralPath $server)) { throw "HomeCloud server executable is missing: $server" }
    if (-not (Test-Path -LiteralPath $storageRoot)) { throw "HomeCloud storage is missing: $storageRoot" }

    Start-Process -FilePath $server -ArgumentList @($storageRoot, $webRoot) `
        -WorkingDirectory $projectRoot -WindowStyle Hidden
}

& (Join-Path $PSScriptRoot 'start_module_service.ps1') -BindAddress '0.0.0.0' -Port 8081
