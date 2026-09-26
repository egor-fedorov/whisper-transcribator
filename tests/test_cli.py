import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

from whisper_transcribator.cli import (
    build_parser,
    choose_compute_type,
    format_timestamp,
    list_models,
    main,
    normalize_language,
    render_srt,
    render_text,
    resolve_model_reference,
    resolve_output_path,
    sanitize_model_name,
    validate_args,
)


class OutputPathTests(unittest.TestCase):
    def test_uses_explicit_output_when_present(self) -> None:
        path = resolve_output_path(Path("/tmp/lecture.mp4"), "/tmp/result.txt", None, "text")
        self.assertEqual(path, Path("/tmp/result.txt"))

    def test_uses_output_dir_in_batch_mode(self) -> None:
        path = resolve_output_path(Path("/tmp/lecture.mp4"), None, "/tmp/out", "text")
        self.assertEqual(path, Path("/tmp/out/lecture.txt"))

    def test_defaults_to_input_basename_for_srt(self) -> None:
        path = resolve_output_path(Path("/tmp/lecture.mp4"), None, None, "srt")
        self.assertEqual(path, Path("/tmp/lecture.srt"))


class ArgumentValidationTests(unittest.TestCase):
    def test_requires_input_without_list_models(self) -> None:
        parser = build_parser()
        args = parser.parse_args([])
        with self.assertRaises(SystemExit):
            validate_args(args)

    def test_rejects_output_with_multiple_inputs(self) -> None:
        parser = build_parser()
        args = parser.parse_args(["one.mp4", "two.mp4", "-o", "result.txt"])
        with self.assertRaises(SystemExit):
            validate_args(args)

    def test_rejects_output_and_output_dir_together(self) -> None:
        parser = build_parser()
        args = parser.parse_args(["one.mp4", "-o", "result.txt", "--output-dir", "out"])
        with self.assertRaises(SystemExit):
            validate_args(args)


class FormattingTests(unittest.TestCase):
    def test_normalize_language_auto_becomes_none(self) -> None:
        self.assertIsNone(normalize_language("auto"))

    def test_normalize_language_preserves_code(self) -> None:
        self.assertEqual(normalize_language("RU"), "ru")

    def test_format_timestamp_for_srt(self) -> None:
        self.assertEqual(
            format_timestamp(3661.123, always_include_hours=True, decimal_marker=","),
            "01:01:01,123",
        )

    def test_render_srt(self) -> None:
        segments = [
            SimpleNamespace(start=0.0, end=1.25, text="Hello"),
            SimpleNamespace(start=2.0, end=3.5, text="World"),
        ]
        result = render_srt(segments)
        self.assertIn("00:00:00,000 --> 00:00:01,250", result)
        self.assertIn("Hello", result)
        self.assertIn("World", result)

    def test_render_text_joins_segments_with_spaces(self) -> None:
        segments = [
            SimpleNamespace(text=" First sentence."),
            SimpleNamespace(text="Second sentence. "),
            SimpleNamespace(text=""),
        ]
        self.assertEqual(render_text(segments), "First sentence. Second sentence.\n")


class ModelWorkflowTests(unittest.TestCase):
    def test_sanitize_model_name(self) -> None:
        self.assertEqual(
            sanitize_model_name("Systran/faster-whisper-large-v3"),
            "Systran-faster-whisper-large-v3",
        )

    def test_list_models_with_download_root(self) -> None:
        output = list_models("/models")
        self.assertIn("Model download root: /models", output)
        self.assertIn("small", output)
        self.assertIn("on-demand", output)

    def test_resolve_model_reference_for_existing_local_path(self) -> None:
        with TemporaryDirectory() as tmpdir:
            model_dir = Path(tmpdir) / "model"
            model_dir.mkdir()
            resolved = resolve_model_reference(str(model_dir))
            self.assertEqual(resolved, str(model_dir.resolve()))

    def test_resolve_model_reference_for_missing_local_path(self) -> None:
        with self.assertRaises(SystemExit):
            resolve_model_reference("./missing-model")

    def test_choose_compute_type_auto(self) -> None:
        self.assertEqual(choose_compute_type("auto", "cpu"), "int8")
        self.assertEqual(choose_compute_type("auto", "cuda"), "float16")


class MainSmokeTests(unittest.TestCase):
    def test_main_writes_output_with_mocked_transcriber(self) -> None:
        with TemporaryDirectory() as tmpdir:
            input_path = Path(tmpdir) / "lecture.mp4"
            output_path = Path(tmpdir) / "lecture.txt"
            input_path.write_bytes(b"not-real-media")

            fake_segments = [
                SimpleNamespace(
                    id=0,
                    start=0.0,
                    end=1.0,
                    text="Hello",
                    avg_logprob=None,
                    compression_ratio=None,
                    no_speech_prob=None,
                    words=None,
                ),
                SimpleNamespace(
                    id=1,
                    start=1.0,
                    end=2.0,
                    text="world",
                    avg_logprob=None,
                    compression_ratio=None,
                    no_speech_prob=None,
                    words=None,
                ),
            ]
            fake_info = SimpleNamespace(
                language="ru",
                language_probability=0.99,
                duration=1.0,
                duration_after_vad=1.0,
            )

            with patch("whisper_transcribator.cli.load_faster_whisper", return_value=object()):
                with patch(
                    "whisper_transcribator.cli.ensure_model_is_available", return_value="small"
                ):
                    with patch(
                        "whisper_transcribator.cli.build_transcriber", return_value=object()
                    ):
                        with patch(
                            "whisper_transcribator.cli.transcribe_file",
                            return_value=(fake_segments, fake_info),
                        ):
                            exit_code = main(
                                [
                                    str(input_path),
                                    "-o",
                                    str(output_path),
                                    "--model",
                                    "small",
                                    "--language",
                                    "ru",
                                ]
                            )

            self.assertEqual(exit_code, 0)
            self.assertEqual(output_path.read_text(encoding="utf-8"), "Hello world\n")


if __name__ == "__main__":
    unittest.main()
