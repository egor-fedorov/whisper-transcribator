from __future__ import annotations

import importlib
import json
import re
from pathlib import Path

from .jobs import resolve_media_path

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


def load_faster_whisper():
    try:
        import faster_whisper
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Python package 'faster-whisper' is not installed. "
            "Build the Docker image or run 'pip install .' locally."
        ) from exc

    return faster_whisper


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
                snapshot = Path(
                    utils.download_model(model_name, cache_dir=download_root, local_files_only=True)
                )
                status = "incomplete" if model_problems(snapshot) else "cached"
                target = snapshot
            except Exception:
                pass
        lines.append(f"{model_name:18} {status:10} {target or ''}")

    lines.append("")
    lines.append(
        "You can also pass a Hugging Face repo id or a local converted model directory to --model."
    )
    return "\n".join(lines)


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
            model_reference,
            cache_dir=download_root,
            local_files_only=local_files_only,
        )
    except Exception as exc:
        detail = f" Legacy cache: {', '.join(legacy_problems)}." if legacy_problems else ""
        raise SystemExit(f"Unable to prepare model '{model_reference}': {exc}.{detail}") from exc
    return validate_model_directory(Path(downloaded_path))
