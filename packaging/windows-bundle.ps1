# Package an existing tested MSVC build. No compilation, model downloads or inference.
param(
    [string]$Build = '.build/windows', [string]$Media = '.build/windows-ffmpeg',
    [string]$Vcpkg = '.build/vcpkg', [string]$Output = '.build/artifacts/windows-x86_64'
)
. "$PSScriptRoot/windows-common.ps1"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $Build = (Resolve-Path $Build).Path
    $Media = (Resolve-Path $Media).Path
    $Vcpkg = (Resolve-Path $Vcpkg).Path
    New-Item -ItemType Directory -Force $Output | Out-Null
    $Output = (Resolve-Path $Output).Path
    $bundle = Join-Path $Build 'bundle'
    if (Test-Path $bundle) { Remove-Item -Recurse -Force $bundle }
    foreach ($directory in @('bin', 'share', 'licenses', 'sources')) {
        New-Item -ItemType Directory "$bundle/$directory" | Out-Null
    }
    $metadata = Read-Metadata "$Build/generated/package.env"
    if ($metadata.WT_TARGET_OS -ne 'windows' -or $metadata.WT_TARGET_ARCH -ne 'x86_64') {
        throw 'Expected a Windows x64 build'
    }
    $pins = Get-Content -Raw vcpkg.json | ConvertFrom-Json
    $baseline = (Invoke-Checked git @('-C', $Vcpkg, 'rev-parse', 'HEAD')).Trim()
    if ($baseline -ne $pins.'builtin-baseline') { throw 'vcpkg checkout differs from pinned baseline' }
    $crt = @(Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT')
    if ($crt.Count -ne 1) { throw 'Expected one release x64 CRT redistributable directory' }
    $installed = Join-Path $Build 'vcpkg_installed/x64-windows'
    $search = @("$Build/bin", "$Media/prefix/bin", "$installed/bin")
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue('whisper-transcribator.exe')
    $backends = @(Get-ChildItem "$Build/bin/ggml-cpu-*.dll" | Sort-Object Name | ForEach-Object Name)
    if ('ggml-cpu-x64.dll' -notin $backends) { throw 'Missing baseline CPU plugin' }
    foreach ($plugin in $backends) { $queue.Enqueue($plugin) }
    $copied = @{}
    $links = @()
    $runtime = @()
    while ($queue.Count) {
        $name = $queue.Dequeue()
        if ($copied.ContainsKey($name)) { continue }
        if (Test-SystemLibrary $name) { throw "Refusing to bundle an OS library: $name" }
        $source = Resolve-WindowsDependency $name $search $crt[0].FullName
        Copy-Item -LiteralPath $source.FullName -Destination "$bundle/bin/$name"
        $copied[$name] = $true
        if (Test-MsvcRuntime $name) {
            $runtime += "$name $($source.VersionInfo.FileVersion) SHA256=$((Get-FileHash $source.FullName).Hash.ToLowerInvariant())"
        }
        $imports = @(Get-Imports "$bundle/bin/$name")
        $links += "# $name"
        $links += $imports
        foreach ($dependency in $imports) {
            if (!(Test-SystemLibrary $dependency)) { $queue.Enqueue($dependency) }
        }
    }
    if ($runtime.Count -eq 0) { throw 'No app-local MSVC runtime collected' }
    Write-Utf8 "$bundle/share/backends.txt" $backends
    Write-Utf8 "$bundle/share/linked-libraries.txt" $links
    Write-Utf8 "$bundle/share/msvc-runtime.txt" $runtime
    Write-Utf8 "$bundle/share/windows-toolchain.txt" @(
        "VCPKG_BASELINE=$baseline", "MSVC_VERSION=$env:VCToolsVersion",
        "SDK_VERSION=$($env:WindowsSDKVersion.TrimEnd([char[]]'\/'))"
    )
    # CMake writes CRLF on Windows; release.sh compares portable LF metadata.
    Write-Utf8 "$bundle/share/build-metadata.env" (Get-Content "$Build/generated/package.env")
    Copy-Item 'packaging/windows-runtime-notice.txt' "$bundle/licenses/MSVC-runtime.txt"
    Copy-Item 'LICENSE' "$bundle/licenses/whisper-transcribator-MIT.txt"
    $ffmpeg = @(Get-ChildItem $Media -Directory -Filter 'ffmpeg-*')
    if ($ffmpeg.Count -ne 1) { throw 'Expected one pinned FFmpeg source directory' }
    Copy-Item "$Media/ffmpeg.tar.xz" "$bundle/sources/$($ffmpeg[0].Name).tar.xz"
    Copy-Item "$($ffmpeg[0].FullName)/COPYING.LGPLv2.1" "$bundle/licenses/FFmpeg-LGPL-2.1.txt"
    Copy-Item "$($ffmpeg[0].FullName)/ffbuild/config.log" "$bundle/sources/ffmpeg-config.log"
    $dependencies = @(
        @('whisper', "whisper.cpp-$($metadata.WT_WHISPER_REVISION)", 'LICENSE', 'whisper.cpp-MIT'),
        @('cli11', "CLI11-$($metadata.WT_CLI11_VERSION)", 'LICENSE', 'CLI11-BSD'),
        @('nlohmann_json', "nlohmann-json-$($metadata.WT_JSON_VERSION)", 'LICENSE.MIT', 'nlohmann-json-MIT')
    )
    foreach ($entry in $dependencies) {
        $source = "$Build/_deps/$($entry[0])-src"
        Invoke-Checked tar @('-C', $source, '-czf', "$bundle/sources/$($entry[1]).tar.gz", '.')
        Copy-Item "$source/$($entry[2])" "$bundle/licenses/$($entry[3]).txt"
    }
    & "$PSScriptRoot/windows-sources.ps1" -Vcpkg $Vcpkg -Installed $installed -Bundle $bundle
    $version = $metadata.WT_PACKAGE_VERSION
    Invoke-Checked tar @('-czf', "$bundle/sources/whisper-transcribator-$version.tar.gz",
        'CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', '.gitattributes', 'cmake', 'src', 'tests', 'packaging', 'LICENSE')
    $checksums = @(Get-ChildItem $bundle -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($bundle, $_.FullName).Replace('\', '/')
        "$((Get-FileHash $_.FullName).Hash.ToLowerInvariant())  $relative"
    })
    Write-Utf8 "$bundle/share/files.sha256" $checksums
    $name = "whisper-transcribator-$version-windows-x86_64-cpu.zip"
    $archive = Join-Path $Output $name
    if (Test-Path $archive) { Remove-Item $archive }
    # Retain every staged entry without Compress-Archive's hidden-file filtering.
    [IO.Compression.ZipFile]::CreateFromDirectory($bundle, $archive)
    Write-Utf8 "$Output/SHA256SUMS" @("$((Get-FileHash $archive).Hash.ToLowerInvariant())  $name")
    Write-Output "Archive: $archive"
} finally { Pop-Location }
