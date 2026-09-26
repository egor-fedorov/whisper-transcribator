from __future__ import annotations

import ctypes
import importlib
import sys
import time
from pathlib import Path

from .outputs import format_timestamp
from .types import Segment, TranscriptInfo


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
        raise SystemExit(
            f"Unable to initialize faster-whisper model '{model_reference}': {exc}"
        ) from exc

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
        result.append(
            Segment(**{name: getattr(segment, name, None) for name in Segment.__dataclass_fields__})
        )
        now = time.monotonic()
        if now - last_report >= 30:
            print(
                f"Progress {input_path.name}: {format_timestamp(segment.end)} / "
                f"{format_timestamp(info.duration)}; elapsed {now - started:.0f}s",
                file=sys.stderr,
                flush=True,
            )
            last_report = now
    return result, TranscriptInfo(
        **{name: getattr(info, name, None) for name in TranscriptInfo.__dataclass_fields__}
    )
