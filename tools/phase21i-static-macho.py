#!/usr/bin/env python3
"""Audit genuine Clang ARM64 Darwin static pthread initializer data."""
import runpy
import struct
import sys
from pathlib import Path

_validator = runpy.run_path(str(Path(__file__).with_name("phase21i-macho.py")))
inspect = _validator["inspect"]
MachOError = _validator["MachOError"]
EXPECTED_IMPORTS = {
    "_pthread_mutex_lock", "_pthread_mutex_trylock", "_pthread_mutex_unlock",
    "_pthread_mutex_destroy", "_pthread_cond_wait", "_pthread_cond_signal",
    "_pthread_cond_broadcast", "_pthread_cond_destroy",
}
PATTERNS = {
    "normal": (0x32AAABA7, 64),
    "recursive": (0x32AAABA2, 64),
    "errorcheck": (0x32AAABA1, 64),
    "firstfit": (0x32AAABA3, 64),
    "condition": (0x3CB0B1BB, 48),
}


def get_bytes(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise MachOError("out of bounds section data")
    return data[offset:offset + size]


def find_data(data):
    if len(data) < 32:
        raise MachOError("short Mach-O header")
    cmd_count, cmd_bytes = struct.unpack("<II", get_bytes(data, 16, 8))
    if cmd_count > cmd_bytes // 8:
        raise MachOError("invalid command count")
    end = 32 + cmd_bytes
    get_bytes(data, 32, cmd_bytes)
    pos = 32
    sections = []
    for _ in range(cmd_count):
        cmd, size = struct.unpack("<II", get_bytes(data, pos, 8))
        if size < 8 or pos + size > end:
            raise MachOError("bad load command")
        if cmd == 0x19:
            if size < 72:
                raise MachOError("short segment command")
            nsects = struct.unpack("<I", get_bytes(data, pos + 64, 4))[0]
            if nsects > (size - 72) // 80:
                raise MachOError("invalid section count")
            for i in range(nsects):
                section = pos + 72 + 80 * i
                name = get_bytes(data, section, 16).split(b"\0", 1)[0]
                size_bytes = struct.unpack("<Q", get_bytes(data, section+40, 8))[0]
                file_offset = struct.unpack("<I", get_bytes(data, section+48, 4))[0]
                flags = struct.unpack("<I", get_bytes(data, section+64, 4))[0]
                if name in (b"__data", b"__const", b"__rodata"):
                    if (flags & 0xff) != 1:
                        section_bytes = get_bytes(data, file_offset, size_bytes)
                        sections.append((file_offset, section_bytes))
        pos += size
    if pos != end or not sections:
        raise MachOError("missing well-formed data sections")
    return sections


def inspect_initializers(data):
    branches, relocations = inspect(data, EXPECTED_IMPORTS)
    locations = {}
    for name, (signature, extent) in PATTERNS.items():
        pattern = struct.pack("<Q", signature) + bytes(extent-8)
        candidates = [
            offset+index
            for offset, section in find_data(data)
            for index in range(0, len(section) - len(pattern) + 1, 8)
            if section[index:index+len(pattern)] == pattern
        ]
        if len(candidates) != 1:
            raise MachOError(f"expected exactly one {name} published LP64 initializer, got {len(candidates)}")
        locations[name] = candidates[0]
    return branches, relocations, locations


def selftest(data):
    branches, relocations, locations = inspect_initializers(data)
    for name, offset in locations.items():
        broken = bytearray(data)
        broken[offset] ^= 0x01
        try:
            inspect_initializers(bytes(broken))
        except MachOError:
            pass
        else:
            raise AssertionError(f"damaged {name} signature accepted")
    print(f"phase21i: {len(locations)} exact Clang-compiled Apple LP64 static initializer byte patterns, "
          f"{branches} external ARM64 imports, {relocations} relocations, corrupt signatures rejected")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: phase21i-static-macho.py arm64-darwin-object")
    try:
        selftest(Path(sys.argv[1]).read_bytes())
    except (OSError, MachOError, AssertionError) as exc:
        sys.exit(f"phase21i static Mach-O validation failed: {exc}")
