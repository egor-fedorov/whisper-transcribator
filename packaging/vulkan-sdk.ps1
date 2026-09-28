# Developer/CI toolchain only; no SDK components are added to release archives.
param([string]$Root = '.build/vulkan-sdk', [string]$Cache = '.build/vulkan-sdk-cache')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (!$IsWindows -and !$IsLinux) { throw 'Vulkan prototype supports Linux and Windows only' }
$pins = Get-Content "$PSScriptRoot/vulkan-sdk.json" -Raw | ConvertFrom-Json
$platform = if ($IsWindows) { 'windows' } else { 'linux' }
$pin = $pins.$platform
New-Item -ItemType Directory -Force $Root, $Cache | Out-Null
$Root = (Resolve-Path $Root).Path
$archive = Join-Path (Resolve-Path $Cache).Path ([IO.Path]::GetFileName($pin.url))
if (!(Test-Path $archive)) {
    Invoke-WebRequest $pin.url -OutFile "$archive.part" -MaximumRetryCount 3 -RetryIntervalSec 5
    if ((Get-FileHash "$archive.part" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
        throw 'Vulkan SDK checksum mismatch'
    }
    Move-Item "$archive.part" $archive -Force
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
    throw 'Cached Vulkan SDK checksum mismatch; remove the cached download and retry'
}
if ($IsWindows) {
    $sdk = Join-Path $Root $pins.version
    $process = Start-Process $archive -Wait -PassThru -ArgumentList @(
        '--root', "`"$sdk`"", '--accept-licenses', '--default-answer', '--confirm-command',
        'install', 'copy_only=1')
    if ($process.ExitCode -ne 0) { throw "Vulkan SDK installer failed ($($process.ExitCode))" }
    $bin = Join-Path $sdk 'Bin'
} else {
    & tar -xJf $archive -C $Root
    if ($LASTEXITCODE -ne 0) { throw 'Vulkan SDK extraction failed' }
    $sdk = Join-Path $Root "$($pins.version)/x86_64"
    $bin = Join-Path $sdk 'bin'
}
& "$bin/glslc" --version
if ($LASTEXITCODE -ne 0) { throw 'Vulkan shader compiler failed' }
$env:VULKAN_SDK = $sdk
$env:PATH = "$bin$([IO.Path]::PathSeparator)$env:PATH"
if ($env:GITHUB_ENV) {
    "VULKAN_SDK=$sdk" >> $env:GITHUB_ENV
    $bin >> $env:GITHUB_PATH
}
Write-Host "VULKAN_SDK=$sdk"
