#!/usr/bin/env python3
"""Build a genuine SDK-free arm64 Mach-O executable using LLVM's ld64.lld."""
import argparse
import pathlib
import struct
import subprocess
import sys

MAGIC_64 = 0xFEEDFACF
MH_OBJECT = 1
LC_BUILD_VERSION = 0x32
LC_VERSION_MIN_MACOSX = 0x24

def run(command):
    completed = subprocess.run(command, text=True, capture_output=True,
                               check=False, timeout=25)
    if completed.returncode:
        raise RuntimeError("command failed: " + repr(command) + "\n" +
                           completed.stdout + completed.stderr)

def remove_object_platform_command(path):
    data = bytearray(path.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated object")
    magic, _, _, kind, count, size = struct.unpack_from("<6I", data, 0)
    if magic != MAGIC_64 or kind != MH_OBJECT or 32 + size > len(data):
        raise ValueError("not a bounded, Clang-produced Mach-O object")
    cursor = 32
    end = cursor + size
    found = False
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("invalid load command")
        opcode, length = struct.unpack_from("<II", data, cursor)
        if length < 8 or length % 8 or cursor + length > end:
            raise ValueError("invalid load command length")
        if opcode in (LC_BUILD_VERSION, LC_VERSION_MIN_MACOSX):
            data[cursor:end - length] = data[cursor + length:end]
            data[end - length:end] = bytes(length)
            struct.pack_into("<II", data, 16, count - 1, size - length)
            found = True
            break
        cursor += length
    if not found:
        raise ValueError("missing original platform metadata")
    rewritten = path.with_suffix(".link.o")
    rewritten.write_bytes(data)
    return rewritten

def main():
    parser = argparse.ArgumentParser()
    for argument in ("clang", "linker", "main", "helper", "out"):
        parser.add_argument("--" + argument, required=True)
    options = parser.parse_args()
    output = pathlib.Path(options.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    sources = (pathlib.Path(options.main), pathlib.Path(options.helper))
    objects = [output.with_name(output.name + "-" + str(i) + ".o")
               for i in range(len(sources))]
    for src, obj in zip(sources, objects):
        run([options.clang, "--target=arm64-apple-darwin",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-O2", "-c", str(src), "-o", str(obj)])
    common = [options.linker, "-arch", "arm64",
              "-platform_version", "macos", "13.0", "13.0",
              "-e", "_main", "-fatal_warnings", "-o", str(output)]
    original = subprocess.run(common + [str(obj) for obj in objects],
                              capture_output=True, text=True, timeout=25,
                              check=False)
    if original.returncode:
        combined = original.stdout + original.stderr
        # Swift's patched LLD artificially rejects inputs with platform
        # metadata. Only this known issue permits the deterministic retry.
        if "This version of lld does not support linking for platform" not in combined:
            raise RuntimeError("Darwin linker failed: " + combined)
        adjusted = [remove_object_platform_command(obj) for obj in objects]
        run(common + [str(obj) for obj in adjusted])
        print("phase15a: Swift LLD platform-metadata workaround applied")
    if not output.is_file() or output.stat().st_size < 32:
        raise RuntimeError("linker failed to create executable")
    data = output.read_bytes()
    if struct.unpack_from("<I", data, 0)[0] != MAGIC_64 or (
            struct.unpack_from("<I", data, 12)[0] != 2):
        raise RuntimeError("linker did not produce real MH_EXECUTE")
    print("phase15a: genuine ARM64 MH_EXECUTE created:", output)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase15a: " + str(exc))
