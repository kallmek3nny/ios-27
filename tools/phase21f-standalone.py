#!/usr/bin/env python3
"""Strict independent native mutex stress and SDK-free Mach-O compiler check."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def run(args, limit=120):
    print("+", " ".join(map(str, args)), flush=True)
    result = subprocess.run(args, capture_output=True, text=True, timeout=limit,
                            check=False)
    if result.returncode:
        raise RuntimeError("exit {}: {} {}".format(result.returncode,
                           result.stdout[-5000:], result.stderr[-5000:]))
    if result.stdout.strip():
        print(result.stdout.strip(), flush=True)

def main():
    cc = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(cc) or not clang:
        raise RuntimeError("native cc and clang are required")
    with tempfile.TemporaryDirectory(prefix="phase21f-") as folder:
        tmp = Path(folder)
        flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 "-pthread", "-I" + str(ROOT / "include")]
        for opt in ("-O0", "-O2"):
            for test in ("functional", "stress", "adversarial", "randomized", "isolation"):
                exe = tmp / (test + opt)
                run([cc, *flags, opt, str(ROOT / "src/libsystem21f_mutex.c"),
                     str(ROOT / ("tests/phase21f-" + test + ".c")),
                     "-o", str(exe)], 120)
                run([str(exe)], 120)
        for opt in ("-O0", "-O2"):
            exe = tmp / ("exhaustion" + opt)
            run([cc, *flags, opt, "-DIOS27_21F_MAX_WAITERS=16",
                 str(ROOT / "src/libsystem21f_mutex.c"),
                 str(ROOT / "tests/phase21f-exhaustion.c"),
                 "-o", str(exe)], 120)
            run([str(exe)], 120)
        for target in ("arm64-apple-darwin", "x86_64-apple-darwin"):
            obj = tmp / (target + ".o")
            run([clang, "--target=" + target, "-ffreestanding", "-fno-builtin",
                 "-fno-stack-protector", "-std=c11", "-Wall", "-Wextra",
                 "-Wpedantic", "-Werror", "-O2", "-c",
                 str(ROOT / "tests/phase21f-darwin-calls.c"), "-o", str(obj)], 60)
            data = obj.read_bytes()
            if data[:4] != bytes.fromhex("cffaedfe"):
                raise RuntimeError(target + ": expected 64-bit Mach-O magic")
            for symbol in (b"_pthread_mutex_init\0", b"_pthread_mutex_lock\0",
                           b"_pthread_mutex_trylock\0", b"_pthread_mutex_unlock\0",
                           b"_pthread_mutex_destroy\0", b"_pthread_create\0",
                           b"_pthread_join\0"):
                if symbol not in data:
                    raise RuntimeError(target + ": missing symbol " + repr(symbol))
            print("phase21f: compiled genuine " + target + " pthread callsites")
    print("phase21f: all independent -Werror checks passed")

if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase21f failure: " + str(exc))
