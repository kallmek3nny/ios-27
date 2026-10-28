#!/usr/bin/env python3
"""Independent phase21b lifecycle stress; hard bounds on every subprocess."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src/libsystem21b_lifecycle.c"
TEST = ROOT / "tests/phase21b-native.c"
DARWIN = ROOT / "tests/phase21b-darwin-arm64.c"

def run(args, timeout=120):
    print("+", " ".join(str(x) for x in args), flush=True)
    r = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout, check=False)
    if r.stdout.strip():
        print(r.stdout.strip(), flush=True)
    if r.returncode:
        raise RuntimeError(f"command exit {r.returncode}")
    return r.stdout

def main():
    cc = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(cc) or not clang:
        raise RuntimeError("native C compiler and Clang required")
    with tempfile.TemporaryDirectory(prefix="phase21b-") as scratch:
        p = Path(scratch)
        args = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-pthread",
                "-I" + str(ROOT / "include")]
        for opt in ("-O0", "-O2"):
            exe = p / ("lifecycle-" + opt[1:])
            run([cc, *args, opt, str(SRC), str(TEST), "-o", str(exe)], 120)
            run([str(exe)], 90)
        if os.environ.get("PHASE21B_SANITIZE") == "1":
            exe = p / "sanitized"
            run([cc, *args, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", str(SRC), str(TEST),
                 "-o", str(exe)], 120)
            run([str(exe)], 120)
        obj = p / "arm64-darwin.o"
        run([clang, "-target", "arm64-apple-darwin", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-O2", "-I" + str(ROOT / "include"), "-c", str(DARWIN), "-o", str(obj)], 120)
        if obj.read_bytes()[:4] != bytes.fromhex("cffaedfe"):
            raise RuntimeError("expected a real 64-bit Mach-O object")
        disasm = shutil.which("llvm-objdump")
        if disasm:
            inspect = run([disasm, "--macho", "--syms", "--reloc", str(obj)], 30)
            for symbol in ("_ios27_21b_thread_create", "_ios27_21b_thread_join",
                           "_ios27_21b_thread_self"):
                if symbol not in inspect:
                    raise RuntimeError(f"missing ARM64 Darwin external relocation {symbol}")
        print("phase21b: genuine Clang ARM64 Mach-O external ABI callsites emitted")

if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
        sys.exit(f"phase21b failed: {exc}")
