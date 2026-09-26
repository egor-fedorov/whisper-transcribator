from __future__ import annotations

import argparse
import ctypes
import importlib
import json
import logging
import os
import re
import sys
import tempfile
import time
from dataclasses import asdict, is_dataclass
from pathlib import Path


KNOWN_MODEL_NAMES = (
    "tiny.en",
    "tiny",
    "base.en",
    "base",
    "small.en",
    "small",
    "medium.en",
    "medium",
    "large-v1",
    "large-v2",
    "large-v3",
    "large",
    "distil-small.en",
    "distil-medium.en",
    "distil-large-v2",
    "distil-large-v3",
    "distil-large-v3.5",
    "large-v3-turbo",
    "turbo",
)
DEFAULT_MODEL = "small"
DEFAULT_OUTPUT_FORMAT = "text"
DEFAULT_LANGUAGE = "ru"
DEFAULT_DOWNLOAD_ROOT = os.environ.get("WHISPER_DOWNLOAD_ROOT")
DEFAULT_MODEL_NAME = os.environ.get("WHISPER_MODEL", DEFAULT_MODEL)
EXTENSIONS = {"text": ".txt", "srt": ".srt", "json": ".json"}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="whisper-transcribator",
        description="Transcribe media files into text via faster-whisper.",
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Input media files. Any source that PyAV can decode is supported.",
    )
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
        default=DEFAULT_MODEL_NAME,
        help="Built-in faster-whisper model name, HF repo id, or path to a converted local model directory.",
    )
    parser.add_argument(
        "--download-root",
        default=DEFAULT_DOWNLOAD_ROOT,
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
    if not args.list_models and not args.inputs:
        raise SystemExit("At least one input file is required unless --list-models is used.")

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


def load_faster_whisper():
    try:
        import faster_whisper
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Python package 'faster-whisper' is not installed. "
            "Build the Docker image or run 'pip install .' locally."
        ) from exc

    return faster_whisper


def normalize_language(language: str) -> str | None:
    normalized = language.strip().lower()
    return None if normalized == "auto" else normalized


def resolve_media_path(path_str: str) -> Path:
    return Path(path_str).expanduser().resolve()


def resolve_output_path(
    input_path: Path,
    output: str | None,
    output_dir: str | None,
    output_format: str,
) -> Path:
    extension = EXTENSIONS[output_format]
    if output:
        return resolve_media_path(output)

    filename = f"{input_path.stem}{extension}"
    if output_dir:
        return resolve_media_path(output_dir) / filename

    return input_path.with_suffix(extension)


def same_file(first: Path, second: Path) -> bool:
    return first.resolve() == second.resolve() or (
        first.exists() and second.exists() and first.samefile(second)
    )


def prepare_jobs(args: argparse.Namespace) -> list[tuple[Path, dict[str, Path]]]:
    inputs = [resolve_media_path(value) for value in args.inputs]
    for path in inputs:
        if not path.is_file():
            raise SystemExit(f"Input file not found: {path}")

    formats = tuple(EXTENSIONS) if args.format == "all" else (args.format,)
    jobs = []
    destinations: list[Path] = []
    for source in inputs:
        outputs = {}
        for output_format in formats:
            target = resolve_output_path(source, args.output, args.output_dir, output_format)
            if any(same_file(target, path) for path in inputs):
                raise SystemExit(f"Output would overwrite an input file: {target}")
            if any(same_file(target, path) for path in destinations):
                raise SystemExit(f"Multiple outputs resolve to the same file: {target}")
            if target.exists() and (not args.overwrite or not target.is_file()):
                raise SystemExit(f"Output already exists: {target}. Use --overwrite to replace a transcript.")
            destinations.append(target)
            outputs[output_format] = target
        jobs.append((source, outputs))

    # Check destination access before downloading a model or processing hours of audio.
    for directory in {path.parent for path in destinations}:
        try:
            directory.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryFile(dir=directory):
                pass
        except OSError as exc:
            raise SystemExit(f"Cannot write results in {directory}: {exc}") from exc
    return jobs


def looks_like_local_path(model_value: str) -> bool:
    return model_value.startswith(("/", "./", "../", "~"))


def resolve_model_reference(model_value: str) -> str:
    candidate = Path(model_value).expanduser()

    if candidate.exists() or looks_like_local_path(model_value):
        resolved = resolve_media_path(model_value)
        if not resolved.is_dir():
            raise SystemExit(f"Model directory not found: {resolved}")
        return str(resolved)

    return model_value.strip()


def sanitize_model_name(model_name: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "-", model_name.strip())


def default_download_root_label(download_root: str | None) -> str:
    return download_root if download_root else "standard Hugging Face cache"


def model_problems(directory: Path) -> list[str]:
    problems = []
    for name in ("model.bin", "config.json", "tokenizer.json"):
        path = directory / name
        if not path.is_file() or path.stat().st_size == 0:
            problems.append(f"missing/empty {name}")
        elif name.endswith(".json"):
            try:
                json.loads(path.read_text(encoding="utf-8"))
            except (ValueError, OSError) as exc:
                problems.append(f"invalid {name}: {exc}")
    return problems


def validate_model_directory(directory: Path) -> str:
    problems = model_problems(directory)
    if problems:
        raise SystemExit(f"Incomplete model directory {directory}: {', '.join(problems)}")
    return str(directory)


def list_models(download_root: str | None) -> str:
    lines = [f"Model download root: {default_download_root_label(download_root)}", ""]
    try:
        utils = importlib.import_module("faster_whisper.utils")
    except ModuleNotFoundError:
        utils = None
    for model_name in KNOWN_MODEL_NAMES:
        status = "on-demand"
        target = None
        if download_root:
            target = resolve_media_path(download_root) / sanitize_model_name(model_name)
            if target.is_dir():
                status = "incomplete" if model_problems(target) else "cached"
        if status != "cached" and utils is not None:
            try:
                snapshot = Path(utils.download_model(
                    model_name, cache_dir=download_root, local_files_only=True
                ))
                status = "incomplete" if model_problems(snapshot) else "cached"
                target = snapshot
            except Exception:
                pass
        lines.append(f"{model_name:18} {status:10} {target or ''}")

    lines.append("")
    lines.append("You can also pass a Hugging Face repo id or a local converted model directory to --model.")
    return "\n".join(lines)


def choose_device(requested: str) -> str:
    if requested == "cpu":
        return "cpu"
    try:
        ctranslate2 = importlib.import_module("ctranslate2")
        available = ctranslate2.get_cuda_device_count() > 0
        if available:
            ctranslate2.get_supported_compute_types("cuda")
            for library in ("libcublas.so.12", "libcudnn.so.9"):
                ctypes.CDLL(library)
            return "cuda"
        reason = "no CUDA devices visible to CTranslate2"
    except (ImportError, RuntimeError, OSError) as exc:
        reason = str(exc)
    if requested == "cuda":
        raise SystemExit(f"CUDA unavailable: {reason}. Use the CUDA image with --gpus all.")
    print(f"Using CPU: {reason}", file=sys.stderr)
    return "cpu"


def choose_compute_type(requested: str, device: str) -> str:
    if requested != "auto":
        return requested

    return "float16" if device == "cuda" else "int8"


def format_timestamp(
    seconds: float,
    always_include_hours: bool = False,
    decimal_marker: str = ".",
) -> str:
    assert seconds >= 0, "non-negative timestamp expected"

    milliseconds = round(seconds * 1000.0)
    hours = milliseconds // 3_600_000
    milliseconds -= hours * 3_600_000
    minutes = milliseconds // 60_000
    milliseconds -= minutes * 60_000
    secs = milliseconds // 1_000
    milliseconds -= secs * 1_000
    hours_marker = f"{hours:02d}:" if always_include_hours or hours > 0 else ""
    return f"{hours_marker}{minutes:02d}:{secs:02d}{decimal_marker}{milliseconds:03d}"


def render_text(segments: list[object]) -> str:
    text = " ".join(segment.text.strip() for segment in segments if segment.text.strip())
    return text.strip() + "\n"


def render_srt(segments: list[object]) -> str:
    blocks: list[str] = []
    for segment in segments:
        text = segment.text.strip()
        if not text:
            continue
        start = format_timestamp(segment.start, always_include_hours=True, decimal_marker=",")
        end = format_timestamp(segment.end, always_include_hours=True, decimal_marker=",")
        blocks.append(f"{len(blocks) + 1}\n{start} --> {end}\n{text}")
    return "\n\n".join(blocks).strip() + "\n"


def dataclass_to_dict(value: object) -> dict | None:
    if value is None:
        return None
    if is_dataclass(value):
        return asdict(value)
    return dict(value)


def segment_to_dict(segment: object) -> dict:
    data = {
        "id": getattr(segment, "id", None),
        "start": segment.start,
        "end": segment.end,
        "text": segment.text.strip(),
        "avg_logprob": getattr(segment, "avg_logprob", None),
        "compression_ratio": getattr(segment, "compression_ratio", None),
        "no_speech_prob": getattr(segment, "no_speech_prob", None),
    }
    words = getattr(segment, "words", None)
    if words is not None:
        data["words"] = [dataclass_to_dict(word) for word in words]
    return data


def render_json(source: Path, model_name: str, info: object, segments: list[object]) -> str:
    payload = {
        "source": str(source),
        "model": model_name,
        "language": getattr(info, "language", None),
        "language_probability": getattr(info, "language_probability", None),
        "duration": getattr(info, "duration", None),
        "duration_after_vad": getattr(info, "duration_after_vad", None),
        "text": " ".join(segment.text.strip() for segment in segments if segment.text.strip()).strip(),
        "segments": [segment_to_dict(segment) for segment in segments],
    }
    return json.dumps(payload, ensure_ascii=False, indent=2) + "\n"


def write_output(
    input_path: Path,
    output_path: Path,
    output_format: str,
    model_name: str,
    info: object,
    segments: list[object],
    overwrite: bool = False,
) -> None:
    if not any(segment.text.strip() for segment in segments):
        raise SystemExit(f"No transcript produced for: {input_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    if output_format == "text":
        content = render_text(segments)
    elif output_format == "srt":
        content = render_srt(segments)
    elif output_format == "json":
        content = render_json(input_path, model_name, info, segments)
    else:
        raise SystemExit(f"Unsupported output format: {output_format}")

    atomic_write(output_path, content, overwrite)


def atomic_write(output_path: Path, content: str, overwrite: bool = False) -> None:
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=output_path.parent,
            prefix=".whisper-", suffix=".tmp", delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), 0o644)
        if overwrite:
            os.replace(temporary, output_path)
        else:
            # Publish without a race that could clobber another writer's result.
            os.link(temporary, output_path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def ensure_model_is_available(
    faster_whisper,
    model_reference: str,
    download_root: str | None,
    local_files_only: bool,
) -> str:
    if Path(model_reference).exists():
        return validate_model_directory(Path(model_reference))

    legacy_problems = []
    if download_root:
        target_dir = resolve_media_path(download_root) / sanitize_model_name(model_reference)
        if target_dir.is_dir():
            legacy_problems = model_problems(target_dir)
            if not legacy_problems:
                return str(target_dir)

    utils = importlib.import_module("faster_whisper.utils")
    try:
        downloaded_path = utils.download_model(
            model_reference, cache_dir=download_root, local_files_only=local_files_only,
        )
    except Exception as exc:
        detail = f" Legacy cache: {', '.join(legacy_problems)}." if legacy_problems else ""
        raise SystemExit(f"Unable to prepare model '{model_reference}': {exc}.{detail}") from exc
    return validate_model_directory(Path(downloaded_path))


def build_transcriber(
    faster_whisper,
    model_reference: str,
    device: str,
    compute_type: str,
    cpu_threads: int,
    batch_size: int,
    download_root: str | None,
    local_files_only: bool,
):
    kwargs = {
        "device": device,
        "compute_type": compute_type,
        "cpu_threads": cpu_threads,
        "download_root": download_root,
        "local_files_only": local_files_only,
    }

    try:
        model = faster_whisper.WhisperModel(model_reference, **kwargs)
    except Exception as exc:
        raise SystemExit(f"Unable to initialize faster-whisper model '{model_reference}': {exc}") from exc

    if batch_size > 1:
        return faster_whisper.BatchedInferencePipeline(model=model)

    return model


def transcribe_file(
    transcriber,
    input_path: Path,
    language: str | None,
    beam_size: int,
    initial_prompt: str | None,
    word_timestamps: bool,
    vad_filter: bool,
    vad_min_silence_ms: int | None,
    batch_size: int,
):
    transcribe_kwargs = {
        "language": language,
        "beam_size": beam_size,
        "initial_prompt": initial_prompt,
        "word_timestamps": word_timestamps,
        "vad_filter": vad_filter,
    }

    if vad_min_silence_ms is not None:
        transcribe_kwargs["vad_parameters"] = {"min_silence_duration_ms": vad_min_silence_ms}

    if batch_size > 1:
        transcribe_kwargs["batch_size"] = batch_size

    started = time.monotonic()
    last_report = started - 30
    segments, info = transcriber.transcribe(str(input_path), **transcribe_kwargs)
    result = []
    for segment in segments:
        result.append(segment)
        now = time.monotonic()
        if now - last_report >= 30:
            print(
                f"Progress {input_path.name}: {format_timestamp(segment.end)} / "
                f"{format_timestamp(info.duration)}; elapsed {now - started:.0f}s",
                file=sys.stderr, flush=True,
            )
            last_report = now
    return result, info


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    validate_args(args)

    if args.list_models:
        print(list_models(args.download_root))
        return 0

    jobs = prepare_jobs(args)
    logging.basicConfig(level=logging.WARNING, format="%(levelname)s: %(message)s")
    logging.getLogger("faster_whisper").setLevel(logging.INFO)
    faster_whisper = load_faster_whisper()
    language = normalize_language(args.language)
    device = choose_device(args.device)
    compute_type = choose_compute_type(args.compute_type, device)
    model_reference = resolve_model_reference(args.model)
    print(f"Model: {args.model}; device: {device}; compute: {compute_type}; batch: {args.batch_size}",
          file=sys.stderr, flush=True)
    prepared_model = ensure_model_is_available(
        faster_whisper=faster_whisper,
        model_reference=model_reference,
        download_root=args.download_root,
        local_files_only=args.local_files_only,
    )
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

    for input_path, outputs in jobs:
        started = time.monotonic()
        print(f"Transcribing {input_path} -> {', '.join(map(str, outputs.values()))}",
              file=sys.stderr, flush=True)
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
                    input_path=input_path, output_path=output_path,
                    output_format=output_format, model_name=args.model,
                    info=info, segments=segments, overwrite=args.overwrite,
                )
        except Exception as exc:
            raise SystemExit(f"Failed to transcribe {input_path}: {exc}") from exc
        except KeyboardInterrupt:
            print(f"Interrupted: {input_path}; this file may need to be transcribed again.",
                  file=sys.stderr)
            return 130
        print(f"Done: {input_path.name}; {len(segments)} segments; "
              f"elapsed {time.monotonic() - started:.1f}s", file=sys.stderr, flush=True)

    return 0
