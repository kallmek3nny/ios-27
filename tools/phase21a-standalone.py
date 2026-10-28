#!/usr/bin/env python3
"""Independent bounded phase21a host and Darwin compiler verification."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src/libsystem21a_abi.c"

def run(args, seconds=60):
    print("+", " ".join(map(str, args)), flush=True)
    result = subprocess.run(args, capture_output=True, text=True, timeout=seconds, check=False)
    if result.returncode:
        raise RuntimeError("exit %d:\n%s\n%s" % (result.returncode, result.stdout, result.stderr))
    if result.stdout.strip():
        print(result.stdout.strip())

def main():
    cc = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(cc) or not clang:
        raise RuntimeError("host C compiler and clang required")
    flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             "-pthread", "-I" + str(ROOT / "include")]
    with tempfile.TemporaryDirectory(prefix="phase21a-") as d:
        out = Path(d)
        for optimization in ("-O0", "-O2"):
            for name in ("phase21a-native.c", "phase21a-boundaries.c", "phase21a-regions.c", "phase21a-conditions.c", "phase21a-once.c"):
                executable = out / (name + optimization)
                run([cc, *flags, optimization, str(SRC), str(ROOT / "tests" / name),
                     "-o", str(executable)])
                run([str(executable)])
        if os.environ.get("PHASE21A_SANITIZE") == "1":
            for name in ("phase21a-native.c", "phase21a-boundaries.c", "phase21a-regions.c", "phase21a-conditions.c", "phase21a-once.c"):
                executable = out / (name + "-sanitized")
                run([cc, *flags, "-O1", "-g", "-fno-omit-frame-pointer",
                     "-fsanitize=address,undefined", str(SRC),
                     str(ROOT / "tests" / name), "-o", str(executable)])
                run([str(executable)])
        object_file = out / "phase21a-darwin.o"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-std=c11",
             "-Wall", "-Wextra", "-Werror", "-O2",
             "-I" + str(ROOT / "include"), "-c",
             str(ROOT / "tests/phase21a-arm64-darwin.c"), "-o", str(object_file)])
        run([sys.executable, str(ROOT / "tools/phase21a-macho-verify.py"),
             str(object_file)])
    print("phase21a: all native and compiler-produced Darwin checks passed")

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase21a: " + str(exc))
