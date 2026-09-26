# Artifact Dependency Notes

The application code is MIT. That does not relicense the following dependencies.

| Component | Artifact treatment |
| --- | --- |
| whisper.cpp / ggml | MIT notice and exact source tree included |
| nlohmann/json header | Header with MIT notice included |
| FFmpeg 8.0.1 | Shared LGPL build, original source archive and configure log included |
| libstdc++, libgcc, libgomp | Distribution copyright/exception notices included |
| cuBLAS, CUDA runtime (CUDA archive only) | Proprietary NVIDIA notices included; driver excluded |
| Model weights | Not distributed by this project |

The FFmpeg build disables autodetection, GPL/nonfree components, networking and
external codec libraries. It dynamically links the included libraries; users can
replace them. See the [FFmpeg distribution checklist](https://ffmpeg.org/legal.html).
The GNU runtime exception is documented by the
[FSF](https://www.gnu.org/licenses/gcc-exception-3.1-faq.en.html).
CUDA redistribution is governed by the
[CUDA 12.8.1 EULA](https://docs.nvidia.com/cuda/archive/12.8.1/eula/index.html),
not the repository MIT license.

These are build notes, not a legal opinion or a blanket redistribution approval.
Before publishing an archive, inspect `ldd`, `licenses/` and `sources/`, check for
missing or dangling notices, and check the terms of every actual dependency.
If the build image, compiler or CUDA version changes, repeat that audit.
Only local experimental artifacts have been produced; no binaries were published.
