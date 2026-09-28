# Run in a matching MSVC developer shell after building decoding-only FFmpeg.
param(
    [string]$Build = '.build/windows',
    [string]$Media = '.build/windows-ffmpeg/prefix',
    [string]$Vcpkg = '.build/vcpkg',
    [string]$PkgConfig = 'C:/msys64/mingw64/bin/pkgconf.exe',
    [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64',
    [ValidateSet('msvc', 'clangcl')][string]$Compiler,
    [switch]$Vulkan,
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
    if (!$Compiler) { $Compiler = if ($Architecture -eq 'arm64') { 'clangcl' } else { 'msvc' } }
    if ($Architecture -eq 'arm64' -and ($Compiler -ne 'clangcl' -or $Vulkan)) {
        throw 'Windows ARM64 requires ClangCL and currently supports CPU only'
    }
    if ($env:VSCMD_ARG_TGT_ARCH -ne $Architecture) { throw "Use a $Architecture developer shell" }
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
        $vulkanOption = if ($Vulkan) { 'ON' } else { 'OFF' }
        if ($Compiler -eq 'clangcl') {
            $clang = (Get-Command clang-cl.exe -ErrorAction Stop).Source.Replace('\', '/')
            # Use the same linker/SDK manifest merger as MSVC, not LLVM's XML merger.
            $link = (Resolve-Path "$env:VCToolsInstallDir/bin/Host$($env:VSCMD_ARG_HOST_ARCH)/$Architecture/link.exe").Path.Replace('\', '/')
            $mt = (Resolve-Path "$env:WindowsSdkVerBinPath/$($env:VSCMD_ARG_HOST_ARCH)/mt.exe").Path.Replace('\', '/')
            $compilerOptions = @("-DCMAKE_C_COMPILER=$clang", "-DCMAKE_CXX_COMPILER=$clang",
                "-DCMAKE_LINKER=$link", "-DCMAKE_MT=$mt")
            if ($Architecture -eq 'arm64') {
                $compilerOptions += @('-DCMAKE_C_COMPILER_TARGET=aarch64-pc-windows-msvc',
                    '-DCMAKE_CXX_COMPILER_TARGET=aarch64-pc-windows-msvc')
            }
        }
        $cpuOptions = @('-DGGML_CPU_ALL_VARIANTS=ON')
        if ($Architecture -eq 'arm64') {
            # Pinned ggml has no Windows ARM variant dispatcher; keep a portable NEON plugin.
            $cpuOptions = @('-DGGML_CPU_ALL_VARIANTS=OFF', '-DGGML_CPU_ARM_ARCH=armv8-a')
        }
        Invoke-Checked cmake (@('-S', '.', '-B', $Build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DWT_WERROR=ON',
            "-DWT_VULKAN=$vulkanOption",
            "-DCMAKE_TOOLCHAIN_FILE=$vcpkgPath/scripts/buildsystems/vcpkg.cmake",
            "-DVCPKG_TARGET_TRIPLET=$Architecture-windows", '-DVCPKG_MANIFEST_FEATURES=tests',
            "-DPKG_CONFIG_EXECUTABLE=$PkgConfig",
            "-DCMAKE_LIBRARY_PATH=$mediaPath/bin",
            '-DWT_TEST_OPENSSL=C:/Program Files/Git/usr/bin/openssl.exe',
            '-DGGML_NATIVE=OFF', '-DGGML_BACKEND_DL=ON',
            '-DGGML_OPENMP=OFF') + $compilerOptions + $cpuOptions)
    }
    if ($Stage -in @('All', 'Build')) {
        Invoke-Checked cmake @('--build', $Build, '--parallel', '3', '--', '-k', '0')
        Copy-Item "$Media/bin/*.dll" "$Build/bin" -Force
        Copy-Item "$Build/vcpkg_installed/$Architecture-windows/bin/*.dll" "$Build/bin" -Force
    }
    if ($Stage -in @('All', 'Test')) {
        Invoke-Checked ctest @('--test-dir', $Build, '--output-on-failure')
    }
} finally { Pop-Location }
