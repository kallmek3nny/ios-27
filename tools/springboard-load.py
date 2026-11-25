#!/usr/bin/env python3
"""Load the audited Simulator SpringBoard graph as private, non-executable data.

This is a loader checkpoint, NOT a SpringBoard launcher. It never calls an
Apple entry point, installs a device package, or grants executable permission.
"""

import argparse
from contextlib import ExitStack
import ctypes
from dataclasses import dataclass
from functools import partial
from hashlib import sha256
from importlib.util import module_from_spec, spec_from_file_location
import json
import mmap
import os
from pathlib import Path
import stat
from struct import unpack_from
import sys


spec = spec_from_file_location("springboard_fixup_stage", Path(__file__).with_name("apple-fixup-stage.py"))
staging = module_from_spec(spec)
spec.loader.exec_module(staging)
audit = staging.audit

# Exact local-input profile already recorded by the earlier dependency audit
# and Android mapping probe. These are hashes, never distributed Apple bytes.
PROFILE = {
    "binaries/SpringBoard.app/SpringBoard":
        (133872, "64b0179b4e91b9b78078992927676783d292b38b59f113a8814965b661706bd6"),
    "binaries/SpringBoard.framework/SpringBoard":
        (36539312, "7fdfe20b9b178bf853d46d776ff87ec9e722c36dee7820d79c285722eca9c5d7"),
    "dyld-cache/dyld_sim_shared_cache_arm64":
        (2043297792, "261b3c34a02657479cb77e0b87ec4b93dc1c20c4a7325b6ab7b2e8370947a0ef"),
    "dyld-cache/dyld_sim_shared_cache_arm64.01":
        (1786019840, "b3751143a1b94e2f6798adf77bcd2c7c24755f3e9ef4aa83c2f6ce376f646396"),
}
MAX_IMAGE = 128 * 1024 * 1024
MAX_STANDALONE = 1024 * 1024 * 1024
MAX_CACHE = 8 * 1024 * 1024 * 1024
MAX_IMAGES = 256
U64_MAX = (1 << 64) - 1


def rounded(value, page):
    audit.check(0 <= value <= U64_MAX - page + 1, "mapping size overflow")
    return (value + page - 1) & -page


@dataclass(frozen=True)
class Region:
    address: int
    size: int
    offset: int
    file_size: int
    zeroes: tuple = ()


@dataclass(frozen=True)
class ImagePlan:
    path: str
    base: int
    size: int
    data: bytearray
    regions: tuple


def image_plan(path, image, base, data, page):
    """Validate all ranges before the first native mapping is attempted."""
    audit.check(0 < len(data) <= MAX_IMAGE, "standalone image exceeds load limit")
    audit.check(base >= 65536 and base % page == 0, "unaligned standalone base")
    preferred = image.segments[b"__TEXT"][0]
    commands, command_bytes = audit.unpack("II", data, 16)
    cursor, end = 32, 32 + command_bytes
    regions = []
    for _ in range(commands):
        command, length = audit.unpack("II", data, cursor, end)
        command_end = audit.interval(cursor, length, end, "mapping load command")
        if command == 0x19:
            name, vm, size, offset, file_size, maximum, protection, count, _ = audit.unpack(
                "16s4Q4I", data, cursor + 8, command_end)
            name = name.rstrip(b"\0")
            if name == b"__PAGEZERO":
                audit.check(vm == offset == file_size == maximum == protection == count == 0,
                            "invalid page-zero mapping")
                cursor = command_end
                continue
            audit.check(maximum <= 7 and protection in (1, 3, 5)
                        and not protection & ~maximum and maximum & 6 != 6,
                        "unsupported standalone mapping protection")
            audit.check(vm >= preferred and file_size <= size and size <= MAX_IMAGE
                        and vm <= U64_MAX - size,
                        "invalid standalone mapping geometry")
            audit.check(vm - preferred <= MAX_IMAGE - size and (vm - preferred) % page == 0,
                        "unaligned or oversized standalone mapping")
            audit.interval(offset, file_size, len(data), "standalone segment bytes")
            audit.check(cursor + 72 + 80 * count == command_end, "invalid mapping section table")
            zeroes = []
            for i in range(count):
                section = cursor + 72 + i * 80
                _, segment_name, address, section_size, section_offset, _, _, _, flags, _, _, _ = audit.unpack(
                    "16s16s2Q8I", data, section, command_end)
                audit.check(segment_name.rstrip(b"\0") == name, "section/segment name mismatch")
                audit.interval(address - vm, section_size, size, "section VM range")
                if flags & 0xFF in (1, 0xC, 0x12):
                    zeroes.append((address - vm, section_size))
                elif section_size:
                    audit.interval(section_offset - offset, section_size, file_size,
                                   "section file range")
                    audit.check(section_offset - offset == address - vm,
                                "section file/VM mismatch")
            if size:
                regions.append(Region(base + vm - preferred, rounded(size, page),
                                      offset, file_size, tuple(zeroes)))
        cursor = command_end
    audit.check(cursor == end and regions, "empty or malformed standalone mapping plan")
    regions.sort(key=lambda region: region.address)
    audit.check(all(left.address + left.size <= right.address
                    for left, right in zip(regions, regions[1:])), "overlapping mapping granules")
    file_ranges = sorted((region.offset, region.offset + region.file_size)
                         for region in regions if region.file_size)
    audit.check(all(left[1] <= right[0] for left, right in zip(file_ranges, file_ranges[1:])),
                "overlapping standalone file segments")
    fixups = []
    image.chain_sites(len(image.chained_imports()), fixups)
    for offset, _, _ in fixups:
        owners = [region for region in regions
                  if region.offset <= offset and offset + 8 <= region.offset + region.file_size]
        audit.check(len(owners) == 1, "fixup is not in one file-backed mapping")
        region = owners[0]
        relative = offset - region.offset
        audit.check(not any(relative < start + length and start < relative + 8
                            for start, length in region.zeroes), "fixup overlaps zero-fill section")
    span = regions[-1].address + regions[-1].size - base
    audit.check(0 < span <= MAX_IMAGE and base <= U64_MAX - span, "standalone mapping overflow")
    return ImagePlan(path, base, span, data, tuple(regions))


def plan(cache, app, external, open_standalone, page, weak_policy, progress=None, platform_memory=None,
         patch_cache=False):
    audit.check(page in (4096, 16384, 65536), "unsupported host mapping page size")
    prepared = []

    def record(*args):
        prepared.append(args)
        if progress:
            progress(f"staged {args[0]}")

    result = staging.stage(cache, app, external, open_standalone, weak_policy=weak_policy,
                           prepared=record, platform_memory=platform_memory)
    audit.check(not result["deferred_images"], "unresolved standalone imports prevent mapping")
    audit.check(0 < len(prepared) <= MAX_IMAGES, "standalone image count exceeds load limit")
    audit.check(not cache.slide_versions, "cache slide fixups are not implemented by this loader")
    images = tuple(image_plan(*item, page) for item in prepared)
    audit.check(sum(image.size for image in images) <= MAX_STANDALONE,
                "standalone graph exceeds mapping quota")
    audit.check(sum(mapping.size for mapping in cache.mappings) <= MAX_CACHE,
                "cache exceeds mapping quota")
    spans = [(image.base, image.base + image.size) for image in images]
    for mapping in cache.mappings:
        audit.check(mapping.address >= 65536 and mapping.address % page == 0
                    and mapping.offset % page == 0 and mapping.size % page == 0
                    and mapping.size > 0 and mapping.address <= U64_MAX - mapping.size,
                    "unaligned or overflowing cache mapping")
        audit.check(mapping.protection in (1, 3, 5), "unsupported cache mapping protection")
        spans.append((mapping.address, mapping.address + mapping.size))
    spans.sort()
    audit.check(all(left[1] <= right[0] for left, right in zip(spans, spans[1:])),
                "cache/standalone mapping overlap")
    if patch_cache:
        audit.check(platform_memory is not None, "cache platform patches require --platform-memory")
        module_spec = spec_from_file_location("cache_platform_overrides", Path(__file__).with_name("cache-platform-overrides.py"))
        patcher = module_from_spec(module_spec)
        module_spec.loader.exec_module(patcher)
        resolver = audit.Resolver(cache, app, {})
        exports = staging.validate_platform_memory(platform_memory)
        replacement_base = result["images"][staging.PLATFORM_MEMORY_INSTALL_NAME]["vm_base"]
        targets = {}
        for symbol in sorted(exports):
            original = resolver.resolve("/usr/lib/libSystem.B.dylib", symbol)
            audit.check(original and original[0] == staging.PLATFORM_HOST
                        and original[1].export_is_host_placeholder(original[2], original[3]),
                        f"cache platform symbol is not a host placeholder: {symbol}")
            old = original[1].segments[b"__TEXT"][0] + original[3]
            # Darwin may alias memcpy to memmove. The overlap-safe routine
            # also satisfies memcpy's non-overlapping-buffer contract.
            canonical = "_memmove" if symbol == "_memcpy" else symbol
            new = replacement_base + exports[canonical]
            audit.check(old not in targets or targets[old] == new, "incompatible platform export aliases")
            targets[old] = new
        result["cache_platform_patches"] = patcher.plan(cache, targets)
        result["cache_platform_patch_count"] = len(result["cache_platform_patches"])
    return images, result


class NativeMemory:
    """Owned MAP_FIXED_NOREPLACE reservations; deliberately no executable mode."""

    def __init__(self):
        audit.check(sys.platform in ("linux", "android") and ctypes.sizeof(ctypes.c_void_p) == 8,
                    "native mapping requires 64-bit Linux/Android")
        self.page = os.sysconf("SC_PAGE_SIZE")
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,
                                  ctypes.c_int, ctypes.c_int, ctypes.c_long]
        self.libc.mmap.restype = ctypes.c_void_p
        self.libc.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        self.libc.mprotect.restype = ctypes.c_int
        self.libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.libc.munmap.restype = ctypes.c_int
        self.owned = []

    def _contains(self, address, size):
        audit.check(size >= 0 and any(base <= address and address + size <= base + length
                                     for base, length in self.owned), "access outside owned mapping")

    def reserve(self, address, size, fd=-1, offset=0):
        audit.check(address >= 65536 and address % self.page == size % self.page == offset % self.page == 0
                    and size > 0 and address <= U64_MAX - size, "invalid native mapping range")
        audit.check(all(address + size <= base or base + length <= address
                        for base, length in self.owned), "overlapping owned mappings")
        # File-backed cache pages are read-only. Anonymous images start as
        # inaccessible reservations. No MAP_FIXED replacement or RWX path.
        flags = 0x02 | 0x100000 | (0x20 if fd == -1 else 0)
        pointer = self.libc.mmap(address, size, 0 if fd == -1 else 1, flags, fd, offset)
        if pointer == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_errno(), "private mapping refused (possibly address occupied)")
        if pointer != address:
            if pointer is not None:
                self.libc.munmap(pointer, size)
            raise ValueError("kernel did not honor non-replacing fixed mapping")
        self.owned.append((address, size))

    def protect(self, address, size, mode):
        self._contains(address, size)
        audit.check(size > 0 and mode in (0, 1, 3)
                    and address % self.page == size % self.page == 0,
                    "invalid/non-executable mapping protection")
        if self.libc.mprotect(address, size, mode):
            raise OSError(ctypes.get_errno(), "mapping protection failed")

    def write(self, address, data):
        self._contains(address, len(data))
        if data:
            view = (ctypes.c_ubyte * len(data)).from_buffer(data)
            ctypes.memmove(address, view, len(data))

    def zero(self, address, size):
        self._contains(address, size)
        if size:
            ctypes.memset(address, 0, size)

    def digest(self, address, size):
        self._contains(address, size)
        return sha256(ctypes.string_at(address, size)).digest()

    def word(self, address):
        self._contains(address, 8)
        return ctypes.c_uint64.from_address(address).value

    def verify_permissions(self, expected):
        actual = []
        with open("/proc/self/maps", encoding="ascii") as source:
            for line in source:
                fields = line.split()
                begin, end = (int(value, 16) for value in fields[0].split("-"))
                actual.append((begin, end, fields[1]))
        for start, end, mode in expected:
            cursor = start
            for begin, limit, observed in actual:
                if begin <= cursor < limit:
                    audit.check(observed == mode, "native mapping permissions did not match plan")
                    cursor = min(limit, end)
                    if cursor == end:
                        break
            audit.check(cursor == end, "native mapping has an unexpected hole")

    def close(self):
        failed = []
        for address, size in reversed(self.owned):
            if self.libc.munmap(address, size):
                failed.append((address, size))
        self.owned = failed
        if failed:
            raise OSError(ctypes.get_errno(), "native mapping cleanup failed")

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def materialize(cache, images, memory, fds, progress=None, cache_patches=()):
    """The caller owns cleanup, including every partially mapped failure."""
    permissions = []
    copied = zeroed = 0
    patch_pages = {}
    for patch in cache_patches:
        address = patch["address"]
        page = address & -memory.page
        audit.check(address % 8 == 0 and any(m.address <= page and page + memory.page <= m.address + m.size
                                            and m.protection in (1, 3) for m in cache.mappings),
                    "cache patch page is not non-executable data")
        data, offset = cache.locate(address, 8)
        audit.check(unpack_from("<Q", data, offset)[0] == patch["original"], "cache patch source changed")
        audit.check(any(image.base <= patch["target"] < image.base + image.size for image in images),
                    "cache replacement target is outside loaded images")
        patch_pages.setdefault(page, []).append(patch)
    for mapping in cache.mappings:
        memory.reserve(mapping.address, mapping.size, fds[mapping.part], mapping.offset)
        permissions.append((mapping.address, mapping.address + mapping.size, "r--p"))
    for image in images:
        if progress:
            progress(f"mapping {image.path}")
        memory.reserve(image.base, image.size)
        cursor = image.base
        for region in image.regions:
            if cursor < region.address:
                permissions.append((cursor, region.address, "---p"))
            memory.protect(region.address, region.size, 3)
            expected = bytearray(image.data[region.offset:region.offset + region.file_size])
            memory.write(region.address, expected)
            for offset, size in region.zeroes:
                memory.zero(region.address + offset, size)
                audit.check(memory.digest(region.address + offset, size) == sha256(bytes(size)).digest(),
                            "mapped zero-fill section is not zero")
                overlap = max(0, min(region.file_size - offset, size))
                if overlap:
                    expected[offset:offset + overlap] = b"\0" * overlap
                zeroed += size
            audit.check(memory.digest(region.address, region.file_size) == sha256(expected).digest(),
                        "mapped image readback did not match staged bytes")
            memory.protect(region.address, region.size, 1)
            copied += region.file_size
            cursor = region.address + region.size
            permissions.append((region.address, cursor, "r--p"))
    app = next(image for image in images if image.path == "<app>")
    framework = next(image for image in images if image.path == staging.SPRINGBOARD)
    audit.check(memory.word(app.base + 0x18008) == framework.base + 0x8AFC,
                "mapped SpringBoard entry bind mismatch")
    for page, patches in patch_pages.items():
        memory.protect(page, memory.page, 3)
        for patch in patches:
            audit.check(memory.word(patch["address"]) == patch["original"], "mapped cache pointer changed")
            memory.write(patch["address"], bytearray(patch["target"].to_bytes(8, "little")))
            audit.check(memory.word(patch["address"]) == patch["target"], "cache pointer readback mismatch")
        memory.protect(page, memory.page, 1)
    memory.verify_permissions(permissions)
    return {"cache_regions": len(cache.mappings), "standalone_images": len(images),
            "copied_standalone_bytes": copied, "zero_filled_bytes": zeroed,
            "cache_mapped_bytes": sum(mapping.size for mapping in cache.mappings),
            "entry_bind_verified": True, "permissions_verified": True,
            "cache_platform_pointers_replaced": len(cache_patches),
            "executable_pages": 0}


def verified_inputs(root, progress=None):
    paths = {}
    for relative, (size, expected) in PROFILE.items():
        if progress:
            progress(f"verifying {relative}")
        path = root / relative
        audit.check(path.is_file() and not path.is_symlink(), f"missing/linked profile input: {relative}")
        audit.check(path.stat().st_size == size, f"profile size mismatch: {relative}")
        with path.open("rb") as source:
            audit.check(os.fstat(source.fileno()).st_size == size, f"profile size changed: {relative}")
            # Feed the read-only buffer directly to hashlib instead of
            # allocating/copying multi-megabyte chunks across a WSL mount.
            with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
                digest = sha256(data).hexdigest()
        audit.check(digest == expected, f"profile SHA-256 mismatch: {relative}")
        paths[relative] = path
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-root", type=Path, required=True,
                        help="existing external ios27-runtime-inputs directory (no downloads)")
    parser.add_argument("--runtime-root", type=Path,
                        help="override the extracted RuntimeRoot below springboard-closure-inputs")
    parser.add_argument("--map", action="store_true", help="materialize private non-executable mappings, then release them")
    parser.add_argument("--weak-policy", choices=("unique", "load-order"), default="unique")
    parser.add_argument("--platform-memory", type=Path,
                        help="explicit original-source PlatformMemory.dylib for twelve memory/string imports")
    parser.add_argument("--cache-platform-patches", action="store_true",
                        help="also plan/apply validated v4 cache data-pointer overrides (still non-executable)")
    parser.add_argument("--verbose", action="store_true", help="print bounded load-stage progress to stderr")
    args = parser.parse_args()

    def progress(message):
        if args.verbose:
            print(message, file=sys.stderr, flush=True)

    try:
        root = args.input_root.resolve(strict=True)
        audit.check(not root.is_relative_to(Path(__file__).resolve().parents[1]),
                    "Apple inputs must remain outside the repository")
        paths = verified_inputs(root, progress)
        runtime_root = args.runtime_root or root / (
            "springboard-closure-inputs/Library/Developer/CoreSimulator/Profiles/Runtimes/"
            "iOS 27.0.simruntime/Contents/Resources/RuntimeRoot")
        runtime_root = runtime_root.resolve(strict=True)
        audit.check(runtime_root.is_dir(), "missing extracted RuntimeRoot")
        with ExitStack() as stack:
            progress("reading cache and image metadata")
            cache_path = paths["dyld-cache/dyld_sim_shared_cache_arm64"]
            cache = audit.Cache(stack, cache_path)
            app = audit.Image(audit.mapped_file(stack, paths["binaries/SpringBoard.app/SpringBoard"]))
            framework = audit.Image(audit.mapped_file(stack, paths["binaries/SpringBoard.framework/SpringBoard"]))
            platform_memory = (audit.Image(audit.mapped_file(stack, args.platform_memory))
                               if args.platform_memory else None)
            audit.check(cache.parts == 2 and app.kind == 2 and app.platform == 7
                        and app.entry_offset == 0x618 and framework.kind == 6
                        and framework.platform == 7 and framework.install_name == staging.SPRINGBOARD,
                        "unexpected Simulator SpringBoard profile")
            memory = stack.enter_context(NativeMemory()) if args.map else None
            page = max(16384, memory.page) if memory else 16384
            progress("resolving the dependency graph and staging fixups")
            images, staged = plan(cache, app, {staging.SPRINGBOARD: framework},
                                  partial(audit.standalone, stack, runtime_root), page, args.weak_policy, progress,
                                  platform_memory, args.cache_platform_patches)
            progress("all mapping ranges validated")
            report = {"scope": "SpringBoard non-executable loader checkpoint",
                      "state": "mapped-non-executable" if args.map else "planned-non-executable",
                      "profile_sha256_verified": True, "mapping_page_size": page,
                      "staging": staged, "springboard_entered": False,
                      "rendered_ios_frame": False, "ready_to_execute": False,
                      "execution_blockers": [
                          "macOS host kernel/platform/pthread implementations (see staging.host_runtime)",
                          "shared-cache weak-override patch tables and dynamic loads",
                          "dyld/libSystem TLS and process initialization",
                          "Objective-C/Swift registration and ordered initializers",
                          "Darwin/Mach service and Simulator host ABI compatibility",
                          "BackBoard/FrontBoard and CoreAnimation/IOSurface/Metal presentation",
                      ]}
            if memory:
                fds = []
                for part, relative in enumerate(("dyld-cache/dyld_sim_shared_cache_arm64",
                                                  "dyld-cache/dyld_sim_shared_cache_arm64.01")):
                    fd = os.open(paths[relative], os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
                    stack.callback(os.close, fd)
                    metadata = os.fstat(fd)
                    source = next(mapping.data for mapping in cache.mappings if mapping.part == part)
                    audit.check(stat.S_ISREG(metadata.st_mode) and metadata.st_size == len(source)
                                and os.pread(fd, 104, 0) == source[:104], "cache changed before native mapping")
                    fds.append(fd)
                report["mapping"] = materialize(cache, images, memory, fds, progress,
                                                  staged.get("cache_platform_patches", ()))
                staged["cached_prebound_references_replaced"] = bool(report["mapping"]["cache_platform_pointers_replaced"])
            progress("releasing mappings")
        if args.map:
            report["mapping"]["all_mappings_released"] = not memory.owned
        progress("load checkpoint complete (Apple code not executed)")
        print(json.dumps(report, indent=2, sort_keys=True))
    except (OSError, ValueError, MemoryError) as error:
        parser.exit(1, f"SpringBoard load checkpoint failed: {error}\n")


if __name__ == "__main__":
    main()
