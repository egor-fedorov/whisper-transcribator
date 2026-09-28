# Manual, same-host end-to-end comparison; never use a full lecture here.
param(
    [string]$Msvc = '.build/windows/bin',
    [string]$ClangCl = '.build/windows-clangcl/bin',
    [string]$Work = '.build/windows-benchmark',
    [ValidateRange(1, 64)][int]$Threads = 2,
    [ValidateRange(3, 9)][int]$Iterations = 3
)
. "$PSScriptRoot/../../packaging/windows-common.ps1"
$bins = [ordered]@{ msvc = (Resolve-Path $Msvc).Path; clangcl = (Resolve-Path $ClangCl).Path }
New-Item -ItemType Directory -Force "$Work/results" | Out-Null
$Work = (Resolve-Path $Work).Path
$results = "$Work/results"
$jfk = "$Work/jfk.wav"
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/ggml-org/whisper.cpp/927cfce34f31707e17f2bff35c349632fb9e2c3a/samples/jfk.wav' `
    -OutFile $jfk -MaximumRetryCount 3 -RetryIntervalSec 5
if ((Get-FileHash $jfk).Hash -ne '59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e') {
    throw 'Public audio checksum mismatch'
}
$sample = "$Work/sample.wav"
Invoke-Checked "$($bins.msvc)/wt-audio-fixture.exe" @('--repeat', $jfk, $sample)
Invoke-Checked "$($bins.msvc)/whisper-transcribator.exe" @('models', 'download', 'small', '--download-root', "$Work/models")
$model = "$Work/models/ggml-small.bin"
if ((Get-FileHash $model).Hash -ne '1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b') {
    throw 'Small model checksum mismatch'
}
$diagnostics = [ordered]@{}
foreach ($name in $bins.Keys) {
    $binary = "$($bins[$name])/whisper-transcribator.exe"
    $doctor = (Invoke-Checked $binary @('doctor', '--device', 'cpu', '--json', '--cpu-threads', "$Threads")) | ConvertFrom-Json
    if ($doctor.errors.Count) { throw "Doctor failed for $name" }
    $diagnostics[$name] = [ordered]@{
        doctor = $doctor
        plugins = @(Get-ChildItem "$($bins[$name])/ggml-cpu-*.dll" | ForEach-Object Name | Sort-Object)
    }
}
foreach ($field in @('source_revision', 'source_dirty', 'backend_revision', 'cpu_backend')) {
    if ($diagnostics.msvc.doctor.$field -cne $diagnostics.clangcl.doctor.$field) {
        throw "Comparison requires the same $field"
    }
}
$measurements = [Collections.Generic.List[object]]::new()
function Measure-Run([string]$Name, [int]$Round) {
    $prefix = "$results/$Name-$Round"
    $binary = "$($bins[$Name])/whisper-transcribator.exe"
    $arguments = @($sample, '-o', "$prefix.json", '--format', 'json', '--model', $model,
        '--local-files-only', '--language', 'en', '--device', 'cpu', '--cpu-threads', "$Threads",
        '--beam-size', '1', '--no-vad', '--chunk-seconds', '120', '--quiet', '--overwrite')
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $binary @arguments 1> "$prefix.stdout.log" 2> "$prefix.stderr.log"
    $code = $LASTEXITCODE
    $timer.Stop()
    if ($code -ne 0) { throw "Benchmark $Name round $Round failed ($code); see $prefix.stderr.log" }
    $transcript = Get-Content -Raw "$prefix.json" | ConvertFrom-Json
    if ($transcript.duration -lt 40 -or $transcript.duration -gt 42 -or
        !$transcript.segments.Count -or $transcript.text -notmatch 'country') { throw "Unexpected benchmark transcript: $prefix" }
    $measurement = [ordered]@{ compiler = $Name; round = $Round; seconds = $timer.Elapsed.TotalSeconds }
    Write-Output "$Name round ${Round}: $($measurement.seconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)) s"
    if ($Round -gt 0) { $measurements.Add($measurement) }
}
# Warm both models/file caches, then alternate order to reduce systematic bias.
Measure-Run msvc 0
Measure-Run clangcl 0
for ($round = 1; $round -le $Iterations; ++$round) {
    $order = if ($round % 2) { @('msvc', 'clangcl') } else { @('clangcl', 'msvc') }
    foreach ($name in $order) { Measure-Run $name $round }
}
$medians = [ordered]@{}
foreach ($name in $bins.Keys) {
    $times = @($measurements | Where-Object compiler -eq $name | ForEach-Object seconds | Sort-Object)
    $medians[$name] = ($times[[int][Math]::Floor(($times.Count - 1) / 2)] + $times[[int][Math]::Floor($times.Count / 2)]) / 2
}
$report = [ordered]@{
    scope = 'Same hosted VM; warmed end-to-end CLI time including model verification/loading, not pure inference or a hardware-wide claim'
    utc = [DateTime]::UtcNow.ToString('o')
    cpu = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors)
    os = [Environment]::OSVersion.VersionString
    toolchain = @{ msvc = $env:VCToolsVersion; clangcl = (Invoke-Checked clang-cl @('--version')) }
    threads = $Threads
    audio_sha256 = (Get-FileHash $sample).Hash.ToLowerInvariant()
    model_sha256 = (Get-FileHash $model).Hash.ToLowerInvariant()
    diagnostics = $diagnostics
    measurements = @($measurements.ToArray())
    median_seconds = $medians
    msvc_over_clangcl = $medians.msvc / $medians.clangcl
}
Write-Utf8 "$results/comparison.json" ($report | ConvertTo-Json -Depth 12)
$summary = "MSVC median: $($medians.msvc.ToString('F3')) s; ClangCL median: $($medians.clangcl.ToString('F3')) s; MSVC/ClangCL: $($report.msvc_over_clangcl.ToString('F3')). Same VM, 41-second fixture, small model, $Threads threads; includes model loading."
Write-Output $summary
if ($env:GITHUB_STEP_SUMMARY) { Add-Content $env:GITHUB_STEP_SUMMARY $summary }
