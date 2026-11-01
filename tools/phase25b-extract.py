#!/usr/bin/env python3
"""Allowlist exact reloc-free Clang ARM64 Darwin method instruction sequences.

This deliberately does NOT implement a general Mach-O loader or relocator.
Unexpected compiled code fails closed; no unverified bytes become executable.
"""
import pathlib
import struct
import sys

RET = 0xd65f03c0
EXPECTED_ALPHA = {
    '+[P25Root kind]': ('kind', [0x52800320, RET]),
    '-[P25Root add:]': ('add', [0x91001440, RET]),
    '-[P25Root twice:]': ('twice', [0x1e602800, RET]),
    '-[P25Root triple:]': ('triple', [0x1e202801, 0x1e202820, RET]),
    '-[P25Root echo:]': ('echo', [0xaa0203e0, RET]),
    '-[P25Root six:b:c:d:e:f:]': ('six', [0x8b0200e0, RET]),
}
EXPECTED_BETA = {'-[P25Child childLeaf:]': ('child', [0x91005040, RET])}
EXPECTED_GAMMA = {'-[P25Root(Replacement) add:]': ('category', [0x91002440, RET])}

def extract(source: pathlib.Path, out: pathlib.Path) -> None:
    expected = {'alpha': EXPECTED_ALPHA, 'beta': EXPECTED_BETA,
                'gamma': EXPECTED_GAMMA}.get(source.stem)
    if expected is None:
        raise ValueError('unapproved image; only alpha, beta, gamma accepted')
    data = source.read_bytes()
    if len(data) < 32 or len(data) > 4_194_304:
        raise ValueError('image size invalid')
    hdr = struct.unpack_from('<8I', data)
    if hdr[0] != 0xfeedfacf or hdr[1] != 0x0100000c or hdr[3] != 6:
        raise ValueError('not an ARM64 Darwin dylib')
    ncmds, sz = hdr[4:6]
    if ncmds > 96 or sz > len(data)-32:
        raise ValueError('invalid load command region')
    pos, end = 32, 32 + sz
    sections = {}
    symbols = None
    for _ in range(ncmds):
        if pos+8 > end: raise ValueError('truncated load command')
        op, length = struct.unpack_from('<II', data, pos)
        if length < 8 or length % 8 or pos+length > end:
            raise ValueError('bad load command size')
        if op == 0x19:
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<4Q', data, pos+24)
            maxprot, initprot, nsects, flags = struct.unpack_from('<4I', data, pos+56)
            if nsects > 64 or 72+80*nsects > length:
                raise ValueError('invalid segment sections')
            if fileoff > len(data) or filesize > len(data)-fileoff:
                raise ValueError('segment out of file bounds')
            for i in range(nsects):
                off = pos+72+i*80
                sectname = data[off:off+16].rstrip(b'\0')
                addr, size = struct.unpack_from('<2Q', data, off+32)
                file_offset = struct.unpack_from('<I', data, off+48)[0]
                flags = struct.unpack_from('<I', data, off+64)[0]
                sections[(sectname, i)] = (addr, size, file_offset, initprot, flags)
                if sectname == b'__text':
                    if initprot & 4 == 0: raise ValueError('text is not executable')
                    text_region = (addr, size, file_offset)
        if op == 2:
            symbols = struct.unpack_from('<4I', data, pos+8)
        pos += length
    if pos != end or not symbols or 'text_region' not in locals():
        raise ValueError('missing required commands')
    symoff, nsyms, stroff, strsize = symbols
    if (nsyms > 10000 or symoff > len(data) or
        nsyms*16 > len(data)-symoff or stroff > len(data) or
        strsize > len(data)-stroff):
        raise ValueError('invalid symbol table')
    names = {}
    for i in range(nsyms):
        strx, typ, sect, desc, addr = struct.unpack_from('<IBBHQ', data, symoff+i*16)
        if strx >= strsize: continue
        name_end = data.find(b'\0', stroff+strx, stroff+strsize)
        if name_end < 0: continue
        name = data[stroff+strx:name_end].decode('ascii', errors='strict')
        if name in expected and (typ & 0x0e) == 0x0e:
            names[name] = addr
    if set(names) != set(expected):
        raise ValueError(f'missing compiler method symbols {set(expected)-set(names)}')
    addr, size, fileoff = text_region
    out.mkdir(parents=True, exist_ok=True)
    for name, (key, opcodes) in expected.items():
        vmaddr = names[name]
        length = len(opcodes)*4
        if vmaddr < addr or vmaddr-addr > size or length > size-(vmaddr-addr):
            raise ValueError(f'function outside __text: {name}')
        off = fileoff+(vmaddr-addr)
        if off > len(data) or length > len(data)-off:
            raise ValueError('truncated instruction bytes')
        actual = list(struct.unpack_from('<' + 'I'*len(opcodes), data, off))
        if actual != opcodes:
            raise ValueError(f'unexpected Clang instruction sequence for {name}: {actual!r}')
        (out/f'{key}.bin').write_bytes(data[off:off+length])
        print(f'validated linked Darwin {name}: {length//4} ARM64 instructions')

if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: phase25b-extract.py alpha.dylib output-directory')
    extract(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
