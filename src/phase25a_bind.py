#!/usr/bin/env python3
"""Phase 25a: bounded, non-executing relocation-aware ObjC2 Mach-O image binder.

This reads compiler-linked MH_DYLIB binaries. It NEVER executes guest IMPs or
maps guest text executable in the host. Only validated 64-bit chained fixups
are supported; classic dyld bind/rebase, arm64e and signed pointers fail closed.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from threading import RLock
import struct

MAX_BYTES = 8 * 1024 * 1024
MAX_SEGMENTS = 24
MAX_SECTIONS = 128
MAX_ITEMS = 128
MAX_CHAIN = 16384
MAX_NAME = 128
LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034
LC_DYLD_INFO_ONLY = 0x80000022
LC_DYLD_INFO = 0x22
MH_MAGIC_64 = 0xfeedfacf
CPU_ARM64 = 0x0100000c
MH_DYLIB = 6


class BindError(ValueError):
    pass


def require(test: bool, message: str) -> None:
    if not test:
        raise BindError(message)


def u16(buf: bytes, off: int) -> int:
    require(0 <= off <= len(buf) - 2, 'u16 outside mapping')
    return struct.unpack_from('<H', buf, off)[0]


def u32(buf: bytes, off: int) -> int:
    require(0 <= off <= len(buf) - 4, 'u32 outside mapping')
    return struct.unpack_from('<I', buf, off)[0]


def i32(buf: bytes, off: int) -> int:
    require(0 <= off <= len(buf) - 4, 'i32 outside mapping')
    return struct.unpack_from('<i', buf, off)[0]


def u64(buf: bytes, off: int) -> int:
    require(0 <= off <= len(buf) - 8, 'u64 outside mapping')
    return struct.unpack_from('<Q', buf, off)[0]


def span(buf: bytes, off: int, count: int) -> bytes:
    require(off >= 0 and count >= 0 and off <= len(buf) and count <= len(buf) - off,
            'invalid file range')
    return buf[off:off + count]


def cstr(buf: bytes, off: int, max_length: int = MAX_NAME) -> str:
    raw = span(buf, off, min(max_length, len(buf) - off))
    end = raw.find(b'\0')
    require(0 < end < max_length, 'invalid or unbounded string')
    require(all(32 <= byte <= 126 for byte in raw[:end]), 'non-ascii metadata name')
    return raw[:end].decode('ascii')


def name16(buf: bytes) -> str:
    return buf.split(b'\0', 1)[0].decode('ascii', errors='strict')


@dataclass(frozen=True)
class Segment:
    name: str
    addr: int
    size: int
    fileoff: int
    filesize: int
    maxprot: int
    initprot: int


@dataclass(frozen=True)
class Section:
    name: str
    segment: int
    addr: int
    size: int


@dataclass(frozen=True)
class Import:
    name: str
    addend: int = 0


@dataclass(frozen=True)
class Method:
    selector: str
    encoding: str
    imp: int
    owner: str


@dataclass
class Class:
    name: str
    addr: int
    meta: int
    parent: str | None
    instance_methods: dict[str, Method]
    class_methods: dict[str, Method]
    ivars: tuple[str, ...]
    protocols: tuple[str, ...]
    image: str
    generation: int
    version: int = 0


@dataclass
class Image:
    name: str
    data: bytes
    base: int
    dependencies: tuple[str, ...]
    segments: list[Segment] = field(default_factory=list)
    sections: dict[str, Section] = field(default_factory=dict)
    fixups: dict[int, int | Import] = field(default_factory=dict)
    classes: dict[str, Class] = field(default_factory=dict)
    categories: list[tuple[str, str, dict[str, Method], dict[str, Method]]] = field(default_factory=list)
    category_originals: dict[str, Class] = field(default_factory=dict)
    selectors: set[str] = field(default_factory=set)
    references: list[tuple[str, str]] = field(default_factory=list)
    preferred: int = 0

    def read(self, vm: int, length: int, *, executable: bool = False) -> bytes:
        require(length >= 0 and vm >= self.preferred, 'invalid guest address')
        matches = [s for s in self.segments if vm >= s.addr and
                   length <= s.size and vm - s.addr <= s.size - length]
        require(len(matches) == 1, 'ambiguous or unmapped guest address')
        seg = matches[0]
        require(bool(seg.initprot & 4) if executable else bool(seg.initprot & 1),
                'guest mapping permission failure')
        require(vm - seg.addr <= seg.filesize and
                length <= seg.filesize - (vm - seg.addr), 'non-file-backed metadata')
        return span(self.data, seg.fileoff + vm - seg.addr, length)

    def val(self, vm: int, n: int = 8) -> int:
        data = self.read(vm, n)
        return int.from_bytes(data, 'little')

    def ptr(self, vm: int, allow_external: bool = False) -> int | Import:
        require(vm % 8 == 0, 'misaligned pointer field')
        original = self.val(vm)
        require(vm in self.fixups or original == 0, 'unvalidated pointer without fixup')
        ptr = self.fixups.get(vm, 0)
        if isinstance(ptr, Import):
            require(allow_external, 'unresolved external metadata pointer')
            return ptr
        require(isinstance(ptr, int), 'invalid pointer value')
        return ptr

    def vm(self, addr: int) -> int:
        require(addr >= self.base, 'address below guest slide')
        relative = addr - self.base
        require(relative <= (1 << 44), 'guest pointer outside VM range')
        return self.preferred + relative

    def string(self, addr: int) -> str:
        vm = self.vm(addr)
        part = self.read(vm, min(MAX_NAME, self._remaining(vm)))
        return cstr(part, 0)

    def _remaining(self, vm: int) -> int:
        for seg in self.segments:
            if seg.addr <= vm < seg.addr + seg.filesize:
                return seg.addr + seg.filesize - vm
        raise BindError('string outside file-backed mapping')

    def get_pointer(self, vm: int, external: bool = False) -> int | Import:
        raw = self.ptr(vm, external)
        if isinstance(raw, Import):
            return raw
        if raw:
            self.read(self.vm(raw), 1)
        return raw

    def get_name(self, vm: int) -> str:
        p = self.get_pointer(vm)
        require(isinstance(p, int) and p != 0, 'null string pointer')
        return self.string(p)

    def section_slots(self, sec: str) -> list[int]:
        s = self.sections.get(sec)
        if not s:
            return []
        require(s.size % 8 == 0 and s.size // 8 <= MAX_ITEMS,
                'oversized/unaligned metadata pointer section')
        return [s.addr + index * 8 for index in range(s.size // 8)]


def parse_image(name: str, data: bytes, base: int, deps: tuple[str, ...]) -> Image:
    require(32 <= len(data) <= MAX_BYTES, 'invalid macho size')
    require(u32(data, 0) == MH_MAGIC_64 and u32(data, 4) == CPU_ARM64 and
            (u32(data, 8) & 0xff) == 0 and u32(data, 12) == MH_DYLIB,
            'unsupported architecture or non-linked dylib (arm64e rejected)')
    count, size = u32(data, 16), u32(data, 20)
    require(0 < count <= 256 and 0 < size <= len(data) - 32, 'bad load commands')
    result = Image(name, data, base, deps)
    pos, fixup = 32, None
    classic = False
    for _ in range(count):
        require(pos <= size + 32 - 8, 'truncated load command')
        cmd, length = u32(data, pos), u32(data, pos + 4)
        require(length >= 8 and length % 8 == 0 and length <= size + 32 - pos,
                'invalid load command size')
        if cmd == LC_SEGMENT_64:
            require(length >= 72, 'short segment')
            nsects = u32(data, pos + 64)
            require(nsects <= (length - 72) // 80 and
                    nsects + len(result.sections) <= MAX_SECTIONS and
                    len(result.segments) < MAX_SEGMENTS, 'section count exceeds bound')
            seg = Segment(name16(span(data, pos + 8, 16)), u64(data, pos + 24),
                          u64(data, pos + 32), u64(data, pos + 40),
                          u64(data, pos + 48), u32(data, pos + 56), u32(data, pos + 60))
            require(seg.size >= seg.filesize and seg.size < (1 << 36) and
                    (seg.filesize == 0 or seg.fileoff <= len(data) and
                     seg.filesize <= len(data) - seg.fileoff), 'invalid segment bounds')
            require(seg.initprot & ~7 == 0 and seg.maxprot & ~7 == 0 and
                    (seg.initprot & ~seg.maxprot) == 0 and
                    seg.initprot & 6 != 6, 'W^X or unsupported segment protections')
            idx = len(result.segments)
            result.segments.append(seg)
            for j in range(nsects):
                p = pos + 72 + j * 80
                sname = name16(span(data, p, 16))
                vm, length2 = u64(data, p + 32), u64(data, p + 40)
                require(vm >= seg.addr and length2 <= seg.size and
                        vm - seg.addr <= seg.size - length2, 'section outside owning segment')
                require(sname not in result.sections, 'ambiguous duplicated section name')
                result.sections[sname] = Section(sname, idx, vm, length2)
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            require(fixup is None and length >= 16, 'duplicate/short fixup payload')
            fixup = (u32(data, pos + 8), u32(data, pos + 12))
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            require(length >= 48, 'short classic fixup command')
            classic |= any(u32(data, pos + k) != 0 for k in (8, 16, 24, 32))
        pos += length
    require(pos == 32 + size, 'load command padding mismatch')
    require(result.segments, 'missing VM segments')
    mapped = [s for s in result.segments if s.filesize and s.name != '__PAGEZERO']
    result.preferred = min(s.addr for s in mapped)
    require(base > 0 and base % 0x1000 == 0, 'unaligned guest load base')
    for i, a in enumerate(mapped):
        for b in mapped[i + 1:]:
            require(a.addr + a.size <= b.addr or b.addr + b.size <= a.addr,
                    'overlapping VM segments')
    require(fixup is not None and not classic, 'unsupported classic or missing chained fixups')
    parse_chains(result, fixup)
    return result


def parse_chains(im: Image, payload: tuple[int, int]) -> None:
    off, length = payload
    f = span(im.data, off, length)
    require(len(f) >= 28 and u32(f, 0) == 0, 'unsupported fixup version')
    starts, imports, strings, icount, form, symform = (u32(f, x) for x in (4, 8, 12, 16, 20, 24))
    require(symform == 0 and form in (1, 2) and icount <= MAX_ITEMS and
            strings < len(f), 'unsupported chained import format')
    imports_data = []
    for i in range(icount):
        width = 4 if form == 1 else 8
        raw = u32(f, imports + i * width)
        ordinal = raw & 0xff
        if ordinal & 0x80:
            ordinal -= 256
        require(-3 <= ordinal <= 127 and not (raw & 0x100),
                'unsupported chained weak import or ordinal')
        symbol = cstr(f, strings + (raw >> 9))
        addend = i32(f, imports + i * width + 4) if form == 2 else 0
        imports_data.append(Import(symbol, addend))
    nsegs = u32(f, starts)
    require(nsegs == len(im.segments), 'chained segment count mismatch')
    require(starts <= len(f) and 4 + nsegs * 4 <= len(f) - starts, 'truncated starts')
    for i in range(nsegs):
        rel = u32(f, starts + 4 + i * 4)
        if not rel:
            continue
        ptr = starts + rel
        size = u32(f, ptr)
        page_size, fmt = u16(f, ptr + 4), u16(f, ptr + 6)
        seg_off, pages = u64(f, ptr + 8), u16(f, ptr + 20)
        seg = im.segments[i]
        require(fmt in (2, 6) and page_size in (4096, 16384) and
                22 + pages * 2 <= size and size <= len(f) - ptr and
                seg_off == seg.addr - im.preferred and pages <= 8192,
                'invalid chained segment header/unsupported format')
        require(not (seg.initprot & 4) and (seg.maxprot & 2),
                'fixup in executable or immutable segment')
        for page in range(pages):
            start = u16(f, ptr + 22 + page * 2)
            if start == 0xffff:
                continue
            require(start < page_size and start % 4 == 0 and not (start & 0x8000),
                    'multi-start/invalid chained pointer')
            chain = page * page_size + start
            for step in range(page_size // 4):
                require(chain + 8 <= seg.filesize and (chain % page_size) <= page_size - 8,
                        'chain outside file-backed page')
                loc = seg.addr + chain
                require(loc not in im.fixups, 'overlapping chained pointers')
                raw = u64(im.data, seg.fileoff + chain)
                skip = (raw >> 51) & 0xfff
                if raw & (1 << 63):
                    index = raw & 0xffffff
                    require(index < len(imports_data) and ((raw >> 32) & 0x7ffff) == 0,
                            'malformed chained bind')
                    addend = (raw >> 24) & 0xff
                    if addend >= 128:
                        addend -= 256
                    original = imports_data[index]
                    im.fixups[loc] = Import(original.name, original.addend + addend)
                else:
                    require(((raw >> 36) & 0x7fff) == 0, 'unsupported tagged/rebase bits')
                    target = raw & ((1 << 36) - 1)
                    if fmt == 2:
                        target -= im.preferred
                    require(target >= 0, 'rebase target before image')
                    vm = im.preferred + target
                    require(any(s.addr <= vm < s.addr + s.size for s in im.segments),
                            'rebase target not owned by image')
                    im.fixups[loc] = im.base + target
                require(len(im.fixups) <= MAX_CHAIN, 'too many chained fixups')
                if not skip:
                    break
                chain += skip * 4
                require(chain % page_size + 8 <= page_size, 'cross-page chained fixup')
            else:
                raise BindError('unterminated chained pointer page')


def read_methods(im: Image, pointer_slot: int, owner: str) -> dict[str, Method]:
    raw = im.get_pointer(pointer_slot)
    require(isinstance(raw, int), 'invalid method list pointer')
    if not raw:
        return {}
    vm = im.vm(raw)
    entsize, count = im.val(vm, 4), im.val(vm + 4, 4)
    relative = bool(entsize & 0x80000000)
    direct = bool(entsize & 0x40000000)
    stride = entsize & 0x0000ffff
    require((stride == 12 if relative else stride == 24) and
            (relative or not direct) and count <= MAX_ITEMS,
            'unsupported relative/absolute method list layout')
    im.read(vm + 8, count * stride)
    result: dict[str, Method] = {}
    for idx in range(count):
        p = vm + 8 + idx * stride
        if relative:
            name_vm = p + i32(im.read(p, 4), 0)
            type_vm = p + 4 + i32(im.read(p + 4, 4), 0)
            imp_vm = p + 8 + i32(im.read(p + 8, 4), 0)
            if not direct:
                rawname = im.get_pointer(name_vm)
                require(isinstance(rawname, int) and rawname != 0,
                        'relative indirect selector not bound')
                name_vm = im.vm(rawname)
            selector = im.string(im.base + name_vm - im.preferred)
            encoding = im.string(im.base + type_vm - im.preferred)
            imp = im.base + imp_vm - im.preferred
        else:
            selector = im.get_name(p)
            encoding = im.get_name(p + 8)
            imp_ref = im.get_pointer(p + 16)
            require(isinstance(imp_ref, int), 'external IMP unsupported')
            imp = imp_ref
        require(imp != 0 and (imp & 3) == 0, 'missing/misaligned IMP')
        im.read(im.vm(imp), 4, executable=True)
        require(selector not in result, 'duplicate selector inside method list')
        result[selector] = Method(selector, encoding, imp, owner)
        im.selectors.add(selector)
    return result


def read_protocol_list(im: Image, pointer_slot: int) -> tuple[str, ...]:
    ref = im.get_pointer(pointer_slot)
    require(isinstance(ref, int), 'bad protocol list')
    if not ref:
        return ()
    vm = im.vm(ref)
    count = im.val(vm)
    require(count <= MAX_ITEMS, 'protocol count exceeds bound')
    im.read(vm + 8, count * 8)
    found = []
    for i in range(count):
        proto = im.get_pointer(vm + 8 + i * 8, external=True)
        require(isinstance(proto, int), 'external protocol adoption needs binding')
        name = im.get_name(im.vm(proto) + 8)
        require(name not in found, 'duplicate adopted protocol')
        found.append(name)
    return tuple(found)


def read_ivars(im: Image, pointer_slot: int, max_size: int) -> tuple[str, ...]:
    ref = im.get_pointer(pointer_slot)
    require(isinstance(ref, int), 'bad ivar list')
    if not ref:
        return ()
    vm = im.vm(ref)
    stride, count = im.val(vm, 4), im.val(vm + 4, 4)
    require(stride == 32 and count <= MAX_ITEMS, 'unsupported ivar list')
    names = []
    for i in range(count):
        p = vm + 8 + i * stride
        offset = im.get_pointer(p)
        require(isinstance(offset, int) and offset, 'bad ivar offset cell')
        pos = im.val(im.vm(offset), 4)
        name = im.get_name(p + 8)
        encoding = im.get_name(p + 16)
        align = im.val(p + 24, 4)
        width = im.val(p + 28, 4)
        require(pos <= max_size and 0 < width <= max_size - pos and
                align <= 12 and (pos % (1 << align)) == 0 and encoding,
                'invalid compiler ivar size/offset/alignment')
        require(name not in names, 'duplicate ivar')
        names.append(name)
    return tuple(names)


def parse_class(im: Image, cls_addr: int, generation: int) -> Class:
    vm = im.vm(cls_addr)
    meta = im.get_pointer(vm)
    require(isinstance(meta, int) and meta, 'invalid metaclass')
    meta_vm = im.vm(meta)
    im.read(meta_vm, 40)
    parent_ptr = im.get_pointer(vm + 8, external=True)
    ro = im.get_pointer(vm + 32)
    meta_ro = im.get_pointer(meta_vm + 32)
    require(isinstance(ro, int) and ro and isinstance(meta_ro, int) and meta_ro,
            'class_ro/metaclass_ro not relocated')
    ro_vm = im.vm(ro)
    meta_ro_vm = im.vm(meta_ro)
    im.read(ro_vm, 72)
    im.read(meta_ro_vm, 72)
    name = im.get_name(ro_vm + 24)
    require(name == im.get_name(meta_ro_vm + 24), 'metaclass name mismatch')
    size = im.val(ro_vm + 8, 4)
    require(0 < size <= 65536, f'invalid class instance size {name}: {size}, class={vm:x}, ro={ro_vm:x}, raw={im.read(ro_vm, 16).hex()}')
    parent = None
    if isinstance(parent_ptr, Import):
        require(parent_ptr.name.startswith('_OBJC_CLASS_$_') and
                parent_ptr.addend == 0, 'unsupported external superclass')
        parent = parent_ptr.name[len('_OBJC_CLASS_$_'):]
    elif isinstance(parent_ptr, int) and parent_ptr:
        parent = '@' + str(parent_ptr)
    methods = read_methods(im, ro_vm + 32, name)
    class_methods = read_methods(im, meta_ro_vm + 32, name)
    ivars = read_ivars(im, ro_vm + 48, size)
    protocols = read_protocol_list(im, ro_vm + 40)
    require(parent != name, 'self-superclass cycle')
    return Class(name, cls_addr, meta, parent, methods, class_methods,
                 ivars, protocols, im.name, generation)


def inspect(im: Image, generation: int) -> None:
    require(im.sections.get('__objc_classlist') is not None or
            im.sections.get('__objc_catlist') is not None,
            'no objective-c metadata in image')
    for slot in im.section_slots('__objc_classlist'):
        ref = im.get_pointer(slot)
        require(isinstance(ref, int) and ref, 'invalid classlist binding')
        obj = parse_class(im, ref, generation)
        require(obj.name not in im.classes, 'duplicate class in one image')
        im.classes[obj.name] = obj
    for slot in im.section_slots('__objc_protolist'):
        proto = im.get_pointer(slot)
        require(isinstance(proto, int) and proto, 'invalid protocol reference')
        im.get_name(im.vm(proto) + 8)
    for section in ('__objc_classrefs', '__objc_superrefs'):
        for slot in im.section_slots(section):
            ref = im.get_pointer(slot, external=True)
            if isinstance(ref, Import):
                require(ref.name.startswith('_OBJC_CLASS_$_') and
                        ref.addend == 0, 'unhandled class reference symbol')
                im.references.append((section, ref.name[len('_OBJC_CLASS_$_'):]))
            else:
                require(isinstance(ref, int) and ref, 'invalid local class reference')
                # Local class refs are checked against the image's classes at commit.
                im.references.append((section, '@' + str(ref)))
    for slot in im.section_slots('__objc_selrefs'):
        im.selectors.add(im.get_name(slot))
    for slot in im.section_slots('__objc_catlist'):
        cat = im.get_pointer(slot)
        require(isinstance(cat, int) and cat, 'invalid category list')
        vm = im.vm(cat)
        catname = im.get_name(vm)
        target = im.get_pointer(vm + 8, external=True)
        if isinstance(target, Import):
            require(target.name.startswith('_OBJC_CLASS_$_') and
                    target.addend == 0, 'bad category target import')
            target_name = target.name[len('_OBJC_CLASS_$_'):]
        else:
            require(isinstance(target, int) and target, 'null category class')
            targets = [c.name for c in im.classes.values() if c.addr == target]
            require(len(targets) == 1, 'category target not owned by image')
            target_name = targets[0]
        im.categories.append((catname, target_name,
                              read_methods(im, vm + 16, catname),
                              read_methods(im, vm + 24, catname)))
        read_protocol_list(im, vm + 32)


class Registry:
    """Atomic registration, cross-image class identity, and safe image retirement."""
    def __init__(self) -> None:
        self.lock = RLock()
        self.images: dict[str, Image] = {}
        self.classes: dict[str, Class] = {}
        self.selectors: dict[str, str] = {}
        self.generation = 0

    def register(self, name: str, data: bytes, deps: tuple[str, ...] = ()) -> int:
        with self.lock:
            if name in self.images:
                old = self.images[name]
                require(old.data == data and old.dependencies == deps,
                        'name reused with different image generation')
                return old.base
            require(len(self.images) < 32 and all(dep in self.images for dep in deps) and
                    name not in deps, 'unavailable dependency or image quota')
            # Unique guest virtual address space with a deterministic 256 MiB gap.
            base = 0x100000000 + (self.generation + 1) * 0x10000000
            im = parse_image(name, data, base, deps)
            inspect(im, self.generation + 1)
            known = dict(self.classes)
            for cname, candidate in im.classes.items():
                require(cname not in known, 'duplicate class from independent dylib')
                known[cname] = candidate
            addresses = {c.addr: c.name for c in known.values()}
            for candidate in im.classes.values():
                if candidate.parent and candidate.parent.startswith('@'):
                    ptr = int(candidate.parent[1:])
                    require(ptr in addresses, 'local superclass not registered')
                    candidate.parent = addresses[ptr]
                require(not candidate.parent or candidate.parent in known,
                        'external superclass dependency not loaded')
                seen = {candidate.name}
                p = candidate.parent
                while p:
                    require(p not in seen, 'superclass dependency cycle')
                    seen.add(p)
                    p = known[p].parent
            for section, target in im.references:
                if target.startswith('@'):
                    require(int(target[1:]) in addresses,
                            f'unbound local {section}')
                else:
                    require(target in known, f'unbound external {section}')
            # Categories are staged in source-order; later successfully registered
            # images override earlier methods in dependency order.
            replacement: dict[str, Class] = {}
            for catname, target, instance, class_methods in im.categories:
                require(target in known, f'category {catname} targets missing class')
                if target not in replacement:
                    c = known[target]
                    replacement[target] = Class(c.name, c.addr, c.meta, c.parent,
                                                dict(c.instance_methods),
                                                dict(c.class_methods), c.ivars,
                                                c.protocols, c.image, c.generation, c.version)
                staged = replacement[target]
                staged.instance_methods.update(instance)
                staged.class_methods.update(class_methods)
                staged.version += 1
            im.category_originals = {n: known[n] for n in replacement}
            self.classes.update(im.classes)
            self.classes.update(replacement)
            self.images[name] = im
            for selector in im.selectors:
                self.selectors.setdefault(selector, selector)
            self.generation += 1
            return base

    def lookup(self, class_name: str, selector: str, class_method: bool = False) -> Method | None:
        with self.lock:
            seen: set[str] = set()
            current = class_name
            while current:
                require(current not in seen and current in self.classes,
                        'unknown class or superclass cycle')
                seen.add(current)
                cls = self.classes[current]
                method = (cls.class_methods if class_method else cls.instance_methods).get(selector)
                if method:
                    return method
                current = cls.parent
            return None

    def unload(self, name: str) -> None:
        with self.lock:
            require(name in self.images, 'unknown image')
            require(not any(name in other.dependencies for other in self.images.values()),
                    'image still referenced by dependent')
            image = self.images[name]
            # Only unroll a category overlay when no later image replaces the
            # same target. This keeps the override stack deterministic.
            order = list(self.images)
            later = [self.images[n] for n in order[order.index(name) + 1:]]
            require(not any(target in other.category_originals
                            for other in later for target in image.category_originals),
                    'category has newer overrides')
            for target, prior in image.category_originals.items():
                self.classes[target] = prior
            for cls in self.images[name].classes.values():
                require(not any(c.parent == cls.name for c in self.classes.values()
                                if c.image != name), 'class still inherited externally')
            for cname in self.images[name].classes:
                del self.classes[cname]
            del self.images[name]
            self.selectors = {s: s for im in self.images.values() for s in im.selectors}
            self.generation += 1
