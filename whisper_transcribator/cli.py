from __future__ import annotations

import argparse
import importlib
import json
import logging
import os
import re
import signal
import sys
import time
from contextlib import contextmanager
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path

from . import __version__
from .engine import build_transcriber, choose_compute_type, choose_device, transcribe_file
from .jobs import prepare_jobs
from .jobs import (
    resolve_media_path as resolve_media_path,
)
from .jobs import (
    resolve_output_path as resolve_output_path,
)
from .jobs import (
    same_file as same_file,
)
from .models import (
    default_download_root_label as default_download_root_label,
)
from .models import (
    ensure_model_is_available,
    list_models,
    load_faster_whisper,
    resolve_model_reference,
)
from .models import (
    looks_like_local_path as looks_like_local_path,
)
from .models import (
    model_problems as model_problems,
)
from .models import (
    sanitize_model_name as sanitize_model_name,
)
from .models import (
    validate_model_directory as validate_model_directory,
)
from .outputs import (
    atomic_write as atomic_write,
)
from .outputs import (
    dataclass_to_dict as dataclass_to_dict,
)
from .outputs import (
    format_timestamp as format_timestamp,
)
from .outputs import (
    render_json as render_json,
)
from .outputs import (
    render_srt as render_srt,
)
from .outputs import (
    render_text as render_text,
)
from .outputs import (
    segment_to_dict as segment_to_dict,
)
from .outputs import write_output
from .workers import run_workers

DEFAULT_MODEL = "small"
DEFAULT_OUTPUT_FORMAT = "text"
DEFAULT_LANGUAGE = "ru"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="whisper-transcribator",
        description="Transcribe media files into text via faster-whisper.",
        epilog="Commands: transcribe (default), models list, models download MODEL, doctor.",
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Input media files. Any source that PyAV can decode is supported.",
    )
    parser.add_argument("--version", action="version", version=__version__)
    parser.add_argument(
        "--input-dir", help="Read supported media in one directory, sorted by filename."
    )
    parser.add_argument(
        "--jobs", type=int, default=1, help="CPU worker processes. Each holds a model."
    )
    parser.add_argument("--naming", choices=("source", "numbered"), default="source")
    parser.add_argument(
        "--prefix", default="result", help="Prefix for numbered outputs and their mapping."
    )
    parser.add_argument(
        "--skip-existing",
        action="store_true",
        help="Skip complete nonempty output sets; does not verify freshness.",
    )
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument(
        "-o",
        "--output",
        help="Output path for a single input file. Extension can match the selected format.",
    )
    parser.add_argument(
        "--output-dir",
        help="Directory where result files should be written. Useful for batch mode.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "srt", "json", "all"),
        default=DEFAULT_OUTPUT_FORMAT,
        help="Output format.",
    )
    parser.add_argument(
        "--overwrite", action="store_true", help="Allow replacing existing transcripts."
    )
    parser.add_argument(
        "--language",
        default=DEFAULT_LANGUAGE,
        help='Language code. Use "auto" to enable language detection.',
    )
    parser.add_argument(
        "--model",
        default=os.environ.get("WHISPER_MODEL", DEFAULT_MODEL),
        help="Built-in faster-whisper model name, HF repo id, or path to a converted local model directory.",
    )
    parser.add_argument(
        "--download-root",
        default=os.environ.get("WHISPER_DOWNLOAD_ROOT"),
        help="Directory where named faster-whisper models should be downloaded/cached.",
    )
    parser.add_argument(
        "--local-files-only",
        action="store_true",
        help="Do not download models from Hugging Face. Only use local or already cached models.",
    )
    parser.add_argument(
        "--device",
        choices=("cpu", "cuda", "auto"),
        default="auto",
        help="Inference device.",
    )
    parser.add_argument(
        "--compute-type",
        default="auto",
        help="CTranslate2 compute type. Example: int8, float16, int8_float16, float32.",
    )
    parser.add_argument(
        "--cpu-threads",
        type=int,
        default=0,
        help="Number of CPU threads passed to faster-whisper. 0 keeps library defaults.",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=1,
        help="If > 1, use BatchedInferencePipeline with the given batch size.",
    )
    parser.add_argument(
        "--beam-size",
        type=int,
        default=5,
        help="Beam size for decoding.",
    )
    parser.add_argument(
        "--initial-prompt",
        help="Optional initial prompt to improve transcription of jargon or names.",
    )
    parser.add_argument(
        "--word-timestamps",
        action="store_true",
        help="Include word-level timestamps in JSON output and internal segment data.",
    )
    parser.add_argument(
        "--no-vad",
        action="store_true",
        help="Disable VAD filtering.",
    )
    parser.add_argument(
        "--vad-min-silence-ms",
        type=int,
        help="Override faster-whisper min_silence_duration_ms for VAD.",
    )
    parser.add_argument(
        "--list-models",
        action="store_true",
        help="Print supported faster-whisper model names and cache status.",
    )
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if not args.list_models and not args.inputs and not args.input_dir:
        raise SystemExit("At least one input file is required unless --list-models is used.")
    if args.input_dir and args.inputs:
        raise SystemExit("Use inputs or --input-dir, not both.")
    if args.jobs < 1:
        raise SystemExit("--jobs must be >= 1.")
    if args.naming == "numbered" and (not args.output_dir or args.output):
        raise SystemExit("--naming numbered requires --output-dir and does not accept --output.")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.prefix):
        raise SystemExit("--prefix accepts letters, digits, underscore and hyphen only.")

    if args.output and len(args.inputs) != 1:
        raise SystemExit("--output can only be used with a single input file.")

    if args.output and args.output_dir:
        raise SystemExit("Use either --output or --output-dir, not both.")

    if args.format == "all" and (args.output or not args.output_dir):
        raise SystemExit("--format all requires --output-dir and cannot be used with --output.")

    if args.batch_size < 1:
        raise SystemExit("--batch-size must be >= 1.")

    if args.batch_size > 1 and args.no_vad:
        raise SystemExit("--batch-size > 1 requires VAD; remove --no-vad or use --batch-size 1.")

    if args.beam_size < 1:
        raise SystemExit("--beam-size must be >= 1.")

    if args.cpu_threads < 0:
        raise SystemExit("--cpu-threads must be >= 0.")

    if args.vad_min_silence_ms is not None and args.vad_min_silence_ms < 0:
        raise SystemExit("--vad-min-silence-ms must be >= 0.")


def normalize_language(language: str) -> str | None:
    normalized = language.strip().lower()
    return None if normalized == "auto" else normalized


def transcribe_command(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        validate_args(args)
    except SystemExit as exc:
        parser.error(str(exc))

    if args.list_models:
        print(list_models(args.download_root))
        return 0

    jobs = prepare_jobs(args)
    if not jobs:
        print("No files to transcribe.", file=sys.stderr)
        return 0
    logging.basicConfig(level=logging.WARNING, format="%(levelname)s: %(message)s")
    logging.getLogger("faster_whisper").setLevel(logging.INFO)
    faster_whisper = load_faster_whisper()
    language = normalize_language(args.language)
    device = choose_device(args.device)
    if device == "cuda" and args.jobs != 1:
        parser.error("CUDA requires --jobs 1; use --batch-size for batching within a file.")
    compute_type = choose_compute_type(args.compute_type, device)
    model_reference = resolve_model_reference(args.model)
    print(
        f"Model: {args.model}; device: {device}; compute: {compute_type}; batch: {args.batch_size}",
        file=sys.stderr,
        flush=True,
    )
    prepared_model = ensure_model_is_available(
        faster_whisper=faster_whisper,
        model_reference=model_reference,
        download_root=args.download_root,
        local_files_only=args.local_files_only,
    )
    args.device = device
    args.compute_type = compute_type
    args.prepared_model = prepared_model
    if args.jobs > 1:
        return parallel_transcribe(jobs, args)
    transcriber = build_transcriber(
        faster_whisper=faster_whisper,
        model_reference=prepared_model,
        device=device,
        compute_type=compute_type,
        cpu_threads=args.cpu_threads,
        batch_size=args.batch_size,
        download_root=args.download_root,
        local_files_only=args.local_files_only,
    )

    failed = False
    for input_path, outputs in jobs:
        started = time.monotonic()
        print(
            f"Transcribing {input_path} -> {', '.join(map(str, outputs.values()))}",
            file=sys.stderr,
            flush=True,
        )
        try:
            segments, info = transcribe_file(
                transcriber=transcriber,
                input_path=input_path,
                language=language,
                beam_size=args.beam_size,
                initial_prompt=args.initial_prompt,
                word_timestamps=args.word_timestamps,
                vad_filter=not args.no_vad,
                vad_min_silence_ms=args.vad_min_silence_ms,
                batch_size=args.batch_size,
            )
            for output_format, output_path in outputs.items():
                write_output(
                    input_path=input_path,
                    output_path=output_path,
                    output_format=output_format,
                    model_name=args.model,
                    info=info,
                    segments=segments,
                    overwrite=args.overwrite,
                    run=run_metadata(args),
                )
        except Exception as exc:
            if args.continue_on_error:
                print(f"Failed to transcribe {input_path}: {exc}", file=sys.stderr)
                failed = True
                continue
            raise SystemExit(f"Failed to transcribe {input_path}: {exc}") from exc
        except SystemExit as exc:
            if not args.continue_on_error:
                raise
            print(f"Failed to transcribe {input_path}: {exc}", file=sys.stderr)
            failed = True
            continue
        print(
            f"Done: {input_path.name}; {len(segments)} segments; "
            f"elapsed {time.monotonic() - started:.1f}s",
            file=sys.stderr,
            flush=True,
        )

    return int(failed)


def run_metadata(args):
    versions = {"app": __version__}
    for name in ("faster-whisper", "ctranslate2", "av"):
        try:
            versions[name] = version(name)
        except PackageNotFoundError:
            versions[name] = None
    return {
        "versions": versions,
        "device": args.device,
        "compute_type": args.compute_type,
        "beam_size": args.beam_size,
        "batch_size": args.batch_size,
        "cpu_threads": args.cpu_threads,
        "vad": not args.no_vad,
        "language": args.language,
        "word_timestamps": args.word_timestamps,
        "vad_min_silence_ms": args.vad_min_silence_ms,
        "initial_prompt": args.initial_prompt,
        "model_revision": Path(args.prepared_model).name
        if Path(args.prepared_model).parent.name == "snapshots"
        else None,
    }


def worker(task):
    job, args = task
    global _worker_transcriber
    try:
        if "_worker_transcriber" not in globals():
            _worker_transcriber = build_transcriber(
                load_faster_whisper(),
                args.prepared_model,
                "cpu",
                args.compute_type,
                args.cpu_threads,
                args.batch_size,
                args.download_root,
                True,
            )
        segments, info = transcribe_file(
            _worker_transcriber,
            job.source,
            normalize_language(args.language),
            args.beam_size,
            args.initial_prompt,
            args.word_timestamps,
            not args.no_vad,
            args.vad_min_silence_ms,
            args.batch_size,
        )
        for fmt, target in job.outputs.items():
            write_output(
                job.source,
                target,
                fmt,
                args.model,
                info,
                segments,
                args.overwrite,
                run_metadata(args),
            )
        return str(job.source), None
    except (Exception, SystemExit) as exc:
        return str(job.source), str(exc)


def parallel_transcribe(jobs, args, call=worker):
    return run_workers(jobs, args, call)


class Interrupted(BaseException):
    def __init__(self, signum):
        self.signum = signum


@contextmanager
def interruption_handlers():
    previous = {}

    def interrupt(signum, _frame):
        raise Interrupted(signum)

    try:
        for signum in (signal.SIGINT, signal.SIGTERM):
            previous[signum] = signal.signal(signum, interrupt)
        yield
    finally:
        for signum, handler in previous.items():
            signal.signal(signum, handler)


def doctor(argv):
    parser = argparse.ArgumentParser(prog="whisper-transcribator doctor")
    parser.add_argument("--device", choices=("cpu", "cuda", "auto"), default="auto")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    report = {
        "version": __version__,
        "python": sys.version.split()[0],
        "dependencies": {},
        "errors": [],
    }
    for package, module in (
        ("faster-whisper", "faster_whisper"),
        ("ctranslate2", "ctranslate2"),
        ("av", "av"),
    ):
        try:
            importlib.import_module(module)
            report["dependencies"][package] = version(package)
        except Exception as exc:
            report["errors"].append(f"{package}: {exc}")
    try:
        device = choose_device(args.device)
        report["device"] = device
        runtime = importlib.import_module("ctranslate2")
        report["compute_types"] = sorted(runtime.get_supported_compute_types(device))
    except (Exception, SystemExit) as exc:
        report["errors"].append(str(exc))
    print(
        json.dumps(report, indent=2)
        if args.json
        else "\n".join(f"{k}: {v}" for k, v in report.items())
    )
    return int(bool(report["errors"]))


def models_command(argv):
    parser = argparse.ArgumentParser(prog="whisper-transcribator models")
    parser.add_argument("action", choices=("list", "download"))
    parser.add_argument("model", nargs="?")
    parser.add_argument("--download-root", default=os.environ.get("WHISPER_DOWNLOAD_ROOT"))
    parser.add_argument("--local-files-only", action="store_true")
    args = parser.parse_args(argv)
    if args.action == "list":
        if args.model:
            parser.error("models list does not accept a model")
        print(list_models(args.download_root))
    else:
        if not args.model:
            parser.error("models download requires MODEL")
        print(
            ensure_model_is_available(
                load_faster_whisper(),
                resolve_model_reference(args.model),
                args.download_root,
                args.local_files_only,
            )
        )
    return 0


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    try:
        with interruption_handlers():
            if argv and argv[0] == "doctor":
                return doctor(argv[1:])
            if argv and argv[0] == "models":
                return models_command(argv[1:])
            if argv and argv[0] == "transcribe":
                argv.pop(0)
            return transcribe_command(argv)
    except Interrupted as exc:
        print("Interrupted; unfinished files must be transcribed again.", file=sys.stderr)
        return 128 + exc.signum
    except KeyboardInterrupt:
        return 130
    except OSError as exc:
        print(f"Filesystem or runtime error: {exc}", file=sys.stderr)
        return 1
