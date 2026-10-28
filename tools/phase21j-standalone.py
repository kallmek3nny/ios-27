#!/usr/bin/env python3
"""Independent native phase 21j diagnostics and integration build/test runner."""
from pathlib import Path
import json
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

def run(args, seconds, env=None, input_data=None, echo=True):
    if echo:
        print("+", " ".join(map(str, args)), flush=True)
    completed = subprocess.run(
        args, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=seconds, env=env, input=input_data,
    )
    if completed.stdout and echo:
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n", flush=True)
    if completed.returncode:
        raise RuntimeError("command failed ({})".format(completed.returncode))
    return completed.stdout

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
            for test in ("diagnostics", "integration", "schedule", "schedule-races", "replay", "lockorder", "deadlock-integration", "export", "handoff", "broadcast", "rules", "large-cycle", "boundaries", "identities", "trace-cli"):
                output = Path(temp) / (test + optimize)
                sources = [*SOURCES]
                if test == "integration":
                    sources.extend(INTEGRATION)
                if test in ("schedule", "schedule-races", "replay"):
                    sources.append(ROOT / "src/libsystem21j_schedule.c")
                if test == "replay":
                    sources.append(ROOT / "src/libsystem21c_sync.c")
                if test in ("lockorder", "large-cycle", "boundaries"):
                    sources.append(ROOT / "src/libsystem21j_lockorder.c")
                if test == "boundaries":
                    sources.append(ROOT / "src/libsystem21j_schedule.c")
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
                if test == "trace-cli":
                    sources.append(ROOT / "src/libsystem21j_schedule.c")
                    sources.append(ROOT / "tools/phase21j-trace.c")
                else:
                    sources.append(ROOT / "tests" / ("phase21j-" + test + ".c"))
                run([compiler, *flags, optimize, *map(str, sources), "-o", str(output)], 120)
                for attempt in range(8 if stress else 1):
                    env = os.environ.copy()
                    if stress and test == "integration":
                        env["PHASE21J_SEED_BASE"] = hex(
                            0x21A000009F + attempt * 0x10000)
                        print("phase21j stress seed:", env["PHASE21J_SEED_BASE"],
                              flush=True)
                    if test == "trace-cli":
                        command = [str(output), "0x21feed", "4", "512"]
                        first = run(command, 150, env)
                        second = run(command, 150, env)
                        if first != second:
                            raise RuntimeError("trace digest varied for fixed seed")
                        summary = json.loads(first.strip())
                        if (summary["digest"] != "0x084ccace638e3858" or
                                summary["injections"] != 86 or
                                summary["last"] != 598 or
                                summary["dropped"] != 0):
                            raise RuntimeError("trace CLI golden mismatch")
                        captured = run(command + ["--jsonl"], 150, env,
                                       echo=False)
                        verifier = [sys.executable, str(ROOT / "tools" /
                                     "phase21j-verify-trace.py")]
                        run(verifier, 30, env, input_data=captured)
                        corrupted = captured.replace('"kind":6', '"kind":5', 1)
                        if corrupted == captured:
                            raise RuntimeError("missing corruption candidate")
                        rejected = subprocess.run(
                            verifier, input=corrupted, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30, env=env, check=False,
                        )
                        if rejected.returncode == 0:
                            raise RuntimeError("corrupted trace was accepted")
                    else:
                        run([str(output)], 150, env)
    print("phase21j: native compiler -Werror, {}, diagnostics, integration, schedules, schedule races and synchronization replay, lock order, deadlock prevention and coherent journal snapshots and cond-wait handoffs and fanout broadcasts and scripted fault rules and 64-actor cycles and invalid scalar matrices and concurrent runtime identities and an independently checked JSON trace CLI passed".format(
        "ASan/UBSan" if sanitize else
        "8x native O2 stress" if stress else "O0/O2"), flush=True)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit("phase21j failed: {}".format(error))
