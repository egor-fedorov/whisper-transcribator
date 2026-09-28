# Include pinned upstream tarballs plus the exact vcpkg recipes/patches used to build them.
param([Parameter(Mandatory)][string]$Vcpkg, [Parameter(Mandatory)][string]$Installed,
    [Parameter(Mandatory)][string]$Bundle)
. "$PSScriptRoot/windows-common.ps1"
$provenance = @()
foreach ($port in @('curl', 'zlib')) {
    $recipe = Join-Path $Vcpkg "ports/$port"
    $manifest = Get-Content -Raw "$recipe/vcpkg.json" | ConvertFrom-Json -AsHashtable
    $version = $manifest['version']
    if (!$version) { $version = $manifest['version-semver'] }
    if (!$version -or $version -notmatch '^\d+\.\d+\.\d+$') { throw "Unexpected $port version" }
    $definition = Get-Content -Raw "$recipe/portfile.cmake"
    if ($definition -notmatch 'SHA512\s+([a-f0-9]{128})') { throw "Missing $port source hash" }
    $hash = $Matches[1]
    if ($port -eq 'curl') { $repository = 'curl/curl'; $ref = 'curl-' + $version.Replace('.', '_') }
    else { $repository = 'madler/zlib'; $ref = "v$version" }
    if ($definition -notmatch "REPO\s+$([regex]::Escape($repository))\s") {
        throw "Unexpected source repository for $port"
    }
    $url = "https://github.com/$repository/archive/$ref.tar.gz"
    $archive = Join-Path $Bundle "sources/$port-$version.tar.gz"
    Invoke-WebRequest -Uri $url -OutFile $archive
    if ((Get-FileHash $archive -Algorithm SHA512).Hash -ne $hash) { throw "Source checksum mismatch: $port" }
    Invoke-Checked tar @('-C', $Vcpkg, '-czf', "$Bundle/sources/$port-vcpkg-recipe.tar.gz", "ports/$port")
    Copy-Item "$Installed/share/$port/copyright" "$Bundle/licenses/$port.txt"
    Copy-Item "$Installed/share/$port/vcpkg.spdx.json" "$Bundle/share/$port.spdx.json"
    $provenance += "$port $version SHA512=$hash $url"
}
Write-Utf8 "$Bundle/share/dependency-sources.txt" $provenance
