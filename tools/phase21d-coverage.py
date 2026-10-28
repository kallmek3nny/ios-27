#!/usr/bin/env python3
"""Isolated, hard-bounded GCC line/branch coverage for the phase21d bridge.

Builds one instrumented bridge object used by all eight compiled test programs
and checks the cumulative coverage, without touching repository build files.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/libsystem21d_rw_once.c"
TESTS = ("stress", "once", "ownership", "timeout", "cancel", "exhaustion", "fault", "integration", "fairness")
MIN_LINES = 90.0
MIN_BRANCHES = 70.0
FLAGS = ("-std=c11", "-O0", "-g", "--coverage", "-Wall", "-Wextra",
         "-Wpedantic", "-Werror", "-pthread", "-I" + str(ROOT / "include"))
WRAPS = ("-Wl,--wrap=pthread_cond_init", "-Wl,--wrap=pthread_cond_destroy",
         "-Wl,--wrap=pthread_cond_wait", "-Wl,--wrap=pthread_cond_timedwait",
         "-Wl,--wrap=malloc")


def run(args, cwd, timeout):
    proc = subprocess.run([str(a) for a in args], cwd=cwd, check=False,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, timeout=timeout)
    if proc.returncode:
        raise RuntimeError("failed command " + " ".join(map(str, args)) +
                           ":\n" + proc.stdout)
    return proc.stdout


def percentage(output, label):
    match = re.search(re.escape(label) + r":\s*([0-9.]+)%", output)
    if match is None:
        raise RuntimeError(f"no {label} in gcov output:\n{output}")
    return float(match.group(1))


def main():
    compiler = os.environ.get("CC", "gcc")
    for program in (compiler, "gcov"):
        if not shutil.which(program):
            raise RuntimeError("missing compiler or coverage tool: " + program)
    with tempfile.TemporaryDirectory(prefix="phase21d-coverage-") as scratch:
        tmp = Path(scratch)
        source_obj = tmp / "libsystem21d_rw_once.o"
        run([compiler, *FLAGS, "-c", SOURCE, "-o", source_obj], tmp, 60)
        for name in TESTS:
            exe = tmp / name
            test = ROOT / f"tests/phase21d-{name}.c"
            wraps = WRAPS if name == "fault" else ()
            run([compiler, *FLAGS, source_obj, test, *wraps, "-o", exe], tmp, 60)
            output = run([exe], tmp, 90)
            if output.strip():
                print(output.strip(), flush=True)
        gcov_out = run(["gcov", "-b", "-c", "-o", tmp, SOURCE], tmp, 30)
        lines = percentage(gcov_out, "Lines executed")
        branches = percentage(gcov_out, "Taken at least once")
        print(f"phase21d GCC coverage: {lines:.2f}% lines, "
              f"{branches:.2f}% branch outcomes", flush=True)
        if lines < MIN_LINES or branches < MIN_BRANCHES:
            raise RuntimeError("coverage below minimum line/branch thresholds")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit(f"phase21d coverage failed: {exc}")
