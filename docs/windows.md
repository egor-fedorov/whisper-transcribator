# Windows Validation

## ARM64

`windows-arm64-cpu.zip` targets native Windows 11 ARM64, not ARM64EC or an
emulated x64 application. The application and ggml use ClangCL with Microsoft's
linker, SDK and `/MD` runtime: the pinned ggml rejects MSVC's ARM compiler.
curl/zlib and decoding-only FFmpeg use the ARM64 MSVC toolchain. FFmpeg's handwritten
assembly is disabled to avoid a separate gas-preprocessor toolchain; this does
not disable ggml's NEON inference.

The ARM64 vcpkg overlay builds test-only OpenSSL with ClangCL as well: the
MSVC 14.51 build crashes inside `libssl` during the loopback TLS handshake.
The pinned source version and TLS checks are unchanged. OpenSSL is not shipped
in the ZIP; model downloads use libcurl with Windows Schannel.

Pinned ggml has no Windows ARM multi-variant dispatcher. The ZIP therefore uses
one dynamically loaded `ggml-cpu.dll`, compiled for baseline ARMv8-A/NEON without
host-native tuning, rather than Linux's SVE/SME variants. GPU/NPU acceleration
is outside this target; use `--device cpu` or `auto`.

The native `windows-11-arm` CI runner builds and runs model-free CTest, verifies
the exact ZIP on a fresh runner, then runs separate short offline inference,
Unicode-path and interruption/resume checks. Every bundled PE image must be
ARM64, including dependencies and the app-local Microsoft runtime; an x64 DLL
cannot silently pass validation through Windows emulation. MSYS2's build utilities
may themselves run under x64 emulation; the shipped application does not.
These checks do not benchmark Snapdragon laptops or validate their GPUs/NPUs.

## Compiler Comparison

MSVC remains the production **x64** compiler. To compare it with ClangCL, dispatch the
`CI` workflow with `run_windows_benchmark=true`. Both builds and all measurements
run in the same `windows-2025` job, not on two differently allocated runners.
Ordinary PR packaging does not download models or run this benchmark.

The manual check uses the pinned `small` model and a 41-second fixture made from
three copies of the public 11-second JFK sample with silence between them. It
uses CPU, two threads, English, beam size 1 and no VAD for both executables. One
warm-up per compiler is excluded, then three runs alternate MSVC/ClangCL order.
The `windows-compiler-comparison` artifact contains raw times, medians, CPU,
compiler versions, source/model/audio hashes, doctor diagnostics and transcripts.
Only results are uploaded, not models. Artifacts expire after seven days.

Times cover the whole CLI, including model verification and loading. They are a
same-VM comparison, not a claim about every Windows CPU or pure inference speed.
Wine and different-machine Linux results are not used for this decision. A
compiler switch requires successful native tests and repeatable measurements;
small differences on a shared VM are not enough.

The pinned ggml currently excludes five CPU variants under `MSVC`, which is also
true for ClangCL's MSVC-compatible mode. Changing the compiler alone does not
restore `ivybridge`, `piledriver`, `cooperlake`, `zen4` or `sapphirerapids`. Both
builds record their actual plugin inventory and selected backend. Extending that
inventory requires a separate upstream/toolchain compatibility change and tests,
not overriding CMake's compiler identity.

For ClangCL, the Alder Lake plugin explicitly enables AVX-VNNI: pinned ggml
defines its intrinsics but omits the compiler target flag. This flag is private
to that plugin, never global or applied to the baseline loader.
Both builds use Microsoft's linker and SDK manifest merger, keeping the runtime
and manifest path constant while comparing compiler code generation. LLVM's
[manifest-merging bugs](https://github.com/llvm/llvm-project/issues/120394) can
otherwise prevent startup before any application code runs.

## Clean Runtime

`package (windows-servercore)` runs the exact ZIP's `doctor` in a pinned Windows
Server Core container with networking disabled and a restricted PATH. It asserts
that no VC++ 140 runtime is installed in the system directories. A negative
control removes the bundled `vcruntime140.dll` and must fail with
`STATUS_DLL_NOT_FOUND`. This supplements PE import inspection; it does not replace
the separate native inference/resume smoke test or test Smart App Control.
Server Core validation is x64-only: the ARM runner has no Docker. ARM64 uses
static PE dependency closure checks and a restricted runtime PATH, but is not
yet tested on a machine without a preinstalled VC++ runtime.

## Signing

The ZIP's application, ggml/whisper, FFmpeg and curl DLLs are not signed by this
project. Bundled Microsoft redistributables keep their upstream signatures.
No signing credentials or signing service are configured yet.

[SignPath Foundation](https://signpath.org/) is a possible option, not an enabled
service. Enrollment requires maintainer approval, provider acceptance and a
protected signing workflow. Its [terms](https://signpath.org/terms) also restrict
signing third-party binaries, so eligibility and coverage of every bundled DLL
must be resolved with the provider before claiming Smart App Control support.
Signing only the executable or a wrapper around unsigned DLLs is not sufficient
evidence. After setup, verify signatures and launch on a native SAC-enabled
Windows machine before documenting support. Do not work around SAC by weakening
the user's security settings.
