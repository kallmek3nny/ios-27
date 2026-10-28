#!/usr/bin/env python3
"""Standalone native stress and Clang-produced Darwin ARM64 callsite build.
No modifications to CMakeLists.txt, no emulation and no Apple SDK.
All compiler, execution and inspection commands have hard deadlines.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/libsystem20d_threads.c"
TEST = ROOT / "tests/phase20d-native.c"
EDGE = ROOT / "tests/phase20d-edge.c"
DARWIN = ROOT / "tests/phase20d-arm64-abi.c"

def run(args, seconds=60, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    p = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=seconds, env=env, check=False)
    if p.returncode:
        raise RuntimeError(f"exit {p.returncode}:\n{p.stdout}")
    if p.stdout.strip():
        print(p.stdout.strip(), flush=True)

def main():
    cc = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(cc) or not clang:
        raise RuntimeError("native C compiler and clang required")
    with tempfile.TemporaryDirectory(prefix="phase20d-") as scratch:
        td = Path(scratch)
        opts = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                "-pthread", "-I" + str(ROOT / "include")]
        for optimization in ("-O0", "-O2"):
            exe = td / ("native-" + optimization[1:])
            run([cc, *opts, optimization, str(SOURCE), str(TEST), "-o", str(exe)])
            run([str(exe)], 30)
            edge = td / ("edge-" + optimization[1:])
            run([cc, *opts, optimization, str(SOURCE), str(EDGE), "-o", str(edge)])
            run([str(edge)], 30)
        if os.environ.get("PHASE20D_SANITIZE") == "1":
            exe = td / "sanitized"
            run([cc, *opts, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", str(SOURCE), str(TEST),
                 "-o", str(exe)])
            run([str(exe)], 45, dict(os.environ,
                ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1"))
        macho = td / "darwin-arm64.o"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-Werror", "-O2",
             "-I" + str(ROOT / "include"), "-c", str(DARWIN), "-o", str(macho)])
        with macho.open("rb") as source:
            if source.read(4) != bytes.fromhex("cffaedfe"):
                raise RuntimeError("Clang failed to emit genuine 64-bit Mach-O object")
        print("phase20d: real Clang ARM64 Darwin Mach-O callsites verified")
    print("phase20d: all standalone milestones passed")

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(f"phase20d failed: {error}")
