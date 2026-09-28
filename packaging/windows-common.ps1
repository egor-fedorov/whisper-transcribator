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
function Get-Imports([string]$Binary) {
    $headers = Invoke-Checked dumpbin @('/nologo', '/headers', $Binary)
    if (($headers -join "`n") -notmatch '8664 machine \(x64\)') { throw "Not x64 PE: $Binary" }
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
