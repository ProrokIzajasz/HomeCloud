param(
    [ValidateSet('127.0.0.1', '0.0.0.0', '::')]
    [string]$BindAddress = '127.0.0.1',
    [ValidateRange(1024, 65535)]
    [int]$Port = 8081
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$server = Join-Path $projectRoot 'build\Debug\homecloud_modules.exe'
$storageRoot = if ($env:HOMECLOUD_STORAGE_ROOT) { $env:HOMECLOUD_STORAGE_ROOT } else { Join-Path $projectRoot 'data' }
$healthUrl = "http://127.0.0.1:$Port/api/hiphop/health"

try {
    $health = Invoke-WebRequest -UseBasicParsing -Uri $healthUrl -TimeoutSec 2
    if ($health.StatusCode -eq 200) {
        Write-Output "HipHop module service is already running on port $Port."
        exit 0
    }
} catch {}

if (-not (Test-Path -LiteralPath $server)) { throw "Module server is missing: $server" }
if (-not (Test-Path -LiteralPath $storageRoot)) { throw "HomeCloud storage is missing: $storageRoot" }

Start-Process -FilePath $server -ArgumentList @($storageRoot, $BindAddress, $Port) `
    -WorkingDirectory $projectRoot -WindowStyle Hidden

for ($attempt = 0; $attempt -lt 20; $attempt++) {
    Start-Sleep -Milliseconds 250
    try {
        $health = Invoke-WebRequest -UseBasicParsing -Uri $healthUrl -TimeoutSec 2
        if ($health.StatusCode -eq 200) {
            Write-Output "HipHop module service started on $BindAddress`:$Port."
            exit 0
        }
    } catch {}
}
throw 'HipHop module service did not become healthy.'
