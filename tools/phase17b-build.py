#!/usr/bin/env python3
"""Create two genuine, SDK-free, linked arm64 Darwin MH_DYLIBs.

Uses Clang-built MH_OBJECTs and ld64.lld (plus a minimal TAPI link stub
for deterministic imported symbols). No Apple SDK, libraries, or binaries.
Every subprocess has an explicit hard timeout.
"""
import argparse
import pathlib
import struct
import subprocess
import sys

MAGIC = 0xFEEDFACF
ARM64 = 0x0100000C
BUILD_VERSION = 0x32
VERSION_MIN = 0x24


def run(args):
    result = subprocess.run(args, capture_output=True, text=True,
                            timeout=30, check=False)
    if result.returncode:
        raise RuntimeError("failed: " + repr(args) + "\n" +
                           result.stdout + result.stderr)


def strip_object_platform_command(path):
    """Narrow Swift-LDD workaround: remove only an MH_OBJECT platform cmd."""
    data = bytearray(path.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated Clang object")
    magic, cpu, _, typ, count, cmdbytes = struct.unpack_from("<6I", data)
    if (magic, cpu, typ) != (MAGIC, ARM64, 1) or cmdbytes > len(data) - 32:
        raise ValueError("not a Clang-produced ARM64 MH_OBJECT")
    end = 32 + cmdbytes
    cursor = 32
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("truncated load command")
        cmd, size = struct.unpack_from("<II", data, cursor)
        if size < 8 or size % 8 or cursor + size > end:
            raise ValueError("invalid load command")
        if cmd in (BUILD_VERSION, VERSION_MIN):
            data[cursor:end - size] = data[cursor + size:end]
            data[end - size:end] = bytes(size)
            struct.pack_into("<II", data, 16, count - 1, cmdbytes - size)
            result = path.with_suffix(".link.o")
            result.write_bytes(data)
            return result
        cursor += size
    raise ValueError("no Clang platform command to strip")


def link(linker, options, obj, *extra):
    args = [linker, "-arch", "arm64", "-platform_version",
            "macos", "13.0", "13.0", "-fatal_warnings"] + options
    command = args + [str(obj)] + [str(x) for x in extra]
    try:
        run(command)
    except RuntimeError as err:
        if "This version of lld does not support linking for platform macOS" not in str(err):
            raise
        # Some Swift-bundled LLD builds reject original object platform
        # records. Adjust link input only, never substitute synthetic code.
        run(args + [str(strip_object_platform_command(obj))] +
            [str(x) for x in extra])


def check_dylib(path):
    data = path.read_bytes()
    if len(data) < 32 or len(data) > 4 * 1024 * 1024:
        raise ValueError("linked dylib has invalid length")
    magic, cpu, subtype, typ, count, size = struct.unpack_from("<6I", data)
    if (magic, cpu, subtype & 255, typ) != (MAGIC, ARM64, 0, 6):
        raise ValueError("linker did not produce ARM64 MH_DYLIB")
    if count > 64 or size > len(data) - 32:
        raise ValueError("invalid dylib load commands")
    cursor = 32
    commands = []
    for _ in range(count):
        if cursor + 8 > 32 + size:
            raise ValueError("truncated command")
        cmd, length = struct.unpack_from("<II", data, cursor)
        if length < 8 or length % 8 or cursor + length > 32 + size:
            raise ValueError("invalid command length")
        commands.append(cmd)
        cursor += length
    if cursor != 32 + size or 0x80000034 not in commands or 0x80000033 not in commands:
        raise ValueError("missing linked chained fixups or exports trie")
    return commands


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--linker", default="ld64.lld")
    parser.add_argument("--output-dir", default="build/phase17b")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    output = pathlib.Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    src = root / "tests" / "fixtures"
    objects = []
    for stem in ("provider", "consumer"):
        obj = output / ("phase17b-" + stem + ".o")
        run([args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-fPIC", "-O2", "-c",
             str(src / ("phase17b-" + stem + ".c")), "-o", str(obj)])
        objects.append(obj)
    provider = output / "phase17b-provider.dylib"
    consumer = output / "phase17b-consumer.dylib"
    link(args.linker, ["-dylib", "-install_name", provider.name,
                       "-o", str(provider)], objects[0])
    # TAPI stub preserves the real LC_LOAD_DYLIB library ordinal without
    # loading the linked provider's LC_BUILD_VERSION in Swift's LLD build.
    stub = output / "phase17b-provider.tbd"
    stub.write_text(
        "--- !tapi-tbd-v3\n"
        "archs: [ arm64 ]\n"
        "platform: macosx\n"
        "install-name: phase17b-provider.dylib\n"
        "current-version: 1.0.0\n"
        "compatibility-version: 1.0.0\n"
        "exports:\n"
        "  - archs: [ arm64 ]\n"
        "    symbols: [ _phase17b_answer ]\n"
        "...\n", encoding="ascii")
    link(args.linker, ["-dylib", "-install_name", consumer.name,
                       "-o", str(consumer)], objects[1], stub)
    provider_cmds = check_dylib(provider)
    consumer_cmds = check_dylib(consumer)
    if 0xcu not in consumer_cmds:
        raise ValueError("consumer lacks genuine LC_LOAD_DYLIB dependency")
    print("phase17b: genuine Clang/LLD ARM64 dylibs: " +
          str(provider) + " and " + str(consumer))
    print("phase17b: provider commands=" + str(len(provider_cmds)) +
          ", consumer commands=" + str(len(consumer_cmds)))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase17b: " + str(exc))
