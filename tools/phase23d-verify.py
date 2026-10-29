#!/usr/bin/env python3
"""Standalone, bounded phase23d build, sanitizer stress, and Darwin object check."""
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SANITIZE = os.environ.get("PHASE23D_SANITIZE") == "1"


def run(args, seconds=30, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    return subprocess.run([str(a) for a in args], check=True, timeout=seconds, **kwargs)


def native_check(where):
    clang = shutil.which("clang")
    if not clang:
        raise RuntimeError("clang required for arm64 Darwin Mach-O production")
    objects = []
    for name in ("client", "echo", "sum", "relay"):
        obj = where / (name + ".o")
        run([clang, "--target=arm64-apple-macos13", "-std=c11", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
             "-O2", "-c", ROOT / "tests" / ("phase23d-arm64-" + name + ".c"),
             "-o", obj])
        header = obj.read_bytes()
        if len(header) < 32 or struct.unpack_from("<IIII", header) != (
                0xfeedfacf, 0x0100000c, 0, 1):
            raise RuntimeError(f"{obj}: not ARM64 Mach-O MH_OBJECT")
        objects.append(obj)
    print("PASS: 4 compiler-produced ARM64 Darwin Mach-O objects validated")
    linker = shutil.which("ld64.lld")
    if not linker:
        print("UNVERIFIED: no Darwin linker; dylib/client linking not performed")
        return
    # This linker is optional: some distribution builds expressly disable
    # Darwin linking. Never silently call those objects linked dylibs.
    try:
        libs = []
        for name in ("echo", "sum", "relay"):
            dylib = where / ("libphase23d_" + name + ".dylib")
            cmd = [linker, "-arch", "arm64", "-platform_version", "macos",
                   "13.0", "13.0", "-dylib", "-install_name",
                   "@rpath/" + dylib.name, "-o", dylib]
            if name == "relay":
                cmd += ["-L", where, "-lphase23d_echo"]
            run(cmd + [where / (name + ".o")], seconds=30,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            libs.append(dylib)
        exe = where / "phase23d-client"
        run([linker, "-arch", "arm64", "-platform_version", "macos",
             "13.0", "13.0", "-e", "_phase23d_start", "-L", where,
             "-lphase23d_echo", "-lphase23d_sum", "-lphase23d_relay",
             "-o", exe, where / "client.o"], seconds=30,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for name, filetype in [(lib, 6) for lib in libs] + [(exe, 2)]:
            hdr = name.read_bytes()
            if len(hdr) < 32 or struct.unpack_from("<I", hdr, 0)[0] != 0xfeedfacf or \
               struct.unpack_from("<I", hdr, 12)[0] != filetype:
                raise RuntimeError(f"bad Mach-O type in {name}")
        print("PASS: 3 arm64 Darwin dylibs and 1 arm64 Darwin executable linked")
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or b"").decode(errors="replace").strip()
        if "does not support linking for platform" in detail:
            print("UNVERIFIED: installed ld64.lld does not support Darwin linking:", detail)
        else:
            raise


def main():
    cc = shutil.which("clang") if SANITIZE else (shutil.which("cc") or shutil.which("clang"))
    if not cc:
        raise RuntimeError("native C compiler not found")
    with tempfile.TemporaryDirectory(prefix="phase23d-") as directory:
        where = pathlib.Path(directory)
        binary = where / "integration"
        flags = ["-std=c11", "-g", "-O1" if SANITIZE else "-O2",
                 "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-pthread",
                 "-I" + str(ROOT / "include")]
        if SANITIZE:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        run([cc, *flags, ROOT / "src/phase23d_bridge.c",
             ROOT / "tests/phase23d-integration.c", "-o", binary], seconds=60)
        run([binary], seconds=180)
        native_check(where)
    print("PASS: phase23d standalone verification; sanitizers=", SANITIZE)


if __name__ == "__main__":
    try:
        main()
    except (subprocess.SubprocessError, OSError, RuntimeError) as error:
        print("FAIL:", error, file=sys.stderr)
        sys.exit(1)
