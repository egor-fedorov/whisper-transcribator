param(
    [Parameter(Mandatory)][string]$Binary,
    [Parameter(Mandatory)][string]$Fixtures,
    [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Binary = (Resolve-Path $Binary).Path
$Fixtures = (Resolve-Path $Fixtures).Path
# Keep the script ASCII while exercising Cyrillic, Chinese and spaces in every path.
$name = 'Unicode ' + [char]0x041B + [char]0x4E2D
$root = Join-Path $Output $name
$cache = Join-Path $root 'model cache'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
Copy-Item (Join-Path $Fixtures 'ggml-tiny.bin') $cache
Copy-Item (Join-Path $Fixtures 'ggml-silero-v6.2.0.bin') $cache
foreach ($format in @('wav', 'mp3')) {
    $sample = Join-Path $root "$name.$format"
    Copy-Item (Join-Path $Fixtures "jfk.$format") $sample
    $outputDirectory = Join-Path $root "result/$format"
    & $Binary $sample --output-dir $outputDirectory --format all `
        --model tiny --download-root $cache --vad-model (Join-Path $cache 'ggml-silero-v6.2.0.bin') `
        --language en --device cpu --cpu-threads 2 --local-files-only
    if ($LASTEXITCODE -ne 0) { throw "Unicode-path $format inference failed" }
    $result = Get-Content -Raw -Encoding utf8 (Join-Path $outputDirectory "$name.json") | ConvertFrom-Json
    if ($result.text -notmatch 'country' -or $result.segments.Count -eq 0 -or
        $result.duration -le 10 -or $result.duration -ge 12) {
        throw "Unicode-path $format inference produced no expected speech/duration"
    }
    foreach ($extension in @('txt', 'srt', 'vtt')) {
        if ((Get-Item (Join-Path $outputDirectory "$name.$extension")).Length -eq 0) {
            throw "Empty Unicode-path $format output: $extension"
        }
    }
    Write-Output "Offline Unicode-path $format inference passed"
}
