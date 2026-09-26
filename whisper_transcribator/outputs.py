from __future__ import annotations

import json
import os
import stat
import tempfile
from dataclasses import asdict, is_dataclass
from pathlib import Path

from .types import Segment, TranscriptInfo

EXTENSIONS = {"text": ".txt", "srt": ".srt", "json": ".json"}


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


def render_text(segments: list[Segment]) -> str:
    text = " ".join(segment.text.strip() for segment in segments if segment.text.strip())
    return text.strip() + "\n"


def render_srt(segments: list[Segment]) -> str:
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


def segment_to_dict(segment: Segment) -> dict:
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


def render_json(
    source: Path,
    model_name: str,
    info: TranscriptInfo,
    segments: list[Segment],
    run: dict | None = None,
) -> str:
    payload = {
        "schema_version": 1,
        "run": run or {},
        "source": str(source),
        "model": model_name,
        "language": getattr(info, "language", None),
        "language_probability": getattr(info, "language_probability", None),
        "duration": getattr(info, "duration", None),
        "duration_after_vad": getattr(info, "duration_after_vad", None),
        "text": " ".join(
            segment.text.strip() for segment in segments if segment.text.strip()
        ).strip(),
        "segments": [segment_to_dict(segment) for segment in segments],
    }
    return json.dumps(payload, ensure_ascii=False, indent=2) + "\n"


def write_output(
    input_path: Path,
    output_path: Path,
    output_format: str,
    model_name: str,
    info: TranscriptInfo,
    segments: list[Segment],
    overwrite: bool = False,
    run: dict | None = None,
) -> None:
    if not any(segment.text.strip() for segment in segments):
        raise SystemExit(f"No transcript produced for: {input_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    if output_format == "text":
        content = render_text(segments)
    elif output_format == "srt":
        content = render_srt(segments)
    elif output_format == "json":
        content = render_json(input_path, model_name, info, segments, run)
    else:
        raise SystemExit(f"Unsupported output format: {output_format}")

    atomic_write(output_path, content, overwrite)


def atomic_write(output_path: Path, content: str, overwrite: bool = False) -> None:
    temporary = None
    try:
        if overwrite and output_path.exists():
            mode = stat.S_IMODE(output_path.stat().st_mode) & 0o777
        else:
            mask = os.umask(0)
            os.umask(mask)
            mode = 0o666 & ~mask
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=output_path.parent,
            prefix=".whisper-",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), mode)
        if overwrite:
            os.replace(temporary, output_path)
        else:
            # Publish without a race that could clobber another writer's result.
            os.link(temporary, output_path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
