import importlib.util
import json
import unittest
import wave
from pathlib import Path
from tempfile import TemporaryDirectory

spec = importlib.util.spec_from_file_location(
    "quality", Path(__file__).resolve().parents[1] / "tools/quality.py"
)
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)


class QualityTests(unittest.TestCase):
    def test_normalization(self):
        self.assertEqual(quality.normalize("Ёжик, ЕЖИК!   foo_bar １２"), "ежик ежик foo bar 12")

    def test_distance(self):
        self.assertEqual(quality.distance("a b c".split(), "a x c d".split()), 2)
        self.assertEqual(quality.distance("", "abc"), 3)
        self.assertEqual(quality.distance("abc", ""), 3)

    def test_duplicate_hypotheses_cannot_double_count_errors(self):
        for paths in ([], [Path("hyp"), Path("hyp/.")]):
            with self.subTest(paths=paths), self.assertRaisesRegex(ValueError, "distinct"):
                quality.score(Path("missing"), paths)

    def test_prepare_and_require_human_review(self):
        with TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base / "source.wav"
            with wave.open(str(source), "wb") as output:
                output.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                output.writeframes(b"\0\0" * 16000 * 10)
            root = base / "quality"
            quality.prepare(root, [source], 10)
            with self.assertRaises(FileExistsError):
                quality.prepare(root, [source], 10)
            with self.assertRaisesRegex(ValueError, "human-verified"):
                quality.score(root, [base / "hypothesis"])
            manifest_path = root / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["clips"][0]["human_reviewed"] = True
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "empty reference"):
                quality.score(root, [base])
            (root / "clip_001.reference.txt").write_text("one two", encoding="utf-8")
            (base / "clip_001.txt").write_text("one three", encoding="utf-8")
            result = quality.score(root, [base])
            self.assertEqual(result["totals"][str(base)]["wer"], 0.5)
            (root / "clip_001.wav").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                quality.score(root, [base])


if __name__ == "__main__":
    unittest.main()
