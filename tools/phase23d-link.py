#!/usr/bin/env python3
"""Link independently compiled SDK-free ARM64 Darwin objects into Mach-O images.

The narrow platform-load-command workaround is required for some Swift-built
LLD distributions; it only edits a *copy* of a validated MH_OBJECT and keeps
linkage and symbol resolutions performed by the real LLVM Mach-O linker.
"""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ARM64 = 0x0100000c
MAGIC = 0xfeedfacf


def run(args):
    cp = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                        timeout=30, check=False)
    if cp.returncode:
        raise RuntimeError("link failed: " + " ".join(map(str, args)) + "\n" + cp.stdout + cp.stderr)


def platform_stripped(obj):
    data = bytearray(obj.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated MH_OBJECT")
    magic, cpu, _, typ, count, nbytes = struct.unpack_from("<6I", data)
    if (magic, cpu, typ) != (MAGIC, ARM64, 1) or nbytes > len(data) - 32:
        raise ValueError("unexpected Mach-O object type")
    pos, end = 32, 32 + nbytes
    for _ in range(count):
        if pos + 8 > end: raise ValueError("invalid Mach-O command header")
        cmd, size = struct.unpack_from("<II", data, pos)
        if size < 8 or size % 8 or pos + size > end:
            raise ValueError("invalid Mach-O load command")
        if cmd in (0x32, 0x24):
            data[pos:end - size] = data[pos + size:end]
            data[end - size:end] = bytes(size)
            struct.pack_into("<II", data, 16, count - 1, nbytes - size)
            adjusted = obj.with_suffix(".link.o")
            adjusted.write_bytes(data)
            return adjusted
        pos += size
    raise ValueError("missing platform build load command")


def link(ld, options, obj, stubs):
    args = [ld, "-arch", "arm64", "-platform_version", "macos", "13.0", "13.0",
            "-fatal_warnings", *options]
    try:
        run([*args, obj, *stubs])
    except RuntimeError as error:
        if "does not support linking for platform macOS" not in str(error):
            raise
        run([*args, platform_stripped(obj), *stubs])


def make_stub(path, name, exports):
    path.write_text("--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n"
                    "install-name: '@rpath/libphase23d_" + name + ".dylib'\n"
                    "current-version: 1.0.0\ncompatibility-version: 1.0.0\n"
                    "exports:\n  - archs: [ arm64 ]\n    symbols: [ " +
                    ", ".join("_" + s for s in exports) + " ]\n...\n", encoding="ascii")


def check(path, filetype, dependencies):
    data = path.read_bytes()
    if len(data) < 32 or len(data) > 4 * 1024 * 1024:
        raise ValueError("Mach-O size invalid")
    magic, cpu, _, typ, ncmds, nbytes = struct.unpack_from("<6I", data)
    if (magic, cpu, typ) != (MAGIC, ARM64, filetype) or ncmds > 96 or nbytes > len(data) - 32:
        raise ValueError("Mach-O header invalid: " + str(path))
    pos, end, libraries, commands = 32, 32 + nbytes, [], []
    for _ in range(ncmds):
        if pos + 8 > end: raise ValueError("truncated load command")
        cmd, size = struct.unpack_from("<II", data, pos)
        if size < 8 or size % 8 or pos + size > end:
            raise ValueError("invalid linked load command")
        commands.append(cmd)
        if cmd in (0xc, 0x18, 0x1f):
            if size < 24: raise ValueError("invalid dylib command")
            offset = struct.unpack_from("<I", data, pos + 8)[0]
            if offset >= size: raise ValueError("invalid dylib name offset")
            raw = data[pos + offset:pos + size].split(b"\x00", 1)[0]
            libraries.append(raw.decode("ascii"))
        pos += size
    if pos != end:
        raise ValueError("load commands have trailing bytes")
    for name in dependencies:
        if "@rpath/libphase23d_" + name + ".dylib" not in libraries:
            raise ValueError("missing explicit dylib dependency: " + name + " in " + str(path))
    if filetype == 6 and 0x80000033 not in commands:
        raise ValueError("linked MH_DYLIB lacks LC_DYLD_EXPORTS_TRIE")
    return libraries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    candidates = ("ld64.lld", "ld64.lld-20", "ld64.lld-19", "ld64.lld-18", "ld64.lld-17")
    ld = next((shutil.which(candidate) for candidate in candidates if shutil.which(candidate)), None)
    if not ld: raise RuntimeError("LLVM ld64.lld required for real Mach-O dylib linking")
    exps = {"echo": ["phase23d_echo"], "sum": ["phase23d_sum"],
            "relay": ["phase23d_relay"]}
    dependencies = {"echo": [], "sum": [], "relay": ["echo"]}
    stubs = {}
    for name in ("echo", "sum", "relay"):
        image = out / ("libphase23d_" + name + ".dylib")
        link(ld, ["-dylib", "-fixup_chains", "-install_name",
                  "@rpath/" + image.name, "-o", image], out / (name + ".o"),
             [stubs[d] for d in dependencies[name]])
        check(image, 6, dependencies[name])
        stub = out / (name + ".tbd")
        make_stub(stub, name, exps[name])
        stubs[name] = stub
    executable = out / "phase23d-client"
    link(ld, ["-e", "_phase23d_start", "-o", executable], out / "client.o",
         [stubs[n] for n in ("echo", "sum", "relay")])
    check(executable, 2, ("echo", "sum", "relay"))
    print("PASS: 3 real linked arm64 Darwin MH_DYLIBs, client MH_EXECUTE, and dependencies")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        print("FAIL:", exc, file=sys.stderr)
        sys.exit(1)
