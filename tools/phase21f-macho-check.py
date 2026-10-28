#!/usr/bin/env python3
"""Validate genuine Darwin target relocations in Clang-emitted Mach-O objects.

Does not execute or emulate Apple binaries. All file offsets are checked.
"""
import argparse
from pathlib import Path
import struct
import sys

CPU = {"arm64": 0x0100000C, "x86_64": 0x01000007}
REQUIRED = {
    "_pthread_mutex_init", "_pthread_mutex_lock", "_pthread_mutex_trylock",
    "_pthread_mutex_unlock", "_pthread_mutex_destroy", "_pthread_create",
    "_pthread_join",
}

def check(data, architecture):
    if len(data) < 32:
        raise ValueError("truncated Mach-O header")
    magic, cpu, _, file_type, ncmds, sizecmds, _, _ = struct.unpack_from("<8I", data)
    if magic != 0xFEEDFACF or cpu != CPU[architecture] or file_type != 1:
        raise ValueError("incorrect Darwin 64-bit Mach-O object architecture or type")
    end_cmds = 32 + sizecmds
    if end_cmds > len(data) or ncmds > sizecmds // 8:
        raise ValueError("malformed Mach-O load-command bounds")
    pos = 32
    symbols = None
    sections = []
    for _ in range(ncmds):
        if pos + 8 > end_cmds:
            raise ValueError("truncated Mach-O load command")
        kind, size = struct.unpack_from("<II", data, pos)
        if size < 8 or pos + size > end_cmds:
            raise ValueError("invalid Mach-O load command size")
        if kind == 2:
            if size < 24 or symbols is not None:
                raise ValueError("invalid Mach-O symtab command")
            symbols = struct.unpack_from("<IIII", data, pos + 8)
        elif kind == 0x19:
            if size < 72:
                raise ValueError("truncated Mach-O segment command")
            count = struct.unpack_from("<I", data, pos + 64)[0]
            if count > (size - 72) // 80:
                raise ValueError("segment section array outside load command")
            for i in range(count):
                off = pos + 72 + i * 80
                reloff, nreloc = struct.unpack_from("<II", data, off + 56)
                if nreloc and (reloff > len(data) or nreloc > (len(data) - reloff) // 8):
                    raise ValueError("section relocations outside object")
                sections.append((reloff, nreloc))
        pos += size
    if pos != end_cmds or symbols is None or not sections:
        raise ValueError("missing Mach-O load commands, sections or symbol table")
    symoff, nsyms, stroff, strsize = symbols
    if symoff > len(data) or nsyms > (len(data) - symoff) // 16:
        raise ValueError("Mach-O symbol table outside object")
    if stroff > len(data) or strsize > len(data) - stroff:
        raise ValueError("Mach-O string table outside object")
    strings = data[stroff:stroff + strsize]
    external = {}
    for i in range(nsyms):
        strx, typ, _, _, _ = struct.unpack_from("<IBBHQ", data, symoff + i * 16)
        if strx >= len(strings):
            raise ValueError("symbol name exceeds string table")
        terminator = strings.find(b"\0", strx)
        if terminator == -1:
            raise ValueError("unterminated Mach-O symbol name")
        name = strings[strx:terminator].decode("ascii", errors="replace")
        if (typ & 0x0F) == 0x01:
            external[i] = name
    referenced = set()
    for offset, nrelocs in sections:
        for i in range(nrelocs):
            _, info = struct.unpack_from("<iI", data, offset + i * 8)
            if info & (1 << 27):
                symbol_index = info & 0xFFFFFF
                if symbol_index >= nsyms:
                    raise ValueError("relocation refers outside symbol table")
                referenced.add(external.get(symbol_index, ""))
    missing = REQUIRED - referenced
    if missing:
        raise ValueError("missing external pthread call relocations: " + ", ".join(sorted(missing)))
    return len(REQUIRED), sum(n for _, n in sections)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("arch", choices=sorted(CPU))
    parser.add_argument("file", type=Path)
    args = parser.parse_args()
    count, total = check(args.file.read_bytes(), args.arch)
    print("phase21f Mach-O {}: {} native Darwin pthread relocations verified among {} relocation records".format(
        args.arch, count, total))

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, struct.error) as error:
        sys.exit("phase21f Mach-O validation failed: " + str(error))
