# Package an existing tested MSVC build. No compilation, model downloads or inference.
param(
    [string]$Build = '.build/windows', [string]$Media = '.build/windows-ffmpeg',
    [string]$Vcpkg = '.build/vcpkg', [string]$Output
)
. "$PSScriptRoot/windows-common.ps1"
. "$PSScriptRoot/windows/runtime.ps1"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $Build = (Resolve-Path $Build).Path
    $Media = (Resolve-Path $Media).Path
    $Vcpkg = (Resolve-Path $Vcpkg).Path
    $metadata = Read-Metadata "$Build/generated/package.env"
    if ($metadata.WT_TARGET_OS -ne 'windows') { throw 'Expected a Windows build' }
    $arch = $metadata.WT_TARGET_ARCH
    $flavor = $metadata.WT_PACKAGE_FLAVOR
    if ($flavor -notin @('cpu', 'vulkan')) { throw 'Unsupported Windows archive flavor' }
    $target = Get-WindowsTarget $arch
    if (!$Output) {
        $Output = ".build/artifacts/windows-$arch"
        if ($flavor -eq 'vulkan') { $Output += '-vulkan' }
    }
    New-Item -ItemType Directory -Force $Output | Out-Null
    $Output = (Resolve-Path $Output).Path
    $bundle = Join-Path $Build 'bundle'
    if (Test-Path $bundle) { Remove-Item -Recurse -Force $bundle }
    foreach ($directory in @('bin', 'share', 'licenses', 'sources')) {
        New-Item -ItemType Directory "$bundle/$directory" | Out-Null
    }
    $pins = Get-Content -Raw vcpkg.json | ConvertFrom-Json
    $baseline = (Invoke-Checked git @('-C', $Vcpkg, 'rev-parse', 'HEAD')).Trim()
    if ($baseline -ne $pins.'builtin-baseline') { throw 'vcpkg checkout differs from pinned baseline' }
    $crt = @(Get-ChildItem (Join-Path $env:VCToolsRedistDir $target.Toolchain) -Directory -Filter 'Microsoft.VC*.CRT')
    if ($crt.Count -ne 1) { throw "Expected one release $arch CRT redistributable directory" }
    $installed = Join-Path $Build "vcpkg_installed/$($target.Toolchain)-windows"
    Copy-WindowsRuntime $Build $Media $installed $bundle $arch $crt[0].FullName $flavor
    if ($flavor -eq 'vulkan') {
        if (!$env:VULKAN_SDK) { throw 'VULKAN_SDK is required for header sources and notices' }
        Copy-Item "$env:VULKAN_SDK/LICENSE.txt" "$bundle/licenses/Vulkan-SDK.txt"
        Copy-Item 'packaging/vulkan-headers-notice.txt' "$bundle/licenses/Vulkan-headers.txt"
        Copy-Item 'packaging/licenses/Apache-2.0.txt' "$bundle/licenses/Apache-2.0.txt"
        Copy-Item 'packaging/vulkan-sdk.json' "$bundle/share/vulkan-sdk.json"
        Push-Location $env:VULKAN_SDK
        try { Invoke-Checked cmake @('-E', 'tar', 'czf', "$bundle/sources/vulkan-sdk-headers.tar.gz",
                'Include/vulkan', 'Include/vk_video', 'Include/spirv') }
        finally { Pop-Location }
        Write-Utf8 "$bundle/share/external-runtime.txt" @('vulkan-1.dll: install a Vulkan-capable GPU driver; do not install the SDK')
    }
    Write-Utf8 "$bundle/share/windows-toolchain.txt" @(
        "VCPKG_BASELINE=$baseline", "MSVC_VERSION=$env:VCToolsVersion",
        "SDK_VERSION=$($env:WindowsSDKVersion.TrimEnd([char[]]'\/'))"
    )
    # CMake writes CRLF on Windows; release.sh compares portable LF metadata.
    Write-Utf8 "$bundle/share/build-metadata.env" (Get-Content "$Build/generated/package.env")
    Copy-Item 'packaging/windows-runtime-notice.txt' "$bundle/licenses/MSVC-runtime.txt"
    $ffmpeg = @(Get-ChildItem $Media -Directory -Filter 'ffmpeg-*')
    if ($ffmpeg.Count -ne 1) { throw 'Expected one pinned FFmpeg source directory' }
    Copy-Item "$Media/ffmpeg.tar.xz" "$bundle/sources/$($ffmpeg[0].Name).tar.xz"
    Copy-Item "$($ffmpeg[0].FullName)/COPYING.LGPLv2.1" "$bundle/licenses/FFmpeg-LGPL-2.1.txt"
    Copy-Item "$($ffmpeg[0].FullName)/ffbuild/config.log" "$bundle/sources/ffmpeg-config.log"
    Invoke-Checked cmake @("-DWT_SOURCE_DIR=$root", "-DWT_BUILD_DIR=$Build", "-DWT_BUNDLE_DIR=$bundle",
        '-P', "$PSScriptRoot/cmake/sources.cmake")
    & "$PSScriptRoot/windows-sources.ps1" -Vcpkg $Vcpkg -Installed $installed -Bundle $bundle
    $version = $metadata.WT_PACKAGE_VERSION
    $checksums = @(Get-ChildItem $bundle -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($bundle, $_.FullName).Replace('\', '/')
        "$((Get-FileHash $_.FullName).Hash.ToLowerInvariant())  $relative"
    })
    Write-Utf8 "$bundle/share/files.sha256" $checksums
    $name = "whisper-transcribator-$version-windows-$arch-$flavor.zip"
    $archive = Join-Path $Output $name
    if (Test-Path $archive) { Remove-Item $archive }
    # Retain every staged entry without Compress-Archive's hidden-file filtering.
    [IO.Compression.ZipFile]::CreateFromDirectory($bundle, $archive)
    Write-Utf8 "$Output/SHA256SUMS" @("$((Get-FileHash $archive).Hash.ToLowerInvariant())  $name")
    Write-Output "Archive: $archive"
} finally { Pop-Location }
