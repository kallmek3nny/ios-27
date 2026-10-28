#!/usr/bin/env python3
"""Independent native phase 21j diagnostics and integration build/test runner."""
from pathlib import Path
import os
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    ROOT / "src/libsystem21j_diagnostics.c",
)
INTEGRATION = (
    ROOT / "src/libsystem21b_lifecycle.c",
    ROOT / "src/libsystem21c_sync.c",
    ROOT / "src/libsystem21e_tls.c",
)

def run(args, seconds):
    print("+", " ".join(map(str, args)), flush=True)
    completed = subprocess.run(
        args, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=seconds,
    )
    if completed.stdout:
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n", flush=True)
    if completed.returncode:
        raise RuntimeError("command failed ({})".format(completed.returncode))

def main():
    if sys.argv[1:] not in ([], ["--sanitize"]):
        raise RuntimeError("expected no args or --sanitize")
    sanitize = sys.argv[1:] == ["--sanitize"]
    architecture = platform.machine().lower()
    if architecture not in ("x86_64", "amd64", "aarch64", "arm64"):
        raise RuntimeError("unexpected native architecture: " + architecture)
    compiler = os.environ.get("CC", "cc")
    if not shutil.which(compiler):
        raise RuntimeError("missing native compiler " + compiler)
    flags = [
        "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-pthread", "-I" + str(ROOT / "include"),
    ]
    if sanitize:
        flags.extend(["-g", "-fsanitize=address,undefined",
                      "-fno-omit-frame-pointer"])
    print("phase21j native architecture: {}; sanitizer={}".format(
        architecture, sanitize), flush=True)
    with tempfile.TemporaryDirectory(prefix="phase21j-") as temp:
        for optimize in (("-O1",) if sanitize else ("-O0", "-O2")):
            for test in ("diagnostics", "integration", "schedule", "schedule-races", "replay", "lockorder"):
                output = Path(temp) / (test + optimize)
                sources = [*SOURCES]
                if test == "integration":
                    sources.extend(INTEGRATION)
                if test in ("schedule", "schedule-races", "replay"):
                    sources.append(ROOT / "src/libsystem21j_schedule.c")
                if test == "replay":
                    sources.append(ROOT / "src/libsystem21c_sync.c")
                if test == "lockorder":
                    sources.append(ROOT / "src/libsystem21j_lockorder.c")
                sources.append(ROOT / "tests" / ("phase21j-" + test + ".c"))
                run([compiler, *flags, optimize, *map(str, sources), "-o", str(output)], 120)
                run([str(output)], 150)
    print("phase21j: native compiler -Werror, {}, diagnostics, integration, schedules, schedule races and synchronization replay and lock order passed".format(
        "ASan/UBSan" if sanitize else "O0/O2"), flush=True)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit("phase21j failed: {}".format(error))
