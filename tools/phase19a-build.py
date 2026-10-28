#!/usr/bin/env python3
"""Reproducible, SDK-free, Clang+LLD ARM64 MH_EXECUTE integration fixture.

No Apple binaries, SDK, host system libraries, downloads, or emulation.
Called by the existing compiled-darwin-real-mh-execute CTest, so this does
not need CMake ownership. Exit 77 only when the host lacks a linker/compiler.
"""
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/fixtures/phase19a-process.c"
MAGIC = 0xFEEDFACF


def command(args):
    result = subprocess.run(args, capture_output=True, text=True,
                            check=False, timeout=25)
    if result.returncode:
        raise RuntimeError(f"{args[0]} failed: {result.stdout}{result.stderr}")
    return result


def no_platform_command(path):
    """Handle LLVM/Swift ld64.lld builds which reject macOS platform metadata."""
    data = bytearray(path.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated Clang object")
    magic, cpu, sub, kind, count, size, flags, reserved = struct.unpack_from(
        "<8I", data)
    if (magic, cpu, sub & 255, kind) != (MAGIC, 0x0100000C, 0, 1):
        raise ValueError("invalid Clang-produced Darwin object")
    pos, end = 32, 32 + size
    if end > len(data):
        raise ValueError("invalid object commands")
    for _ in range(count):
        op, length = struct.unpack_from("<II", data, pos)
        if length < 8 or length % 8 or pos + length > end:
            raise ValueError("malformed load command")
        if op in (0x32, 0x24):
            data[pos:end - length] = data[pos + length:end]
            data[end - length:end] = bytes(length)
            struct.pack_into("<II", data, 16, count - 1, size - length)
            adjusted = path.with_suffix(".link.o")
            adjusted.write_bytes(data)
            return adjusted
        pos += length
    raise ValueError("missing platform command")


def inspect(path):
    data = path.read_bytes()
    if len(data) < 32 or struct.unpack_from("<I", data)[0] != MAGIC:
        raise ValueError("not a real Mach-O")
    if struct.unpack_from("<I", data, 12)[0] != 2:
        raise ValueError("not MH_EXECUTE")
    count, load_bytes = struct.unpack_from("<II", data, 16)
    if 32 + load_bytes > len(data):
        raise ValueError("truncated Mach-O commands")
    p = 32
    segments = {}
    has_main = False
    for _ in range(count):
        op, length = struct.unpack_from("<II", data, p)
        if length < 8 or length % 8 or p + length > 32 + load_bytes:
            raise ValueError("invalid linked command")
        if op == 0x19:
            name = data[p + 8:p + 24].split(b"\0", 1)[0]
            vm, vm_size, off, file_size = struct.unpack_from("<4Q", data, p + 24)
            maxprot, initprot = struct.unpack_from("<II", data, p + 56)
            segments[name] = (vm, vm_size, off, file_size, maxprot, initprot)
            if off + file_size > len(data):
                raise ValueError("truncated linked segment")
        if op == 0x80000028:
            has_main = True
        p += length
    if p != 32 + load_bytes or not has_main:
        raise ValueError("missing linked LC_MAIN")
    for name in (b"__TEXT", b"__DATA_CONST", b"__DATA", b"__LINKEDIT"):
        if name not in segments:
            raise ValueError(f"missing required segment {name!r}")
    # 16 KiB host pages cannot safely share an RX page with RW data.
    mapped = [segments[name] for name in
              (b"__TEXT", b"__DATA_CONST", b"__DATA", b"__LINKEDIT")]
    ranges = [(s[0] & ~16383, (s[0] + s[1] + 16383) & ~16383)
              for s in mapped]
    if any(a[1] > b[0] and b[1] > a[0]
           for i, a in enumerate(ranges) for b in ranges[i + 1:]):
        raise ValueError("not compatible with 16 KiB host pages")
    if segments[b"__DATA_CONST"][5] != 1 or segments[b"__DATA"][5] != 3:
        raise ValueError("linked fixture lost RO/RW segment protection")
    if segments[b"__TEXT"][5] != 5:
        raise ValueError("linked fixture missing RX segment")
    return data


def main():
    if len(sys.argv) != 2:
        raise ValueError("usage: phase19a-build.py OUTPUT")
    output = pathlib.Path(sys.argv[1])
    output.unlink(missing_ok=True)
    compiler = next((shutil.which(name) for name in
                     ("clang", "clang-20", "clang-19", "clang-18", "clang-17")
                     if shutil.which(name)), None)
    linker = shutil.which("ld64.lld")
    if not compiler:
        print("phase19a: clang unavailable")
        return 77
    if not linker:
        print("phase19a: advanced original-source fixture requires ld64.lld")
        return 77
    obj = output.with_suffix(".o")
    command([compiler, "--target=arm64-apple-darwin",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-O2", "-c", str(SOURCE), "-o", str(obj)])
    common = [linker, "-arch", "arm64", "-platform_version", "macos",
              "13.0", "13.0", "-e", "_main", "-fatal_warnings",
              "-segprot", "__DATA_CONST", "r--", "r--",
              "-o", str(output)]
    try:
        command(common + [str(obj)])
    except RuntimeError as exc:
        if "This version of lld does not support linking for platform" not in str(exc):
            raise
        command(common + [str(no_platform_command(obj))])
    inspect(output)
    print(f"phase19a: compiler-linked advanced startup fixture: {output}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, struct.error,
            subprocess.TimeoutExpired) as exc:
        sys.exit(f"phase19a: {exc}")
