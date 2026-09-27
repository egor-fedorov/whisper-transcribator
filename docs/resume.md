# Long Recordings And Resume
This describes the unreleased 0.4 development line. Published 0.3.0 archives do not provide windowing or resume.

## Windows And Memory
Audio is decoded incrementally into mono 16 kHz PCM and recognized in windows of at most 120 seconds. `--chunk-seconds` accepts 30-600. Smaller windows reduce recording-dependent buffers, but not model weights or backend workspaces: this is not a hard RAM/VRAM cap.

The full window is recognized; only complete, non-overlapping segments before the chosen boundary and a two-second end guard are committed. The remaining audio tail is recognized again in the next window, including with `--no-vad`. Smaller windows generally increase this repeated work. The overhead depends on segment lengths and speech; measurements from repeated short samples are not lecture throughput guarantees. Keep 120 seconds unless memory constraints justify reducing it.

With VAD, a suitable pause in the last quarter of the window is preferred. `--chunk-min-silence-ms` controls that pause (200 ms); `--vad-min-silence-ms` independently controls inference VAD (2000 ms). If there is no safe positive segment boundary, the entire window is committed with a warning. Estimated segment timestamps cannot guarantee perfect boundary words. At EOF the remaining segments are committed; silence also advances the checkpoint.

PCM, VAD and recognition buffers cover only the current window. TXT/SRT/VTT/JSON are rendered by iterating saved segments, without collecting the whole transcript in RAM. Container metadata, weights and backend workspaces still consume memory. `--language auto` detects language independently per recognition window, not per word.

An entirely zero-valued window bypasses both Whisper and VAD, even with `--no-vad`, but still advances progress and the checkpoint. This applies to exact digital silence only: quiet audio and windows mixing silence with speech use normal inference. No recognition text is blacklisted. A recording with no transcript still exits with `No transcript produced` and does not publish empty output files; its completed checkpoint remains available.

## Container Timeline
Timestamps refer to the container's playback origin, not the first decoded audio sample. The origin is the container start time, then the earliest known stream start, or the first usable decoded timestamp if neither is known. Leading audio delay is always preserved as silence, emitted in bounded blocks; negative preroll is trimmed. Missing PTS advance by actual decoded samples; a later valid PTS can correct that estimate.

After the first frame, timestamp jitter within the larger of 100 ms or one stream time-base tick is tolerated without inserting/trimming samples or resetting the resampler. This handles codec overlap/rounding, including Ogg Vorbis; accumulated drift beyond the tolerance is still handled. The first timestamp is never snapped to zero. Changes in sample rate, sample format and channel layout preserve delayed resampler samples. Ordinary demuxing retains codec gapless trimming; MPEG-TS alone uses a separate normal metadata probe followed by raw-timestamp decoding to avoid stale sample-rate extrapolation.

`--timestamp-gaps auto` (default) starts a new contiguous section for forward jumps strictly greater than 10 seconds after playback has started, but only for discontinuous containers such as MPEG-TS. Smaller gaps, including a six-second packet loss, remain silence. `--timestamp-gaps preserve` retains all forward gaps beyond the jitter tolerance, including large transport jumps. Other containers preserve forward gaps in either mode. A preserved gap greater than 10 seconds emits one warning per file; bounded buffers prevent a large pause from allocating equally large PCM storage.

Backward jumps beyond the jitter tolerance start a contiguous section in every container, in either mode. This also handles accumulated negative drift from a recording clock running slower than the decoded audio: all samples are retained rather than trimming speech or failing the recording. This is not clock-rate compensation; subsequent timestamps are shifted to keep the audio continuous and may differ from the source clock by the accumulated drift. Corrections produce one warning per file; `--verbose` shows individual boundaries. Discontinuous recordings do not provide an unambiguous continuous original timeline; choose `preserve` when a large forward gap is an intentional pause.

JSON `duration` is the end of the selected, policy-adjusted audio timeline, including its leading delay and preserved gaps. It is not the sum of speech durations and is not padded to the end of a longer video. Progress uses an estimate until decoding determines the actual end; an unknown or exceeded estimate suppresses ETA. Correcting a timestamp discontinuity also invalidates the original duration estimate, so percentages and ETA are withheld rather than calculated against an obsolete timeline.

## Damaged Audio
An `AVERROR_INVALIDDATA` result when submitting an audio packet or receiving a decoded frame is recoverable. The decoder's rejected data is skipped, not repaired. The application warns once per file that the transcript may be incomplete, reports individual recovery events in verbose mode, and prints the total recovered error count at decoding EOF. FFmpeg may also emit its own diagnostics. Do not interpret successful completion as proof that every spoken word survived the recording damage.

Recovery stops at 8 decoder errors without a successful decoded frame, or 32 decoder errors within the most recent 128 input audio packets. These fixed budgets also bound repeated receive errors during draining. Successful packet submission alone does not reset the consecutive-error count. Files that never yield usable audio still fail. Other decoder errors, demuxer/read errors, invalid decoded parameters, allocation failures and cancellation are not converted into skipped data.

Later valid timestamps follow the same timeline policy described above; gaps beyond the jitter tolerance are preserved, while missing timestamps cannot reconstruct the duration of lost audio. Recovery does not flush healthy decoder state or remove successfully decoded samples. A resumed job replays decoding and the same error budgets across its saved prefix, without repeating inference there. Severe corruption remains an error on resume; committed progress is retained and incomplete results are not published. Repairing/replacing the source changes its hash and requires an explicit fresh run.

Audio decoding policy version 1 is part of checkpoint compatibility, independently of timeline version 3 and chunking version 4. Earlier checkpoints without this field are preserved but rejected; finish them with the original binary or explicitly restart.

## Restart An Interrupted Job
```bash
./bin/whisper-transcribator lecture.mp4 --output-dir transcripts --format all \
  --model large-v3 --device cuda --chunk-seconds 120
# Repeat the same command with --resume after interruption:
./bin/whisper-transcribator lecture.mp4 --output-dir transcripts --format all \
  --model large-v3 --device cuda --chunk-seconds 120 --resume
```

Each committed window is saved under the output directory's private `.whisper-transcribator/`. The journal contains transcript fragments and source paths, not audio or weights. Keep it on persistent storage; a Docker bind mount of `/output` also preserves checkpoints. Allow disk space for journal records and staged outputs. Disk errors fail the job.

Resume verifies the input contents and paths, requested destinations, model/VAD SHA-256, selected audio stream, `--timestamp-gaps` policy, inference and formatting settings, backend versions, and decoding/timeline/chunking/rendering algorithm versions. An incompatibility reports changed field paths. It never silently discards progress.

CPU thread counts, affinity and the spelling of model paths/catalog aliases do not determine compatibility. The actual model hashes do. Application version/Git revision are diagnostic metadata, not algorithm identifiers. `run` and top-level `model` describe the original job and remain frozen across resume so partially published JSON can be reconstructed byte-for-byte. Current execution settings appear in stderr; one resumed job may have used different CPU counts.

The completed prefix is decoded and discarded without inference. There is no approximate seek or full PCM cache. Source hashing and prefix decoding can take time; only the uncommitted window needs recognition again. Full model SHA-256 checks still run on each invocation, including `models list`; no persistent metadata-only verification cache is used.

## Recovery And Output Safety
- No checkpoint means `--resume` starts a new job. Existing committed progress requires explicit `--resume` or plain `--overwrite`.
- Failed attempts before the first committed window are safely restarted, even after replacing a broken input. Known private temporary files left during the first manifest/chunk write are also recoverable. Unknown files, unsafe links and corrupt records are never silently removed.
- `--overwrite` without `--resume` explicitly discards this job's journal. `--resume --overwrite` permits replacing outputs, but does not bypass journal compatibility checks.
- Outputs are atomically published individually; the multi-format set is not a transaction. After partial publication, matching completed outputs are retained and missing ones are regenerated without inference. Externally changed outputs require explicit overwrite permission.
- Successful publication removes checkpoint payloads. Small lock files remain deliberately: deleting a lock pathname while another process uses its inode can break mutual exclusion.
- Checkpoint schema 3 / chunking version 4 / audio timeline version 3 do not migrate earlier journals, including schema-3 journals with older algorithm versions. Finish old jobs using their original binary, or explicitly restart with plain `--overwrite`. Old three-format `all` checkpoints require the previous binary, a new destination, or explicitly moving the old checkpoint aside.

Completed transcripts are not modified by an upgrade. JSON output remains schema 2; this does not imply checkpoint compatibility. Published 0.3.0 used JSON schema 1 and has no resumable partial runs.

## Batch Output Names
Directory input is non-recursive and sorted by filename bytes, independent of locale. Explicit input arguments keep their order. Files are processed sequentially with one loaded model.

`--naming numbered` writes `result_001.txt`, etc.; `result_files.json` records the source mapping. Changed input lists or numbered results without their mapping are rejected even with `--overwrite`. Choose a new `--prefix` or output directory; do not delete the mapping to reuse old results.

`--skip-existing` skips only complete sets of nonempty regular files, without checking their content/model. Incomplete sets require explicit overwrite or a verifiable resume checkpoint. Input files and their hardlink/symlink aliases are protected against output collisions. Output/mapping symlinks are rejected even with overwrite or skip; symlinks to directories remain supported.
