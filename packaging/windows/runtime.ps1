# Private PE dependency closure; windows-common.ps1 supplies inspection and provenance policy.
function Copy-WindowsRuntime([string]$Build, [string]$Media, [string]$Installed,
    [string]$Bundle, [string]$Architecture, [string]$Redist) {
    $arch = $Architecture
    $target = Get-WindowsTarget $arch
    $search = @("$Build/bin", "$Media/prefix/bin", "$installed/bin")
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue('whisper-transcribator.exe')
    $backends = @(Get-ChildItem "$Build/bin/ggml-cpu*.dll" | Sort-Object Name | ForEach-Object Name)
    if ($target.Baseline -notin $backends) { throw 'Missing baseline CPU plugin' }
    foreach ($plugin in $backends) { $queue.Enqueue($plugin) }
    $copied = @{}
    $links = @()
    $runtime = @()
    while ($queue.Count) {
        $name = $queue.Dequeue()
        if ($copied.ContainsKey($name)) { continue }
        if (Test-SystemLibrary $name) { throw "Refusing to bundle an OS library: $name" }
        $source = Resolve-WindowsDependency $name $search $Redist
        Copy-Item -LiteralPath $source.FullName -Destination "$bundle/bin/$name"
        $copied[$name] = $true
        if (Test-MsvcRuntime $name) {
            $runtime += "$name $($source.VersionInfo.FileVersion) SHA256=$((Get-FileHash $source.FullName).Hash.ToLowerInvariant())"
        }
        $imports = @(Get-Imports "$bundle/bin/$name" $arch)
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
}
