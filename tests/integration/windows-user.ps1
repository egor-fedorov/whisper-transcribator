# A disposable standard user catches assumptions hidden by the elevated hosted runner.
param([Parameter(Mandatory)][string]$Bin)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Bin = (Resolve-Path $Bin).Path
$user = 'wt-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$password = ConvertTo-SecureString ([Guid]::NewGuid().ToString('N') + 'aA!9') -AsPlainText -Force
$account = "$env:COMPUTERNAME\$user"
$logs = Join-Path $env:RUNNER_TEMP $user
try {
    New-LocalUser -Name $user -Password $password -AccountNeverExpires | Out-Null
    Add-LocalGroupMember -Group (Get-LocalGroup -SID 'S-1-5-32-545') -Member $user
    $credential = [PSCredential]::new($account, $password)
    New-Item -ItemType Directory -Path $logs | Out-Null
    & icacls $Bin /grant "${account}:(OI)(CI)RX"
    if ($LASTEXITCODE) { throw 'Cannot grant access to test binaries' }
    & icacls $logs /grant "${account}:(OI)(CI)M"
    if ($LASTEXITCODE) { throw 'Cannot grant access to test logs' }
    foreach ($test in @('wt-windows-tests.exe', 'whisper-transcribator.exe')) {
        $argsForTest = @(if ($test -eq 'whisper-transcribator.exe') { 'doctor'; '--device'; 'cpu'; '--json' })
        $options = @{
            FilePath = (Join-Path $Bin $test)
            Credential = $credential
            LoadUserProfile = $true
            Environment = @{
                TEMP = $logs
                TMP = $logs
                USERPROFILE = $logs
                LOCALAPPDATA = (Join-Path $logs 'localappdata')
                XDG_CACHE_HOME = ''
                WHISPER_DOWNLOAD_ROOT = ''
            }
            WorkingDirectory = $logs
            RedirectStandardOutput = (Join-Path $logs "$test.stdout")
            RedirectStandardError = (Join-Path $logs "$test.stderr")
            Wait = $true
            PassThru = $true
        }
        if ($argsForTest.Count) { $options.ArgumentList = $argsForTest }
        $process = Start-Process @options
        Get-Content $options.RedirectStandardOutput
        Get-Content $options.RedirectStandardError
        if ($process.ExitCode -ne 0) { throw "$test failed as a standard user ($($process.ExitCode))" }
    }
} finally {
    if (Get-LocalUser -Name $user -ErrorAction SilentlyContinue) { Remove-LocalUser -Name $user }
}
