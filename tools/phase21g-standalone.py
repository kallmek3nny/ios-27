#!/usr/bin/env python3
"""Self-contained, timeout-bounded phase21g concurrency and Darwin-object checks."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(argv, seconds=90):
    print("+", " ".join(map(str, argv)), flush=True)
    result = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=seconds, check=False)
    if result.returncode:
        raise RuntimeError("command returned {}: {}".format(result.returncode, result.stdout))
    if result.stdout.strip():
        print(result.stdout.strip(), flush=True)


def main():
    cc = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(cc) or clang is None:
        raise RuntimeError("C11 compiler and clang required")
    with tempfile.TemporaryDirectory(prefix="phase21g-") as work:
        out = Path(work)
        flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 "-pthread", "-I" + str(ROOT / "include")]
        for optimize in ("-O0", "-O2"):
            exe = out / ("stress" + optimize)
            run([cc, *flags, optimize,
                 str(ROOT / "src/libsystem21g_cond.c"),
                 str(ROOT / "tests/phase21g-stress.c"), "-o", str(exe)], 120)
            for iteration in range(3):
                print("phase21g", optimize, "iteration", iteration + 1, flush=True)
                run([str(exe)], 90)
        obj = out / "phase21g-darwin-arm64.o"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-std=c11", "-O2",
             "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-c",
             str(ROOT / "tests/phase21g-darwin-arm64.c"), "-o", str(obj)], 60)
        data = obj.read_bytes()
        if not data.startswith(bytes.fromhex("cffaedfe")):
            raise RuntimeError("cross compilation did not generate 64-bit Mach-O")
        for symbol in (b"_pthread_create\x00", b"_pthread_join\x00",
                       b"_pthread_cond_wait\x00", b"_pthread_cond_signal\x00",
                       b"_pthread_cond_timedwait\x00", b"_pthread_cond_broadcast\x00",
                       b"_pthread_mutex_unlock\x00", b"_pthread_mutex_lock\x00"):
            if symbol not in data:
                raise RuntimeError("missing Darwin pthread relocation " + repr(symbol))
        print("phase21g: real clang ARM64 Darwin Mach-O symbol references checked")
    print("phase21g: independent C11 stress suite passed")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        sys.exit("phase21g failed: {}".format(error))
