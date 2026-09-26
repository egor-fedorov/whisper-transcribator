"""Prepare short blind-review fixtures and score only human-verified references."""

import argparse
import hashlib
import json
import re
import unicodedata
import wave
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize(text):
    text = unicodedata.normalize("NFKC", text).casefold().replace("ё", "е")
    return " ".join(re.findall(r"[^\W_]+", text, flags=re.UNICODE))


def distance(reference, hypothesis):
    previous = list(range(len(hypothesis) + 1))
    for i, left in enumerate(reference, 1):
        row = [i]
        for j, right in enumerate(hypothesis, 1):
            row.append(min(row[-1] + 1, previous[j] + 1, previous[j - 1] + (left != right)))
        previous = row
    return previous[-1]


def prepare(root, clips, seconds):
    if not 10 <= seconds <= 60:
        raise ValueError("Use a short 10..60 second review excerpt")
    root.mkdir(parents=True, exist_ok=False)
    entries = []
    for index, path in enumerate(clips, 1):
        name = f"clip_{index:03d}"
        target = root / f"{name}.wav"
        with wave.open(str(path), "rb") as source:
            if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (
                1,
                2,
                16000,
            ):
                raise ValueError(f"Expected mono 16-bit 16 kHz PCM WAV: {path}")
            data = source.readframes(seconds * 16000)
        if len(data) != seconds * 16000 * 2:
            raise ValueError(f"Source is shorter than the requested excerpt: {path}")
        with wave.open(str(target), "wb") as output:
            output.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
            output.writeframes(data)
        (root / f"{name}.reference.txt").write_text("", encoding="utf-8")
        entries.append(
            {
                "id": name,
                "audio_sha256": digest(target),
                "seconds": seconds,
                "source_clip": str(path.resolve()),
                "source_offset_seconds": 0,
                "human_reviewed": False,
            }
        )
    (root / "manifest.json").write_text(
        json.dumps({"schema_version": 1, "clips": entries}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def score(root, hypotheses):
    if not hypotheses or len({path.resolve() for path in hypotheses}) != len(hypotheses):
        raise ValueError("Provide distinct hypothesis directories")
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or not manifest.get("clips"):
        raise ValueError("Invalid quality manifest")
    totals = {
        str(path): {
            "word_errors": 0,
            "reference_words": 0,
            "character_errors": 0,
            "reference_characters": 0,
        }
        for path in hypotheses
    }
    rows = []
    seen = set()
    for clip in manifest["clips"]:
        name = clip["id"]
        if not re.fullmatch(r"clip_[0-9]+", name) or name in seen:
            raise ValueError("Invalid or duplicate clip ID")
        seen.add(name)
        if clip.get("human_reviewed") is not True:
            raise ValueError(f"{name}: no human-verified reference; quality verdict is blocked")
        if digest(root / f"{name}.wav") != clip["audio_sha256"]:
            raise ValueError(f"{name}: audio checksum mismatch")
        ref_path = root / f"{name}.reference.txt"
        reference = normalize(ref_path.read_text(encoding="utf-8"))
        if not reference:
            raise ValueError(f"{name}: empty reference")
        for path in hypotheses:
            hyp_path = path / f"{name}.txt"
            hypothesis = normalize(hyp_path.read_text(encoding="utf-8"))
            counts = {
                "word_errors": distance(reference.split(), hypothesis.split()),
                "reference_words": len(reference.split()),
                "character_errors": distance(reference, hypothesis),
                "reference_characters": len(reference),
            }
            for key, value in counts.items():
                totals[str(path)][key] += value
            rows.append(
                {
                    "clip": name,
                    "hypothesis": str(path),
                    **counts,
                    "reference_sha256": digest(ref_path),
                    "hypothesis_sha256": digest(hyp_path),
                }
            )
    for counts in totals.values():
        counts["wer"] = counts["word_errors"] / counts["reference_words"]
        counts["cer"] = counts["character_errors"] / counts["reference_characters"]
    return {
        "normalization": "NFKC, casefold, yo->e, word tokens; CER includes single spaces",
        "clips": rows,
        "totals": totals,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("--root", type=Path, required=True)
    prep.add_argument("--seconds", type=int, default=30)
    prep.add_argument("clips", nargs="+", type=Path)
    evaluate = commands.add_parser("score")
    evaluate.add_argument("--root", type=Path, required=True)
    evaluate.add_argument("--hypotheses", nargs="+", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.root, args.clips, args.seconds)
        else:
            print(json.dumps(score(args.root, args.hypotheses), ensure_ascii=False, indent=2))
    except (OSError, ValueError, KeyError, wave.Error) as exc:
        parser.exit(1, f"Quality check: {exc}\n")


if __name__ == "__main__":
    main()
