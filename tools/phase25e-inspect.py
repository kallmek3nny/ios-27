#!/usr/bin/env python3
"""Fail-closed host-only inspection of compiler-linked phase25e ObjC2 images.

No instruction execution is performed by this module. Native execution cannot
be enabled merely by setting a capability flag or incrementing a host counter.
"""
from __future__ import annotations
import hashlib
from pathlib import Path
import struct
from dataclasses import dataclass

MAGIC = 0xFEEDFACF
ARM64 = 0x0100000C
MH_EXECUTE = 2
MH_DYLIB = 6
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 2
LC_LOAD_DYLIB = 0xC
LC_MAIN = 0x80000028
LIMIT_BYTES = 4 * 1024 * 1024
OBJC_SECTIONS = frozenset((
    "__objc_classlist", "__objc_catlist", "__objc_protolist",
    "__objc_selrefs", "__objc_classrefs", "__objc_superrefs",
    "__objc_const", "__objc_data", "__objc_protorefs", "__objc_ivar",
))


class InspectionFailure(ValueError):
    def __init__(self, category: str, detail: str):
        self.category = category
        self.detail = detail
        super().__init__(f"{category}: {detail}")


def fail(category: str, detail: str) -> None:
    raise InspectionFailure(category, detail)


def text_at(blob: bytes, start: int, end: int, category: str) -> str:
    if not 0 <= start < end <= len(blob):
        fail(category, "string offset escapes bounded section")
    zero = blob.find(b"\0", start, end)
    if zero < 0:
        fail(category, "unterminated Mach-O string")
    try:
        return blob[start:zero].decode("ascii")
    except UnicodeDecodeError:
        fail(category, "non-ascii Mach-O string")


@dataclass(frozen=True)
class Image:
    name: str
    sha256: str
    size: int
    kind: str
    dependencies: tuple[str, ...]
    objc_sections: tuple[str, ...]
    undefined: tuple[str, ...]
    entry_offset: int | None
    segment_count: int
    rx_bytes: int

    def as_dict(self) -> dict:
        return {
            "name": self.name, "sha256": self.sha256, "size_bytes": self.size,
            "kind": self.kind, "dependencies": list(self.dependencies),
            "objc_sections": list(self.objc_sections),
            "undefined": list(self.undefined),
            "entry_offset": self.entry_offset,
            "segment_count": self.segment_count,
            "rx_mapped_bytes": self.rx_bytes,
        }


def inspect_blob(blob: bytes, name: str = "guest") -> Image:
    if not 32 <= len(blob) <= LIMIT_BYTES:
        fail("dyld", "image size outside bounded range")
    magic, cpu, subtype, kind, count, size, flags, reserved = struct.unpack_from("<8I", blob)
    if magic != MAGIC or cpu != ARM64 or kind not in (MH_EXECUTE, MH_DYLIB):
        fail("dyld", "not a linked arm64 Darwin MH_EXECUTE/MH_DYLIB")
    if count > 256 or size > len(blob) - 32 or size % 8:
        fail("dyld", "invalid Mach-O load command envelope")
    if subtype & 0xFF != 0:
        fail("dyld", "unsupported CPU subtype")
    if reserved != 0:
        fail("dyld", "nonzero reserved header field")
    offset = 32
    deps = []
    objc = set()
    undefined = []
    entry = None
    ranges = []
    segment_count = 0
    rx_bytes = 0
    symtab = None
    for _ in range(count):
        if offset + 8 > 32 + size:
            fail("dyld", "truncated load command")
        command, length = struct.unpack_from("<II", blob, offset)
        if length < 8 or length % 8 or offset + length > 32 + size:
            fail("dyld", "out-of-bounds load command")
        if command == LC_LOAD_DYLIB:
            if length < 24:
                fail("dyld", "short LC_LOAD_DYLIB")
            pos = struct.unpack_from("<I", blob, offset + 8)[0]
            if pos < 24 or pos >= length:
                fail("dyld", "bad dylib name offset")
            dep = text_at(blob, offset + pos, offset + length, "dyld")
            if not dep or dep in deps:
                fail("dyld", "duplicate/empty dependency")
            deps.append(dep)
        elif command == LC_MAIN:
            if length != 24 or entry is not None or kind != MH_EXECUTE:
                fail("startup", "invalid or duplicate LC_MAIN")
            entry = struct.unpack_from("<Q", blob, offset + 8)[0]
        elif command == LC_SEGMENT_64:
            if length < 72:
                fail("dyld", "short LC_SEGMENT_64")
            segment_count += 1
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<4Q", blob, offset + 24)
            maxprot, initprot = struct.unpack_from("<2I", blob, offset + 56)
            nsections = struct.unpack_from("<I", blob, offset + 64)[0]
            if nsections > 128 or 72 + 80 * nsections != length:
                fail("dyld", "segment section table length mismatch")
            if filesize and (fileoff > len(blob) or filesize > len(blob) - fileoff):
                fail("dyld", "segment exceeds file data")
            if vmaddr + vmsize > (1 << 64) or fileoff + filesize > (1 << 64):
                fail("dyld", "segment integer overflow")
            if initprot & ~maxprot or (initprot & 2 and initprot & 4):
                fail("dyld", "W^X or protection mismatch")
            if initprot & 4:
                rx_bytes += vmsize
                ranges.append((fileoff, fileoff + filesize))
            for j in range(nsections):
                sec = offset + 72 + 80 * j
                try:
                    secname = blob[sec:sec + 16].split(b"\0", 1)[0].decode("ascii")
                except UnicodeDecodeError:
                    fail("objc metadata", "invalid section identifier")
                secsize = struct.unpack_from("<Q", blob, sec + 40)[0]
                secfile = struct.unpack_from("<I", blob, sec + 48)[0]
                section_type = struct.unpack_from("<I", blob, sec + 64)[0] & 255
                if section_type not in (1, 12, 18) and secsize:
                    if secfile > len(blob) or secsize > len(blob) - secfile:
                        fail("objc metadata", "section bytes exceed input")
                if secname in OBJC_SECTIONS and secsize:
                    objc.add(secname)
        elif command == LC_SYMTAB:
            if length != 24 or symtab is not None:
                fail("symbol binding", "invalid/duplicate symbol table")
            symtab = struct.unpack_from("<4I", blob, offset + 8)
        offset += length
    if offset != 32 + size:
        fail("dyld", "trailing unparsed load-command bytes")
    if not ranges or rx_bytes == 0:
        fail("dyld", "image lacks executable mapped segment")
    if kind == MH_EXECUTE:
        if entry is None or not any(lo <= entry < hi for lo, hi in ranges):
            fail("startup", "LC_MAIN entry not in executable file-backed range")
    elif entry is not None:
        fail("startup", "dylib unexpectedly specifies LC_MAIN")
    if symtab is not None:
        symoff, nsyms, stroff, strsize = symtab
        if nsyms > 16384 or symoff > len(blob) or nsyms * 16 > len(blob) - symoff:
            fail("symbol binding", "symbol table outside file")
        if stroff > len(blob) or strsize > len(blob) - stroff:
            fail("symbol binding", "string table outside file")
        for i in range(nsyms):
            so = symoff + 16 * i
            index, typebits = struct.unpack_from("<IB", blob, so)
            if index == 0:
                continue
            if index >= strsize:
                fail("symbol binding", "symbol name offset outside string table")
            symbol = text_at(blob, stroff + index, stroff + strsize, "symbol binding")
            if (typebits & 0xE) == 0 and typebits & 1:
                undefined.append(symbol)
    if not objc:
        fail("objc metadata", "image has no compiler-produced Objective-C references")
    return Image(
        name=name, sha256=hashlib.sha256(blob).hexdigest(), size=len(blob),
        kind="MH_EXECUTE" if kind == MH_EXECUTE else "MH_DYLIB",
        dependencies=tuple(deps), objc_sections=tuple(sorted(objc)),
        undefined=tuple(sorted(set(undefined))), entry_offset=entry,
        segment_count=segment_count, rx_bytes=rx_bytes,
    )


def inspect_file(path: Path) -> Image:
    return inspect_blob(path.read_bytes(), path.name)


def dependency_order(images: dict[str, Image], executable: str) -> list[str]:
    """Dependency-first deterministic traversal, fail closed on missing files."""
    visiting = set()
    visited = set()
    order = []

    def visit(name: str) -> None:
        if name in visiting:
            fail("dyld", "cyclic dependencies: " + name)
        if name in visited:
            return
        image = images.get(name)
        if image is None:
            fail("dyld", "unresolved dependency " + name)
        visiting.add(name)
        for dep in image.dependencies:
            if not dep.startswith("@rpath/lib") or not dep.endswith(".dylib"):
                fail("dyld", "dependency outside isolated catalog: " + dep)
            visit(dep[len("@rpath/"):])
        visiting.remove(name)
        visited.add(name)
        order.append(name)

    visit(executable)
    return order


def validate_startup(argv: tuple[str, ...], env: tuple[str, ...], apple: tuple[str, ...]) -> None:
    if not argv or not argv[0] or len(argv) > 32 or len(env) > 32 or len(apple) > 32:
        fail("startup", "invalid argc/argv/env/apple vector sizes")
    combined = 0
    for group in (argv, env, apple):
        for item in group:
            if not isinstance(item, str) or not item or "\0" in item:
                fail("startup", "invalid guest string")
            combined += len(item.encode("utf8")) + 1
    if combined > 16384:
        fail("startup", "guest startup strings exceed capacity")
    if not all("=" in item and not item.startswith("=") for item in env):
        fail("startup", "invalid environment pair")


def execution_gate(available: frozenset[str] = frozenset()) -> dict:
    """Never grant native execution via user configuration or host counters.

    Phase25a-d + phase24e adapters must be merged and independently verified
    on main before this gate can be replaced by a genuine guest executor.
    """
    required = frozenset(("dyld", "objc2", "dispatch", "ownership", "lifecycle", "startup"))
    return {
        "status": "blocked_unverified_native_runtime",
        "failure_domain": "integration",
        "missing_verified_interfaces": sorted(required - available),
        "darwin_objc_methods_executed": 0,
        "darwin_objc_sends_executed": 0,
        "ownership_transitions_executed": 0,
        "lifecycle_callbacks_executed": 0,
        "guest_instructions_traced": 0,
        "host_callbacks_as_darwin_code": 0,
    }
