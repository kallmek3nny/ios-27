#!/usr/bin/env python3
"""Narrow, deterministic ARM64 MH_OBJECT -> MH_EXECUTE relocatable fixture linker.

Not a general-purpose Darwin linker. Strictly accepts two original-source
Clang objects with a single external BRANCH26 relocation and zero imports.
"""
import pathlib
import struct
import sys

CPU_ARM64 = 0x0100000C
MAGIC = 0xFEEDFACF
VM = 0x100000000

def need(test, message):
    if not test:
        raise ValueError(message)

def read_object(path):
    data = path.read_bytes()
    need(32 <= len(data) <= 1024 * 1024, "object file size")
    magic, cpu, subtype, kind, ncmds, cmdbytes, flags, reserved = (
        struct.unpack_from("<8I", data))
    need((magic, cpu, subtype & 255, kind) ==
         (MAGIC, CPU_ARM64, 0, 1) and ncmds <= 64 and
         32 + cmdbytes <= len(data), "unsupported Clang Mach-O object")
    pos = 32
    section = None
    symtab = None
    platform = False
    for _ in range(ncmds):
        need(pos + 8 <= 32 + cmdbytes, "truncated object command")
        cmd, size = struct.unpack_from("<II", data, pos)
        need(size >= 8 and size % 8 == 0 and
             pos + size <= 32 + cmdbytes, "malformed load command")
        if cmd == 0x19:
            need(size == 152 and section is None, "expected one text section")
            need(struct.unpack_from("<I", data, pos + 64)[0] == 1,
                 "unsupported section count")
            s = pos + 72
            need(data[s:s + 16].rstrip(b"\0") == b"__text" and
                 data[s + 16:s + 32].rstrip(b"\0") == b"__TEXT",
                 "unexpected section kind")
            addr, length, offset, align, reloff, nreloc, attrs = (
                struct.unpack_from("<QQIIIII", data, s + 32))
            need(0 < length <= 4096 and align == 2 and
                 offset + length <= len(data) and
                 reloff + nreloc * 8 <= len(data) and
                 nreloc <= 1 and attrs & 0x80000000, "bad text section")
            section = (addr, bytes(data[offset:offset + length]),
                       [struct.unpack_from("<II", data, reloff + 8 * j)
                        for j in range(nreloc)])
        elif cmd == 2:
            need(size == 24 and symtab is None, "bad symtab command")
            symtab = struct.unpack_from("<4I", data, pos + 8)
        elif cmd == 0x32:
            need(size >= 24 and struct.unpack_from("<I", data, pos + 8)[0] == 1,
                 "not macOS Clang object")
            platform = True
        elif cmd != 0x0b:
            raise ValueError("unsupported object load command")
        pos += size
    need(pos == 32 + cmdbytes and platform and symtab and section,
         "missing Clang Darwin object metadata")
    symoff, nsyms, stroff, strsize = symtab
    need(nsyms <= 64 and symoff + 16 * nsyms <= len(data) and
         strsize and stroff + strsize <= len(data), "invalid object symbols")
    symbols = []
    for i in range(nsyms):
        index, kind, sect, desc, value = struct.unpack_from(
            "<IBBHQ", data, symoff + 16 * i)
        need(index < strsize, "symbol name outside table")
        raw = data[stroff + index:stroff + strsize]
        need(b"\0" in raw, "unterminated symbol name")
        symbols.append((raw.split(b"\0", 1)[0], kind, sect, value))
    return section, symbols

def defined(symbols, name):
    matching = [s for s in symbols if s[0] == name and
                (s[1] & 0x0e) == 0x0e and s[2] == 1]
    need(len(matching) == 1, "missing/duplicate defined " + repr(name))
    return matching[0][3]

def align(number, boundary):
    return (number + boundary - 1) // boundary * boundary

def padded_name(name):
    return name.ljust(16, b"\0")

def segment(name, vmaddr, vmsize, fileoff, filesize, prot, section_bytes=b""):
    return struct.pack("<II16sQQQQiiII", 0x19, 72 + len(section_bytes),
                       padded_name(name), vmaddr, vmsize, fileoff, filesize,
                       prot, prot, len(section_bytes) // 80, 0) + section_bytes

def link(main_obj, helper_obj, outfile):
    (m_addr, m_code, m_reloc), m_symbols = read_object(main_obj)
    (h_addr, h_code, h_reloc), h_symbols = read_object(helper_obj)
    need(len(m_reloc) == 1 and not h_reloc, "expected precisely one BRANCH26")
    main_value = defined(m_symbols, b"_main")
    helper_value = defined(h_symbols, b"_phase15a_helper")
    need(m_addr <= main_value < m_addr + len(m_code) and
         h_addr <= helper_value < h_addr + len(h_code),
         "defined symbol outside text")
    other_undefined = [s[0] for s in m_symbols + h_symbols
                       if (s[1] & 0x0e) == 0 and s[0] and
                       s[0] != b"_phase15a_helper"]
    need(not other_undefined, "unexpected unresolved symbols")
    offset, bits = m_reloc[0]
    reloc_index = bits & 0xffffff
    need(offset < len(m_code) and offset % 4 == 0 and
         len(m_code) - offset >= 4 and
         reloc_index < len(m_symbols) and
         m_symbols[reloc_index][0] == b"_phase15a_helper" and
         (bits >> 24) & 1 == 1 and (bits >> 25) & 3 == 2 and
         (bits >> 27) & 1 == 1 and bits >> 28 == 2,
         "invalid ARM64 BRANCH26 relocation")
    need(struct.unpack_from("<I", m_code, offset)[0] == 0x94000000,
         "unsupported branch encoding")
    helper_start = align(len(m_code), 4)
    caller_code = bytearray(m_code + bytes(helper_start - len(m_code)) + h_code)
    destination = helper_start + helper_value - h_addr
    delta = destination - offset
    need(delta % 4 == 0 and -(1 << 27) <= delta < (1 << 27),
         "ARM64 BL displacement out of bounds")
    struct.pack_into("<I", caller_code, offset,
                     0x94000000 | ((delta // 4) & 0x3ffffff))
    entry_in_text = main_value - m_addr

    # All fixed-output metadata is explicitly encoded, not disguised as an
    # MH_OBJECT or handcrafted imitation of Apple SDK binaries.
    loadcmd_sizes = (72, 152, 72, 16, 24, 80, 32, 24, 24)
    command_size = sum(loadcmd_sizes)
    textoff = align(32 + command_size, 16)
    need(textoff + len(caller_code) <= 4096, "fixture exceeds text page")
    fixups = struct.pack("<7I", 0, 28, 28, 28, 0, 0, 0)
    symoff = 4096 + align(len(fixups), 8)
    strings = b"\0_main\0_phase15a_helper\0"
    stroff = symoff + 32
    linkedit_len = align(stroff + len(strings) - 4096, 16)
    text_section = struct.pack(
        "<16s16sQQIIIIIIII", padded_name(b"__text"), padded_name(b"__TEXT"),
        VM + textoff, len(caller_code), textoff, 2, 0, 0, 0x80000400,
        0, 0, 0)
    commands = [
        segment(b"__PAGEZERO", 0, VM, 0, 0, 0),
        segment(b"__TEXT", VM, 4096, 0, 4096, 5, text_section),
        segment(b"__LINKEDIT", VM + 4096, 4096, 4096, linkedit_len, 1),
        struct.pack("<4I", 0x80000034, 16, 4096, len(fixups)),
        struct.pack("<6I", 2, 24, symoff, 2, stroff, len(strings)),
        struct.pack("<20I", 0x0b, 80, 0, 0, 0, 2, 2, 0, *([0] * 12)),
        struct.pack("<3I", 0x0e, 32, 12) + b"/usr/lib/dyld\0".ljust(20, b"\0"),
        struct.pack("<6I", 0x32, 24, 1, 0xd0000, 0xd0000, 0),
        struct.pack("<IIQQ", 0x80000028, 24, textoff + entry_in_text, 0)
    ]
    need(sum(map(len, commands)) == command_size, "bad output load commands")
    header = struct.pack("<8I", MAGIC, CPU_ARM64, 0, 2, len(commands),
                         command_size, 0x200085, 0)
    result = bytearray(4096 + linkedit_len)
    result[:32] = header
    result[32:32 + command_size] = b"".join(commands)
    result[textoff:textoff + len(caller_code)] = caller_code
    result[4096:4096 + len(fixups)] = fixups
    result[symoff:symoff + 16] = struct.pack(
        "<IBBHQ", 1, 0x0f, 1, 0, VM + textoff + entry_in_text)
    result[symoff + 16:symoff + 32] = struct.pack(
        "<IBBHQ", 7, 0x0f, 1, 0, VM + textoff + destination)
    result[stroff:stroff + len(strings)] = strings
    outfile.write_bytes(result)

if __name__ == "__main__":
    try:
        if len(sys.argv) != 4:
            raise ValueError("usage: phase15a-link-fallback.py main.o helper.o out")
        link(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]),
             pathlib.Path(sys.argv[3]))
        print("phase15a: linked narrow SDK-free ARM64 MH_EXECUTE fallback")
    except (OSError, ValueError, struct.error) as exc:
        sys.exit("phase15a fallback linker: " + str(exc))
