# Run in an x64 MSVC developer shell after building FFmpeg with packaging/ffmpeg.sh.
param(
    [string]$Build = '.build/windows',
    [string]$Media = '.build/windows-ffmpeg/prefix',
    [string]$Vcpkg = '.build/vcpkg',
    [string]$PkgConfig = 'C:/msys64/mingw64/bin/pkgconf.exe',
    [ValidateSet('msvc', 'clangcl')][string]$Compiler = 'msvc',
    [ValidateSet('All', 'Configure', 'Build', 'Test')][string]$Stage = 'All'
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
    if ($Stage -ne 'Test') {
        $mediaPath = (Resolve-Path $Media).Path.Replace('\', '/')
        $env:PKG_CONFIG_LIBDIR = "$mediaPath/lib/pkgconfig"
        $env:PKG_CONFIG_PATH = ''
    }
    if ($Stage -in @('All', 'Configure')) {
        $PkgConfig = (Resolve-Path $PkgConfig).Path.Replace('\', '/')
        Invoke-Checked $PkgConfig @('--version')
        # FFmpeg installs MSVC import libraries beside its DLLs, not in lib/.
        foreach ($library in @('avformat', 'avcodec', 'swresample', 'avutil')) {
            if (!(Test-Path "$mediaPath/bin/$library.lib")) {
                throw "Missing FFmpeg import library: $mediaPath/bin/$library.lib"
            }
        }
        $vcpkgPath = (Resolve-Path $Vcpkg).Path.Replace('\', '/')
        Invoke-Checked "$vcpkgPath/bootstrap-vcpkg.bat" @('-disableMetrics')
        $compilerOptions = @('-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl')
        if ($Compiler -eq 'clangcl') {
            $clang = (Get-Command clang-cl.exe -ErrorAction Stop).Source.Replace('\', '/')
            $compilerOptions = @("-DCMAKE_C_COMPILER=$clang", "-DCMAKE_CXX_COMPILER=$clang")
        }
        Invoke-Checked cmake (@('-S', '.', '-B', $Build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DWT_WERROR=ON',
            "-DCMAKE_TOOLCHAIN_FILE=$vcpkgPath/scripts/buildsystems/vcpkg.cmake",
            '-DVCPKG_TARGET_TRIPLET=x64-windows', '-DVCPKG_MANIFEST_FEATURES=tests',
            "-DPKG_CONFIG_EXECUTABLE=$PkgConfig",
            "-DCMAKE_LIBRARY_PATH=$mediaPath/bin",
            '-DWT_TEST_OPENSSL=C:/Program Files/Git/usr/bin/openssl.exe',
            '-DGGML_NATIVE=OFF', '-DGGML_BACKEND_DL=ON', '-DGGML_CPU_ALL_VARIANTS=ON',
            '-DGGML_OPENMP=OFF') + $compilerOptions)
    }
    if ($Stage -in @('All', 'Build')) {
        Invoke-Checked cmake @('--build', $Build, '--parallel', '3', '--', '-k', '0')
        Copy-Item "$Media/bin/*.dll" "$Build/bin" -Force
        Copy-Item "$Build/vcpkg_installed/x64-windows/bin/*.dll" "$Build/bin" -Force
    }
    if ($Stage -in @('All', 'Test')) {
        Invoke-Checked ctest @('--test-dir', $Build, '--output-on-failure')
    }
} finally { Pop-Location }
