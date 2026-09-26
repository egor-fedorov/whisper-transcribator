import io
import json
import unittest
from contextlib import redirect_stdout
from types import SimpleNamespace
from unittest.mock import patch

from whisper_transcribator import cli


class CommandTests(unittest.TestCase):
    def test_doctor_reports_failed_import_without_loading_a_model(self):
        with (
            patch.object(cli.importlib, "import_module", side_effect=ImportError("missing")),
            patch.object(cli, "load_faster_whisper") as load,
            redirect_stdout(io.StringIO()) as output,
        ):
            self.assertEqual(cli.main(["doctor", "--device", "cpu", "--json"]), 1)
        self.assertEqual(len(json.loads(output.getvalue())["errors"]), 4)
        load.assert_not_called()

    def test_doctor_reports_compute_types_without_loading_a_model(self):
        runtime = SimpleNamespace(get_supported_compute_types=lambda device: {"int8", "float32"})
        with (
            patch.object(cli.importlib, "import_module", return_value=runtime),
            patch.object(cli, "version", return_value="test"),
            redirect_stdout(io.StringIO()) as output,
        ):
            self.assertEqual(cli.main(["doctor", "--device", "cpu", "--json"]), 0)
        self.assertEqual(json.loads(output.getvalue())["compute_types"], ["float32", "int8"])

    def test_download_is_separate_from_inference(self):
        with (
            patch.object(cli, "load_faster_whisper"),
            patch.object(cli, "ensure_model_is_available", return_value="/cache/model") as ensure,
            patch.object(cli, "build_transcriber") as build,
            redirect_stdout(io.StringIO()) as output,
        ):
            self.assertEqual(cli.main(["models", "download", "small", "--local-files-only"]), 0)
        self.assertEqual(output.getvalue().strip(), "/cache/model")
        self.assertTrue(ensure.call_args.args[-1])
        build.assert_not_called()

    def test_download_requires_a_model(self):
        with self.assertRaises(SystemExit) as error:
            cli.main(["models", "download"])
        self.assertEqual(error.exception.code, 2)

    def test_filesystem_errors_return_failure_without_traceback(self):
        with patch.object(cli, "prepare_jobs", side_effect=PermissionError("denied")):
            self.assertEqual(cli.main(["lecture.mp4"]), 1)


if __name__ == "__main__":
    unittest.main()
