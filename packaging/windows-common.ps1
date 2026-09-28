#requires -Version 7.4
# Shared by the model-free ZIP builder and verifier. Requires developer PowerShell.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
}
function Read-Metadata([string]$Path) {
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -notmatch '^([A-Z_0-9]+)=([^\r\n]*)$') { throw "Invalid metadata: $line" }
        if ($values.ContainsKey($Matches[1])) { throw "Duplicate metadata: $line" }
        $values[$Matches[1]] = $Matches[2]
    }
    return $values
}
function Get-WindowsTarget([string]$Architecture) {
    switch ($Architecture) {
        'x86_64' { return @{ Toolchain = 'x64'; Machine = '8664'; Baseline = 'ggml-cpu-x64.dll' } }
        'arm64' { return @{ Toolchain = 'arm64'; Machine = 'AA64'; Baseline = 'ggml-cpu-armv8.0_1.dll' } }
        default { throw "Unsupported Windows target: $Architecture" }
    }
}
function Assert-PeArchitecture([string]$Headers, [string]$Architecture, [string]$Binary) {
    $target = Get-WindowsTarget $Architecture
    if ($Headers -notmatch "(?im)^\s*$($target.Machine) machine \(") {
        throw "Not $Architecture PE: $Binary"
    }
}
function Get-Imports([string]$Binary, [string]$Architecture = 'x86_64') {
    $headers = Invoke-Checked dumpbin @('/nologo', '/headers', $Binary)
    Assert-PeArchitecture ($headers -join "`n") $Architecture $Binary
    $lines = Invoke-Checked dumpbin @('/nologo', '/dependents', $Binary)
    # Includes delay-load imports too. Anything not in the OS allowlist must be bundled.
    $imports = @($lines | ForEach-Object {
        if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1].ToLowerInvariant() }
    } | Sort-Object -Unique)
    if ($imports.Count -eq 0) { throw "No PE imports found: $Binary" }
    return $imports
}
function Test-SystemLibrary([string]$Name) {
    return $Name -match '^(api-ms-win-|ext-ms-win-)[a-z0-9-]+\.dll$' -or $Name -in @(
        'kernel32.dll', 'kernelbase.dll', 'ntdll.dll', 'advapi32.dll', 'bcrypt.dll',
        'crypt32.dll', 'secur32.dll', 'ws2_32.dll', 'iphlpapi.dll', 'user32.dll',
        'ole32.dll', 'shell32.dll', 'shlwapi.dll', 'version.dll', 'normaliz.dll', 'ucrtbase.dll'
    )
}
function Write-Utf8([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllText($Path, (($Lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
}
function Test-MsvcRuntime([string]$Name) {
    return $Name -match '^(msvcp|vcruntime|concrt)[0-9][a-z0-9_]*\.dll$'
}
function Resolve-WindowsDependency([string]$Name, [string[]]$Directories, [string]$Redist) {
    # Never take a stale, untracked or debug CRT copy from the build tree or PATH.
    if (Test-MsvcRuntime $Name) { $Directories = @($Redist) }
    foreach ($directory in $Directories) {
        if (Test-Path -LiteralPath "$directory/$Name" -PathType Leaf) {
            return Get-Item -LiteralPath "$directory/$Name"
        }
    }
    throw "Cannot resolve non-system dependency: $Name (MSVC runtime must come from redist)"
}
