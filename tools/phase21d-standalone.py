#!/usr/bin/env python3
"""Independent phase21d builder; requires no CMake edits or Apple SDK.

Every subprocess has a hard timeout. Compile two optimization variants and
an authentic Clang arm64-apple-darwin Mach-O object, without emulating an OS.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/libsystem21d_rw_once.c"
FLAGS = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
         "-pthread", "-I" + str(ROOT / "include")]
TESTS = ("stress", "once", "ownership", "timeout", "cancel", "exhaustion", "fault", "integration", "fairness")


def link_flags(name):
    return (["-Wl,--wrap=pthread_cond_init", "-Wl,--wrap=pthread_cond_destroy",
             "-Wl,--wrap=pthread_cond_wait", "-Wl,--wrap=pthread_cond_timedwait",
             "-Wl,--wrap=malloc"] if name == "fault" else [])


def run(args, deadline):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True, timeout=deadline)


def main():
    cc = os.environ.get("CC", "cc")
    clang = os.environ.get("DARWIN_CLANG", "clang")
    if not shutil.which(cc) or not shutil.which(clang):
        raise RuntimeError("native C compiler and Clang are both required")
    with tempfile.TemporaryDirectory(prefix="phase21d-") as directory:
        tmp = Path(directory)
        if os.environ.get("PHASE21D_SKIP_BASE") == "1" and not (
                os.environ.get("PHASE21D_SANITIZE") == "1" or
                os.environ.get("PHASE21D_TSAN") == "1"):
            raise RuntimeError("SKIP_BASE requires a selected sanitizer mode")
        base_levels = () if os.environ.get("PHASE21D_SKIP_BASE") == "1" else (
            "-O0", "-O2")
        for level in base_levels:
            for name in TESTS:
                exe = tmp / (name + level)
                run([cc, *FLAGS, level, SOURCE,
                     ROOT / f"tests/phase21d-{name}.c", *link_flags(name), "-o", exe], 60)
                run([exe], 90)
        if os.environ.get("PHASE21D_SANITIZE") == "1":
            for name in TESTS:
                exe = tmp / (name + "-sanitized")
                run([cc, *FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
                     "-fsanitize=address,undefined", SOURCE,
                     ROOT / f"tests/phase21d-{name}.c", *link_flags(name), "-o", exe], 60)
                run([exe], 100)
        if os.environ.get("PHASE21D_TSAN") == "1":
            for name in TESTS:
                exe = tmp / (name + "-thread-sanitized")
                run([cc, *FLAGS, "-O1", "-g", "-fno-omit-frame-pointer",
                     "-fsanitize=thread", SOURCE,
                     ROOT / f"tests/phase21d-{name}.c", *link_flags(name), "-o", exe], 60)
                run([exe], 100)
        macho = tmp / "phase21d-darwin.o"
        run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-Wall", "-Wextra",
             "-Wpedantic", "-Werror", "-O2", "-I" + str(ROOT / "include"),
             "-c", ROOT / "tests/phase21d-darwin.c", "-o", macho], 60)
        data = macho.read_bytes()
        if len(data) < 32 or struct.unpack_from("<III", data) != (
                0xfeedfacf, 0x0100000c, 0):
            raise RuntimeError("not a genuine Mach-O arm64 object")
        if struct.unpack_from("<I", data, 12)[0] != 1:
            raise RuntimeError("Mach-O is not MH_OBJECT")
        for name in (b"_ios27_21d_rdlock", b"_ios27_21d_wrlock",
                     b"_ios27_21d_unlock", b"_ios27_21d_once",
                     b"_ios27_21d_create", b"_ios27_21d_destroy",
                     b"_ios27_21d_timedrdlock", b"_ios27_21d_timedwrlock"):
            if name not in data:
                raise RuntimeError("missing generated ARM64 Darwin symbol " + repr(name))
        objc = tmp / "phase21d-objc-arm64-darwin.o"
        run([clang, "--target=arm64-apple-darwin",
             "-fobjc-runtime=macosx-14.0", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-Wall", "-Wextra", "-Wpedantic",
             "-Werror", "-O2", "-I" + str(ROOT / "include"), "-c",
             ROOT / "tests/phase21d-objc.m", "-o", objc], 60)
        obj_data = objc.read_bytes()
        if len(obj_data) < 32 or struct.unpack_from("<III", obj_data) != (
                0xfeedfacf, 0x0100000c, 0):
            raise RuntimeError("Objective-C fixture is not genuine Darwin arm64 Mach-O")
        for symbol in (b"_OBJC_CLASS_$_IOS27Phase21DStore",
                       b"_ios27_21d_create", b"_ios27_21d_once",
                       b"_ios27_21d_wrlock", b"_ios27_21d_rdlock"):
            if symbol not in obj_data:
                raise RuntimeError("missing Clang-produced ObjC symbol: " + repr(symbol))
        objcxx = tmp / "phase21d-objcxx-arm64-darwin.o"
        run([clang, "--target=arm64-apple-darwin", "-x", "objective-c++",
             "-std=c++17", "-fobjc-runtime=macosx-14.0", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-Wall", "-Wextra",
             "-Wpedantic", "-Werror", "-O2", "-I" + str(ROOT / "include"),
             "-c", ROOT / "tests/phase21d-objc.m", "-o", objcxx], 60)
        if b"_OBJC_CLASS_$_IOS27Phase21DStore" not in objcxx.read_bytes():
            raise RuntimeError("Objective-C++ fixture lacks Darwin class metadata")
        print("phase21d: native tests and compiler-generated C/Objective-C/Objective-C++ Mach-O verified")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as exc:
        sys.exit(f"phase21d failed: {exc}")
