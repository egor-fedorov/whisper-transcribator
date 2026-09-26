"""Bounded CPU process scheduling and explicit detection of native worker death."""

import multiprocessing
import signal
import sys
from queue import Empty


def worker_loop(tasks, results, call):
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, signal.SIG_DFL)
    while True:
        task = tasks.get()
        if task is None:
            return
        results.put(call(task))


def run_workers(jobs, args, call):
    failed = False
    context = multiprocessing.get_context("spawn")
    count = min(args.jobs, len(jobs))
    tasks, results = context.Queue(count), context.Queue()
    processes = []
    pending = iter(jobs)
    outstanding = 0
    try:
        for _ in range(count):
            process = context.Process(target=worker_loop, args=(tasks, results, call))
            process.start()
            processes.append(process)
            tasks.put((next(pending), args))
            outstanding += 1
        while outstanding:
            # A killed native worker may never put a result on its queue.
            if any(p.exitcode is not None for p in processes):
                raise SystemExit("A transcription worker exited unexpectedly (possible OOM).")
            try:
                source, error = results.get(timeout=0.2)
            except Empty:
                continue
            outstanding -= 1
            if error:
                print(f"Failed to transcribe {source}: {error}", file=sys.stderr)
                failed = True
                if not args.continue_on_error:
                    break
            else:
                print(f"Done: {source}", file=sys.stderr)
            next_job = next(pending, None)
            if next_job is not None:
                tasks.put((next_job, args))
                outstanding += 1
    finally:
        if not outstanding:
            # Let successful workers release native runtimes and their semaphores.
            for _ in processes:
                tasks.put(None)
            for process in processes:
                process.join(timeout=5)
        for process in processes:
            if process.is_alive():
                process.terminate()
        for process in processes:
            process.join(timeout=5)
            if process.is_alive():
                process.kill()
                process.join()
        tasks.cancel_join_thread()
        tasks.close()
        results.close()
    return int(failed)
