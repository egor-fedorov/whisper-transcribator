# Runs only inside the disposable Server Core container (built-in PowerShell 5.1).
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
foreach ($directory in @("$env:SystemRoot/System32", "$env:SystemRoot/SysWOW64")) {
    if (!(Test-Path $directory)) { continue }
    $runtime = @(Get-ChildItem $directory -File | Where-Object { $_.Name -match '^(msvcp|vcruntime|concrt)140.*\.dll$' })
    if ($runtime.Count) { throw "Base image already has a VC++ runtime: $($runtime.Name -join ', ')" }
}
$env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
New-Item -ItemType Directory C:/work | Out-Null
Copy-Item C:/bundle/bin C:/work/bin -Recurse
$binary = 'C:/work/bin/whisper-transcribator.exe'
$doctor = & $binary doctor --device cpu --json | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $doctor.errors.Count -or !$doctor.cpu_backend) { throw 'Server Core doctor failed' }
$doctor | ConvertTo-Json -Depth 8
# Negative control: the host's installed redistributable must not rescue this executable.
Move-Item C:/work/bin/vcruntime140.dll C:/work/vcruntime140.dll
Add-Type @'
using System.Runtime.InteropServices;
public static class ErrorMode {
    [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);
}
'@
[ErrorMode]::SetErrorMode(0x8003) | Out-Null
$process = Start-Process $binary -ArgumentList 'doctor --device cpu --json' -PassThru -NoNewWindow
try {
    if (!$process.WaitForExit(10000)) { $process.Kill(); throw 'Missing-runtime control timed out' }
    if ($process.ExitCode -ne -1073741515) { throw "Expected STATUS_DLL_NOT_FOUND, got $($process.ExitCode)" }
} finally { $process.Dispose() }
Write-Output 'Server Core: doctor passed without a system VC++ runtime; removing the bundled CRT fails as expected'
exit 0
