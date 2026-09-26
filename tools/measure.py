"""Measure one command; use inside Docker to measure inference, not its client."""

import json
import resource
import subprocess
import sys
import time
from pathlib import Path


def gpu_processes():
    try:
        result = subprocess.run(
            [
                "nvidia-smi",
                "--query-compute-apps=pid,used_gpu_memory",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=3,
            check=True,
        )
        values = {}
        for line in result.stdout.splitlines():
            pid, memory = line.split(",")
            if pid.strip().isdigit() and memory.strip().isdigit():
                values[int(pid)] = int(memory)
        return values
    except (OSError, ValueError, subprocess.SubprocessError):
        return {}


def main():
    target = Path(sys.argv[1])
    command = sys.argv[2:]
    if command and command[0] == "--":
        command.pop(0)
    before = gpu_processes()
    start = time.monotonic()
    process = subprocess.Popen(command)
    peak_gpu = 0
    try:
        while process.poll() is None:
            current = gpu_processes()
            peak_gpu = max(peak_gpu, sum(v for pid, v in current.items() if pid not in before))
            time.sleep(0.5)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    stats = {
        "command": command,
        "exit_code": process.returncode,
        "wall_seconds": time.monotonic() - start,
        "peak_child_rss_kib": resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
        "sampled_new_gpu_process_vram_mib": peak_gpu,
        "gpu_sampling_seconds": 0.5,
        "gpu_scope": "sum of GPU processes absent before command; avoid concurrent GPU jobs",
    }
    target.write_text(json.dumps(stats, indent=2) + "\n")
    return process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
