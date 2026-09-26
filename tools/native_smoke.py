"""Exercise a native build with pinned public audio and a local tiny model."""

import argparse
import json
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("sample", type=Path)
    parser.add_argument("model", type=Path)
    parser.add_argument("--device", default="cpu", choices=("cpu", "cuda"))
    args = parser.parse_args()
    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        # Keep Unicode, spaces and parentheses in the real decoder/output path.
        source = root / "лекция (sample).wav"
        source.write_bytes(args.sample.read_bytes())
        command = [
            str(args.binary.resolve()),
            str(source),
            "--model",
            str(args.model.resolve()),
            "--output-dir",
            str(root),
            "--language",
            "en",
            "--device",
            args.device,
            "--cpu-threads",
            "2",
            "--no-vad",
        ]
        subprocess.run(command, check=True)
        transcript = source.with_suffix(".json")
        data = json.loads(transcript.read_text())
        assert data["schema_version"] == 1 and "americans" in data["text"].lower()
        assert source.with_suffix(".txt").read_text().strip() == data["text"]
        assert source.with_suffix(".srt").stat().st_size > 0
        assert all(0 <= s["start"] <= s["end"] <= data["duration"] + 1 for s in data["segments"])
        original = transcript.read_bytes()
        assert subprocess.run(command, capture_output=True).returncode == 2
        assert transcript.read_bytes() == original
        for suffix in (".txt", ".srt", ".json"):
            source.with_suffix(suffix).unlink()
        source.write_bytes(b"not audio")
        assert subprocess.run(command, capture_output=True).returncode == 1
        assert not transcript.exists()
        assert not list(root.glob(".whisper-native-*"))
        assert (
            subprocess.run(
                [str(args.binary.resolve()), "--batch-size", "2"], capture_output=True
            ).returncode
            == 2
        )
    print("Native real-inference and failure smoke tests passed.")


if __name__ == "__main__":
    main()
