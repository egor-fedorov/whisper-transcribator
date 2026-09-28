# Native Windows wrapper for the same TLS/resume scenarios as downloads.sh.
param([Parameter(Mandatory)][string]$Binary, [Parameter(Mandatory)][string]$OpenSSL)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Directory]::CreateTempSubdirectory('wt-tls-').FullName
$started = Get-Date
try {
    & $OpenSSL req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 `
        -addext subjectAltName=IP:127.0.0.1 -keyout "$root/key.pem" -out "$root/cert.pem" 2>$null
    if ($LASTEXITCODE) { throw 'Cannot generate the local TLS fixture' }
    & $Binary "$root/cert.pem" "$root/key.pem"
    $code = $LASTEXITCODE
    if ($code) {
        # Fast-fail exceptions bypass an application's unhandled-exception filter.
        Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $started } `
            -ErrorAction SilentlyContinue | ForEach-Object { Write-Output $_.Message }
        throw ('HTTPS test failed with Windows exit code 0x{0:X8}' -f ($code -band 0xffffffffL))
    }
} finally { Remove-Item -LiteralPath $root -Recurse -Force }
