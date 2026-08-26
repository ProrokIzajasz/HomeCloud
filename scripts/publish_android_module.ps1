param(
    [Parameter(Mandatory)]
    [ValidateSet('homecloud', 'what-to-eat')]
    [string]$ModuleId,
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]{0,31}$')]
    [string]$Version,
    [Parameter(Mandatory)]
    [string]$ApkPath
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$admin = Join-Path $projectRoot 'build\Debug\homecloud_admin.exe'
$storageRoot = if ($env:HOMECLOUD_STORAGE_ROOT) { $env:HOMECLOUD_STORAGE_ROOT } else { Join-Path $projectRoot 'data' }
$resolvedApk = (Resolve-Path -LiteralPath $ApkPath).Path

if ([IO.Path]::GetExtension($resolvedApk) -ne '.apk') { throw 'Only APK files can be published.' }
& $admin $storageRoot publish-android $ModuleId $Version $resolvedApk
if ($LASTEXITCODE -ne 0) { throw "Publishing failed with exit code $LASTEXITCODE." }
