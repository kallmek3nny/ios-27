#!/usr/bin/env python3
"""Narrow Clang MH_OBJECT -> real ARM64 MH_EXECUTE linker for phase17a only.

Supports exactly four known sections and ARM64 PAGE21/PAGEOFF12/ADDEND
relocations. No generic dyld imports, TLS, syscalls, or Apple SDK support.
"""
import pathlib
import struct
import sys

VM = 0x100000000
PAGE = 0x4000
MAGIC = 0xFEEDFACF
NAMES = (b"__text", b"__const", b"__data", b"__bss")
SEGMENTS = (b"__TEXT", b"__DATA_CONST", b"__DATA", b"__DATA")
BASES = (VM, VM + PAGE, VM + 2 * PAGE, VM + 2 * PAGE + 4)


def require(ok, message):
    if not ok:
        raise ValueError(message)


def word(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def align(value, granule):
    return (value + granule - 1) // granule * granule


def name16(value):
    return value.ljust(16, bytes([0]))


def load_object(path):
    data = path.read_bytes()
    require(32 <= len(data) <= 1_048_576, "invalid Clang object size")
    header = struct.unpack_from("<8I", data)
    require(header[:4] == (MAGIC, 0x0100000C, 0, 1),
            "not an original ARM64-all MH_OBJECT")
    require(header[4] <= 64 and 32 + header[5] <= len(data),
            "invalid original load commands")
    pos, sections, table, platform = 32, [], None, False
    for _ in range(header[4]):
        require(pos + 8 <= 32 + header[5], "truncated object command")
        op, size = struct.unpack_from("<II", data, pos)
        require(size >= 8 and not size % 8 and
                pos + size <= 32 + header[5], "invalid object command")
        if op == 0x19:
            count = word(data, pos + 64)
            require(size >= 72 and count <= (size - 72) // 80 and
                    not sections, "unexpected object sections")
            for j in range(count):
                start = pos + 72 + j * 80
                item = struct.unpack_from("<16s16sQQIIIIIIII", data, start)
                section = {
                    "name": item[0].rstrip(bytes([0])),
                    "segment": item[1].rstrip(bytes([0])),
                    "addr": item[2], "size": item[3], "off": item[4],
                    "reloff": item[6], "nreloc": item[7], "flags": item[8]
                }
                sections.append(section)
        elif op == 0x02:
            require(table is None and size == 24, "invalid object symbol table")
            table = struct.unpack_from("<4I", data, pos + 8)
        elif op == 0x32:
            require(size >= 24 and word(data, pos + 8) == 1,
                    "not a macOS-target Clang object")
            platform = True
        elif op not in (0x0b, 0x2e):
            raise ValueError("unexpected object load command")
        pos += size
    require(pos == 32 + header[5] and platform and table,
            "incomplete object metadata")
    require(len(sections) == 4, "fixture section count differs")
    for i, sec in enumerate(sections):
        require(sec["name"] == NAMES[i] and sec["segment"] == SEGMENTS[i] and
                sec["size"] > 0 and sec["size"] <= 4096,
                "unexpected fixture section")
        require((sec["flags"] & 255) == (1 if i == 3 else 0),
                "unsupported section kind")
        require(sec["reloff"] + sec["nreloc"] * 8 <= len(data) and
                (i == 0 or sec["nreloc"] == 0), "unexpected relocations")
        if i < 3:
            require(sec["off"] + sec["size"] <= len(data),
                    "truncated compiled section")
    symoff, count, stroff, stringsize = table
    require(4 <= count <= 64 and stringsize <= 8192 and
            symoff + count * 16 <= len(data) and
            stroff + stringsize <= len(data), "invalid compiler symbols")
    symbols = {}
    for i in range(count):
        strx, typ, sect, desc, value = struct.unpack_from(
            "<IBBHQ", data, symoff + 16 * i)
        require(strx < stringsize and
                bytes([0]) in data[stroff + strx:stroff + stringsize],
                "invalid symbol name")
        raw = data[stroff + strx:stroff + stringsize]
        key = raw.split(bytes([0]), 1)[0]
        if typ & 0xe0:
            continue
        require((typ & 0x0e) == 0x0e and 1 <= sect <= 4,
                "object has unresolved or unsupported symbols")
        original = sections[sect - 1]
        require(original["addr"] <= value <
                original["addr"] + original["size"], "symbol outside section")
        if key in symbols:
            raise ValueError("duplicate compiler symbol")
        symbols[key] = (i, sect, value - original["addr"])
    for name in (b"_main", b"_constant", b"_counter", b"_zero_tail"):
        require(name in symbols, f"missing genuine Clang definition {name!r}")
    return data, sections, symbols, table


def relocation_patch(data, sections, symbols, textoff):
    code = bytearray(data[sections[0]["off"]:
                          sections[0]["off"] + sections[0]["size"]])
    by_index = {index: BASES[sect - 1] + delta
                for index, sect, delta in symbols.values()}
    pending = None
    section = sections[0]
    for j in range(section["nreloc"]):
        offset, bits = struct.unpack_from(
            "<II", data, section["reloff"] + j * 8)
        typ = (bits >> 28) & 15
        idx = bits & 0xffffff
        require(offset % 4 == 0 and offset + 4 <= len(code) and
                (bits >> 25) & 3 == 2, "invalid ARM64 relocation range")
        if typ == 10:
            require(pending is None and not ((bits >> 27) & 1),
                    "unexpected relocation addend")
            signed = idx - (1 << 24) if idx & (1 << 23) else idx
            pending = (offset, signed)
            continue
        require(typ in (3, 4) and (bits >> 27) & 1 == 1 and
                idx in by_index, "unsupported external relocation")
        addend = 0
        if pending is not None:
            require(pending[0] == offset, "dangling ARM64 addend")
            addend = pending[1]
            pending = None
        target = by_index[idx] + addend
        opcode = word(code, offset)
        if typ == 3:
            require((bits >> 24) & 1 and
                    opcode & 0x9f000000 == 0x90000000,
                    "expected ARM64 ADRP")
            pc_page = (VM + textoff + offset) & ~4095
            page_delta = ((target & ~4095) - pc_page) // 4096
            require(-(1 << 20) <= page_delta < (1 << 20),
                    "ADRP displacement out of range")
            imm = page_delta & 0x1fffff
            opcode = ((opcode & ~((3 << 29) | (0x7ffff << 5))) |
                      ((imm & 3) << 29) | ((imm >> 2) << 5))
        else:
            require(not ((bits >> 24) & 1) and
                    (opcode & 0xffc00000) in (0xb9400000, 0xb9000000) and
                    target % 4 == 0, "expected ARM64 32-bit LDR/STR")
            opcode = ((opcode & ~(0xfff << 10)) |
                      (((target & 4095) // 4) << 10))
        struct.pack_into("<I", code, offset, opcode)
    require(pending is None and section["nreloc"] >= 8,
            "missing genuine compiler PAGE21/PAGEOFF12 fixups")
    return bytes(code)


def section(name, segment, address, length, offset, flags):
    return struct.pack("<16s16sQQIIIIIIII", name16(name), name16(segment),
                       address, length, offset, 2, 0, 0, flags, 0, 0, 0)


def segment(name, vm, memsize, fileoff, filesize, prot, secs=b""):
    return struct.pack("<II16sQQQQiiII", 0x19, 72 + len(secs),
                       name16(name), vm, memsize, fileoff, filesize,
                       prot, prot, len(secs) // 80, 0) + secs


def link(input_path, output_path):
    data, sections, symbols, table = load_object(input_path)
    command_size = sum((72, 152, 152, 232, 72, 16, 24, 80, 32, 24, 24))
    textoff = align(32 + command_size, 16)
    textbytes = relocation_patch(data, sections, symbols, textoff)
    require(textoff + len(textbytes) <= PAGE, "text exceeds 16KiB mapping")
    require(sections[1]["size"] == 4 and sections[2]["size"] == 4 and
            sections[3]["size"] == 32, "unexpected original C data profile")
    fixups = struct.pack("<7I", 0, 28, 28, 28, 0, 0, 0)
    linkedit_off = 3 * PAGE
    symoff = linkedit_off + align(len(fixups), 8)
    exported = (b"_main", b"_constant", b"_counter", b"_zero_tail")
    strings = bytes([0]) + bytes([0]).join(exported) + bytes([0])
    stroff = symoff + len(exported) * 16
    linkedit_size = align(stroff + len(strings) - linkedit_off, 16)
    entries = [
        segment(b"__PAGEZERO", 0, VM, 0, 0, 0),
        segment(b"__TEXT", VM, PAGE, 0, PAGE, 5,
                section(b"__text", b"__TEXT", VM + textoff,
                        len(textbytes), textoff, 0x80000400)),
        segment(b"__DATA_CONST", VM + PAGE, PAGE, PAGE, PAGE, 1,
                section(b"__const", b"__DATA_CONST", VM + PAGE, 4, PAGE, 0)),
        segment(b"__DATA", VM + 2 * PAGE, PAGE, 2 * PAGE, PAGE, 3,
                section(b"__data", b"__DATA", VM + 2 * PAGE,
                        4, 2 * PAGE, 0) +
                section(b"__bss", b"__DATA", VM + 2 * PAGE + 4, 32, 0, 1)),
        segment(b"__LINKEDIT", VM + 3 * PAGE, PAGE,
                linkedit_off, linkedit_size, 1),
        struct.pack("<4I", 0x80000034, 16, linkedit_off, len(fixups)),
        struct.pack("<6I", 2, 24, symoff, 4, stroff, len(strings)),
        struct.pack("<20I", 0x0b, 80, 0, 0, 0, 4, 4, 0, *([0] * 12)),
        struct.pack("<3I", 0x0e, 32, 12) +
            (b"/usr/lib/dyld" + bytes([0])).ljust(20, bytes([0])),
        struct.pack("<6I", 0x32, 24, 1, 0xd0000, 0xd0000, 0),
        struct.pack("<IIQQ", 0x80000028, 24,
                    textoff + symbols[b"_main"][2], 0),
    ]
    require(sum(map(len, entries)) == command_size, "invalid linker commands")
    output = bytearray(linkedit_off + linkedit_size)
    struct.pack_into("<8I", output, 0, MAGIC, 0x0100000C, 0, 2,
                     len(entries), command_size, 0x200085, 0)
    output[32:32 + command_size] = b"".join(entries)
    output[textoff:textoff + len(textbytes)] = textbytes
    for i, index in enumerate((1, 2)):
        src = sections[index]
        off = (1 + i) * PAGE
        output[off:off + 4] = data[src["off"]:src["off"] + 4]
    # Deliberately poison file padding covered by S_ZEROFILL to test that
    # native mapping discards it before the executable entry is exposed.
    output[2 * PAGE + 4:2 * PAGE + 36] = bytes([0xa5]) * 32
    output[linkedit_off:linkedit_off + len(fixups)] = fixups
    pos = 1
    for i, name in enumerate(exported):
        _, sect, delta = symbols[name]
        struct.pack_into("<IBBHQ", output, symoff + i * 16,
                         pos, 0x0f, sect, 0, BASES[sect - 1] + delta +
                         (textoff if sect == 1 else 0))
        pos += len(name) + 1
    output[stroff:stroff + len(strings)] = strings
    output_path.write_bytes(output)


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3:
            raise ValueError("usage: phase17a-link-fallback.py OBJECT EXECUTABLE")
        link(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
        print("phase17a: linked compiler-produced PAGE21/PAGEOFF12 image")
    except (ValueError, OSError, struct.error) as exc:
        sys.exit("phase17a fallback: " + str(exc))
