import json
import os
import signal
import subprocess
import sys
import unittest
from contextlib import ExitStack
from multiprocessing.util import Finalize
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

from whisper_transcribator import cli
from whisper_transcribator.jobs import prepare_jobs
from whisper_transcribator.outputs import atomic_write, render_json


def fake_worker(task):
    job, _args = task
    return job, "failed" if job == "bad" else None


def crashed_worker(_task):
    os._exit(9)


def finalizing_worker(task):
    path, _args = task
    Finalize(None, Path(path).write_text, args=("clean shutdown",), exitpriority=0)
    return path, None


class WorkerTests(unittest.TestCase):
    def test_successful_worker_finalizers_run(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "finalized"
            args = SimpleNamespace(jobs=1, continue_on_error=False)
            self.assertEqual(cli.parallel_transcribe([str(path)], args, finalizing_worker), 0)
            self.assertEqual(path.read_text(), "clean shutdown")

    def test_last_worker_error_is_not_lost(self):
        args = SimpleNamespace(jobs=2, continue_on_error=True)
        self.assertEqual(cli.parallel_transcribe(["good", "bad"], args, fake_worker), 1)

    def test_hard_worker_failure_does_not_hang(self):
        args = SimpleNamespace(jobs=1, continue_on_error=True)
        with self.assertRaisesRegex(SystemExit, "worker exited"):
            cli.parallel_transcribe(["bad"], args, crashed_worker)


class DirectoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def args(self, *extra):
        return cli.build_parser().parse_args(["--input-dir", str(self.root), *extra])

    def test_sorted_unicode_and_case_insensitive_extensions(self):
        for name in ("z last.mp4", "a (first).MP4", "лекция.wav", "ignore.txt"):
            (self.root / name).touch()
        jobs = prepare_jobs(self.args())
        self.assertEqual(
            [j.source.name for j in jobs], ["a (first).MP4", "z last.mp4", "лекция.wav"]
        )

    def test_explicit_input_order_is_preserved(self):
        for name in ("b.mp4", "a.mp4"):
            (self.root / name).touch()
        args = cli.build_parser().parse_args([str(self.root / "b.mp4"), str(self.root / "a.mp4")])
        self.assertEqual([j.source.name for j in prepare_jobs(args)], ["b.mp4", "a.mp4"])

    def test_format_override_does_not_skip_srt_because_txt_exists(self):
        (self.root / "lecture.mp4").touch()
        (self.root / "lecture.txt").write_text("old text")
        jobs = prepare_jobs(self.args("--format", "srt", "--skip-existing"))
        self.assertEqual(len(jobs), 1)
        self.assertEqual(set(jobs[0].outputs), {"srt"})

    def test_partial_all_requires_overwrite(self):
        (self.root / "lecture.mp4").touch()
        (self.root / "lecture.txt").write_text("text")
        args = self.args("--format", "all", "--output-dir", str(self.root), "--skip-existing")
        with self.assertRaisesRegex(SystemExit, "--overwrite"):
            prepare_jobs(args)
        args.overwrite = True
        self.assertEqual(len(prepare_jobs(args)), 1)

    def test_complete_all_is_skipped(self):
        (self.root / "lecture.mp4").touch()
        for ext in ("txt", "srt", "json"):
            (self.root / f"lecture.{ext}").write_text("nonempty")
        self.assertEqual(
            prepare_jobs(
                self.args("--format", "all", "--output-dir", str(self.root), "--skip-existing")
            ),
            [],
        )

    def test_collision_is_checked_before_skip(self):
        for name in ("lecture.mp4", "lecture.mp3"):
            (self.root / name).touch()
        (self.root / "lecture.txt").write_text("text")
        with self.assertRaisesRegex(SystemExit, "same file"):
            prepare_jobs(self.args("--skip-existing"))

    def test_mapping_is_preserved_after_input_list_changes(self):
        (self.root / "b.mp4").touch()
        args = self.args("--naming", "numbered", "--output-dir", str(self.root))
        jobs = prepare_jobs(args)
        mapping = self.root / "result_files.json"
        original = mapping.read_bytes()
        self.assertEqual(jobs[0].outputs["text"].name, "result_001.txt")
        (self.root / "a.mp4").touch()
        with self.assertRaisesRegex(SystemExit, "Input list changed"):
            prepare_jobs(args)
        self.assertEqual(mapping.read_bytes(), original)

    def test_numbered_results_without_map_are_not_trusted(self):
        (self.root / "lecture.mp4").touch()
        (self.root / "result_001.txt").write_text("unknown result")
        with self.assertRaisesRegex(SystemExit, "without a mapping"):
            prepare_jobs(
                self.args("--naming", "numbered", "--output-dir", str(self.root), "--skip-existing")
            )

    def test_empty_directory_does_not_load_model(self):
        with patch.object(cli, "load_faster_whisper") as load:
            self.assertEqual(cli.main(["transcribe", "--input-dir", str(self.root)]), 0)
            load.assert_not_called()

    def test_invalid_jobs_are_usage_error(self):
        with self.assertRaises(SystemExit) as error:
            cli.main(["--input-dir", str(self.root), "--jobs", "0"])
        self.assertEqual(error.exception.code, 2)

    def test_continue_on_error_processes_later_file(self):
        for name in ("a.mp4", "b.mp4"):
            (self.root / name).touch()
        segment = SimpleNamespace(start=0, end=1, text="success")
        with ExitStack() as stack:
            for name in ("load_faster_whisper", "build_transcriber"):
                stack.enter_context(patch.object(cli, name))
            stack.enter_context(
                patch.object(cli, "ensure_model_is_available", return_value="small")
            )
            stack.enter_context(
                patch.object(
                    cli,
                    "transcribe_file",
                    side_effect=[
                        RuntimeError("bad audio"),
                        ([segment], SimpleNamespace(duration=1)),
                    ],
                )
            )
            result = cli.main(
                ["--input-dir", str(self.root), "--device", "cpu", "--continue-on-error"]
            )
        self.assertEqual(result, 1)
        self.assertFalse((self.root / "a.txt").exists())
        self.assertEqual((self.root / "b.txt").read_text(), "success\n")

    def test_permissions_honor_umask_and_preserve_existing_mode(self):
        output = self.root / "private.txt"
        previous = os.umask(0o077)
        try:
            atomic_write(output, "private")
        finally:
            os.umask(previous)
        self.assertEqual(output.stat().st_mode & 0o777, 0o600)
        output.chmod(0o640)
        atomic_write(output, "new", overwrite=True)
        self.assertEqual(output.stat().st_mode & 0o777, 0o640)

    def test_schema_and_run_metadata(self):
        content = render_json(
            Path("test.wav"),
            "tiny",
            SimpleNamespace(duration=1),
            [SimpleNamespace(start=0, end=1, text="text")],
            {"device": "cpu"},
        )
        payload = json.loads(content)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["run"]["device"], "cpu")

    def test_termination_during_model_loading(self):
        source = self.root / "lecture.mp4"
        source.touch()
        code = """import sys,time
from whisper_transcribator import cli
def load():
 print("ready",flush=True)
 time.sleep(60)
cli.load_faster_whisper=load
raise SystemExit(cli.main(sys.argv[1:]))
"""
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                process = subprocess.Popen(
                    [sys.executable, "-c", code, str(source)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                try:
                    self.assertEqual(process.stdout.readline().strip(), "ready")
                    process.send_signal(signum)
                    process.communicate(timeout=5)
                    self.assertEqual(process.returncode, 128 + signum)
                    self.assertFalse(source.with_suffix(".txt").exists())
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()


class LauncherTests(unittest.TestCase):
    def test_extra_format_and_device_are_forwarded_consistently(self):
        script = Path(__file__).resolve().parents[1] / "transcribe_dir.sh"
        with TemporaryDirectory() as directory:
            fake = Path(directory) / "docker"
            fake.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            fake.chmod(0o755)
            env = {k: v for k, v in os.environ.items() if not k.startswith("WHISPER_")}
            env["DOCKER_BIN"] = str(fake)
            result = subprocess.run(
                ["bash", str(script), directory, "--format", "srt", "--device", "cuda"],
                env=env,
                capture_output=True,
                text=True,
                check=True,
            )
            args = result.stdout.splitlines()
            self.assertIn("--gpus", args)
            self.assertIn("--input-dir", args)
            self.assertEqual(args[-4:], ["--format", "srt", "--device", "cuda"])


if __name__ == "__main__":
    unittest.main()
