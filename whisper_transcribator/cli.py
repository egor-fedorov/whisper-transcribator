from __future__ import annotations

import argparse
import importlib
import json
import os
import re
import sys
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
        choices=("text", "srt", "json"),
        default=DEFAULT_OUTPUT_FORMAT,
        help="Output format.",
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

    if args.batch_size < 1:
        raise SystemExit("--batch-size must be >= 1.")

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
    path = Path(path_str).expanduser()
    if not path.is_absolute():
        path = (Path.cwd() / path).resolve()
    return path


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


def looks_like_local_path(model_value: str) -> bool:
    return model_value.startswith(("/", "./", "../", "~"))


def resolve_model_reference(model_value: str) -> str:
    candidate = Path(model_value).expanduser()

    if candidate.exists() or looks_like_local_path(model_value):
        resolved = resolve_media_path(model_value)
        if not resolved.exists():
            raise SystemExit(f"Model path not found: {resolved}")
        return str(resolved)

    return model_value.strip()


def sanitize_model_name(model_name: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "-", model_name.strip())


def default_download_root_label(download_root: str | None) -> str:
    return download_root if download_root else "standard Hugging Face cache"


def list_models(download_root: str | None) -> str:
    lines = [f"Model download root: {default_download_root_label(download_root)}", ""]
    for model_name in KNOWN_MODEL_NAMES:
        if download_root:
            target = resolve_media_path(download_root) / sanitize_model_name(model_name)
            status = "cached" if target.is_dir() else "on-demand"
            lines.append(f"{model_name:18} {status:9} {target}")
        else:
            lines.append(f"{model_name}")

    lines.append("")
    lines.append("You can also pass a Hugging Face repo id or a local converted model directory to --model.")
    return "\n".join(lines)


def choose_device(requested: str) -> str:
    if requested != "auto":
        return requested

    if Path("/dev/nvidia0").exists() or shutil_which("nvidia-smi"):
        return "cuda"

    return "cpu"


def choose_compute_type(requested: str, device: str) -> str:
    if requested != "auto":
        return requested

    return "float16" if device == "cuda" else "int8"


def shutil_which(binary: str) -> str | None:
    from shutil import which

    return which(binary)


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
    for index, segment in enumerate(segments, start=1):
        text = segment.text.strip()
        if not text:
            continue
        start = format_timestamp(segment.start, always_include_hours=True, decimal_marker=",")
        end = format_timestamp(segment.end, always_include_hours=True, decimal_marker=",")
        blocks.append(f"{index}\n{start} --> {end}\n{text}")
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
) -> None:
    if not segments:
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

    output_path.write_text(content, encoding="utf-8")


def ensure_model_is_available(
    faster_whisper,
    model_reference: str,
    download_root: str | None,
    local_files_only: bool,
) -> str:
    if Path(model_reference).exists():
        return model_reference

    if download_root:
        target_dir = resolve_media_path(download_root) / sanitize_model_name(model_reference)
        if target_dir.is_dir():
            return str(target_dir)

        utils = importlib.import_module("faster_whisper.utils")
        try:
            downloaded_path = utils.download_model(
                model_reference,
                output_dir=str(target_dir),
                local_files_only=local_files_only,
            )
        except Exception as exc:
            raise SystemExit(
                f"Unable to prepare model '{model_reference}' in {target_dir}: {exc}"
            ) from exc
        return str(downloaded_path)

    return model_reference


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
    }
    if not Path(model_reference).exists():
        kwargs["download_root"] = download_root
        kwargs["local_files_only"] = local_files_only

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

    segments, info = transcriber.transcribe(str(input_path), **transcribe_kwargs)
    return list(segments), info


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    validate_args(args)

    if args.list_models:
        print(list_models(args.download_root))
        return 0

    faster_whisper = load_faster_whisper()
    language = normalize_language(args.language)
    device = choose_device(args.device)
    compute_type = choose_compute_type(args.compute_type, device)
    model_reference = resolve_model_reference(args.model)
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

    for raw_input in args.inputs:
        input_path = resolve_media_path(raw_input)
        if not input_path.is_file():
            raise SystemExit(f"Input file not found: {input_path}")

        output_path = resolve_output_path(
            input_path=input_path,
            output=args.output,
            output_dir=args.output_dir,
            output_format=args.format,
        )

        print(f"Transcribing {input_path} -> {output_path}", file=sys.stderr)
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
        write_output(
            input_path=input_path,
            output_path=output_path,
            output_format=args.format,
            model_name=args.model,
            info=info,
            segments=segments,
        )

    return 0
