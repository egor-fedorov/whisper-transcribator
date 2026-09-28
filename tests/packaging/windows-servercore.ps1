param(
    [Parameter(Mandatory)][string]$Artifacts,
    [string]$Image = 'mcr.microsoft.com/windows/servercore:ltsc2025@sha256:e18a49cbc074dfaa8e106296d51cebd62bbf6effb999f134a5c48eed1c2334e1'
)
. "$PSScriptRoot/../../packaging/windows-common.ps1"
if ((Invoke-Checked docker @('info', '--format', '{{.OSType}}')).Trim() -ne 'windows') {
    throw 'This check needs a Windows container host, not Linux Docker or Wine'
}
$Artifacts = (Resolve-Path $Artifacts).Path
$archives = @(Get-ChildItem $Artifacts -Filter '*-windows-x86_64-cpu.zip')
if ($archives.Count -ne 1) { throw 'Expected one Windows ZIP' }
$archive = $archives[0]
if ((Get-Content -Raw "$Artifacts/SHA256SUMS").TrimEnd() -cne
    "$((Get-FileHash $archive.FullName).Hash.ToLowerInvariant())  $($archive.Name)") { throw 'ZIP checksum mismatch' }
$temporary = [IO.Directory]::CreateTempSubdirectory('wt-servercore-').FullName
try {
    [IO.Compression.ZipFile]::ExtractToDirectory($archive.FullName, $temporary)
    Invoke-Checked docker @('pull', $Image)
    Invoke-Checked docker @('run', '--rm', '--isolation=process', '--network=none',
        '--mount', "type=bind,source=$temporary,target=C:\bundle,readonly",
        '--mount', "type=bind,source=$PSScriptRoot,target=C:\checks,readonly",
        $Image, 'powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', 'C:\checks\windows-servercore-guest.ps1')
} finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
