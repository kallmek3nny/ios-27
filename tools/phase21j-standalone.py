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

def run(args, seconds, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    completed = subprocess.run(
        args, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=seconds, env=env,
    )
    if completed.stdout:
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n", flush=True)
    if completed.returncode:
        raise RuntimeError("command failed ({})".format(completed.returncode))

def main():
    if sys.argv[1:] not in ([], ["--sanitize"], ["--stress"]):
        raise RuntimeError("expected no args, --sanitize or --stress")
    sanitize = sys.argv[1:] == ["--sanitize"]
    stress = sys.argv[1:] == ["--stress"]
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
    print("phase21j native architecture: {}; sanitizer={}; stress={}".format(
        architecture, sanitize, stress), flush=True)
    with tempfile.TemporaryDirectory(prefix="phase21j-") as temp:
        for optimize in (("-O1",) if sanitize else
                         ("-O2",) if stress else ("-O0", "-O2")):
            for test in ("diagnostics", "integration", "schedule", "schedule-races", "replay", "lockorder", "deadlock-integration", "export", "handoff", "broadcast", "rules", "large-cycle"):
                output = Path(temp) / (test + optimize)
                sources = [*SOURCES]
                if test == "integration":
                    sources.extend(INTEGRATION)
                if test in ("schedule", "schedule-races", "replay"):
                    sources.append(ROOT / "src/libsystem21j_schedule.c")
                if test == "replay":
                    sources.append(ROOT / "src/libsystem21c_sync.c")
                if test in ("lockorder", "large-cycle"):
                    sources.append(ROOT / "src/libsystem21j_lockorder.c")
                if test == "deadlock-integration":
                    sources.extend([
                        ROOT / "src/libsystem21c_sync.c",
                        ROOT / "src/libsystem21j_lockorder.c",
                    ])
                if test == "handoff":
                    sources.extend([
                        ROOT / "src/libsystem21c_sync.c",
                        ROOT / "src/libsystem21j_schedule.c",
                    ])
                if test == "broadcast":
                    sources.append(ROOT / "src/libsystem21c_sync.c")
                sources.append(ROOT / "tests" / ("phase21j-" + test + ".c"))
                run([compiler, *flags, optimize, *map(str, sources), "-o", str(output)], 120)
                for attempt in range(8 if stress else 1):
                    env = os.environ.copy()
                    if stress and test == "integration":
                        env["PHASE21J_SEED_BASE"] = hex(
                            0x21A000009F + attempt * 0x10000)
                        print("phase21j stress seed:", env["PHASE21J_SEED_BASE"],
                              flush=True)
                    run([str(output)], 150, env)
    print("phase21j: native compiler -Werror, {}, diagnostics, integration, schedules, schedule races and synchronization replay, lock order, deadlock prevention and coherent journal snapshots and cond-wait handoffs and fanout broadcasts and scripted fault rules and 64-actor cycles passed".format(
        "ASan/UBSan" if sanitize else
        "8x native O2 stress" if stress else "O0/O2"), flush=True)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit("phase21j failed: {}".format(error))
