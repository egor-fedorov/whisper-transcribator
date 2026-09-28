param([Parameter(Mandatory)][string]$Archive, [string]$Repository = "$PSScriptRoot/../..")
. "$PSScriptRoot/../../packaging/windows-common.ps1"
$Archive = (Resolve-Path $Archive).Path
$Repository = (Resolve-Path $Repository).Path
$temporary = [IO.Directory]::CreateTempSubdirectory('wt-sources-').FullName
try {
    $inputs = @('CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', '.gitattributes', 'cmake', 'src', 'tests', 'packaging', 'LICENSE')
    $tree = Invoke-Checked git (@('-C', $Repository, 'ls-tree', '-r', 'HEAD', '--') + $inputs)
    $expected = @{}
    foreach ($line in $tree) {
        if ($line -notmatch '^100[67][45][45] blob ([a-f0-9]+)\t(.+)$') { throw "Unexpected source entry: $line" }
        $expected[$Matches[2]] = $Matches[1]
    }
    Invoke-Checked tar @('-xzf', $Archive, '-C', $temporary)
    $files = @(Get-ChildItem $temporary -File -Recurse -Force)
    $names = @($files | ForEach-Object { [IO.Path]::GetRelativePath($temporary, $_.FullName).Replace('\', '/') })
    if (@(Compare-Object @($expected.Keys) $names -CaseSensitive).Count) { throw 'Source file list differs from Git' }
    foreach ($file in $files) {
        $name = [IO.Path]::GetRelativePath($temporary, $file.FullName).Replace('\', '/')
        $hash = (Invoke-Checked git @('-C', $Repository, 'hash-object', '--no-filters', '--', $file.FullName)).Trim()
        if ($hash -ne $expected[$name]) { throw "Packaged source differs from Git: $name" }
    }
    Write-Output "Packaged source matches Git byte-for-byte ($($files.Count) files)"
} finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
