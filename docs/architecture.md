# Architecture
The product is a local C++17 CLI with one inference implementation, whisper.cpp. Native archives and the optional Docker image contain the same application. This document describes the current code, not a proposed rewrite.

## Responsibilities
| Directory | Owns | Does not own |
| --- | --- | --- |
| `src/app` | CLI parsing, command orchestration, settings composition, diagnostics presentation | Decoding algorithms, durable storage, inference implementation |
| `src/audio` | FFmpeg decoding/resampling, stream selection, bounded timeline alignment and decode recovery | Models, transcript serialization, CLI parsing |
| `src/inference` | Device selection, backend initialization, whisper/VAD sessions and backend logging | Model downloads, output publication, CLI parsing |
| `src/models` | Pinned catalog, cache verification/locking, HTTPS transfers and download recovery | Inference and transcript storage |
| `src/transcript` | Shared value/options types, job planning, window policy/orchestration, compatibility metadata and the publication adapter | FFmpeg, whisper, curl and CLI11 APIs |
| `src/transcript/checkpoint` | Journal state, private record storage, chain validation, recovery, locking and durable publication | Output formatting and backend APIs |
| `src/transcript/render` | Streaming TXT/SRT/VTT/JSON formatting from prepared metadata and a repeatable segment source | Filesystem access, checkpoints and metadata construction |
| `src/platform` | Native files, locks, renames, permissions, system information, signals, CPU limits and SHA-256 | Application policy, logging and CLI settings |
| `src/platform/win32` | Private Windows handle/reparse helpers, ACLs and file-status queries | Public cross-platform APIs and application policy |
| `src/support` | Shared filesystem/string utilities, atomic publication, hashing, cancellation and reporting | Backend APIs and command orchestration |

`platform` may use the lightweight `support/fs.hpp` filesystem alias; higher-level support operations may use platform primitives. This is not permission to create arbitrary circular dependencies. Pure value/configuration headers must not import native handles, backend headers or transport implementations.

## Settings And Dependencies
`app/options.hpp` is the only production owner of the aggregate `CliOptions`. It composes module-owned values rather than copying their defaults: `ModelCacheOptions`, `InferenceOptions`, `AudioOptions`, `JobOptions`, `CheckpointOptions`, `RenderOptions` and `ChunkOptions`. CLI11 binds to those values; strings used to parse audio policies are converted at the application boundary. `--model`, `--device`, command selection and batch error handling remain application concerns.

Consumers take only the settings they need. Job planning receives job and checkpoint policies; a journal receives checkpoint policy and prepared metadata; a renderer receives formatting settings and a repeatable segment source. A whisper session receives inference settings and prepared model descriptors, never model-cache or output options. The VAD boundary call receives its silence threshold explicitly from the application's chunking settings.

Model descriptions and prepared paths/hashes live in `models/types.hpp`. The model-cache API does not include the HTTPS adapter or native file handles. `ensure_cached` offers an explicit transfer callback and permission probe for tests; its production overload selects HTTPS and the real filesystem probe.

Use small value types, free functions, RAII and composition. Introduce a callback or interface at a real substitution boundary, not an abstract base class for each concrete type. Keep independent calculations testable without filesystem access or a model. Do not add a target, factory or generic configuration registry for every directory.

## Audio And Window Boundaries
`audio/reader.hpp` exposes bounded PCM reads, stream selection and duration without FFmpeg headers or JSON diagnostics. Its implementation owns the demuxer, decoder, packets and frames. Opening/probing, packet submission, frame placement and EOF draining are named steps, not separate public services. The reader composes `FrameResampler`, `AudioTimeline` and `DecodeRecovery`; it does not duplicate their policies.

`audio/resampler.cpp` owns sample conversion and reconfiguration; `timeline.cpp` owns timestamp placement; `recovery.cpp` owns decoder error budgets. `runtime.hpp` exposes backend diagnostics and logging setup, implemented in `runtime.cpp` and `logging.cpp`. The private `audio/detail` headers declare shared error checking and a scoped metadata-probe logging guard, and are not dependencies of application code or public audio headers.

Input EOF, decoder EOF and an exhausted resampler are distinct states. At a timestamp boundary the reader drains the old resampler before converting the retained frame; format changes inside the resampler likewise preserve delayed samples. Opening happens after the reader's implementation has been constructed, so its destructor releases partially acquired FFmpeg resources on failure. Keep these lifetimes and the order of recovery/cancellation checks intact when changing the decode loop.

`transcript/boundaries.hpp` contains the pure pause/segment cut API, with no journal, filesystem or pipeline dependency. Both whisper's VAD adapter and `pipeline.cpp` use it; inference does not include the pipeline orchestration API. `boundaries.cpp` stays in `wt_core`, while FFmpeg-specific code stays in `wt_engine`.

## Checkpoints And Rendering
`checkpoint/journal.hpp` remains the public checkpoint facade. `journal.cpp` owns state transitions, lock lifetime and append/finish operations; `storage.cpp` owns bounded record I/O and synchronized removal; `privacy.cpp` owns permission probing and entry safety; `records.cpp` validates manifest fields, segments, languages and the committed hash chain; `recovery.cpp` handles saved, empty, orphaned and retired checkpoints; `checkpoint/publication.cpp` verifies existing outputs, stages results and performs durable publication. Their shared declarations are private under `checkpoint/detail`, not a second application-facing API.

The durable sequence is deliberate: acquire the lock before recovery; on the first append persist the initial manifest, then write the chunk before advancing the manifest; stage and hash every output before saving publication hashes; publish destinations only after that commit; finally rename the checkpoint to `.completed`, sync, remove it and sync again. Keep original record order, hash inputs and restored output metadata intact. Recovery validates orphaned records against a candidate state without mutating the committed one.

`render/render.hpp` exposes only `TranscriptSource`, timestamp formatting and `render_stream`. `render.cpp` dispatches formats, `text.cpp` shares whitespace/paragraph rules with JSON, `subtitles.cpp` handles SRT/VTT timestamps and escaping, and `json.cpp` streams text and segments without accumulating the full transcript. Helpers under `render/detail` are private. JSON intentionally visits the source twice, and serialization order and whitespace are part of the byte-stable publication contract.

`transcript/publication.hpp/.cpp` is the adapter between these independent modules: it supplies the journal's segment visitor and frozen metadata to a renderer callback. Neither module includes the other's API or the adapter. Keep them in the existing `wt_core` target; directories express ownership, not a requirement for additional libraries or interface hierarchies.

## Files And Native Backends
`support/files.hpp` owns path resolution, identity comparisons, text-file reads and directory writability probes. `support/atomic.hpp` owns buffered writes, staging/publication, permission preservation and directory synchronization. `support/permissions.hpp` describes which ownership/mode checks a filesystem can enforce and probes those capabilities with a private temporary file. `support/strings.hpp` holds whitespace trimming and environment-string reads without filesystem or platform dependencies. Include the needed API directly; there is no general `support/io.hpp` umbrella.

`probe_permissions` is an ordinary function, not a replaceable global hook. A journal owns its `PermissionProbe` callback through its private privacy component and caches the result for that output filesystem. Model-cache preparation and partial-file opening accept a probe explicitly at the operation boundary. Production callers use the real probe by default; permissionless-filesystem tests inject their own callbacks without affecting other journals or downloads. The probe can relax only unsupported ownership/mode attributes, never entry-type, symlink, hard-link or model-hash checks.

`platform/file.hpp` remains the OS-free native file API. `file-win32.cpp` implements public operations; private `win32/handles` helpers own native conversions, errno mapping, no-follow opening and safe reopening; `win32/security` owns principal discovery, protected/inherited ACLs and permission inspection; `win32/status` owns identity, timestamps, type and size queries. `PrivateSecurity` cannot be copied or moved because its native structures point into its own buffers. Windows helpers must not appear in public platform headers or higher-level modules. The POSIX backend and the cohesive console/system backend remain separate implementation files.

Keep the safety sequence intact: identify a reopened file before changing its ACL; reject name-surrogate reparse points while checking identity for other reparse types; flush staged content and set destination permissions before rename; sync affected directories after publication. Failed/cancelled writers remove only their temporary output. These are storage contracts, not reasons to introduce a filesystem service hierarchy or alter checkpoint schemas.

## Data Flow And Durability
1. The application parses arguments, plans safe destinations and validates explicit stream selection before preparing models.
2. It resolves CPU/device settings and verifies models, then shares a lazily initialized whisper session across sequential jobs.
3. Each job opens an audio reader, records the selected stream and prepares output metadata plus a compatibility fingerprint. Metadata assembly consumes module-owned configuration values, not the CLI aggregate.
4. The window pipeline reads bounded PCM and calls injected recognition/boundary functions. The journal commits only completed windows; resume decodes the committed prefix without repeating inference.
5. Publication adapts the journal into a repeatable streaming segment source. Renderers write TXT/SRT/VTT/JSON without opening files or constructing run metadata. The journal stages, hashes and publishes outputs before retiring its checkpoint.

Checkpoint fingerprints exclude execution-only CPU counts and application build identifiers. Output metadata retains the original model spelling and run information for byte-stable resumed publication. Refactoring must preserve both contracts, as well as the schema/algorithm versions, atomic write order, permission checks and bounded-memory traversal. An actual change to decoding, windowing or serialization requires a separate compatibility review.

## Tests And Build Boundaries
Test paths distinguish execution scope from domain ownership. `tests/unit` and `tests/integration` contain subsystem directories matching the production owners: `app`, `audio`, `inference`, `models`, `platform`, `support` and `transcript`, only where there are tests. Do not change a CTest name or move a check between labels merely because its source moves.

| Directory | Responsibility |
| --- | --- |
| `tests/unit/<domain>` | Focused policies and contracts, including fake model caches and safe native I/O |
| `tests/unit/architecture` | Source/module ownership and test-helper dependency checks |
| `tests/integration/<domain>` | Composed behavior, crash recovery, synthetic decoding, loopback TLS and CLI/native-platform checks |
| `tests/resource` | Synthetic bounded-memory and planning-scale checks |
| `tests/smoke` | Short real inference and resume using public fixtures; opt-in comparisons live in `tests/benchmarks` |
| `tests/packaging` | Archive, source-provenance and release-helper checks |
| `tests/support` | Assertions and scenario runner (`test.*`), scoped environment/current-directory/stream restoration (`scoped.*`) |
| `tests/support/fixtures` | Temporary directories, job/model/transcript builders, fake PCM/inference and WAV encoding helpers |
| `tests/support/platform` | Child processes, sockets, fixture permissions, RSS measurements and Windows crash diagnostics |
| `tests/support/tools` | The separate `wt-audio-fixture` and `wt-process-runner` helper executables |
| `tests/fixtures` | Reviewed, committed compatibility snapshots, not generated fixture builders |
| `tests/cmake` | Explicit test/support/tool registration and standalone header checks |

Unit, integration and resource checks are model-free. The small resource, smoke and packaging groups are not further split merely for symmetry. Real inference remains separate from packaging and default CTest, and no private recordings or model weights belong in the source tree.

`support/test.hpp` is an assertions/runner API, not an umbrella for fixtures or native helpers. Include the scoped, fixture or platform header actually used. The runner creates a private temporary directory per scenario, retains it on failure, restores cancellation state and scopes native crash diagnostics to the run. Domain fixtures may use the runner's assertions; platform helpers must not depend on domain fixtures or application settings.

Integration fixtures may compose application settings to exercise the same metadata mapping as the CLI. This does not permit production modules to depend on `app`, or tests to regenerate compatibility expectations using the code under test. Rendering tests provide their own prepared metadata.

`unit/module-boundary` rejects application dependencies outside `app`, backend API includes outside their adapters, cross-dependencies between checkpoint storage and rendering, and imports of private helpers outside their implementations, including `platform/win32`. Renderers cannot import application services or construct metadata. The same check enforces the test-helper boundaries above. It checks positive and negative examples as well as the actual sources. `unit/platform-boundary` separately enforces native system-header ownership in production. Standalone compilation checks both production and test headers for accidental transitive includes; private Windows headers are checked on native Windows builds only. These lightweight checks supplement review; they are not a complete C++ dependency analyzer.

`tests/CMakeLists.txt` composes `tests/cmake` through `include`, not nested build directories. `support.cmake` owns the shared library and registration functions; `unit.cmake`, `integration.cmake` and `resource.cmake` own checks and their existing dependency/platform conditions; `tools.cmake` owns auxiliary executables; `headers.cmake` lists standalone headers. Keep source lists explicit. Test names, labels, timeouts, native executable locations and the Windows manifest/link settings are part of the CI contract; moving sources must not create a new library or build target per directory.

Build targets remain `wt_core` (model-free services), `wt_engine` (audio/inference adapters and command execution) and the CLI (parsing/entry point). `cmake` holds pinned dependencies and generated metadata. `packaging` owns archive/container construction; workflows orchestrate native validation and preserve separate model-free packaging and real-inference gates. All generated artifacts belong under `.build`; models, recordings and local benchmark results are not repository sources.
