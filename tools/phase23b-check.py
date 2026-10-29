#!/usr/bin/env python3
"""Independent, bounded native test driver; no shared CMake edits."""
import os
import pathlib
import platform
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "phase23b"


def run(args, seconds):
    print("phase23b check:", " ".join(map(str, args)), flush=True)
    subprocess.run(args, check=True, timeout=seconds, cwd=ROOT)


def main():
    start = time.monotonic()
    BUILD.mkdir(parents=True, exist_ok=True)
    run([sys.executable, "tools/phase23b-build.py", "--output-dir", str(BUILD)], 120)
    flags = ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O1", "-g",
             "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-pthread",
             "-Iinclude", "-fno-omit-frame-pointer"]
    san = os.getenv("PHASE23B_SANITIZE") == "1"
    if san:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    exe = BUILD / ("phase23b-native-san" if san else "phase23b-native")
    run([os.getenv("CC", "cc"), *flags, "src/phase23b_lifecycle.c",
         "tests/phase23b-native.c", "-o", str(exe)], 120)
    files = [str(BUILD / f"phase23b-lib{i}.dylib") for i in range(6)]
    run([str(exe), *files], 180)
    print(f"phase23b: architecture={platform.machine()} sanitize={san} "
          f"active_driver_elapsed_seconds={time.monotonic() - start:.2f}")
    print("phase23b: no ARM64 Darwin constructor executed as native guest code")


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.SubprocessError) as exc:
        sys.exit(f"phase23b checks failed: {exc}")
