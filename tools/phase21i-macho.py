#!/usr/bin/env python3
"""Inspect Clang ARM64 Darwin MH_OBJECT symbol + external branch relocations."""
import struct
import sys
from pathlib import Path

EXPECTED = {
    "_pthread_mutex_init", "_pthread_mutex_destroy", "_pthread_mutex_lock",
    "_pthread_mutex_trylock", "_pthread_mutex_unlock", "_pthread_cond_init",
    "_pthread_cond_destroy", "_pthread_cond_wait", "_pthread_cond_timedwait",
    "_pthread_cond_signal", "_pthread_cond_broadcast",
}

class MachOError(ValueError):
    pass


def inspect(data):
    def take(offset, size):
        if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
            raise MachOError("out of bounds Mach-O field")
        return data[offset:offset + size]

    if len(data) < 32:
        raise MachOError("short Mach-O header")
    magic, cpu, _sub, filetype, ncmds, sizeofcmds, _flags, _reserved = struct.unpack(
        "<IiiIIIII", take(0, 32))
    if magic != 0xfeedfacf or cpu != 0x0100000c or filetype != 1:
        raise MachOError("not ARM64 Darwin MH_OBJECT")
    take(32, sizeofcmds)
    cursor, stop, symtab, relocations = 32, 32 + sizeofcmds, None, []
    if ncmds > sizeofcmds // 8:
        raise MachOError("command count exceeds command bytes")
    for _ in range(ncmds):
        cmd, size = struct.unpack("<II", take(cursor, 8))
        if size < 8 or cursor + size > stop:
            raise MachOError("invalid load command")
        if cmd == 2:
            if size != 24 or symtab is not None:
                raise MachOError("invalid symtab command")
            symtab = struct.unpack("<IIII", take(cursor + 8, 16))
        elif cmd == 0x19:
            if size < 72:
                raise MachOError("short segment")
            nsects = struct.unpack("<I", take(cursor + 64, 4))[0]
            if nsects > (size - 72) // 80:
                raise MachOError("section overflow")
            for j in range(nsects):
                sec = cursor + 72 + 80 * j
                section_size = struct.unpack("<Q", take(sec + 40, 8))[0]
                reloff, nreloc = struct.unpack("<II", take(sec + 56, 8))
                if nreloc > (len(data) - min(reloff, len(data))) // 8:
                    raise MachOError("relocation overflow")
                for k in range(nreloc):
                    address, info = struct.unpack("<iI", take(reloff + k * 8, 8))
                    if address < 0 or address % 4 or address + 4 > section_size:
                        raise MachOError("invalid relocation offset")
                    relocations.append((info & 0xffffff, (info >> 24) & 1,
                                        (info >> 25) & 3, (info >> 27) & 1,
                                        info >> 28))
        cursor += size
    if cursor != stop or symtab is None:
        raise MachOError("bad load-command table or missing symtab")
    symoff, nsyms, stroff, strsize = symtab
    if nsyms > (len(data) - min(symoff, len(data))) // 16:
        raise MachOError("symbol overflow")
    strings = take(stroff, strsize)
    names, types = [], []
    for i in range(nsyms):
        strx = struct.unpack("<I", take(symoff + 16 * i, 4))[0]
        types.append(take(symoff + 16 * i + 4, 1)[0])
        if strx >= strsize:
            raise MachOError("bad string index")
        terminator = strings.find(b"\0", strx)
        if terminator == -1:
            raise MachOError("unterminated symbol")
        names.append(strings[strx:terminator].decode("ascii"))
    for index, _pcrel, _length, external, _kind in relocations:
        if external and index >= len(names):
            raise MachOError("out of range external relocation symbol")
    branches = {names[index] for index, pcrel, length, external, kind in relocations
                if external and kind == 2 and pcrel == 1 and length == 2
                and (types[index] & 0x0f) == 1}
    if not EXPECTED <= branches:
        raise MachOError("missing external ARM64_RELOC_BRANCH26: " +
                         repr(sorted(EXPECTED - branches)))
    return len(branches), len(relocations)


def selftest(original):
    count, total = inspect(original)
    assert count >= len(EXPECTED) and total >= len(EXPECTED)
    for broken in (b"", original[:12], b"\0" + original[1:],
                   original[:20] + b"\xff\xff\xff\xff" + original[24:],
                   original[:16] + b"\xff\xff\xff\xff" + original[20:]):
        try:
            inspect(broken)
        except MachOError:
            pass
        else:
            raise AssertionError("corrupt Mach-O accepted")
    # Corrupt a real compiler-emitted relocation: bad location or symbol ref.
    cursor = 32
    while cursor < len(original):
        cmd, size = struct.unpack("<II", original[cursor:cursor+8])
        if cmd == 0x19:
            nsects = struct.unpack("<I", original[cursor+64:cursor+68])[0]
            for i in range(nsects):
                sec = cursor + 72 + 80*i
                reloff, nreloc = struct.unpack("<II", original[sec+56:sec+64])
                if nreloc:
                    for mutate_at, raw in ((reloff, b"\\xff\\xff\\xff\\x7f"),
                                           (reloff+4, b"\\xff\\xff\\xff\\xff")):
                        broken = bytearray(original)
                        broken[mutate_at:mutate_at+4] = raw
                        try:
                            inspect(bytes(broken))
                        except MachOError:
                            pass
                        else:
                            raise AssertionError("bad relocation accepted")
                    print(f"phase21i: {count} external ARM64 branch targets, {total} relocations; malformed inputs rejected")
                    return
        cursor += size
    raise AssertionError("no relocations discovered")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: phase21i-macho.py path-to-clang-macho-object")
    try:
        selftest(Path(sys.argv[1]).read_bytes())
    except (OSError, MachOError, AssertionError) as exc:
        sys.exit(f"phase21i Mach-O verification failed: {exc}")
