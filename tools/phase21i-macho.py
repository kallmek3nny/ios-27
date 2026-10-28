#!/usr/bin/env python3
"""Inspect Clang ARM64 Darwin MH_OBJECT symbol + external branch relocations."""
import struct
import sys
import random
from pathlib import Path

EXPECTED = {
    "_pthread_mutex_init", "_pthread_mutex_destroy", "_pthread_mutex_lock",
    "_pthread_mutex_trylock", "_pthread_mutex_unlock", "_pthread_cond_init",
    "_pthread_cond_destroy", "_pthread_cond_wait", "_pthread_cond_timedwait",
    "_pthread_cond_signal", "_pthread_cond_broadcast",
}
ATTR_EXPECTED = {
    "_pthread_mutexattr_init", "_pthread_mutexattr_settype",
    "_pthread_mutexattr_destroy", "_pthread_condattr_init",
    "_pthread_condattr_destroy",
}
THREAD_EXPECTED = {
    "_pthread_create", "_pthread_join", "_pthread_mutex_lock",
    "_pthread_mutex_unlock", "_pthread_cond_wait",
    "_pthread_cond_signal", "_pthread_cond_broadcast",
}

class MachOError(ValueError):
    pass


def inspect(data, expected=EXPECTED):
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
        try:
            names.append(strings[strx:terminator].decode("ascii"))
        except UnicodeDecodeError as exc:
            raise MachOError("non-ascii symbol name") from exc
    for index, _pcrel, _length, external, _kind in relocations:
        if external and index >= len(names):
            raise MachOError("out of range external relocation symbol")
    branches = {names[index] for index, pcrel, length, external, kind in relocations
                if external and kind == 2 and pcrel == 1 and length == 2
                and (types[index] & 0x0f) == 1}
    if not expected <= branches:
        raise MachOError("missing external ARM64_RELOC_BRANCH26: " +
                         repr(sorted(expected - branches)))
    return len(branches), len(relocations)


def selftest(original, expected=EXPECTED):
    count, total = inspect(original, expected)
    assert count >= len(expected) and total >= len(expected)
    for broken in (b"", original[:12], b"\0" + original[1:],
                   original[:20] + b"\xff\xff\xff\xff" + original[24:],
                   original[:16] + b"\xff\xff\xff\xff" + original[20:]):
        try:
            inspect(broken, expected)
        except MachOError:
            pass
        else:
            raise AssertionError("corrupt Mach-O accepted")
    # Corrupt real compiler relocations: invalid offset and symbol reference.
    cursor = 32
    found_reloc = False
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
                            inspect(bytes(broken), expected)
                        except MachOError:
                            pass
                        else:
                            raise AssertionError("bad relocation accepted")
                    found_reloc = True
                    break
            if found_reloc:
                break
        cursor += size
    if not found_reloc:
        raise AssertionError("no relocations discovered")
    # Mutate real Clang output with a deterministic campaign. A mutated object
    # may remain valid, but must not cause an unhandled parser exception.
    rng = random.Random(21)
    for trial in range(1500):
        broken = bytearray(original)
        for _ in range(1 + trial % 4):
            at = rng.randrange(len(broken))
            broken[at] = rng.randrange(256)
        try:
            inspect(bytes(broken), expected)
        except MachOError:
            continue
    print(f"phase21i: {count} external ARM64 branch targets, {total} relocations; malformed inputs + 1500 bounded mutations checked")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--threads":
        expected, pathname = THREAD_EXPECTED, sys.argv[2]
    elif len(sys.argv) == 3 and sys.argv[1] == "--attrs":
        expected, pathname = ATTR_EXPECTED, sys.argv[2]
    elif len(sys.argv) == 2:
        expected, pathname = EXPECTED, sys.argv[1]
    else:
        sys.exit("usage: phase21i-macho.py [--threads|--attrs] clang-macho-object")
    try:
        selftest(Path(pathname).read_bytes(), expected)
    except (OSError, MachOError, AssertionError) as exc:
        sys.exit(f"phase21i Mach-O verification failed: {exc}")
