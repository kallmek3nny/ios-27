#!/usr/bin/env python3
"""Long-running active compiler-built mutex concurrency campaign, no sleeps.

The subprocesses and parent invocation must be wrapped in hard timeouts.
The workload consists solely of real native multithreaded test executions.
"""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def command(argv, *, limit, env=None):
    completed = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, env=env, check=False, timeout=limit)
    if completed.returncode:
        raise RuntimeError("{} exited {}: {}".format(argv[0], completed.returncode,
                                                       completed.stdout[-4000:]))
    return completed.stdout

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=180,
                        help="bounded active stress duration in seconds (max 240)")
    parser.add_argument("--compiler", default=os.environ.get("CC", "cc"))
    parser.add_argument("--max-iterations", type=int, default=10000)
    args = parser.parse_args()
    if not 1 <= args.duration <= 240 or not 1 <= args.max_iterations <= 10000:
        parser.error("duration must be 1..240 and max-iterations 1..10000")
    if not shutil.which(args.compiler):
        parser.error("compiler unavailable: " + args.compiler)
    beginning = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="phase21f-campaign-") as folder:
        programs = {}
        for kind in ("randomized", "stress", "adversarial"):
            output = Path(folder) / kind
            command([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra",
                     "-Wpedantic", "-Werror", "-pthread", "-I" + str(ROOT / "include"),
                     str(ROOT / "src/libsystem21f_mutex.c"),
                     str(ROOT / ("tests/phase21f-" + kind + ".c")),
                     "-o", str(output)], limit=120)
            programs[kind] = output
        count = 0
        graph_refusals = 0
        while count < args.max_iterations:
            seed = ((count + 1) * 0x9E3779B9) & 0xFFFFFFFF
            env = dict(os.environ, PHASE21F_SEED=str(seed))
            output = command([str(programs["randomized"])], limit=60, env=env)
            marker = " graph cycles refused"
            if marker not in output:
                raise RuntimeError("randomized test did not print success statistics")
            graph_refusals += int(output.split(" acquisitions, ")[-1].split(marker)[0])
            if count % 5 == 0:
                command([str(programs["stress"])], limit=60)
            if count % 10 == 0:
                command([str(programs["adversarial"])], limit=60)
            count += 1
            elapsed = time.monotonic() - beginning
            if count % 20 == 0:
                print("campaign: {} randomized programs, {:.1f}s active, {} cycles rejected".format(
                      count, elapsed, graph_refusals), flush=True)
            if elapsed >= args.duration:
                break
        print("campaign PASS: {} randomized programs, {:.1f}s actual elapsed, {} cycles rejected".format(
              count, time.monotonic() - beginning, graph_refusals), flush=True)

if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase21f campaign failure: " + str(exc))
