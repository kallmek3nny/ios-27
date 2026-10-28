#!/usr/bin/env python3
"""Phase 19a narrow SDK-free ARM64 Mach-O source linker.

Consumes *real* Clang-produced MH_OBJECT bytes for phase19a-process.c.
Only five specified sections and ARM64 BRANCH26/PAGE21/PAGEOFF12/ADDEND
relocations are accepted. No generic linker, dyld, Apple SDK, or emulation.
Everything is bounds-checked before publishing the linked MH_EXECUTE.
"""
import pathlib
import struct
import sys

MAGIC = 0xFEEDFACF
VM = 0x100000000
PAGE = 0x4000
NAMES = (b"__text", b"__const", b"__data", b"__cstring", b"__bss")
SEGMENTS = (b"__TEXT", b"__DATA_CONST", b"__DATA", b"__TEXT", b"__DATA")


def need(ok, message):
    if not ok:
        raise ValueError(message)


def align(n, granule):
    return (n + granule - 1) // granule * granule


def name16(name):
    return name.ljust(16, b"\0")


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def inspect_object(path):
    image = path.read_bytes()
    need(32 <= len(image) <= 1048576, "object size")
    magic, cpu, subtype, kind, commands, cmdsbytes, _, _ = struct.unpack_from("<8I", image)
    need((magic, cpu, subtype & 255, kind) == (MAGIC, 0x0100000C, 0, 1),
         "not a plain Clang Darwin ARM64 MH_OBJECT")
    need(commands <= 32 and cmdsbytes <= len(image) - 32, "command count")
    cursor, limit = 32, 32 + cmdsbytes
    sections, symtab = [], None
    platform = False
    for _ in range(commands):
        need(cursor + 8 <= limit, "truncated command")
        op, length = struct.unpack_from("<II", image, cursor)
        need(length >= 8 and length % 8 == 0 and length <= limit - cursor,
             "invalid command length")
        if op == 0x19:
            need(length >= 72 and not sections, "duplicate segment")
            count = u32(image, cursor + 64)
            need(count == 5 and length == 72 + 80 * count, "unexpected sections")
            for i in range(count):
                fields = struct.unpack_from("<16s16sQQIIIIIIII",
                                          image, cursor + 72 + 80 * i)
                name, segment = (fields[0].rstrip(b"\0"),
                                 fields[1].rstrip(b"\0"))
                need((name, segment) == (NAMES[i], SEGMENTS[i]),
                     "source section profile changed")
                address, length_vm, fileoff = fields[2:5]
                relocoff, nreloc, flags = fields[6:9]
                need(0 < length_vm <= 4096 and
                     (flags & 255) == (1 if i == 4 else 2 if i == 3 else 0) and
                     relocoff + 8 * nreloc <= len(image), "section bounds")
                if i != 4:
                    need(fileoff + length_vm <= len(image), "file section bounds")
                need(i == 0 or nreloc == 0, "non-code relocations unsupported")
                sections.append(dict(name=name, vm=address, size=length_vm,
                                     off=fileoff, reloff=relocoff,
                                     nreloc=nreloc, flags=flags))
        elif op == 2:
            need(length == 24 and symtab is None, "invalid symbol table")
            symtab = struct.unpack_from("<4I", image, cursor + 8)
        elif op == 0x32:
            need(length >= 24 and u32(image, cursor + 8) == 1,
                 "invalid build version")
            platform = True
        else:
            need(op in (0x0b, 0x2e), "unsupported original load command")
        cursor += length
    need(cursor == limit and symtab and platform, "incomplete Clang object")
    symoff, count, stroff, stringsize = symtab
    need(1 <= count <= 256 and symoff + 16 * count <= len(image) and
         stringsize <= 16384 and stroff + stringsize <= len(image),
         "symbol bounds")
    symbols = {}
    for i in range(count):
        nameoff, kind, section, _, value = struct.unpack_from(
            "<IBBHQ", image, symoff + i * 16)
        need(nameoff < stringsize, "string index")
        raw = image[stroff + nameoff:stroff + stringsize]
        need(b"\0" in raw, "unterminated symbol")
        name = raw.split(b"\0", 1)[0]
        if kind & 0xe0:
            continue
        need((kind & 0x0e) == 0x0e and 1 <= section <= 5,
             "unresolved symbol")
        sec = sections[section - 1]
        need(sec["vm"] <= value < sec["vm"] + sec["size"],
             "symbol outside its section")
        symbols[i] = (name, section, value - sec["vm"])
    need(any(v[0] == b"_main" and v[1] == 1 for v in symbols.values()),
         "missing compiler-produced _main")
    need(sections[1]["size"] == 32 and sections[2]["size"] == 4 and
         sections[4]["size"] == 256, "unexpected globals layout")
    return image, sections, symbols


def section(name, segment, address, size, offset, flags):
    return struct.pack("<16s16sQQIIIIIIII", name16(name), name16(segment),
                       address, size, offset, 2, 0, 0, flags, 0, 0, 0)


def segment(name, address, fileoff, filebytes, protection, sections=b""):
    return struct.pack("<II16sQQQQiiII", 0x19, 72 + len(sections),
                       name16(name), address, PAGE if name != b"__PAGEZERO" else VM,
                       fileoff, filebytes, protection, protection,
                       len(sections) // 80, 0) + sections


def patch_code(image, src, symbols, bases, textoff):
    section0 = src[0]
    code = bytearray(image[section0["off"]:section0["off"] + section0["size"]])
    pending = None
    for j in range(section0["nreloc"]):
        offset, bits = struct.unpack_from("<II", image, section0["reloff"] + 8 * j)
        typ = bits >> 28 & 15
        idx = bits & 0xffffff
        need(offset % 4 == 0 and offset + 4 <= len(code) and
             (bits >> 25) & 3 == 2, "bad relocation location")
        if typ == 10:
            need(pending is None and not (bits >> 27 & 1), "bad relocation addend")
            pending = (offset, idx - (1 << 24) if idx & (1 << 23) else idx)
            continue
        need((bits >> 27) & 1 and idx in symbols and typ in (2, 3, 4),
             "unsupported branch/page relocation")
        _, index, delta = symbols[idx]
        extra = 0
        if pending is not None:
            need(pending[0] == offset, "detached relocation addend")
            extra = pending[1]
            pending = None
        target = bases[index - 1] + delta + extra
        current = VM + textoff + offset
        insn = u32(code, offset)
        if typ == 2:
            disp = target - current
            need(disp % 4 == 0 and -(1 << 27) <= disp < (1 << 27) and
                 (insn & 0x7c000000) == 0x14000000, "invalid BRANCH26")
            insn = (insn & ~0x03ffffff) | ((disp // 4) & 0x03ffffff)
        elif typ == 3:
            need((insn & 0x9f000000) == 0x90000000, "PAGE21 not ADRP")
            pages = ((target & ~4095) - (current & ~4095)) // 4096
            need(-(1 << 20) <= pages < (1 << 20), "ADRP span overflow")
            part = pages & 0x1fffff
            insn = ((insn & ~((3 << 29) | (0x7ffff << 5))) |
                    ((part & 3) << 29) | ((part >> 2) << 5))
        else:
            offset_in_page = target & 4095
            if (insn & 0x7f000000) == 0x11000000:
                scale = 1  # 32/64-bit ADD immediate
            elif (insn & 0x3b000000) == 0x39000000:
                scale = 1 << (insn >> 30 & 3)  # unsigned LDR/STR
            else:
                raise ValueError("PAGEOFF12 opcode requires dynamic linker")
            need(offset_in_page % scale == 0 and
                 offset_in_page // scale < 4096, "PAGEOFF12 alignment")
            insn = ((insn & ~(0xfff << 10)) |
                    ((offset_in_page // scale) << 10))
        struct.pack_into("<I", code, offset, insn)
    need(pending is None and section0["nreloc"] >= 24,
         "missing original Clang relocation graph")
    return code


def link(source, destination):
    image, src, symbols = inspect_object(source)
    fixups = struct.pack("<7I", 0, 28, 28, 28, 0, 0, 0)
    linkedit = 3 * PAGE
    symoff = linkedit + align(len(fixups), 8)
    strings = b"\0_main\0"
    stroff = symoff + 16
    linksize = align(stroff + len(strings) - linkedit, 16)
    # 11 load commands; four loadable segments plus __PAGEZERO.
    command_size = sum((72, 232, 152, 232, 72, 16, 24, 80, 32, 24, 24))
    textoff = align(32 + command_size, 16)
    cstringoff = align(textoff + src[0]["size"], 16)
    need(cstringoff + src[3]["size"] <= PAGE, "code/strings exceed RX page")
    bases = (VM + textoff, VM + PAGE, VM + 2 * PAGE,
             VM + cstringoff, VM + 2 * PAGE + src[2]["size"])
    main_candidates = [bases[0] + d for n, sec, d in symbols.values()
                       if n == b"_main" and sec == 1]
    need(len(main_candidates) == 1, "ambiguous _main")
    entryoff = main_candidates[0] - VM
    code = patch_code(image, src, symbols, bases, textoff)
    entries = [
        segment(b"__PAGEZERO", 0, 0, 0, 0),
        segment(b"__TEXT", VM, 0, PAGE, 5,
                section(b"__text", b"__TEXT", bases[0],
                        len(code), textoff, 0x80000400) +
                section(b"__cstring", b"__TEXT", bases[3],
                        src[3]["size"], cstringoff, 2)),
        segment(b"__DATA_CONST", VM + PAGE, PAGE, PAGE, 1,
                section(b"__const", b"__DATA_CONST", bases[1],
                        src[1]["size"], PAGE, 0)),
        segment(b"__DATA", VM + 2 * PAGE, 2 * PAGE, PAGE, 3,
                section(b"__data", b"__DATA", bases[2],
                        src[2]["size"], 2 * PAGE, 0) +
                section(b"__bss", b"__DATA", bases[4],
                        src[4]["size"], 0, 1)),
        segment(b"__LINKEDIT", VM + 3 * PAGE, linkedit, linksize, 1),
        struct.pack("<4I", 0x80000034, 16, linkedit, len(fixups)),
        struct.pack("<6I", 2, 24, symoff, 1, stroff, len(strings)),
        struct.pack("<20I", 0x0b, 80, 0, 0, 0, 1, 1, 0, *([0] * 12)),
        struct.pack("<3I", 0x0e, 32, 12) +
            b"/usr/lib/dyld\0".ljust(20, b"\0"),
        struct.pack("<6I", 0x32, 24, 1, 0xd0000, 0xd0000, 0),
        struct.pack("<IIQQ", 0x80000028, 24, entryoff, 0),
    ]
    need(sum(map(len, entries)) == command_size, "command size mismatch")
    output = bytearray(linkedit + linksize)
    struct.pack_into("<8I", output, 0, MAGIC, 0x0100000C, 0, 2,
                     len(entries), command_size, 0x200085, 0)
    output[32:32 + command_size] = b"".join(entries)
    output[textoff:textoff + len(code)] = code
    for i, dst in ((1, PAGE), (2, 2 * PAGE), (3, cstringoff)):
        sec = src[i]
        output[dst:dst + sec["size"]] = image[sec["off"]:sec["off"] + sec["size"]]
    # Deliberately dirty file bytes covered by __bss: the runner must zero them.
    output[2 * PAGE + src[2]["size"]:
           2 * PAGE + src[2]["size"] + src[4]["size"]] = b"\xa5" * src[4]["size"]
    output[linkedit:linkedit + len(fixups)] = fixups
    struct.pack_into("<IBBHQ", output, symoff, 1, 0x0f, 1, 0, main_candidates[0])
    output[stroff:stroff + len(strings)] = strings
    destination.write_bytes(output)


if __name__ == "__main__":
    try:
        need(len(sys.argv) == 3, "usage: phase19a-link-fallback.py OBJECT EXECUTABLE")
        link(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
        print("phase19a: linked original Clang BRANCH26/PAGE21/PAGEOFF12 executable")
    except (OSError, ValueError, struct.error) as exc:
        sys.exit(f"phase19a fallback: {exc}")
