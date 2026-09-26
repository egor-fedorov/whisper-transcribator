import os
import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory


class DirectoryScriptTests(unittest.TestCase):
    script = Path(__file__).resolve().parents[1] / "run_whisper.sh"
    docker_success = '''
while [[ $# -gt 0 ]]; do
    if [[ "$1" == -o ]]; then
        shift
        printf 'transcript\n' > "$PWD/${1##*/}"
        return 0
    fi
    shift
done
return 2
'''

    def setUp(self):
        self.temporary = TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_script(self, docker_body=None, **settings):
        env = os.environ.copy()
        for key in ("MODEL", "THREADS", "JOBS", "CONTAINER", "PREFIX", "OVERWRITE",
                    "MAP_FILE", "DEVICE", "COMPUTE_TYPE"):
            env.pop(key, None)
        env.update(settings)
        function = "docker() {\n" + (docker_body or self.docker_success) + "\n}\n"
        return subprocess.run(["bash", "-c", function + 'export -f docker; exec bash "$1"',
                               "test", str(self.script)], cwd=self.root, env=env,
                              capture_output=True, text=True, timeout=10)

    def test_all_background_failures_produce_nonzero_exit(self):
        for name in ("a.mp4", "b.mp4", "c.mp4"):
            (self.root / name).touch()
        result = self.run_script("return 23", JOBS="4")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("Все файлы обработаны!", result.stdout)
        self.assertEqual(len(list(self.root.glob("*.txt"))), 0)

    def test_tail_job_failure_is_not_lost(self):
        for name in ("a.mp4", "b.mp4", "c.mp4"):
            (self.root / name).touch()
        result = self.run_script('[[ "$*" != *c.mp4* ]] || return 23\n' + self.docker_success,
                                 JOBS="2")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(list(self.root.glob("*.txt"))), 2)

    def test_sorting_spaces_and_rerun(self):
        for name in ("z last.mp4", "a (first).mp4"):
            (self.root / name).touch()
        self.assertEqual(self.run_script(JOBS="2").returncode, 0)
        mapping = (self.root / "result_files.tsv").read_text()
        self.assertIn("001\tresult_001.txt\ta (first).mp4", mapping)
        self.assertEqual(self.run_script("return 23").returncode, 0)

    def test_changed_input_list_preserves_manifest(self):
        (self.root / "b.mp4").touch()
        self.assertEqual(self.run_script().returncode, 0)
        manifest = self.root / "result_files.tsv"
        original = manifest.read_bytes()
        (self.root / "a.mp4").touch()
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertEqual(manifest.read_bytes(), original)

    def test_invalid_jobs_do_not_touch_manifest(self):
        for jobs in ("0", "-1", "oops"):
            with self.subTest(jobs=jobs):
                self.assertNotEqual(self.run_script(JOBS=jobs).returncode, 0)
        self.assertFalse((self.root / "result_files.tsv").exists())


if __name__ == "__main__":
    unittest.main()
