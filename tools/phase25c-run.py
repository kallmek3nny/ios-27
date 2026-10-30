#!/usr/bin/env python3
"""Phase25c isolated build/test driver. Every subprocess has a hard timeout."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build-phase25c"
BUILD.mkdir(exist_ok=True)

def run(argv, seconds=120, env=None):
    print("+", " ".join(map(str, argv)), flush=True)
    subprocess.run(list(map(str, argv)), check=True, cwd=ROOT,
                   timeout=seconds, env=env)

def native(cc, sanitizer=False):
    target = BUILD / ("test-asan-ubsan" if sanitizer else "test-native")
    flags = ["-std=c11", "-O1" if sanitizer else "-O2",
             "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-pthread",
             "-Iinclude"]
    if sanitizer:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    run([cc, *flags, "src/phase25c_lifetime.c", "tests/phase25c-lifetime.c",
         "-o", target], 300)
    env = dict(os.environ)
    if sanitizer:
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    run([target], 180, env=env)

def darwin(clang):
    objc = ROOT / "tests/phase25c-arc-darwin.m"
    flags = ["--target=arm64-apple-darwin", "-fobjc-runtime=macosx-11.0",
             "-fobjc-arc", "-fno-objc-exceptions", "-fno-exceptions",
             "-O1", "-Wall", "-Wextra", "-Werror"]
    obj = BUILD / "phase25c-arc-arm64-darwin.o"
    assembly = BUILD / "phase25c-arc-arm64-darwin.s"
    run([clang, *flags, "-c", objc, "-o", obj], 120)
    run([clang, *flags, "-S", objc, "-o", assembly], 120)
    text = assembly.read_text()
    required = ["objc_retain", "objc_release", "objc_storeStrong",
                "objc_storeWeak", "objc_loadWeakRetained",
                "objc_destroyWeak", "objc_autoreleasePoolPush",
                "objc_autoreleasePoolPop", "objc_msgSend",
                "objc_autoreleaseReturnValue"]
    absent = [name for name in required if "_"+name not in text]
    if absent:
        raise RuntimeError("missing real Clang ARC ABI references: " + ", ".join(absent))
    print("darwin: compiler-produced ARM64 Mach-O object and ARC references:",
          ", ".join(required), flush=True)
    # Link a real Mach-O dylib on macOS where a supported native ld64 exists.
    # The dylib is NOT executed; guest binder and msgSend are owned by 25a/b.
    if platform.system() == "Darwin":
        output = BUILD / "phase25c-arc-arm64-darwin.dylib"
        run([clang, "--target=arm64-apple-darwin", "-dynamiclib", "-nostdlib",
             "-Wl,-undefined,dynamic_lookup", obj, "-o", output], 120)
        print("darwin: real arm64 compiler-linked dylib", output, flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--clang", default=shutil.which("clang") or "clang")
    parser.add_argument("--skip-sanitizers", action="store_true")
    args = parser.parse_args()
    native(args.cc)
    if not args.skip_sanitizers:
        native(args.clang, sanitizer=True)
    darwin(args.clang)
    print("phase25c checks passed (native Darwin runtime execution unverified)")

if __name__ == "__main__":
    try:
        main()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired,
            FileNotFoundError, RuntimeError) as exc:
        print("phase25c check failed:", exc, file=sys.stderr)
        sys.exit(1)
