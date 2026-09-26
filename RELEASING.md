# Release Checklist

No automatic publication credentials or release workflow are configured.
Run these checks on a clean checkout before tagging a public release:

1. Run the CI commands in CONTRIBUTING, including Python 3.10 and 3.13 tests,
   Ruff, ShellCheck, coverage, wheel/sdist builds and Git history secret scanning.
2. Install the wheel and sdist in separate clean environments outside the source
   tree. Run `pip check`, `--version`, `doctor`, and real offline tiny inference.
3. Build CPU and CUDA images. Test CPU without networking; test CUDA inference on
   trusted hardware and record driver/runtime versions. Do not use public PRs to
   access a personal GPU runner.
4. Review dependency updates and license notices. Python Docker dependencies are
   pinned in requirements.lock; native FFmpeg/whisper.cpp versions need explicit
   review. Pinned versions are not a guarantee of current security fixes.
5. Verify that recordings, transcripts, prompts, tokens and model caches are not
   tracked. Inspect all branches/tags that will be pushed, not just main.
6. Update CHANGELOG and version metadata, then create a reviewed release commit
   and tag. Publish only after the owner chooses repository visibility and names.

The C++ archives are experimental and not part of the supported Python release.
Their separate gates include ASan/UBSan smoke tests, clean-system CPU/GPU tests,
architecture/glibc documentation, dependency notices and paired benchmarks.
Do not promote the prototype solely because it compiles or is fast on one GPU.
