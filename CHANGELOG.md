# Changelog

## 0.2.0 - Unreleased

- Unified `transcribe`, `models list/download`, `doctor` and version commands.
- Directory sorting, numbered mappings, skip policy and CPU workers in the CLI.
- Process-death detection, SIGINT/SIGTERM cleanup and permission-aware outputs.
- Versioned JSON with run metadata; existing transcript fields retained.
- Thin Docker launchers, package tests, offline inference smoke test and CI.
- Experimental C++/whisper.cpp CLI with CPU/CUDA build recipes.
- MIT license, English/Russian documentation and contribution/security policy.

Migration: numbered mappings now use JSON rather than TSV. Existing numbered
files without their new mapping are not trusted; choose a new prefix/output
directory. `MAP_FILE` is no longer accepted. A directory means all recognized
media extensions, not only MP4. Native archives are experimental and have a
smaller CLI than the Python implementation.
