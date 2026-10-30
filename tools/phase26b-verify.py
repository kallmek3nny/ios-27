#!/usr/bin/env python3
"""Bounded independent phase26b verification, no shared build edits."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(argv, seconds=120):
    print("+", " ".join(map(str, argv)), flush=True)
    result = subprocess.run(argv, text=True, capture_output=True, timeout=seconds,
                            check=False)
    if result.returncode:
        raise RuntimeError("exit {}: {}\n{}".format(
            result.returncode, result.stdout, result.stderr))
    if result.stdout:
        print(result.stdout.strip(), flush=True)


def verify_macho(path):
    data = path.read_bytes()
    if len(data) < 32:
        raise RuntimeError("linked Darwin executable truncated")
    magic, cputype, subtype, filetype = struct.unpack_from("<4I", data)
    if (magic, cputype, filetype) != (0xfeedfacf, 0x0100000c, 2):
        raise RuntimeError("expected real ARM64 Darwin MH_EXECUTE")
    if b"\x01\x10\x00\xd4" not in data:
        raise RuntimeError("missing compiler-produced SVC #0x80 instruction")
    print("phase26b: inspected linked ARM64 Darwin MH_EXECUTE with SVC #0x80; NOT executed")


def main():
    start = time.monotonic()
    clang = shutil.which("clang")
    ld64 = shutil.which("ld64.lld")
    cc = os.environ.get("CC", "cc")
    if not clang or not shutil.which(cc) or not ld64:
        raise RuntimeError("cc, clang and genuine ld64.lld are required")
    flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             "-pthread", "-I" + str(ROOT / "include")]
    with tempfile.TemporaryDirectory(prefix="phase26b-") as tmp:
        scratch = Path(tmp)
        for opt in ("-O0", "-O2"):
            exe = scratch / ("bsd" + opt)
            run([cc, *flags, opt, str(ROOT / "src/phase26b_bsd.c"),
                 str(ROOT / "tests/phase26b-bsd.c"), "-o", str(exe)], 120)
            run([str(exe)], 180)
        if os.environ.get("PHASE26B_SANITIZE") == "1":
            exe = scratch / "sanitized"
            run([clang, *flags, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", str(ROOT / "src/phase26b_bsd.c"),
                 str(ROOT / "tests/phase26b-bsd.c"), "-o", str(exe)], 120)
            run([str(exe)], 180)
        obj = scratch / "guest.o"
        output = scratch / "guest-macho"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-O2", "-Werror",
             "-c", str(ROOT / "tests/phase26b-arm64.c"), "-o", str(obj)], 120)
        run([ld64, "-arch", "arm64", "-platform_version", "macos",
             "13.0", "13.0", "-e", "_main", "-o", str(output), str(obj)], 120)
        verify_macho(output)
    print("phase26b: all checks passed in {:.2f}s".format(time.monotonic() - start))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase26b verification failed: {}".format(exc))
