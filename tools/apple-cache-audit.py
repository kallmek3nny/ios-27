#!/usr/bin/env python3
"""Read-only ARM64 iOS Simulator shared-cache and chained-import audit."""

import argparse
from collections import Counter, deque
from contextlib import ExitStack
from dataclasses import dataclass
from functools import cached_property, partial
import json
import mmap
from pathlib import Path, PurePosixPath
import re
import stat
from struct import Struct


MACH_HEADER = Struct("<8I")
SEGMENT = Struct("<16s4Q4I")
CACHE_MAGIC = b"dyld_v1   arm64\0"
CACHE_UUID = 88
CACHE_PLATFORM = 216
CACHE_REGION = 224
CACHE_SLIDES = 312
CACHE_SUBCACHES = 392
CACHE_IMAGES = 448
DEPENDENCY_KINDS = {
    0xC: "load",
    0x80000018: "weak",
    0x8000001F: "reexport",
    0x80000023: "upward",
    0x20: "lazy",
}
DEFERRED = {"weak", "lazy"}
# The upstream simulator cache builder imports these from the macOS host:
# dyld/cache_builder/update_dyld_sim_shared_cache.cpp, sMacOSHostLibs.
SIMULATOR_HOST_LIBRARIES = frozenset(
    f"/usr/lib/system/libsystem_{name}.dylib" for name in ("kernel", "platform", "pthread")
)


class AuditError(ValueError):
    pass


def check(condition, message):
    if not condition:
        raise AuditError(message)


def interval(offset, size, limit, label):
    check(0 <= offset <= limit and 0 <= size <= limit - offset,
          f"{label} is out of bounds")
    return offset + size


def unpack(fmt, data, offset, limit=None):
    layout = Struct("<" + fmt)
    interval(offset, layout.size, len(data) if limit is None else limit, fmt)
    return layout.unpack_from(data, offset)


def cstring(data, offset, limit):
    end = data.find(b"\0", offset, min(offset + 4096, limit))
    check(0 <= offset < limit and end >= offset, "invalid bounded string")
    return data[offset:end].decode("ascii")


def mapped_file(stack, path):
    check(stat.S_ISREG(path.stat().st_mode), f"expected a regular file: {path.name}")
    with path.open("rb") as source:
        return stack.enter_context(mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ))


@dataclass(frozen=True)
class Mapping:
    data: mmap.mmap
    part: int
    address: int
    size: int
    offset: int
    protection: int

    def locate(self, address, size):
        relative = address - self.address
        interval(relative, size, self.size, "cache VM span")
        return self.data, self.offset + relative


class Cache:
    def __init__(self, stack, path):
        main = mapped_file(stack, path)
        platform = self.header(main)
        region, region_size = unpack("QQ", main, CACHE_REGION)
        self.region = (region, region_size)
        self.patch_span = unpack("QQ", main, 152)
        sub_offset, sub_count = unpack("II", main, CACHE_SUBCACHES)
        check(sub_count <= 64, "too many cache parts")
        interval(sub_offset, sub_count * 56, len(main), "subcache table")
        parts = [(main, region)]
        suffixes = set()
        for i in range(sub_count):
            uuid, vm_offset, suffix = unpack("16sQ32s", main, sub_offset + i * 56)
            suffix, terminator, _ = suffix.partition(b"\0")
            check(terminator and re.fullmatch(rb"\.[A-Za-z0-9_-]+", suffix)
                  and suffix not in suffixes, "invalid or duplicate subcache suffix")
            suffixes.add(suffix)
            sub = mapped_file(stack, path.with_name(path.name + suffix.decode("ascii")))
            check(self.header(sub) == platform and sub[CACHE_UUID:CACHE_UUID + 16] == uuid,
                  "subcache platform or UUID mismatch")
            parts.append((sub, region + vm_offset))

        self.mappings = []
        self.slide_versions = Counter()
        for part, (data, base) in enumerate(parts):
            mappings = self.read_mappings(data, part)
            check(mappings[0].address == base and mappings[0].offset == 0,
                  "cache part base mismatch")
            for mapping in mappings:
                interval(mapping.address - region, mapping.size, region_size, "shared region")
            self.mappings.extend(mappings)
            slide_offset, slide_count = unpack("II", data, CACHE_SLIDES)
            check(slide_count in (0, len(mappings)), "slide mapping count mismatch")
            interval(slide_offset, slide_count * 56, len(data), "slide mapping table")
            for i in range(slide_count):
                row = unpack("6Q2I", data, slide_offset + i * 56)
                mapping = mappings[i]
                check(row[:3] == (mapping.address, mapping.size, mapping.offset),
                      "slide mapping geometry mismatch")
                offset, size = row[3:5]
                if size:
                    interval(offset, size, len(data), "slide info")
                    version, = unpack("I", data, offset, offset + size)
                    self.slide_versions[version] += 1

        self.mappings.sort(key=lambda m: m.address)
        check(all(left.address + left.size <= right.address
                  for left, right in zip(self.mappings, self.mappings[1:])),
              "overlapping cache VM mappings")
        images_offset, images_count = unpack("II", main, CACHE_IMAGES)
        check(0 < images_count <= 65536, "invalid cache image count")
        interval(images_offset, images_count * 32, len(main), "cache image table")
        self.paths = {}
        self.image_addresses = []
        for i in range(images_count):
            address, _, _, offset, _ = unpack("QQQII", main, images_offset + i * 32)
            path = cstring(main, offset, len(main))
            check(path.startswith("/") and path not in self.paths, "invalid cache image path")
            self.paths[path] = address
            self.image_addresses.append(address)
        self.parts = len(parts)
        self.images = {}

    @staticmethod
    def header(data):
        offset, = unpack("I", data, 16)
        check(data[:16] == CACHE_MAGIC and CACHE_IMAGES + 8 <= offset <= len(data),
              "expected a modern ARM64 simulator cache header")
        platform, flags = unpack("II", data, CACHE_PLATFORM)
        check(platform == 7 or (platform == 2 and flags & (1 << 9)),
              "cache does not declare iOS Simulator")
        return platform

    @staticmethod
    def read_mappings(data, part):
        offset, count = unpack("II", data, 16)
        check(0 < count <= 64, "invalid cache mapping count")
        interval(offset, count * 32, len(data), "mapping table")
        mappings = []
        for i in range(count):
            address, size, start, maximum, protection = unpack("QQQII", data, offset + i * 32)
            check(size and maximum <= 7 and not (protection & ~maximum),
                  "invalid cache mapping")
            interval(start, size, len(data), "cache mapping file span")
            mappings.append(Mapping(data, part, address, size, start, protection))
        return mappings

    def locate(self, address, size):
        for mapping in self.mappings:
            if mapping.address <= address < mapping.address + mapping.size:
                return mapping.locate(address, size)
        raise AuditError(f"unmapped cache address {address:#x}")

    def image(self, address):
        if address in self.images:
            return self.images[address]
        data, offset = self.locate(address, MACH_HEADER.size)
        size, = unpack("I", data, offset + 20)
        self.locate(address, MACH_HEADER.size + size)
        image = Image(data, offset, self)
        self.images[address] = image
        return image


@dataclass(frozen=True)
class Dependency:
    kind: str
    path: str


@dataclass(frozen=True)
class Section:
    name: bytes
    segment: bytes
    address: int
    size: int
    file_offset: int
    flags: int


class Image:
    def __init__(self, data, offset=0, cache=None):
        magic, cpu, subtype, self.kind, count, size, self.flags, _ = unpack("8I", data, offset)
        check(magic == 0xFEEDFACF and cpu == 0x0100000C and subtype == 0,
              "expected a thin generic ARM64 Mach-O")
        check(self.kind in (2, 6, 8), "expected an executable, dylib or bundle")
        self.data, self.cache = data, cache
        self.dependencies = []
        self.segments = {}
        self.sections = []
        self.install_name = None
        self.export_span = None
        self.fixup_span = None
        self.classic_bind_bytes = 0
        self.initializers = 0
        self.objc_load_candidates = 0
        self.entry_offset = None
        self.platform = None
        cursor = offset + MACH_HEADER.size
        end = interval(cursor, size, len(data), "load commands")
        check(count <= size // 8 and size <= 1 << 20, "invalid load command count or size")
        seen_dyld = set()
        for _ in range(count):
            cmd, length = unpack("II", data, cursor, end)
            check(length >= 8 and length % 8 == 0, "invalid load command length")
            limit = interval(cursor, length, end, "load command")
            if cmd == 0x19:  # LC_SEGMENT_64
                self.segment(cursor + 8, limit)
            elif cmd == 0xD or cmd in DEPENDENCY_KINDS:
                name, _, _, _ = unpack("4I", data, cursor + 8, limit)
                check(24 <= name < length, "invalid dylib name offset")
                path = cstring(data, cursor + name, limit)
                if cmd == 0xD:
                    check(self.install_name is None, "duplicate install name")
                    self.install_name = path
                else:
                    self.dependencies.append(Dependency(DEPENDENCY_KINDS[cmd], path))
            elif cmd == 0x80000028:  # LC_MAIN
                check(self.entry_offset is None, "duplicate LC_MAIN")
                self.entry_offset, _ = unpack("QQ", data, cursor + 8, limit)
            elif cmd == 0x80000034:  # LC_DYLD_CHAINED_FIXUPS
                check(self.fixup_span is None, "duplicate chained fixup command")
                self.fixup_span = unpack("II", data, cursor + 8, limit)
            elif cmd == 0x80000033:  # LC_DYLD_EXPORTS_TRIE
                check(cmd not in seen_dyld, "duplicate dyld export command")
                seen_dyld.add(cmd)
                self.export_span = unpack("II", data, cursor + 8, limit)
            elif cmd in (0x22, 0x80000022):  # LC_DYLD_INFO(_ONLY)
                check(0x22 not in seen_dyld, "duplicate dyld info command")
                seen_dyld.add(0x22)
                sizes = unpack("10I", data, cursor + 8, limit)
                self.classic_bind_bytes = sizes[3] + sizes[5] + sizes[7]
                if self.export_span is None:
                    self.export_span = sizes[8:10]
            elif cmd == 0x32:  # LC_BUILD_VERSION
                check(self.platform is None, "duplicate platform command")
                self.platform, _, _, _ = unpack("4I", data, cursor + 8, limit)
            cursor = limit
        check(cursor == end and b"__TEXT" in self.segments, "invalid Mach-O command extent")

    def segment(self, start, end):
        name, address, vm_size, offset, file_size, _, protection, count, _ = unpack(
            "16s4Q4I", self.data, start, end)
        name = name.rstrip(b"\0")
        check(name not in self.segments and file_size <= vm_size, "invalid segment")
        self.segments[name] = (address, vm_size, offset, file_size, protection)
        start += SEGMENT.size
        check(start + count * 80 == end, "invalid section table")
        for i in range(count):
            section = start + i * 80
            name, segment, address, size, file_offset, _, _, _, flags, _, _, _ = unpack(
                "16s16s2Q8I", self.data, section, end)
            self.sections.append(Section(name.rstrip(b"\0"), segment.rstrip(b"\0"),
                                         address, size, file_offset, flags))
            kind = flags & 0xFF
            if kind in (0x9, 0x16):  # S_MOD_INIT_FUNC_POINTERS / S_INIT_FUNC_OFFSETS
                width = 8 if kind == 0x9 else 4
                check(size % width == 0, "misaligned initializer section")
                self.initializers += size // width
            if name.rstrip(b"\0") in (b"__objc_nlclslist", b"__objc_nlcatlist"):
                check(size % 8 == 0, "misaligned ObjC non-lazy list")
                self.objc_load_candidates += size // 8

    @cached_property
    def host_placeholder_ranges(self):
        """Detect the observed zero-code host placeholders, not arbitrary stubs.

        A matching export is metadata, not an implemented function. Restrict
        this classification to the three documented host libraries in a cache,
        and require every declared instruction section to be bounded and zero.
        Nonzero code is simply unclassified, never proof of compatibility.
        """
        if self.cache is None or self.install_name not in SIMULATOR_HOST_LIBRARIES:
            return ()
        code = [section for section in self.sections if section.flags & 0x80000400 and section.size]
        if not code:
            return ()
        ranges, total = [], 0
        all_zero = True
        for section in code:
            check(section.segment in self.segments and section.flags & 0xFF == 0
                  and section.address % 4 == section.size % 4 == 0,
                  "invalid simulator host code section")
            address, _, offset, file_size, protection = self.segments[section.segment]
            check(protection == 5, "simulator host code is not RX")
            interval(section.address - address, section.size, file_size, "simulator host code VM")
            interval(section.file_offset - offset, section.size, file_size, "simulator host code file")
            check(section.address - address == section.file_offset - offset,
                  "simulator host code file/VM mismatch")
            total += section.size
            check(total <= 1024 * 1024, "simulator host code inspection limit exceeded")
            data, start = self.cache.locate(section.address, section.size)
            all_zero &= not any(data[start:start + section.size])
            ranges.append((section.address, section.address + section.size))
        return tuple(ranges) if all_zero else ()

    def export_is_host_placeholder(self, flags, offset):
        if flags & 3:  # absolute/TLS symbols are not instruction addresses
            return False
        address = self.segments[b"__TEXT"][0] + offset
        return any(start <= address and address + 4 <= end
                   for start, end in self.host_placeholder_ranges)

    def linkedit(self, span):
        if not span or not span[1]:
            return b""
        offset, size = span
        check(b"__LINKEDIT" in self.segments, "missing linkedit segment")
        address, _, start, length, _ = self.segments[b"__LINKEDIT"]
        interval(offset - start, size, length, "linkedit span")
        check(size <= 1 << 26, "linkedit audit limit exceeded")
        data = self.data
        if self.cache:
            data, offset = self.cache.locate(address + offset - start, size)
        return data[offset:interval(offset, size, len(data), "linkedit file span")]

    def chained_imports(self):
        data = self.linkedit(self.fixup_span)
        if not data:
            return []
        version, starts, imports, symbols, count, kind, encoding = unpack("7I", data, 0)
        check(version == encoding == 0 and kind in (1, 2, 3),
              "unsupported chained-import version, format or symbol encoding")
        width = {1: 4, 2: 8, 3: 16}[kind]
        check(28 <= starts <= imports <= symbols <= len(data) and count <= 65536,
              "invalid chained-import table")
        interval(imports, count * width, symbols, "chained imports")
        result = []
        for i in range(count):
            pos = imports + i * width
            if kind == 3:
                raw, addend = unpack("Qq", data, pos, symbols)
                check((raw >> 17) & 0x7FFF == 0, "reserved chained import bits")
                ordinal, weak, name, maximum = raw & 0xFFFF, (raw >> 16) & 1, raw >> 32, 0xFFFF
            else:
                raw, = unpack("I", data, pos, symbols)
                addend = unpack("i", data, pos + 4, symbols)[0] if kind == 2 else 0
                ordinal, weak, name, maximum = raw & 0xFF, (raw >> 8) & 1, raw >> 9, 0xFF
            if ordinal > maximum - 15:
                ordinal -= maximum + 1
            check(ordinal <= len(self.dependencies), "chained import ordinal outside dylib table")
            result.append((ordinal, bool(weak), cstring(data, symbols + name, len(data)), addend))
        return result

    def chain_sites(self, import_count, records=None):
        data = self.linkedit(self.fixup_span)
        if not data:
            return Counter(), [], 0
        check(self.cache is None, "chain site census requires a standalone image")
        starts, imports = unpack("II", data, 4)
        count, = unpack("I", data, starts, imports)
        check(count <= len(self.segments), "too many chained segment records")
        table_end = interval(starts + 4, count * 4, imports, "chained segment table")
        base = self.segments[b"__TEXT"][0]
        formats, binds, rebases = Counter(), [], 0
        for i, (address, vm_size, file_offset, file_size, protection) in enumerate(
                list(self.segments.values())[:count]):
            relative, = unpack("I", data, starts + 4 + i * 4, table_end)
            if not relative:
                continue
            pos = starts + relative
            check(pos >= table_end, "chained segment record overlaps its table")
            size, page_size, fmt, vm_offset, _, pages = unpack("IHHQIH", data, pos, imports)
            end = interval(pos, size, imports, "chained segment record")
            page_table_end = interval(pos + 22, pages * 2, end, "chained page table")
            check(fmt in (2, 6) and page_size in (4096, 16384)
                  and vm_offset == address - base and (protection & 3) == 3
                  and not (protection & 4) and pages * page_size <= vm_size,
                  "unsupported chained segment geometry or pointer format")
            formats[fmt] += 1
            for page in range(pages):
                first, = unpack("H", data, pos + 22 + page * 2, page_table_end)
                if first == 0xFFFF:
                    continue
                check(not (first & 0x8000), "multi-start chained page is unsupported")
                slot = first
                while True:
                    check(slot % 4 == 0, "misaligned chained slot")
                    interval(slot, 8, page_size, "chained slot in page")
                    relative = page * page_size + slot
                    interval(relative, 8, file_size, "chained slot in segment")
                    raw, = unpack("Q", self.data, file_offset + relative)
                    if records is not None:
                        records.append((file_offset + relative, fmt, raw))
                    if raw >> 63:
                        index = raw & 0xFFFFFF
                        check(index < import_count and not ((raw >> 32) & 0x7FFFF),
                              "invalid chained bind slot")
                        addend = (raw >> 24) & 0xFF
                        binds.append((vm_offset + relative, index,
                                      addend - 256 if addend >= 128 else addend))
                    else:
                        check(not ((raw >> 44) & 0x7F), "reserved chained rebase bits")
                        rebases += 1
                    distance = ((raw >> 51) & 0xFFF) * 4
                    if not distance:
                        break
                    check(distance >= 8, "overlapping chained slots")
                    slot += distance
        return formats, binds, rebases

    def export(self, name):
        return trie_lookup(self.exports, name)

    @cached_property
    def exports(self):
        return self.linkedit(self.export_span)


def uleb(data, cursor, end):
    result = 0
    for shift in range(0, 70, 7):
        check(cursor < end, "truncated export trie integer")
        byte = data[cursor]
        cursor += 1
        check(shift < 63 or byte <= 1, "overflowing export trie integer")
        result |= (byte & 127) << shift
        if byte < 128:
            return result, cursor
    raise AuditError("overflowing export trie integer")


def trie_lookup(data, name):
    if not data:
        return None
    node, seen = 0, set()
    while True:
        check(node < len(data) and node not in seen, "invalid export trie node")
        seen.add(node)
        size, cursor = uleb(data, node, len(data))
        end = interval(cursor, size, len(data), "export terminal")
        result = None
        if size:
            flags, cursor = uleb(data, cursor, end)
            value, cursor = uleb(data, cursor, end)
            target = None
            if flags & 8:  # EXPORT_SYMBOL_FLAGS_REEXPORT
                target = cstring(data, cursor, end)
                cursor += len(target) + 1
            elif flags & 0x10:  # EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER
                _, cursor = uleb(data, cursor, end)
            check(cursor == end, "invalid export terminal extent")
            if not name:
                result = flags, value, target
        check(end < len(data), "missing export trie child count")
        cursor = end + 1
        match = None
        for _ in range(data[end]):
            edge = cstring(data, cursor, len(data))
            check(edge, "empty export trie edge")
            child, cursor = uleb(data, cursor + len(edge) + 1, len(data))
            check(0 < child < len(data), "export trie child outside trie")
            if name.startswith(edge):
                check(match is None, "ambiguous export trie edge")
                match = child, name[len(edge):]
        if result is not None:
            return result
        if match is None:
            return None
        node, name = match


class Resolver:
    def __init__(self, cache, app, external, open_standalone=None):
        self.cache = cache
        self.app = app
        self.external = external
        self.open_standalone = open_standalone
        self.absent = set()

    def image(self, path):
        if path == "<app>":
            return self.app
        if path in self.external:
            return self.external[path]
        if path in self.cache.paths:
            return self.cache.image(self.cache.paths[path])
        if self.open_standalone and path not in self.absent:
            image = self.open_standalone(path)
            if image is not None:
                self.external[path] = image
                return image
            self.absent.add(path)
        return None

    def closure(self):
        queue = deque([("<app>", True)])
        visited, missing, images = set(), set(), {}
        while queue:
            path, required = queue.popleft()
            if (path, required) in visited:
                continue
            visited.add((path, required))
            image = self.image(path)
            if image is None:
                missing.add((path, required))
                continue
            images[path] = image
            for dep in image.dependencies:
                queue.append((dep.path, required and dep.kind not in DEFERRED))

        return images, {
            "required_paths": sum(required for _, required in visited),
            "cached_paths": sum(path in self.cache.paths and path not in self.external
                                for path in images),
            "unique_cached_images": len({self.cache.paths[path] for path in images
                                         if path in self.cache.paths and path not in self.external}),
            "standalone_dylibs": sum(path in self.external for path in images),
            "missing": [
                {"install_name": path, "required": (path, True) in missing,
                 "referenced_by": sorted(
                     [source, dep.kind] for source, image in images.items()
                     for dep in image.dependencies if dep.path == path)}
                for path in sorted({path for path, _ in missing})
            ],
            "declared_initializer_slots": sum(img.initializers for img in set(images.values())),
            "objc_nonlazy_list_entries": sum(img.objc_load_candidates for img in set(images.values())),
        }

    def resolve(self, path, name, seen=frozenset()):
        key = (path, name)
        if key in seen:
            return None
        check(len(seen) < 128, "reexport depth limit exceeded")
        image = self.image(path)
        if image is None:
            return None
        result = image.export(name)
        seen = seen | {key}
        if result:
            flags, value, target = result
            if target is None:
                return path, image, flags, value
            check(0 < value <= len(image.dependencies), "invalid export reexport ordinal")
            return self.resolve(image.dependencies[value - 1].path, target or name, seen)
        for dep in image.dependencies:
            if dep.kind == "reexport":
                result = self.resolve(dep.path, name, seen)
                if result:
                    return result
        return None

    def bind_census(self, image_path, image):
        check(not image.classic_bind_bytes, f"classic binds are outside the census for {image_path}")
        imports = image.chained_imports()
        formats, binds, rebases = image.chain_sites(len(imports))
        use = Counter(index for _, index, _ in binds)
        counts, missing, resolved = Counter(), {}, {}
        for index, (ordinal, weak, name, _) in enumerate(imports):
            if index not in use:
                counts["unused"] += 1
                continue
            if ordinal > 0:
                path = image.dependencies[ordinal - 1].path
            elif ordinal == 0:
                path = image_path
            elif ordinal == -1:
                path = "<app>"
            else:
                counts["special_ordinal"] += 1
                missing.setdefault(("special_ordinal", str(ordinal)), []).append(name)
                continue
            result = self.resolve(path, name)
            if result:
                provider, target, flags, value = result
                if target.export_is_host_placeholder(flags, value):
                    counts["host_placeholder"] += 1
                    missing.setdefault(("host_placeholder", provider), []).append(name)
                else:
                    counts["resolved"] += 1
                resolved[index] = (provider, target, flags, value)
            else:
                reason = "unresolved_weak" if weak else "unresolved_required"
                counts[reason] += 1
                missing.setdefault((reason, path), []).append(name)

        return {
            "chained_import_entries": len(imports),
            "chained_bind_sites": len(binds),
            "chained_rebase_sites": rebases,
            "chained_segment_pointer_formats": dict(formats),
            "import_entries_by_result": dict(counts),
            "unresolved_by_provider": [
                {"result": reason, "provider": path, "count": len(names),
                 "examples": sorted(set(names))[:8]}
                for (reason, path), names in sorted(missing.items())
            ],
        }, imports, binds, resolved


def host_runtime_requirements(images, resolver):
    """Summarize observed host placeholders and representative required APIs."""
    placeholders = []
    for path, image in images.items():
        ranges = image.host_placeholder_ranges
        if ranges:
            placeholders.append({"install_name": path,
                                 "zero_code_bytes": sum(end - start for start, end in ranges),
                                 "instruction_sections": len(ranges)})
    probes = []
    for library, symbols in (
            ("kernel", ("_getpid", "_mach_msg2_trap", "___libkernel_init")),
            ("pthread", ("_pthread_getspecific", "___pthread_init")),
            ("platform", ("___libplatform_init",))):
        path = f"/usr/lib/system/libsystem_{library}.dylib"
        if path not in images:
            continue
        for symbol in symbols:
            result = resolver.resolve(path, symbol)
            backing = "missing_export"
            if result:
                _, image, flags, offset = result
                backing = ("zero_filled_host_placeholder" if image.export_is_host_placeholder(flags, offset)
                           else "export_present_not_execution_verified")
            probes.append({"library": path, "symbol": symbol, "backing": backing})
    return {"zero_filled_host_libraries": sorted(placeholders, key=lambda row: row["install_name"]),
            "required_api_probes": probes,
            "execution_verified": False,
            "scope": "observed simulator host placeholders; no API execution or compatibility claim"}


def audit(cache, app, external, open_standalone=None):
    resolver = Resolver(cache, app, external, open_standalone)
    images, closure = resolver.closure()
    report = {
        "scope": "read-only iOS Simulator dependency and symbol audit",
        "cache_parts": cache.parts,
        "cache_paths": len(cache.paths),
        "cache_region": {"preferred_address": cache.region[0], "size": cache.region[1]},
        "cache_vm_mappings": [
            {"part": m.part, "address": m.address, "size": m.size,
             "file_offset": m.offset, "initial_protection": m.protection}
            for m in cache.mappings
        ],
        "cache_slide_info_versions": dict(cache.slide_versions),
        "dependency_closure": closure,
        "host_runtime": host_runtime_requirements(images, resolver),
        "standalone_images": {},
    }
    for path, image in images.items():
        if path != "<app>" and path not in external:
            continue
        census, imports, binds, resolved = resolver.bind_census(path, image)
        census["declared_initializer_slots"] = image.initializers
        census["objc_nonlazy_list_entries"] = image.objc_load_candidates
        if path == "<app>":
            census["lc_main_file_offset"] = app.entry_offset
            entries = []
            for offset, index, site_addend in binds:
                ordinal, weak, name, import_addend = imports[index]
                entry = {"symbol": name, "library_ordinal": ordinal, "weak": weak,
                         "slot_image_offset": offset, "addend": import_addend + site_addend}
                if index in resolved:
                    provider, target, flags, value = resolved[index]
                    address, size, file_start, file_size, protection = target.segments[b"__TEXT"]
                    entry.update({"export_provider": provider, "export_flags": flags,
                                  "export_value": value})
                    if (flags & 3) == 0 and 0 <= value < min(size, file_size) and protection & 4:
                        entry["export_text_vm_address"] = address + value
                        if not target.cache:
                            entry["export_file_offset"] = file_start + value
                entries.append(entry)
            census["bind_sites"] = entries
        report["standalone_images"][path] = census
    return report


def standalone(stack, root, install_name):
    virtual = PurePosixPath(install_name)
    if virtual.anchor != "/" or ".." in virtual.parts or any(
            "\\" in part or ":" in part for part in virtual.parts):
        return None
    path = root.joinpath(*virtual.parts[1:]).resolve()
    check(path.is_relative_to(root), "runtime-root path escapes its root")
    if not path.is_file():
        return None
    image = Image(mapped_file(stack, path))
    check(image.kind == 6 and image.platform == 7 and image.install_name == install_name,
          f"unexpected platform, file type or install name for {install_name}")
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True, help="main ARM64 simulator cache file")
    parser.add_argument("--app", type=Path, required=True, help="standalone ARM64 Mach-O executable")
    parser.add_argument("--dylib", type=Path, action="append", default=[],
                        help="standalone dylib, keyed by LC_ID_DYLIB (repeatable)")
    parser.add_argument("--runtime-root", type=Path,
                        help="look up non-cached absolute install names below this local root")
    args = parser.parse_args()
    try:
        with ExitStack() as stack:
            cache = Cache(stack, args.cache)
            app = Image(mapped_file(stack, args.app))
            check(app.kind == 2 and app.platform == 7, "app must be an iOS Simulator MH_EXECUTE")
            external = {}
            for path in args.dylib:
                image = Image(mapped_file(stack, path))
                name = image.install_name
                check(image.kind == 6 and image.platform == 7 and name
                      and name.startswith("/") and name not in external,
                      "expected an iOS Simulator dylib with a unique absolute install name")
                external[name] = image
            open_standalone = None
            if args.runtime_root:
                root = args.runtime_root.resolve(strict=True)
                check(root.is_dir(), "runtime root is not a directory")
                open_standalone = partial(standalone, stack, root)
            report = audit(cache, app, external, open_standalone)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"audit failed: {exc}\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
