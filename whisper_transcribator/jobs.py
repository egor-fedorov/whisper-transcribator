from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from .outputs import EXTENSIONS, atomic_write

MEDIA_EXTENSIONS = frozenset(
    [
        ".aac",
        ".aiff",
        ".avi",
        ".flac",
        ".m4a",
        ".m4b",
        ".m4v",
        ".mkv",
        ".mov",
        ".mp3",
        ".mp4",
        ".mpeg",
        ".mpg",
        ".oga",
        ".ogg",
        ".opus",
        ".wav",
        ".webm",
        ".wma",
        ".wmv",
    ]
)


@dataclass(frozen=True)
class Job:
    source: Path
    outputs: dict[str, Path]

    def __iter__(self):
        return iter((self.source, self.outputs))


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


def prepare_jobs(args: argparse.Namespace) -> list[Job]:
    inputs = [resolve_media_path(value) for value in args.inputs]
    if args.input_dir:
        directory = resolve_media_path(args.input_dir)
        if not directory.is_dir():
            raise SystemExit(f"Input directory not found: {directory}")
        inputs = [
            p.resolve()
            for p in sorted(
                (
                    p
                    for p in directory.iterdir()
                    if p.is_file() and p.suffix.lower() in MEDIA_EXTENSIONS
                ),
                key=lambda p: os.fsencode(p.name),
            )
        ]
    for path in inputs:
        if not path.is_file():
            raise SystemExit(f"Input file not found: {path}")

    formats = tuple(EXTENSIONS) if args.format == "all" else (args.format,)
    jobs = []
    destinations: list[Path] = []
    for index, source in enumerate(inputs, 1):
        outputs = {}
        for output_format in formats:
            target = resolve_output_path(source, args.output, args.output_dir, output_format)
            if args.naming == "numbered":
                target = target.with_name(f"{args.prefix}_{index:03d}{EXTENSIONS[output_format]}")
            if any(same_file(target, path) for path in inputs):
                raise SystemExit(f"Output would overwrite an input file: {target}")
            if any(same_file(target, path) for path in destinations):
                raise SystemExit(f"Multiple outputs resolve to the same file: {target}")
            destinations.append(target)
            outputs[output_format] = target
        jobs.append(Job(source, outputs))

    pending = []
    for job in jobs:
        if (
            args.skip_existing
            and not args.overwrite
            and all(p.is_file() and p.stat().st_size for p in job.outputs.values())
        ):
            print(f"Skipping complete result: {job.source.name}", file=sys.stderr)
            continue
        for target in job.outputs.values():
            if target.exists() and (not args.overwrite or not target.is_file()):
                raise SystemExit(
                    f"Output already exists: {target}. Use --overwrite to replace a transcript."
                )
        pending.append(job)

    # Check destination access before downloading a model or processing hours of audio.
    for directory in {path.parent for path in destinations}:
        try:
            directory.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryFile(dir=directory):
                pass
        except OSError as exc:
            raise SystemExit(f"Cannot write results in {directory}: {exc}") from exc
    if args.naming == "numbered" and jobs:
        mapping = resolve_media_path(args.output_dir) / f"{args.prefix}_files.json"
        if any(same_file(mapping, p) for p in inputs):
            raise SystemExit(f"Mapping would overwrite an input file: {mapping}")
        content = (
            json.dumps(
                [
                    {
                        "index": i,
                        "source": str(job.source),
                        "outputs": {k: p.name for k, p in job.outputs.items()},
                    }
                    for i, job in enumerate(jobs, 1)
                ],
                ensure_ascii=False,
                indent=2,
            )
            + "\n"
        )
        if mapping.exists():
            if mapping.read_text(encoding="utf-8") != content:
                raise SystemExit(
                    f"Input list changed; mapping preserved: {mapping}. Use a new --prefix."
                )
        else:
            if any(p.exists() for p in destinations):
                raise SystemExit("Numbered results exist without a mapping. Use a new --prefix.")
            try:
                atomic_write(mapping, content)
            except FileExistsError:
                if mapping.read_text(encoding="utf-8") != content:
                    raise SystemExit(f"Conflicting mapping: {mapping}") from None
    return pending
