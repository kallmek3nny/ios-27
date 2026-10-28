#!/usr/bin/env python3
"""Mutate real compiler-produced Mach-O objects to test fail-closed validation."""
import argparse
import importlib.util
from pathlib import Path
import struct
import sys

MODULE = Path(__file__).with_name("phase21f-macho-check.py")
spec = importlib.util.spec_from_file_location("phase21f_macho_check", MODULE)
if spec is None or spec.loader is None:
    sys.exit("unable to load phase21f Mach-O checker")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

def mutate(source, start, replacement):
    value = bytearray(source)
    value[start:start + len(replacement)] = replacement
    return bytes(value)

def find_symtab(source):
    ncmds = struct.unpack_from("<I", source, 16)[0]
    pos = 32
    for _ in range(ncmds):
        kind, size = struct.unpack_from("<II", source, pos)
        if kind == 2:
            return pos
        pos += size
    raise ValueError("fixture lacks symtab")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("arch", choices=sorted(checker.CPU))
    parser.add_argument("file", type=Path)
    args = parser.parse_args()
    raw = args.file.read_bytes()
    checker.check(raw, args.arch)
    symtab = find_symtab(raw)
    bad = [
        raw[:20],
        mutate(raw, 0, b"BAD!"),
        mutate(raw, 4, struct.pack("<I", 0)),
        mutate(raw, 12, struct.pack("<I", 2)),
        mutate(raw, 16, struct.pack("<I", 0)),
        mutate(raw, 36, struct.pack("<I", 0)),
        mutate(raw, symtab + 8, struct.pack("<I", len(raw) + 1)),
        mutate(raw, symtab + 16, struct.pack("<I", len(raw) + 1)),
        mutate(raw, symtab + 20, struct.pack("<I", len(raw) + 1)),
        raw.replace(b"_pthread_mutex_lock\0", b"_pthread_mutex_l0ck\0"),
        raw[:-16],
    ]
    for index, damaged in enumerate(bad):
        if damaged == raw:
            raise RuntimeError("mutation {} unexpectedly unchanged".format(index))
        try:
            checker.check(damaged, args.arch)
        except (ValueError, struct.error):
            continue
        raise RuntimeError("malformed Mach-O mutation {} was accepted".format(index))
    print("phase21f Mach-O {}: {} malformed real-object mutations rejected".format(
        args.arch, len(bad)))

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, struct.error) as exc:
        sys.exit("phase21f malformed Mach-O check failed: " + str(exc))
