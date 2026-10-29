#!/usr/bin/env python3
"""Self-contained, bounded phase22a tests. Does not modify shared CMake files."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
CASES = ("native", "faults", "stress", "races", "sandbox")
FLAGS = ("-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-pthread")


def run(argv, seconds=120, env=None):
    print("+", " ".join(map(str, argv)), flush=True)
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, timeout=seconds, check=False, env=env)
    if p.stdout:
        print(p.stdout[-6000:], flush=True)
    if p.returncode:
        raise RuntimeError("exit {} from {}".format(p.returncode, argv[0]))


def main():
    started = time.monotonic()
    compiler = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(compiler) or not clang:
        raise RuntimeError("cc and clang required")
    print("phase22a native host: " + platform.machine(), flush=True)
    with tempfile.TemporaryDirectory(prefix="phase22a-") as d:
        tmp = Path(d)
        sanitizer = os.environ.get("PHASE22A_SANITIZE") == "1"
        builds = (("-O1",),) if sanitizer else (("-O0",), ("-O2",))
        for opt in builds:
            for case in CASES:
                exe = tmp / (case + opt[0])
                sources = [ROOT / "tests" / ("phase22a-" + case + ".c")]
                if case == "sandbox":
                    sources.insert(0, ROOT / "src/phase22a_child.c")
                else:
                    sources.insert(0, ROOT / "src/phase22a_space.c")
                opts = list(opt)
                env = None
                # Seccomp probe must run without sanitizer runtime syscalls.
                if sanitizer and case != "sandbox":
                    opts += ["-g", "-fno-omit-frame-pointer",
                             "-fsanitize=address,undefined"]
                    env = dict(os.environ,
                               ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                               UBSAN_OPTIONS="halt_on_error=1")
                run([compiler, *FLAGS, *opts, "-I" + str(ROOT / "include"),
                     *map(str, sources), "-o", str(exe)], seconds=120)
                run([str(exe)], seconds=180, env=env)
        obj = tmp / "darwin-arm64.o"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-stack-protector", "-fno-builtin", "-Werror", "-O2",
             "-c", str(ROOT / "tests/phase22a-darwin.c"), "-o", str(obj)],
            seconds=60)
        binary = obj.read_bytes()
        if binary[:4] != bytes.fromhex("cffaedfe"):
            raise RuntimeError("ARM64 Darwin Mach-O MH_OBJECT expected")
        for symbol in (b"_ios27_22a_probe\0", b"_ios27_22a_map\0",
                       b"_ios27_22a_protect\0", b"_ios27_22a_unmap\0"):
            if symbol not in binary:
                raise RuntimeError("missing real Mach-O external symbol: " + repr(symbol))
    print("phase22a: bounded native tests passed in {:.2f}s".format(time.monotonic() - started))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as e:
        sys.exit("phase22a failure: {}".format(e))
