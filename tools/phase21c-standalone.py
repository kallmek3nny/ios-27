#!/usr/bin/env python3
"""Standalone -Werror multithreaded tests and real ARM64 Darwin object."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def run(argv, seconds=90):
    print("+", " ".join(map(str, argv)), flush=True)
    p = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=seconds, check=False)
    if p.returncode:
        raise RuntimeError("exit {}: {}".format(p.returncode, p.stdout))
    if p.stdout.strip():
        print(p.stdout.strip(), flush=True)
def main():
    cc = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(cc) or not clang:
        raise RuntimeError("cc and clang are required")
    with tempfile.TemporaryDirectory(prefix="phase21c-") as scratch:
        dest = Path(scratch)
        flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 "-pthread", "-I" + str(ROOT / "include")]
        for optimize in ("-O0", "-O2"):
            exe = dest / ("native" + optimize)
            run([cc, *flags, optimize,
                 str(ROOT / "src/libsystem21c_sync.c"),
                 str(ROOT / "tests/phase21c-native.c"),
                 "-o", str(exe)], 120)
            run([str(exe)], 90)
            race = dest / ("races" + optimize)
            run([cc, *flags, optimize,
                 str(ROOT / "src/libsystem21c_sync.c"),
                 str(ROOT / "tests/phase21c-races.c"),
                 "-o", str(race)], 120)
            run([str(race)], 90)
            cancel = dest / ("cancel" + optimize)
            run([cc, *flags, optimize,
                 str(ROOT / "src/libsystem21c_sync.c"),
                 str(ROOT / "tests/phase21c-cancel.c"),
                 "-o", str(cancel)], 120)
            run([str(cancel)], 90)
        obj = dest / "darwin-arm64.o"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-Werror", "-O2",
             "-I" + str(ROOT / "include"), "-c",
             str(ROOT / "tests/phase21c-darwin-abi.c"), "-o",
             str(obj)], 60)
        if obj.read_bytes()[:4] != bytes.fromhex("cffaedfe"):
            raise RuntimeError("Clang did not emit a 64-bit Mach-O object")
        compiled = dest / "darwin-real-pthreads-arm64.o"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-Werror", "-O2",
             "-c", str(ROOT / "tests/phase21c-darwin-threaded.c"),
             "-o", str(compiled)], 60)
        generated = compiled.read_bytes()
        if generated[:4] != bytes.fromhex("cffaedfe"):
            raise RuntimeError("real pthread calls were not compiled as Mach-O")
        for symbol in (b"_pthread_create\x00", b"_pthread_join\x00",
                       b"_pthread_mutex_lock\x00", b"_pthread_mutex_unlock\x00",
                       b"_pthread_cond_wait\x00", b"_pthread_cond_signal\x00",
                       b"_pthread_cond_broadcast\x00"):
            if symbol not in generated:
                raise RuntimeError("missing Darwin pthread relocation symbol: " + repr(symbol))
        print("phase21c: Clang-produced ARM64 Darwin Mach-O with genuine pthread callsites verified")
    print("phase21c: independent stress and compiler checks passed")
if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit("phase21c failure: {}".format(error))
