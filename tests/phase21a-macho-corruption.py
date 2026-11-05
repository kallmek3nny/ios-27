#!/usr/bin/env python3
"""Mutation checks against a *real* Clang ARM64 Darwin Mach-O object."""
import importlib.util
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
PARSER = ROOT / "tools/phase21a-macho-verify.py"
spec = importlib.util.spec_from_file_location("phase21a_macho_parser", PARSER)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def rejected(data):
    try:
        module.verify(data)
    except (ValueError, struct.error):
        return
    raise AssertionError("malformed ARM64 Mach-O was incorrectly accepted")


def replace_u32(data, offset, value):
    result = bytearray(data)
    struct.pack_into("<I", result, offset, value)
    return bytes(result)


def strip_branch_relocations(data):
    result = bytearray(data)
    _, _, _, _, ncmds, sizeofcmds, _, _ = struct.unpack_from("<IiiIIIII", result)
    command = 32
    touched = 0
    for _ in range(ncmds):
        kind, size = struct.unpack_from("<II", result, command)
        if kind == 0x19:
            count = struct.unpack_from("<I", result, command + 64)[0]
            for j in range(count):
                section = command + 72 + j * 80
                reloff, nreloc = struct.unpack_from("<II", result, section + 56)
                for k in range(nreloc):
                    offset = reloff + k * 8 + 4
                    info = struct.unpack_from("<I", result, offset)[0]
                    if (info >> 28) == 2:
                        struct.pack_into("<I", result, offset,
                                         info & 0x0fffffff)
                        touched += 1
        command += size
    if not touched:
        raise AssertionError("real object contained no ARM64 BRANCH26 relocations")
    return bytes(result)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: phase21a-macho-corruption.py compiled-macho.o")
    real = Path(sys.argv[1]).read_bytes()
    assert module.verify(real)[2] >= 1
    variants = [
        real[:31],
        replace_u32(real, 0, 0),
        replace_u32(real, 4, 0),
        replace_u32(real, 12, 2),
        replace_u32(real, 16, 0xffffffff),
        replace_u32(real, 20, 0xffffffff),
        replace_u32(real, 36, 4),
        real.replace(b"_pthread_once", b"_pthread_nonc", 1),
        strip_branch_relocations(real),
    ]
    for variant in variants:
        rejected(variant)
    print("phase21a: 9 malformed genuine ARM64 Mach-O mutations rejected")


if __name__ == "__main__":
    main()
