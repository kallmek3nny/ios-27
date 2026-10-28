#!/usr/bin/env python3
"""Validate real compiler-produced arm64 Darwin MH_OBJECT symbol relocations.

This deliberately parses actual Mach-O load commands, nlist_64 and relocation
records. Merely finding a pthread name in object bytes is not ABI evidence.
"""
import struct
import sys
from pathlib import Path

MH_MAGIC_64 = 0xfeedfacf
CPU_TYPE_ARM64 = 0x0100000c
MH_OBJECT = 1
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 2
ARM64_RELOC_BRANCH26 = 2
REQUIRED = (
    b"_pthread_mutex_init",
    b"_pthread_mutex_lock",
    b"_pthread_mutex_unlock",
    b"_pthread_cond_signal",
    b"_pthread_rwlock_wrlock",
    b"_pthread_once",
    b"_pthread_attr_init",
    b"_pthread_attr_getstacksize",
    b"_pthread_mutexattr_settype",
    b"_pthread_condattr_init",
    b"_pthread_rwlockattr_init",
    b"_pthread_key_create",
    b"_pthread_setspecific",
    b"_pthread_getspecific",
    b"_pthread_self",
    b"_pthread_equal",
)


def verify(data: bytes) -> tuple[int, int, int]:
    if len(data) < 32:
        raise ValueError("truncated mach_header_64")
    magic, cpu, subtype, filetype, ncmds, sizeofcmds, flags, reserved = \
        struct.unpack_from("<IiiIIIII", data)
    if magic != MH_MAGIC_64 or cpu != CPU_TYPE_ARM64 or filetype != MH_OBJECT:
        raise ValueError("expected arm64 Darwin MH_OBJECT")
    if ncmds == 0 or ncmds > 128 or sizeofcmds > len(data) - 32:
        raise ValueError("invalid Mach-O command count or size")
    offset, end = 32, 32 + sizeofcmds
    symtab = None
    relocs = []
    for _ in range(ncmds):
        if offset + 8 > end:
            raise ValueError("load command truncated")
        cmd, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > end:
            raise ValueError("invalid load command size")
        if cmd == LC_SYMTAB:
            if size < 24 or symtab is not None:
                raise ValueError("malformed or duplicate symtab")
            symtab = struct.unpack_from("<IIII", data, offset + 8)
        if cmd == LC_SEGMENT_64:
            if size < 72:
                raise ValueError("truncated segment_command_64")
            nsects = struct.unpack_from("<I", data, offset + 64)[0]
            if nsects > 128 or 72 + nsects * 80 > size:
                raise ValueError("malformed sections")
            for j in range(nsects):
                section = offset + 72 + j * 80
                reloff, nreloc = struct.unpack_from("<II", data, section + 56)
                if nreloc > 100000 or reloff > len(data) or nreloc * 8 > len(data) - reloff:
                    raise ValueError("relocation table extends past file")
                relocs.extend(range(reloff, reloff + nreloc * 8, 8))
        offset += size
    if offset != end or symtab is None:
        raise ValueError("missing symtab or malformed load command table")
    symoff, nsyms, stroff, strsize = symtab
    if not nsyms or nsyms > 100000 or symoff > len(data) or nsyms * 16 > len(data) - symoff:
        raise ValueError("invalid nlist_64 table")
    if stroff > len(data) or strsize > len(data) - stroff:
        raise ValueError("invalid string table")
    strings = data[stroff:stroff + strsize]
    names = []
    undef = set()
    for index in range(nsyms):
        string_index, typ, section, description, value = struct.unpack_from(
            "<IBBHQ", data, symoff + index * 16)
        if string_index >= len(strings):
            raise ValueError("symbol name out of string table")
        terminator = strings.find(b"\x00", string_index)
        if terminator < 0:
            raise ValueError("unterminated symbol name")
        name = strings[string_index:terminator]
        names.append(name)
        if typ & 0x01 and (typ & 0x0e) == 0:
            undef.add(name)
    missing = set(REQUIRED) - undef
    if missing:
        raise ValueError("not undefined external pthread imports: " +
                         ", ".join(x.decode() for x in sorted(missing)))
    called = set()
    for reloc in relocs:
        addr, info = struct.unpack_from("<iI", data, reloc)
        symbolnum = info & 0x00ffffff
        extern = (info >> 27) & 1
        kind = (info >> 28) & 15
        if extern and kind == ARM64_RELOC_BRANCH26:
            if symbolnum >= nsyms:
                raise ValueError("invalid external relocation symbol index")
            called.add(names[symbolnum])
    if not (called & set(REQUIRED)):
        raise ValueError("no real ARM64_RELOC_BRANCH26 to pthread import")
    return len(undef), len(relocs), len(called & set(REQUIRED))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: phase21a-macho-verify.py compiled-arm64-macho.o")
    result = verify(Path(sys.argv[1]).read_bytes())
    print("phase21a: verified arm64 Mach-O nlist imports and external BRANCH26 "
          "relocations (undefined=%d, relocations=%d, direct-pthread-calls=%d)" % result)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, struct.error) as exc:
        sys.exit("phase21a macho validation failed: " + str(exc))
