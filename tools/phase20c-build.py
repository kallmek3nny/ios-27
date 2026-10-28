#!/usr/bin/env python3
"""Standalone phase20c test runner. Every subprocess is time-bounded.

Genuine ARM64 Darwin ObjC2 MH_OBJECT (not executed on non-Darwin hosts),
plus native GNU-legacy Clang-generated Objective-C message execution.
No external ObjC runtime, Apple SDK, emulation, or project CMake edits.
"""
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(args, seconds=90, env=None):
    result = subprocess.run(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            check=False, timeout=seconds, env=env)
    if result.returncode:
        raise RuntimeError(f"{args[0]} exit {result.returncode}:\n{result.stdout}")
    if result.stdout.strip():
        print(result.stdout.strip(), flush=True)


def verify_macho(path):
    data = path.read_bytes()
    if len(data) < 32 or struct.unpack_from("<4I", data) != (
            0xfeedfacf, 0x0100000c, 0, 1):
        raise RuntimeError("not an ARM64 Darwin MH_OBJECT")
    for symbol in (b"__objc_classlist", b"__objc_methname", b"Ledger",
                   b"Extended", b"objc_msgSend", b"phase20c_store_child"):
        if symbol not in data:
            raise RuntimeError(f"missing genuine Objective-C marker {symbol!r}")
    print(f"validated compiled arm64 darwin objc2 object ({len(data)} bytes)",
          flush=True)


def main():
    clang = shutil.which(os.getenv("CLANG", "clang"))
    cc = shutil.which(os.getenv("CC", "cc"))
    if not clang or not cc:
        raise RuntimeError("native C compiler and Clang required")
    include = "-I" + str(ROOT / "include")
    source = ROOT / "src/objc20c_lifetime.c"
    fixture = ROOT / "tests/phase20c-compiled.m"
    lifecycle = ROOT / "tests/phase20c-lifecycle.c"
    host = ROOT / "tests/phase20c-compiled-host.c"
    common = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", include]
    with tempfile.TemporaryDirectory(prefix="ios27-phase20c-") as temporary:
        build = pathlib.Path(temporary)
        darwin = build / "objc-darwin.o"
        run([clang, "--target=arm64-apple-darwin", "-x", "objective-c",
             "-fobjc-runtime=macosx-13.0", "-fno-objc-arc",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             "-Wall", "-Wextra", "-Werror", "-O1", "-c",
             str(fixture), "-o", str(darwin)])
        verify_macho(darwin)
        for opt in ("-O0", "-O2"):
            suffix = opt[1:]
            executable = build / ("lifecycle-" + suffix)
            run([cc, *common, opt, str(source), str(lifecycle),
                 "-o", str(executable)])
            run([str(executable)], seconds=45)
            object_file = build / ("objc-" + suffix + ".o")
            run([clang, "-x", "objective-c", "-fobjc-runtime=gcc",
                 "-fno-objc-arc", "-Wall", "-Wextra", "-Werror", opt,
                 "-c", str(fixture), "-o", str(object_file)])
            executable = build / ("objc-host-" + suffix)
            run([cc, *common, opt, str(source), str(host),
                 str(object_file), "-o", str(executable)])
            run([str(executable)], seconds=45)
        if os.environ.get("PHASE20C_SANITIZE") == "1":
            env = dict(os.environ,
                       ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                       UBSAN_OPTIONS="halt_on_error=1")
            exe = build / "sanitized"
            run([cc, *common, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", str(source), str(lifecycle),
                 "-o", str(exe)])
            run([str(exe)], seconds=60, env=env)
    print("phase20c: all standalone native and compile-only tests passed")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as err:
        sys.exit(f"phase20c failed: {err}")
