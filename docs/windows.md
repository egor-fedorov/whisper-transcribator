# Windows Validation

## Compiler Comparison

MSVC remains the production compiler. To compare it with ClangCL, dispatch the
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

## Clean Runtime

`package (windows-servercore)` runs the exact ZIP's `doctor` in a pinned Windows
Server Core container with networking disabled and a restricted PATH. It asserts
that no VC++ 140 runtime is installed in the system directories. A negative
control removes the bundled `vcruntime140.dll` and must fail with
`STATUS_DLL_NOT_FOUND`. This supplements PE import inspection; it does not replace
the separate native inference/resume smoke test or test Smart App Control.

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
