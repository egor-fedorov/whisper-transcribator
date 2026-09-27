# Long Recordings And Resume
This describes version 0.4.0. Older 0.3.0 archives do not provide windowing or resume.

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
`--decode-errors tolerant` is the default. Errors returned by audio packet submission or frame retrieval, including `AVERROR_INVALIDDATA` and `AVERROR_PATCHWELCOME`, are recoverable except for allocation failure and cancellation. `EAGAIN` and EOF remain decoder flow-control signals, not recoverable errors. Demuxer/read errors and invalid decoded parameters remain fatal. `--decode-errors strict` stops on the first decoder error.

The decoder's rejected data is skipped, not repaired. The application warns once per file that the transcript may be incomplete, reports individual recovery events in verbose mode, and prints the total recovered error count at decoding EOF. FFmpeg may also emit its own diagnostics. Successful completion does not prove that every spoken word survived the recording damage.

In tolerant mode, `--decode-error-limit-seconds 30` stops recovery when decoder errors leave 30 seconds of represented input audio without a successful decoded frame. Packet counts, wall-clock time and PTS jumps are not used as a corruption budget. Packet durations are preferred, then codec frame duration, then the last successful frame's duration. If none is available, that packet does not advance the time budget. Successful packet submission alone does not reset it; a decoded frame does. There is no whole-file error-percentage limit.

Increase the limit for unusually damaged input, or set `--decode-error-limit-seconds 0` to disable the time limit. An independent guard always stops a stuck decoder after 1024 errors without either consuming another input packet or producing a frame. This guard also covers unknown packet durations and draining at EOF; cancellation remains available. Files that never yield usable audio still fail, and empty transcripts are not published.

Later valid timestamps follow the timeline policy above; gaps beyond the jitter tolerance are preserved, while missing timestamps cannot reconstruct lost audio duration. Recovery does not flush healthy decoder state or remove successfully decoded samples. Resume replays decoding and its error policy across the saved prefix, without repeating inference there. A deterministic failure will recur with the same options: `--resume` alone cannot bypass damage. The error explains how to relax the policy or repair the input with ffmpeg. Changed decoding options or a repaired/replaced source require a new output destination or `--overwrite` **without** `--resume`. Committed progress remains intact until that explicit restart; a failed job does not publish partial results.

Audio decoding policy version 1 and both decode options are part of checkpoint compatibility, independently of timeline version 3 and chunking version 4. Earlier checkpoints without these fields, including earlier drafts of the recovery policy, are preserved but rejected; finish them with the original binary or explicitly restart.

## Restart An Interrupted Job
```bash
./bin/whisper-transcribator lecture.mp4 --output-dir transcripts --format all \
  --model large-v3 --device cuda --chunk-seconds 120
# Repeat the same command with --resume after interruption:
./bin/whisper-transcribator lecture.mp4 --output-dir transcripts --format all \
  --model large-v3 --device cuda --chunk-seconds 120 --resume
```

Each committed window is saved under the output directory's private `.whisper-transcribator/`. The journal contains transcript fragments and source paths, not audio or weights. Keep it on persistent storage; a Docker bind mount of `/output` also preserves checkpoints. Allow disk space for journal records and staged outputs. Disk errors fail the job. On FAT32, exFAT and similar drives, see [filesystems without POSIX permissions](#filesystems-without-posix-permissions).

Resume verifies the input contents and paths, requested destinations, model/VAD SHA-256, selected audio stream, timestamp-gap and decode-error policies, inference and formatting settings, backend versions, and decoding/timeline/chunking/rendering algorithm versions. An incompatibility reports changed field paths. It never silently discards progress.

CPU thread counts, affinity and the spelling of model paths/catalog aliases do not determine compatibility. The actual model hashes do. Application version/Git revision are diagnostic metadata, not algorithm identifiers. `run` and top-level `model` describe the original job and remain frozen across resume so partially published JSON can be reconstructed byte-for-byte. Current execution settings appear in stderr; one resumed job may have used different CPU counts.

The completed prefix is decoded and discarded without inference. There is no approximate seek or full PCM cache. Source hashing and prefix decoding can take time; only the uncommitted window needs recognition again. Full model SHA-256 checks still run on each invocation, including `models list`; no persistent metadata-only verification cache is used.

## Recovery And Output Safety
- No checkpoint means `--resume` starts a new job. Existing committed progress requires explicit `--resume` or plain `--overwrite`.
- Failed attempts before the first committed window are safely restarted, even after replacing a broken input. Known private temporary files left during the first manifest/chunk write are also recoverable. Unknown files, unsafe links and corrupt records are never silently removed.
- `--overwrite` without `--resume` explicitly discards this job's journal. `--resume --overwrite` permits replacing outputs, but does not bypass journal compatibility checks.
- Without `--overwrite`, publication never replaces an existing name. It uses an atomic no-replace rename: Linux `renameat2` with `RENAME_NOREPLACE`, which ext4, XFS, Btrfs, tmpfs, FAT and exFAT support, or macOS `renamex_np` with `RENAME_EXCL` on APFS and HFS+. Where a filesystem rejects it, such as NFS, a hard link publishes the file instead. Only on filesystems with neither, it checks that the name is absent under a directory lock and then renames. That last fallback excludes other whisper-transcribator processes, but not another program creating the same name in between. `--overwrite` replaces outputs with an ordinary atomic rename.
- Outputs are atomically published individually; the multi-format set is not a transaction. After partial publication, matching completed outputs are retained and missing ones are regenerated without inference. Externally changed outputs require explicit overwrite permission.
- Successful publication removes checkpoint payloads. Small lock files remain deliberately: deleting a lock pathname while another process uses its inode can break mutual exclusion.
- Checkpoint schema 3 / chunking version 4 / audio timeline version 3 do not migrate earlier journals, including schema-3 journals with older algorithm versions. Finish old jobs using their original binary, or explicitly restart with plain `--overwrite`. Old three-format `all` checkpoints require the previous binary, a new destination, or explicitly moving the old checkpoint aside.

Completed transcripts are not modified by an upgrade. JSON output remains schema 2; this does not imply checkpoint compatibility. Published 0.3.0 used JSON schema 1 and has no resumable partial runs.

## Filesystems Without POSIX Permissions
FAT32, exFAT, NTFS without permission mapping and many FUSE or SMB mounts do not store file owners and modes: they report fixed values from mount options, so a directory created with mode 0700 may appear as 0777 or belong to another user. Checkpoints require an owner-only directory owned by you. When that check fails, a temporary file in the output directory shows whether the filesystem can store owners and modes. Only the attributes it cannot store are ignored, and only for checkpoint entries on the output directory's own filesystem, not a separately mounted directory inside it. Symlinks, hard links and unexpected file types are always rejected. On filesystems that store permissions, the strict checks are unchanged.

The job then continues with one warning per output directory: checkpoint privacy cannot be enforced there, and the mount options decide who can read saved fragments. Mount such drives for your user only (for example with `uid=` and `umask=077`, or `dmask`/`fmask`) if other local users should not read transcripts in progress.

Checkpoints deliberately stay next to the outputs instead of moving to a per-user state directory such as `~/.local/state`. Staged outputs must be on the destination filesystem to be published atomically. Resume keeps working when a drive moves to another machine or container. Docker users keep relying on a bind mount of `/output` to preserve progress. The published transcripts are exposed to the same mount options in either case. The model cache follows the same rule for partial downloads, whose final SHA-256 verification is unaffected.

## Batch Output Names
Directory input is non-recursive and sorted by filename bytes, independent of locale. Explicit input arguments keep their order. Files are processed sequentially with one loaded model.

`--naming numbered` writes `result_001.txt`, etc.; `result_files.json` records the source mapping. Changed input lists or numbered results without their mapping are rejected even with `--overwrite`. Choose a new `--prefix` or output directory; do not delete the mapping to reuse old results.

`--skip-existing` skips only complete sets of nonempty regular files, without checking their content/model. Incomplete sets require explicit overwrite or a verifiable resume checkpoint. Input files and their hardlink/symlink aliases are protected against output collisions. Output/mapping symlinks are rejected even with overwrite or skip; symlinks to directories remain supported.

Filesystems that ignore letter case, such as APFS and HFS+ in their default macOS format, FAT and exFAT, turn outputs like `Lecture.txt` and `lecture.txt` from `Lecture.mp4` and `lecture.m4a` into one file. Planning compares names as written, so the first job publishes normally; a later job whose output already is a file published earlier in the same run fails before its inference, even with `--overwrite`. Use `--naming numbered` for such inputs.
