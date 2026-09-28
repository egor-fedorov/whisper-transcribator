# Model-free tests, also runnable under PowerShell on Linux.
. "$PSScriptRoot/../../packaging/windows-common.ps1"
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
    Write-Output 'Windows runtime provenance policy passed'
} finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
