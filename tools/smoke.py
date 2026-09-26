"""Fetch pinned public fixtures, then exercise an installed package offline."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

SAMPLE_URL = "https://raw.githubusercontent.com/ggml-org/whisper.cpp/927cfce34f31707e17f2bff35c349632fb9e2c3a/samples/jfk.wav"
SAMPLE_SHA256 = "59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e"
MODEL_REVISION = "d90ca5fe260221311c53c58e660288d3deb8d356"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(".cache/smoke"))
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    args = parser.parse_args()
    root = args.root.resolve()
    sample = root / "jfk.wav"
    if args.prepare:
        from huggingface_hub import snapshot_download

        root.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(SAMPLE_URL, sample)
        snapshot_download(
            "Systran/faster-whisper-tiny",
            revision=MODEL_REVISION,
            cache_dir=str(root / "models"),
            allow_patterns=["*.json", "model.bin", "vocabulary.*"],
        )
    assert hashlib.sha256(sample.read_bytes()).hexdigest() == SAMPLE_SHA256
    model = root / "models/models--Systran--faster-whisper-tiny/snapshots" / MODEL_REVISION
    if args.prepare:
        return
    env = dict(os.environ, HF_HUB_OFFLINE="1")
    output = root / ("result-" + args.device)
    subprocess.run(
        [
            sys.executable,
            "-m",
            "whisper_transcribator",
            "transcribe",
            str(sample),
            "--model",
            str(model),
            "--local-files-only",
            "--language",
            "en",
            "--device",
            args.device,
            "--cpu-threads",
            "2",
            "--format",
            "all",
            "--output-dir",
            str(output),
            "--overwrite",
        ],
        check=True,
        env=env,
    )
    data = json.loads((output / "jfk.json").read_text())
    assert data["schema_version"] == 1
    assert "americans" in data["text"].lower()
    assert data["text"] == (output / "jfk.txt").read_text().strip()
    assert data["segments"] and (output / "jfk.srt").stat().st_size
    assert all(0 <= s["start"] <= s["end"] <= data["duration"] + 1 for s in data["segments"])
    print("Offline real-inference smoke test passed.")


if __name__ == "__main__":
    main()
