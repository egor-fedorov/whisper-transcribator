# Third-Party Components

The application is MIT licensed. Dependency licenses apply independently.
Model weights are downloaded separately and retain their upstream terms.

| Component | Pin / origin | Notices in archive |
| --- | --- | --- |
| whisper.cpp / GGML | commit 927cfce34f31707e17f2bff35c349632fb9e2c3a | MIT, source archive |
| CLI11 | 2.5.0 | BSD, source archive |
| nlohmann JSON | 3.11.2, independent hash-pinned dependency | MIT, source archive |
| FFmpeg | 8.0.1, no GPL/nonfree components | LGPL-2.1, source and configure log |
| libcurl, OpenSSL and transitive distro libraries | Ubuntu 22.04 security packages | package notices and source archives |
| GCC runtimes | Ubuntu build compiler | GPL runtime exception notices |
| CUDA runtime/cuBLAS, NCCL when linked | CUDA 12.8.1 build image | vendor package notices |

aarch64 CPU archives are compiled with Ubuntu 22.04's `clang-15` against the same
GCC C++ runtime and without OpenMP; they bundle no LLVM runtime library.
CPU archives do not include CUDA libraries. CUDA archives include redistributable
runtime libraries, but **not** the NVIDIA driver or glibc. The archive contains
`licenses/`, `sources/` and `share/system-sources.tsv`; the packaging script fails
on an unrecognized library owner instead of silently omitting its notices.
Distro source archives correspond to the selected binary package versions.
GCC runtime exception notices are included; GCC source archives are not bundled.
CUDA libraries remain subject to their vendor terms.

CMake dependencies, FFmpeg and catalog models are hash-pinned. Apt security
packages are intentionally refreshed, so builds are not claimed to be
bit-for-bit reproducible. Each exported archive has a generated SHA-256 file.
When changing dependencies, inspect the actual linked libraries and notices
before distribution. Do not discard the license/source directories when publishing.

The FFmpeg build enables local-file decoding for common lecture containers
(MP4/MOV, Matroska/WebM, WAV, MP3, Ogg, FLAC, AAC, AIFF, AVI, ASF and MPEG).
Not every codec inside those containers is supported. The exact decoder list
is in [Dockerfile](../packaging/Dockerfile). No network media protocol is enabled.
