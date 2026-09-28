# Run in an x64 MSVC developer shell after building FFmpeg with packaging/ffmpeg.sh.
param(
    [string]$Build = '.build/windows',
    [string]$Media = '.build/windows-ffmpeg/prefix',
    [string]$Vcpkg = '.build/vcpkg',
    [string]$PkgConfig = 'C:/msys64/mingw64/bin/pkgconf.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
}
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $PkgConfig = (Resolve-Path $PkgConfig).Path.Replace('\', '/')
    Invoke-Checked $PkgConfig @('--version')
    $mediaPath = (Resolve-Path $Media).Path.Replace('\', '/')
    $vcpkgPath = (Resolve-Path $Vcpkg).Path.Replace('\', '/')
    $env:PKG_CONFIG_LIBDIR = "$mediaPath/lib/pkgconfig"
    $env:PKG_CONFIG_PATH = ''
    Invoke-Checked "$vcpkgPath/bootstrap-vcpkg.bat" @('-disableMetrics')
    Invoke-Checked cmake @('-S', '.', '-B', $Build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DWT_WERROR=ON',
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgPath/scripts/buildsystems/vcpkg.cmake",
        '-DVCPKG_TARGET_TRIPLET=x64-windows', '-DVCPKG_MANIFEST_FEATURES=tests',
        "-DPKG_CONFIG_EXECUTABLE=$PkgConfig",
        '-DWT_TEST_OPENSSL=C:/Program Files/Git/usr/bin/openssl.exe',
        '-DGGML_NATIVE=OFF', '-DGGML_BACKEND_DL=ON', '-DGGML_CPU_ALL_VARIANTS=ON',
        '-DGGML_OPENMP=OFF')
    Invoke-Checked cmake @('--build', $Build, '--parallel', '3')
    Copy-Item "$mediaPath/bin/*.dll" "$Build/bin" -Force
    Copy-Item "$Build/vcpkg_installed/x64-windows/bin/*.dll" "$Build/bin" -Force
    Invoke-Checked ctest @('--test-dir', $Build, '--output-on-failure')
} finally { Pop-Location }
