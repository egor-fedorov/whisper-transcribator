import json
import os
import unittest
from contextlib import ExitStack
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import Mock, patch

from whisper_transcribator import cli
from whisper_transcribator.cli import (
    atomic_write,
    build_parser,
    choose_device,
    ensure_model_is_available,
    list_models,
    main,
    prepare_jobs,
    render_srt,
    validate_args,
    write_output,
)


class SafetyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "lecture.mp4"
        self.source.write_bytes(b"original media")

    def args(self, *extra):
        return build_parser().parse_args([str(self.source), *extra])

    def test_rejects_source_overwrite_even_when_requested(self):
        for name in (self.source, self.root / "alias", self.root / "hardlink"):
            if name.name == "alias":
                name.symlink_to(self.source)
            elif name.name == "hardlink":
                os.link(self.source, name)
            with self.subTest(name=name), self.assertRaisesRegex(SystemExit, "input file"):
                prepare_jobs(self.args("-o", str(name), "--overwrite"))
        self.assertEqual(self.source.read_bytes(), b"original media")

    def test_missing_later_input_fails_before_model_loading(self):
        with patch("whisper_transcribator.cli.load_faster_whisper") as load:
            with self.assertRaisesRegex(SystemExit, "Input file not found"):
                main([str(self.source), str(self.root / "missing.mp4")])
            load.assert_not_called()

    def test_rejects_basename_collision(self):
        other = self.source.with_suffix(".mp3")
        other.touch()
        args = self.args(str(other), "--output-dir", str(self.root / "out"))
        with self.assertRaisesRegex(SystemExit, "same file"):
            prepare_jobs(args)

    def test_existing_transcript_requires_overwrite(self):
        output = self.source.with_suffix(".txt")
        output.write_text("old")
        with self.assertRaisesRegex(SystemExit, "--overwrite"):
            prepare_jobs(self.args())
        prepare_jobs(self.args("--overwrite"))
        self.assertEqual(output.read_text(), "old")

    def test_atomic_failure_preserves_existing_output(self):
        output = self.root / "result.txt"
        output.write_text("old")
        with patch("whisper_transcribator.cli.os.fsync", side_effect=OSError("disk full")):
            with self.assertRaisesRegex(OSError, "disk full"):
                atomic_write(output, "new", overwrite=True)
        self.assertEqual(output.read_text(), "old")
        self.assertEqual(list(self.root.glob(".whisper-*")), [])

    def test_atomic_publication_does_not_overwrite_competing_result(self):
        output = self.root / "result.txt"
        output.write_text("other writer")
        with self.assertRaises(FileExistsError):
            atomic_write(output, "new")
        self.assertEqual(output.read_text(), "other writer")
        atomic_write(output, "replacement", overwrite=True)
        self.assertEqual(output.read_text(), "replacement")

    def test_empty_text_does_not_publish_output(self):
        output = self.root / "result.txt"
        with self.assertRaisesRegex(SystemExit, "No transcript"):
            write_output(self.source, output, "text", "small", None,
                         [SimpleNamespace(text="  ")])
        self.assertFalse(output.exists())

    def test_rejects_invalid_all_and_batch_combinations(self):
        for extra in (("--format", "all"), ("--format", "all", "-o", "out.txt"),
                      ("--batch-size", "8", "--no-vad")):
            with self.subTest(extra=extra), self.assertRaises(SystemExit):
                validate_args(self.args(*extra))

    def test_all_formats_use_one_transcription(self):
        segment = SimpleNamespace(start=0.0, end=1.0, text="Hello")
        with ExitStack() as stack:
            stack.enter_context(patch("whisper_transcribator.cli.load_faster_whisper"))
            stack.enter_context(patch("whisper_transcribator.cli.ensure_model_is_available"))
            stack.enter_context(patch("whisper_transcribator.cli.build_transcriber"))
            transcribe = stack.enter_context(patch(
                "whisper_transcribator.cli.transcribe_file",
                return_value=([segment], SimpleNamespace(duration=1.0)),
            ))
            self.assertEqual(main([str(self.source), "--device", "cpu", "--format", "all",
                                   "--output-dir", str(self.root / "out")]), 0)
            transcribe.assert_called_once()
        for extension in ("txt", "srt", "json"):
            self.assertTrue((self.root / "out" / f"lecture.{extension}").is_file())
        payload = json.loads((self.root / "out" / "lecture.json").read_text())
        self.assertEqual(payload["text"], "Hello")

    def test_generator_error_includes_input_and_publishes_nothing(self):
        def broken_segments():
            yield SimpleNamespace(start=0, end=1, text="Partial")
            raise RuntimeError("decoder failed")

        transcriber = Mock()
        transcriber.transcribe.return_value = (broken_segments(), SimpleNamespace(duration=2))
        with ExitStack() as stack:
            stack.enter_context(patch("whisper_transcribator.cli.load_faster_whisper"))
            stack.enter_context(patch("whisper_transcribator.cli.ensure_model_is_available"))
            stack.enter_context(patch("whisper_transcribator.cli.build_transcriber", return_value=transcriber))
            with self.assertRaisesRegex(SystemExit, r"lecture.mp4.*decoder failed"):
                main([str(self.source), "--device", "cpu"])
        self.assertFalse(self.source.with_suffix(".txt").exists())

    def test_srt_numbers_ignore_empty_segments(self):
        result = render_srt([SimpleNamespace(start=0, end=1, text=""),
                             SimpleNamespace(start=1, end=2, text="Hello")])
        self.assertTrue(result.startswith("1\n"))


class ModelCacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.legacy = self.root / "small"
        self.legacy.mkdir()
        self.snapshot = self.root / "snapshot"
        self.snapshot.mkdir()
        for name, content in (("model.bin", b"model"), ("config.json", b"{}"),
                              ("tokenizer.json", b"{}")):
            (self.snapshot / name).write_bytes(content)

    def test_empty_legacy_cache_downloads_snapshot(self):
        utils = Mock()
        utils.download_model.return_value = str(self.snapshot)
        with patch("whisper_transcribator.cli.importlib.import_module", return_value=utils):
            path = ensure_model_is_available(None, "small", str(self.root), False)
        self.assertEqual(path, str(self.snapshot))
        utils.download_model.assert_called_once_with("small", cache_dir=str(self.root),
                                                     local_files_only=False)

    def test_offline_failure_identifies_missing_files(self):
        utils = Mock()
        utils.download_model.side_effect = RuntimeError("not cached")
        with patch("whisper_transcribator.cli.importlib.import_module", return_value=utils):
            with self.assertRaisesRegex(SystemExit, "missing/empty model.bin"):
                ensure_model_is_available(None, "small", str(self.root), True)
        self.assertTrue(utils.download_model.call_args.kwargs["local_files_only"])

    def test_local_model_without_tokenizer_is_rejected_without_download(self):
        (self.snapshot / "tokenizer.json").unlink()
        with patch("whisper_transcribator.cli.importlib.import_module") as imports:
            with self.assertRaisesRegex(SystemExit, "tokenizer.json"):
                ensure_model_is_available(None, str(self.snapshot), None, True)
            imports.assert_not_called()

    def test_complete_legacy_cache_is_reused(self):
        for file in self.snapshot.iterdir():
            (self.legacy / file.name).write_bytes(file.read_bytes())
        with patch("whisper_transcribator.cli.importlib.import_module") as imports:
            self.assertEqual(ensure_model_is_available(None, "small", str(self.root), True),
                             str(self.legacy))
            imports.assert_not_called()

    def test_list_does_not_report_empty_directory_as_cached(self):
        utils = Mock()
        utils.download_model.side_effect = RuntimeError("not cached")
        with patch("whisper_transcribator.cli.importlib.import_module", return_value=utils):
            line = next(line for line in list_models(str(self.root)).splitlines()
                        if line.startswith("small "))
        self.assertIn("incomplete", line)
        for call in utils.download_model.call_args_list:
            self.assertTrue(call.kwargs["local_files_only"])


class DeviceTests(unittest.TestCase):
    def test_no_gpu_auto_falls_back_but_explicit_cuda_fails(self):
        runtime = Mock()
        runtime.get_cuda_device_count.return_value = 0
        with patch("whisper_transcribator.cli.importlib.import_module", return_value=runtime):
            self.assertEqual(choose_device("auto"), "cpu")
            with self.assertRaisesRegex(SystemExit, "CUDA unavailable"):
                choose_device("cuda")

    def test_missing_cuda_libraries_are_reported(self):
        runtime = Mock()
        runtime.get_cuda_device_count.return_value = 1
        with patch("whisper_transcribator.cli.importlib.import_module", return_value=runtime), \
             patch.object(cli.ctypes, "CDLL", side_effect=OSError("missing cuDNN")):
            self.assertEqual(choose_device("auto"), "cpu")
            with self.assertRaisesRegex(SystemExit, "missing cuDNN"):
                choose_device("cuda")

    def test_working_cuda_is_selected(self):
        runtime = Mock()
        runtime.get_cuda_device_count.return_value = 1
        with patch("whisper_transcribator.cli.importlib.import_module", return_value=runtime), \
             patch.object(cli.ctypes, "CDLL"):
            self.assertEqual(choose_device("auto"), "cuda")


if __name__ == "__main__":
    unittest.main()
