param([Parameter(Mandatory)][string]$Binary)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
New-Item -ItemType Directory $root | Out-Null
$previousDriver = $env:VK_DRIVER_FILES
$previousIcd = $env:VK_ICD_FILENAMES
try {
    # Suppress every hardware/software ICD, independently of the runner's installed drivers.
    $env:VK_DRIVER_FILES = Join-Path $root 'missing-icd.json'
    $env:VK_ICD_FILENAMES = $env:VK_DRIVER_FILES
    foreach ($device in @('cpu', 'auto', 'vulkan')) {
        $stdout = Join-Path $root "$device.json"
        & $Binary doctor --device $device --json > $stdout
        $status = $LASTEXITCODE
        $result = Get-Content $stdout -Raw | ConvertFrom-Json
        if ($device -eq 'vulkan') {
            if ($status -ne 1 -or !($result.errors -match 'Vulkan requested but unavailable')) {
                throw 'Explicit Vulkan did not fail clearly without an ICD'
            }
        } elseif ($status -ne 0 -or $result.device -ne 'cpu' -or $result.errors.Count -ne 0) {
            throw "$device did not remain usable without Vulkan"
        }
    }
} finally {
    $env:VK_DRIVER_FILES = $previousDriver
    $env:VK_ICD_FILENAMES = $previousIcd
    Remove-Item $root -Recurse -Force
}
