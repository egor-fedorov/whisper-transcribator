# Verify the exact ZIP, not the development directory. No models or inference.
param([Parameter(Mandatory)][string]$Artifacts, [string]$Unpacked = 'unpacked')
. "$PSScriptRoot/../../packaging/windows-common.ps1"
$Artifacts = (Resolve-Path $Artifacts).Path
$archives = @(Get-ChildItem $Artifacts -Filter '*-windows-x86_64-cpu.zip')
if ($archives.Count -ne 1) { throw 'Expected exactly one Windows ZIP' }
$archive = $archives[0]
$expected = "$((Get-FileHash $archive.FullName).Hash.ToLowerInvariant())  $($archive.Name)"
if ((Get-Content -Raw "$Artifacts/SHA256SUMS").TrimEnd() -cne $expected) { throw 'ZIP checksum mismatch' }
$bundle = Join-Path $Artifacts $Unpacked
if (Test-Path $bundle) { throw "Use a fresh extraction directory: $bundle" }
[IO.Compression.ZipFile]::ExtractToDirectory($archive.FullName, $bundle)
$metadata = Read-Metadata "$bundle/share/build-metadata.env"
if ($metadata.WT_TARGET_OS -ne 'windows' -or $metadata.WT_TARGET_ARCH -ne 'x86_64' -or
    $archive.Name -ne "whisper-transcribator-$($metadata.WT_PACKAGE_VERSION)-windows-x86_64-cpu.zip") {
    throw 'ZIP target or version metadata mismatch'
}
$listed = @{}
foreach ($line in Get-Content "$bundle/share/files.sha256") {
    if ($line -notmatch '^([a-f0-9]{64})  ((bin|share|licenses|sources)/[^\r\n]+)$') { throw "Bad inventory: $line" }
    $hash = $Matches[1]; $relative = $Matches[2]
    if ($relative -match '(^|/)\.\.(/|$)|\\|:' -or $listed.ContainsKey($relative)) { throw "Unsafe inventory: $relative" }
    $listed[$relative] = $true
    if ((Get-FileHash -LiteralPath "$bundle/$relative").Hash -ne $hash) { throw "File checksum mismatch: $relative" }
}
$actual = @(Get-ChildItem $bundle -Recurse -File)
if ($actual.Count -ne $listed.Count + 1) { throw 'Uninventoried archive files' }
foreach ($file in $actual) {
    $relative = [IO.Path]::GetRelativePath($bundle, $file.FullName).Replace('\', '/')
    if ($relative -ne 'share/files.sha256' -and !$listed.ContainsKey($relative)) { throw "Unexpected file: $relative" }
}
foreach ($notice in @('whisper-transcribator-MIT', 'whisper.cpp-MIT', 'FFmpeg-LGPL-2.1',
        'CLI11-BSD', 'nlohmann-json-MIT', 'curl', 'zlib', 'MSVC-runtime')) {
    if ((Get-Item "$bundle/licenses/$notice.txt").Length -eq 0) { throw "Empty notice: $notice" }
}
foreach ($source in @('whisper-transcribator-*.tar.gz', 'whisper.cpp-*.tar.gz', 'ffmpeg-*.tar.xz',
        'CLI11-*.tar.gz', 'nlohmann-json-*.tar.gz', 'curl-*.tar.gz', 'zlib-*.tar.gz', 'ffmpeg-config.log')) {
    if (@(Get-ChildItem "$bundle/sources/$source").Count -eq 0) { throw "Missing sources: $source" }
}
$backends = @(Get-Content "$bundle/share/backends.txt")
$plugins = @(Get-ChildItem "$bundle/bin/ggml-cpu-*.dll" | ForEach-Object Name)
if ('ggml-cpu-x64.dll' -notin $plugins -or @(Compare-Object $backends $plugins).Count) {
    throw 'CPU plugin inventory mismatch'
}
foreach ($file in Get-ChildItem "$bundle/bin" -File) {
    if ($file.Name -notmatch '^(whisper-transcribator\.exe|whisper\.dll|ggml(-base|-cpu-[a-z0-9]+)?\.dll|av(codec|format|util)-\d+\.dll|swresample-\d+\.dll|libcurl\.dll|z\.dll|(msvcp|vcruntime|concrt)140[^/]*\.dll)$') {
        throw "Unwanted executable/library: $($file.Name)"
    }
    foreach ($import in Get-Imports $file.FullName) {
        if (!(Test-SystemLibrary $import) -and !(Test-Path "$bundle/bin/$import")) {
            throw "Missing app-local dependency: $($file.Name) -> $import"
        }
    }
}
$path = $env:PATH
try {
    # Neither Git/MSYS2, Visual Studio, system FFmpeg nor the build tree can supply DLLs.
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $binary = "$bundle/bin/whisper-transcribator.exe"
    if ((Invoke-Checked $binary @('--version')).Trim() -ne $metadata.WT_PACKAGE_VERSION) { throw 'Binary version mismatch' }
    $doctor = (Invoke-Checked $binary @('doctor', '--device', 'cpu', '--json')) | ConvertFrom-Json
    if ($doctor.device -ne 'cpu' -or $doctor.errors.Count -or !$doctor.cpu_backend) { throw 'Archive doctor failed' }
    $baseline = Join-Path $Artifacts 'baseline'
    New-Item -ItemType Directory "$baseline/bin" | Out-Null
    Copy-Item "$bundle/bin/*" "$baseline/bin"
    Get-ChildItem "$baseline/bin/ggml-cpu-*.dll" | Where-Object Name -ne 'ggml-cpu-x64.dll' | Remove-Item
    $doctor = (Invoke-Checked "$baseline/bin/whisper-transcribator.exe" @('doctor', '--device', 'cpu', '--json')) | ConvertFrom-Json
    if ($doctor.errors.Count -or $doctor.cpu_backend -ne 'ggml-cpu-x64.dll') { throw 'Baseline CPU loader failed' }
    $cwd = Join-Path $baseline 'cwd'
    New-Item -ItemType Directory $cwd | Out-Null
    Move-Item "$baseline/bin/ggml-cpu-x64.dll" $cwd
    Push-Location $cwd
    try {
        $doctor = & "$baseline/bin/whisper-transcribator.exe" doctor --device cpu --json | ConvertFrom-Json
        if ($LASTEXITCODE -ne 1 -or !($doctor.errors -match 'No compatible CPU backend')) {
            throw 'Missing CPU plugin did not fail safely (or loaded from CWD)'
        }
    } finally { Pop-Location }
} finally { $env:PATH = $path }
Write-Output 'Windows ZIP integrity, dependencies, doctor and baseline loader passed (no inference)'
