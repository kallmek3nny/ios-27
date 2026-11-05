#!/usr/bin/env python3
"""Bounded multithreaded sanitizer campaign with independently checked exits.

Each compiled program is run with a new reproducible seed. This intentionally
uses real execution, not elapsed-time sleeps or synthetic fixture-only tests.
"""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def utc():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")

def run(argv, limit, env=None):
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, env=env, check=False, timeout=limit)
    if p.returncode:
        raise RuntimeError("{} exited {}: {}".format(argv[0], p.returncode,
                                                       p.stdout[-5000:]))
    return p.stdout

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitizer", choices=("tsan", "asan"), required=True)
    parser.add_argument("--duration", type=int, default=180)
    args = parser.parse_args()
    if not 1 <= args.duration <= 600:
        parser.error("duration must be 1..600 seconds")
    compiler = "gcc" if args.sanitizer == "tsan" else "clang"
    if not shutil.which(compiler):
        parser.error("compiler missing: " + compiler)
    sanitizer = ["-fsanitize=thread"] if args.sanitizer == "tsan" else [
        "-fsanitize=address,undefined"]
    start = time.monotonic()
    print("sanitizer campaign {} start UTC {}".format(args.sanitizer, utc()), flush=True)
    with tempfile.TemporaryDirectory(prefix="phase21f-sanitizer-") as directory:
        binaries = {}
        for test in ("randomized", "stress", "cancel-storm", "graph-depth"):
            out = Path(directory) / test
            run([compiler, "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wpedantic", "-Werror", "-pthread", *sanitizer,
                 "-fno-omit-frame-pointer", "-I" + str(ROOT / "include"),
                 str(ROOT / "src/libsystem21f_mutex.c"),
                 str(ROOT / ("tests/phase21f-" + test + ".c")),
                 "-o", str(out)], 120)
            binaries[test] = out
        count = 0
        refused = 0
        while time.monotonic() - start < args.duration:
            seed = ((count + 1) * 0xD1B54A32) & 0xFFFFFFFF
            output = run([str(binaries["randomized"])], 90,
                         dict(os.environ, PHASE21F_SEED=str(seed)))
            marker = " graph cycles refused"
            if marker not in output:
                raise RuntimeError("missing randomized audit statistics")
            refused += int(output.split(" acquisitions, ")[-1].split(marker)[0])
            if count % 4 == 0:
                run([str(binaries["stress"])], 90)
            if count % 8 == 0:
                run([str(binaries["cancel-storm"])], 90)
            if count % 12 == 0:
                run([str(binaries["graph-depth"])], 90)
            count += 1
            if count % 10 == 0:
                print("{}: {} randomized programs, {:.1f}s active, {} cycles refused".format(
                      args.sanitizer, count, time.monotonic() - start, refused), flush=True)
    print("sanitizer campaign {} PASS end UTC {}, {:.1f}s elapsed, {} randomized programs, {} cycles refused".format(
          args.sanitizer, utc(), time.monotonic() - start, count, refused), flush=True)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit("phase21f sanitizer campaign FAILED: " + str(error))
