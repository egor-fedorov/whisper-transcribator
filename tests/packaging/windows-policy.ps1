# Model-free tests, also runnable under PowerShell on Linux.
. "$PSScriptRoot/../../packaging/windows-common.ps1"
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
