# Run this file once from Windows PowerShell started as Administrator.
$ErrorActionPreference = 'Stop'
$ruleName = 'HomeCloud HipHop Modules TCP 8081'
$projectRoot = Split-Path -Parent $PSScriptRoot
$server = Join-Path $projectRoot 'build\Debug\homecloud_modules.exe'

if (-not (Test-Path -LiteralPath $server)) { throw "Module server is missing: $server" }
if (-not (Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow `
        -Protocol TCP -LocalPort 8081 -Profile Private -Program $server | Out-Null
}
Write-Output 'Private-network firewall access enabled for HomeCloud module service on TCP 8081.'
