"""Local, private comparison corpus. Never uploads audio or transcripts."""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(command, log):
    print("Running:", log, flush=True)
    with log.open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("phase", choices=("prepare", "cpu", "gpu", "full"))
    parser.add_argument("--inputs", nargs=3, type=Path)
    parser.add_argument("--root", type=Path, default=ROOT / "benchmark-results/comparison")
    parser.add_argument(
        "--native-cpu",
        type=Path,
        default=ROOT / "dist/native-cpu-unpacked/bin/whisper-transcribator-native",
    )
    parser.add_argument(
        "--native-cuda",
        type=Path,
        default=ROOT / "dist/native-cuda-unpacked/bin/whisper-transcribator-native",
    )
    parser.add_argument("--python-image", default="whisper-transcribator:0.2.0-cuda")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument(
        "--allow-long-run",
        action="store_true",
        help="Explicitly opt in to two full-recording inference runs; agree a time budget first.",
    )
    args = parser.parse_args()
    if args.phase == "full" and not args.allow_long_run:
        parser.error(
            "Full recordings are optional. Use short clips, or explicitly --allow-long-run."
        )
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    manifest_path = root / "corpus.json"
    if args.phase == "prepare":
        if not args.inputs:
            parser.error("--inputs requires the three source files")
        if manifest_path.exists() or any(root.glob("clip_*.wav")):
            raise SystemExit(f"Refusing to replace an existing corpus: {root}")
        clips = []
        for index, (source_index, start) in enumerate(
            ((0, 600), (0, 9810), (1, 5400), (2, 2100)), 1
        ):
            target = root / f"clip_{index}.wav"
            source = args.inputs[source_index].resolve()
            subprocess.run(
                [
                    "ffmpeg",
                    "-nostdin",
                    "-v",
                    "error",
                    "-y",
                    "-ss",
                    str(start),
                    "-i",
                    str(source),
                    "-t",
                    "120",
                    "-map",
                    "0:a:0",
                    "-vn",
                    "-ar",
                    "16000",
                    "-ac",
                    "1",
                    "-c:a",
                    "pcm_s16le",
                    str(target),
                ],
                check=True,
            )
            clips.append(
                {
                    "file": target.name,
                    "source": str(source),
                    "start": start,
                    "duration": 120,
                    "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                }
            )
        manifest_path.write_text(
            json.dumps({"clips": clips, "full": str(args.inputs[0].resolve())}, indent=2)
        )
        return
    manifest = json.loads(manifest_path.read_text())
    inputs = [root / item["file"] for item in manifest["clips"]]
    for path, item in zip(inputs, manifest["clips"]):
        assert hashlib.sha256(path.read_bytes()).hexdigest() == item["sha256"]
    if args.phase == "full":
        inputs = [Path(manifest["full"])]
    device = "cpu" if args.phase == "cpu" else "cuda"
    model = "small" if device == "cpu" else "large-v3"
    for repeat in range(1 if args.phase == "full" else args.repeats):
        for backend in ("python", "native"):
            out = root / f"{args.phase}-{backend}-{repeat + 1}"
            if out.exists():
                raise SystemExit(f"Refusing to mix benchmark runs: {out}")
            out.mkdir()
            common = [
                "--output-dir",
                str(out),
                "--format",
                "all",
                "--language",
                "ru",
                "--device",
                device,
                "--cpu-threads",
                "4",
                "--beam-size",
                "5",
                "--no-vad",
            ]
            if backend == "native":
                binary = args.native_cpu if device == "cpu" else args.native_cuda
                command = [
                    sys.executable,
                    str(ROOT / "tools/measure.py"),
                    str(out / "metrics.json"),
                    str(binary),
                    *map(str, inputs),
                    "--model",
                    str(ROOT / f"models/whisper-cpp/ggml-{model}.bin"),
                    *common,
                ]
            else:
                command = ["docker", "run", "--rm", "--network", "none"]
                if device == "cuda":
                    command += [
                        "--gpus",
                        "all",
                        "--device",
                        "/dev/nvidia-uvm",
                        "--device",
                        "/dev/nvidia-uvm-tools",
                    ]
                for directory, readonly in (
                    (ROOT / "tools", True),
                    (ROOT / "models", True),
                    (root, False),
                ):
                    command += [
                        "--mount",
                        f"type=bind,src={directory},dst={directory}"
                        + (",readonly" if readonly else ""),
                    ]
                if args.phase == "full":
                    command += [
                        "--mount",
                        f"type=bind,src={inputs[0].parent},dst={inputs[0].parent},readonly",
                    ]
                command += [
                    "--entrypoint",
                    "python",
                    args.python_image,
                    str(ROOT / "tools/measure.py"),
                    str(out / "metrics.json"),
                    "python",
                    "-m",
                    "whisper_transcribator",
                    *map(str, inputs),
                    "--model",
                    model,
                    "--download-root",
                    str(ROOT / "models"),
                    "--local-files-only",
                    "--compute-type",
                    "float32" if device == "cpu" else "float16",
                    *common,
                ]
            run(command, out / "run.log")
            print((out / "metrics.json").read_text(), flush=True)


if __name__ == "__main__":
    main()
