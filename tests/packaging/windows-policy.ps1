# Model-free tests, also runnable under PowerShell on Linux.
. "$PSScriptRoot/../../packaging/windows-common.ps1"
. "$PSScriptRoot/../../packaging/windows/runtime.ps1"
Assert-PeArchitecture '            AA64 machine (ARM64)' arm64 'fixture.dll'
Assert-PeArchitecture '            8664 machine (x64)' x86_64 'fixture.dll'
foreach ($case in @(@('8664', 'arm64'), @('AA64', 'x86_64'), @('A641', 'arm64'), @('14C', 'x86_64'))) {
    $rejected = $false
    try { Assert-PeArchitecture "    $($case[0]) machine (fixture)" $case[1] 'fixture.dll' }
    catch { $rejected = $_.Exception.Message -match 'Not .* PE' }
    if (!$rejected) { throw 'Wrong architecture silently accepted' }
}
if ((Get-WindowsTarget arm64).Baseline -ne 'ggml-cpu-armv8.0_1.dll') { throw 'Wrong ARM64 baseline' }
$temporary = [IO.Directory]::CreateTempSubdirectory('wt-policy-').FullName
try {
    $build = New-Item -ItemType Directory "$temporary/build"
    $redist = New-Item -ItemType Directory "$temporary/redist"
    foreach ($name in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'concrt140.dll')) {
        Write-Utf8 "$build/$name" @('wrong runtime')
        Write-Utf8 "$redist/$name" @('redist runtime')
        $resolved = Resolve-WindowsDependency $name @($build.FullName) $redist.FullName
        if ($resolved.DirectoryName -ne $redist.FullName) { throw "Wrong CRT origin: $name" }
        Remove-Item "$redist/$name"
        $rejected = $false
        try { Resolve-WindowsDependency $name @($build.FullName) $redist.FullName | Out-Null }
        catch { $rejected = $_.Exception.Message -match 'must come from redist' }
        if (!$rejected) { throw 'Missing redistributable silently fell back to the build directory' }
    }
    Write-Utf8 "$build/libcurl.dll" @('ordinary dependency')
    if ((Resolve-WindowsDependency 'libcurl.dll' @($build.FullName) $redist.FullName).DirectoryName -ne $build.FullName) {
        throw 'Non-CRT dependency lookup failed'
    }
    if (!(Test-SystemLibrary 'ucrtbase.dll') -or (Test-SystemLibrary 'vcruntime140.dll')) { throw 'OS/CRT policy mismatch' }
    # Exercise the complete collector with a synthetic PE graph, including a dependency cycle.
    # Native archive verification still runs real dumpbin independently on every bundled image.
    function Get-Imports([string]$Binary, [string]$Architecture) {
        if ($Architecture -ne 'arm64') { throw 'Architecture not forwarded to PE inspection' }
        switch ([IO.Path]::GetFileName($Binary)) {
            'whisper-transcribator.exe' { return @('ggml.dll', 'kernel32.dll') }
            'ggml-cpu-armv8.0_1.dll' { return @('ggml.dll', 'vcruntime140.dll') }
            'ggml.dll' { return @('libcurl.dll', 'vcruntime140.dll') }
            'libcurl.dll' { return @('ggml.dll', 'ucrtbase.dll') }
            'vcruntime140.dll' { return @('kernel32.dll') }
            default { throw "Unexpected image: $Binary" }
        }
    }
    $native = "$temporary/native build"
    $bundle = "$temporary/bundle"
    foreach ($dir in @("$native/bin", "$bundle/bin", "$bundle/share")) {
        New-Item -ItemType Directory -Force $dir | Out-Null
    }
    foreach ($name in @('whisper-transcribator.exe', 'ggml-cpu-armv8.0_1.dll', 'ggml.dll', 'libcurl.dll')) {
        Write-Utf8 "$native/bin/$name" @($name)
    }
    Write-Utf8 "$redist/vcruntime140.dll" @('redist runtime')
    Write-Utf8 "$native/bin/vcruntime140.dll" @('stale runtime')
    Copy-WindowsRuntime $native "$temporary/media" "$temporary/installed" $bundle arm64 $redist.FullName
    $expected = @('whisper-transcribator.exe', 'ggml-cpu-armv8.0_1.dll', 'ggml.dll', 'libcurl.dll', 'vcruntime140.dll')
    if (@(Compare-Object $expected @(Get-ChildItem "$bundle/bin" -Name)).Count) { throw 'Wrong dependency closure' }
    $hash = (Get-FileHash "$redist/vcruntime140.dll").Hash.ToLowerInvariant()
    if ((Get-FileHash "$bundle/bin/vcruntime140.dll").Hash.ToLowerInvariant() -ne $hash) { throw 'Wrong runtime bytes' }
    if ((Get-Content "$bundle/share/msvc-runtime.txt") -notmatch "SHA256=$hash") { throw 'Missing CRT provenance' }
    if ((Get-Content "$bundle/share/backends.txt") -ne 'ggml-cpu-armv8.0_1.dll') { throw 'Wrong backend inventory' }
    if (@(Get-Content "$bundle/share/linked-libraries.txt" | Where-Object { $_ -like '# *' }).Count -ne 5) {
        throw 'Cyclic dependency collected more than once'
    }
    Remove-Item "$native/bin/libcurl.dll"
    $rejected = $false
    try { Copy-WindowsRuntime $native "$temporary/media" "$temporary/installed" $bundle arm64 $redist.FullName }
    catch { $rejected = $_.Exception.Message -match 'Cannot resolve non-system dependency: libcurl.dll' }
    if (!$rejected) { throw 'Missing transitive dependency accepted' }
    Remove-Item "$native/bin/ggml-cpu-armv8.0_1.dll"
    $rejected = $false
    try { Copy-WindowsRuntime $native "$temporary/media" "$temporary/installed" $bundle arm64 $redist.FullName }
    catch { $rejected = $_.Exception.Message -match 'Missing baseline CPU plugin' }
    if (!$rejected) { throw 'Missing baseline accepted' }
    Write-Output 'Windows runtime provenance policy passed'
} finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
