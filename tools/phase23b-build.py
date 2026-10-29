#!/usr/bin/env python3
"""Compile/link six real SDK-free arm64 Darwin dylibs in a dependency chain.

No binary is executed by this tool. Each command has a hard timeout.
"""
import argparse
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def run(argv):
    proc = subprocess.run(argv, timeout=45, capture_output=True, text=True,
                          check=False)
    if proc.returncode:
        raise RuntimeError(f"{argv}: {proc.stdout}{proc.stderr}")


def strip_platform(path):
    b = bytearray(path.read_bytes())
    if len(b) < 32:
        raise ValueError("short object")
    magic, cpu, _, typ, count, cmdbytes = struct.unpack_from("<6I", b)
    if (magic, cpu, typ) != (0xfeedfacf, 0x0100000c, 1):
        raise ValueError("not a clang arm64 mach-o object")
    off, end = 32, 32 + cmdbytes
    for _ in range(count):
        cmd, sz = struct.unpack_from("<II", b, off)
        if sz < 8 or sz % 8 or off + sz > end:
            raise ValueError("bad object load command")
        if cmd in (0x32, 0x24):
            b[off:end-sz] = b[off+sz:end]
            b[end-sz:end] = bytes(sz)
            struct.pack_into("<II", b, 16, count - 1, cmdbytes - sz)
            fallback = path.with_suffix(".link.o")
            fallback.write_bytes(b)
            return fallback
        off += sz
    raise ValueError("platform command missing")


def link(linker, obj, output, previous):
    base = [linker, "-arch", "arm64", "-platform_version", "macos", "11.0",
            "11.0", "-fatal_warnings", "-no_fixup_chains", "-U", "dyld_stub_binder", "-dylib", "-install_name",
            output.name, "-o", str(output)]
    other = [str(previous)] if previous else []
    try:
        run(base + [str(obj)] + other)
    except RuntimeError as exc:
        if "This version of lld does not support linking for platform macOS" not in str(exc):
            raise
        run(base + [str(strip_platform(obj))] + other)


def inspect(path, needs_dependency):
    data = path.read_bytes()
    if not 32 <= len(data) <= 4 * 1024 * 1024:
        raise ValueError(f"invalid dylib length: {path}")
    magic, cpu, _, filetype, ncmds, cmdbytes = struct.unpack_from("<6I", data)
    if (magic, cpu, filetype) != (0xfeedfacf, 0x0100000c, 6):
        raise ValueError(f"not MH_DYLIB: {path}")
    if ncmds > 64 or 32 + cmdbytes > len(data):
        raise ValueError("bad load commands")
    off, end = 32, 32 + cmdbytes
    sections = set()
    has_dep = False
    for _ in range(ncmds):
        if off + 8 > end:
            raise ValueError("short load command")
        cmd, length = struct.unpack_from("<II", data, off)
        if length < 8 or length % 8 or off + length > end:
            raise ValueError("bad load command")
        if cmd == 0xc:
            has_dep = True
        if cmd == 0x19:
            if length < 72:
                raise ValueError("bad segment")
            nsects, = struct.unpack_from("<I", data, off + 64)
            if 72 + nsects * 80 > length:
                raise ValueError("bad section table")
            for j in range(nsects):
                s = off + 72 + j * 80
                name = data[s:s+16].split(b"\0", 1)[0]
                size, = struct.unpack_from("<Q", data, s + 40)
                if name in (b"__mod_init_func", b"__mod_term_func") and size >= 8:
                    sections.add(name)
        off += length
    if off != end or sections != {b"__mod_init_func", b"__mod_term_func"}:
        raise ValueError(f"missing initializer/terminator table: {path} {sections}")
    if needs_dependency and not has_dep:
        raise ValueError(f"no linked dylib dependency: {path}")
    return len(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--linker", default=None)
    parser.add_argument("--output-dir", default="build/phase23b")
    args = parser.parse_args()
    if args.linker is None:
        # Ubuntu LLVM packages install versioned ld64.lld executables.
        args.linker = next(
            (path for name in ("ld64.lld", "ld64.lld-21", "ld64.lld-20",
                               "ld64.lld-19", "ld64.lld-18", "ld64.lld-17",
                               "ld64.lld-16", "ld64.lld-15")
             if (path := shutil.which(name))), None)
    if args.linker is None:
        raise RuntimeError("ld64.lld not installed (LLVM lld required)")
    dest = pathlib.Path(args.output_dir).resolve()
    dest.mkdir(parents=True, exist_ok=True)
    prior = None
    for i in range(6):
        obj = dest / f"phase23b-lib{i}.o"
        dylib = dest / f"phase23b-lib{i}.dylib"
        run([args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-fPIC", "-O2", "-c",
             str(ROOT / "tests" / f"phase23b-lib{i}.c"), "-o", str(obj)])
        link(args.linker, obj, dylib, prior)
        nbytes = inspect(dylib, prior is not None)
        print(f"phase23b: linked {dylib.name} ({nbytes} bytes), init+term present")
        stub = dest / f"phase23b-lib{i}.tbd"
        stub.write_text(
            "--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n"
            f"install-name: {dylib.name}\ncurrent-version: 1.0.0\n"
            "compatibility-version: 1.0.0\nexports:\n  - archs: [ arm64 ]\n"
            f"    symbols: [ _phase23b_value{i}, _phase23b_shared ]\n...\n",
            encoding="ascii")
        prior = stub


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit(f"phase23b fixtures: {exc}")
