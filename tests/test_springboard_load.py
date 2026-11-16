#!/usr/bin/env python3
"""Original synthetic metadata and non-executable mapping tests only."""

import ctypes
from hashlib import sha256
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
from struct import pack, pack_into, unpack_from
import subprocess
import sys
from tempfile import TemporaryDirectory, TemporaryFile
import unittest
from unittest.mock import patch


spec = spec_from_file_location("springboard_load", Path(__file__).resolve().parents[1] / "tools/springboard-load.py")
loader = module_from_spec(spec)
sys.modules[spec.name] = loader
spec.loader.exec_module(loader)
audit, staging = loader.audit, loader.staging


def command(kind, data):
    return pack("<II", kind, len(data) + 8) + data


def segment(name, vm, offset, size, protection):
    return command(0x19, pack("<16s4Q4I", name, vm, size, offset, size,
                              protection, protection, 0, 0))


def dylib(kind, name):
    raw = name.encode("ascii") + b"\0"
    return command(kind, pack("<4I", 24, 0, 0, 0) + raw + b"\0" * (-len(raw) % 8))


def macho(kind, commands, flags=0):
    data = b"".join(commands)
    return pack("<8I", 0xFEEDFACF, 0x0100000C, 0, kind, len(commands), len(data), flags, 0) + data


def uleb(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def export(symbol, value, flags=0):
    edge = symbol.encode("ascii") + b"\0"
    terminal = uleb(flags) + uleb(value)
    if flags & 8:
        terminal += b"\0"
    elif flags & 0x10:
        terminal += uleb(0)
    return b"\0\1" + edge + uleb(len(edge) + 3) + uleb(len(terminal)) + terminal + b"\0"


def provider(path, symbol, weak=False, dependencies=()):
    commands = [segment(b"__TEXT", 0, 0, 0x10000, 5), dylib(0xD, path)]
    commands += [dylib(0xC, name) for name in dependencies]
    image = audit.Image(macho(6, commands, 0x8000 if weak else 0).ljust(0x10000, b"\0"))
    image.exports = export(symbol, 0x8AFC if symbol == "_SBSystemAppMain" else 0x400,
                           4 if weak else 0)
    return image


def consumer(symbol, ordinal, dependencies, *, kind=6, weak=False, page=4096):
    starts = pack("<4I", 3, 0, 16, 0) + pack("<IHHQIHH", 24, page, 6, 0x18000, 0, 1, 8)
    imports = 28 + len(starts)
    fixups = pack("<7I", 0, 28, imports, imports + 4, 1, 1, 0)
    fixups += starts + pack("<I", (ordinal & 255) | (256 if weak else 0)) + symbol.encode() + b"\0"
    linkedit = 0x18000 + page
    commands = [segment(b"__TEXT", 0, 0, 0x18000, 5),
                segment(b"__DATA", 0x18000, 0x18000, page, 3),
                segment(b"__LINKEDIT", linkedit, linkedit, len(fixups), 1)]
    commands += [dylib(0xC, name) for name in dependencies]
    commands += [command(0x80000034, pack("<II", linkedit, len(fixups)))]
    raw = bytearray(macho(kind, commands).ljust(linkedit, b"\0") + fixups)
    pack_into("<Q", raw, 0x18008, 1 << 63)
    return audit.Image(bytes(raw))


class EmptyCache:
    paths = {}
    mappings = ()
    slide_versions = {}
    region = (0x1C0000000, 0x100000)


def inputs(page=4096, extra=None):
    extra = extra or {}
    app = consumer("_SBSystemAppMain", 1, [staging.SPRINGBOARD], kind=2, page=page)
    framework = provider(staging.SPRINGBOARD, "_SBSystemAppMain", dependencies=tuple(extra))
    return EmptyCache(), app, {staging.SPRINGBOARD: framework, **extra}


def bss_framework(section_size=16):
    data = pack("<16s4Q4I", b"__DATA", 0x10000, 4096, 0x10000, 64, 3, 3, 1, 0)
    data += pack("<16s16s2Q8I", b"__bss", b"__DATA", 0x10010, section_size,
                 0, 3, 0, 0, 1, 0, 0, 0)
    header = macho(6, [segment(b"__TEXT", 0, 0, 0x10000, 5),
                       command(0x19, data), dylib(0xD, staging.SPRINGBOARD)])
    image = audit.Image(header.ljust(0x10000, b"\0") + b"x" * 64)
    image.exports = export("_SBSystemAppMain", 0x8AFC)
    return image


class FakeMemory:
    def __init__(self, fail_protect=0, corrupt_word=False):
        self.blocks = {}
        self.protections = []
        self.expected_modes = []
        self.fail_protect = fail_protect
        self.corrupt_word = corrupt_word
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.blocks.clear()
        self.closed = True

    def reserve(self, address, size, fd=-1, offset=0):
        if fd != -1:
            raise ValueError("fake file mapping not supported")
        self.blocks[address] = bytearray(size)

    def span(self, address, size):
        for base, data in self.blocks.items():
            if base <= address and address + size <= base + len(data):
                return memoryview(data)[address - base:address - base + size]
        raise ValueError("outside fake mapping")

    def protect(self, address, size, mode):
        self.protections.append(mode)
        if len(self.protections) == self.fail_protect:
            raise OSError("injected protection failure")
        if mode not in (0, 1, 3):
            raise ValueError("executable protection")
        self.span(address, size)

    def write(self, address, data):
        self.span(address, len(data))[:] = data

    def zero(self, address, size):
        self.span(address, size)[:] = b"\0" * size

    def digest(self, address, size):
        return sha256(self.span(address, size)).digest()

    def word(self, address):
        return 0 if self.corrupt_word else unpack_from("<Q", self.span(address, 8))[0]

    def verify_permissions(self, expected):
        self.expected_modes = [mode for _, _, mode in expected]


class PlanTests(unittest.TestCase):
    def test_private_fixups_and_complete_nonexecuting_mapping(self):
        cache, app, external = inputs(16384)
        original = app.data
        images, report = loader.plan(cache, app, external, None, 16384, "unique")
        self.assertEqual(report["staged_images"], 2)
        self.assertEqual(app.data, original)
        memory = FakeMemory()
        with memory:
            result = loader.materialize(cache, images, memory, [])
            self.assertTrue(result["entry_bind_verified"])
            self.assertEqual(result["executable_pages"], 0)
            self.assertEqual(result["standalone_images"], 2)
            self.assertFalse(any(mode & 4 for mode in memory.protections))
            self.assertEqual(set(memory.expected_modes), {"r--p"})
        self.assertTrue(memory.closed)
        self.assertFalse(memory.blocks)
        self.assertEqual(app.data, original)

    def weak_inputs(self, *, first_weak=True, second_weak=True, missing=False, weak_import=False):
        extras = {"/client": consumer("_value", -3, [], weak=weak_import)}
        if not missing:
            extras["/first"] = provider("/first", "_value", first_weak)
            extras["/second"] = provider("/second", "_value", second_weak)
            # hasWeakDefs means the image participates in the shallow scan,
            # not that every exported definition in it is itself weak.
            for name in ("/first", "/second"):
                extras[name].flags |= staging.WEAK_DEFINES
        return inputs(extra=extras)

    def test_ambiguous_weak_definitions_stay_rejected_by_default(self):
        cache, app, external = self.weak_inputs()
        with self.assertRaisesRegex(ValueError, "unresolved standalone imports"):
            loader.plan(cache, app, external, None, 4096, "unique")

    def test_explicit_load_order_coalesces_first_weak_definition(self):
        cache, app, external = self.weak_inputs()
        _, result = loader.plan(cache, app, external, None, 4096, "load-order")
        self.assertEqual(result["deferred_images"], [])
        self.assertEqual(result["weak_choices"]["_value"], {
            "provider": "/first", "export_offset": 0x400, "candidate_count": 2, "strong": False})
        self.assertEqual(result["images"]["/client"]["load_order_coalesced_targets"], 1)
        self.assertEqual(result["images"]["/client"]["unique_weak_targets"], 0)

    def test_later_strong_definition_overrides_first_weak(self):
        cache, app, external = self.weak_inputs(second_weak=False)
        _, result = loader.plan(cache, app, external, None, 4096, "load-order")
        self.assertEqual(result["weak_choices"]["_value"]["provider"], "/second")
        self.assertTrue(result["weak_choices"]["_value"]["strong"])

    def test_first_strong_definition_keeps_load_order(self):
        cache, app, external = self.weak_inputs(first_weak=False, second_weak=False)
        _, result = loader.plan(cache, app, external, None, 4096, "load-order")
        self.assertEqual(result["weak_choices"]["_value"]["provider"], "/first")

    def test_load_order_uses_direct_definitions_not_reexports_or_resolvers(self):
        for flags in (8, 0x10):
            with self.subTest(flags=flags):
                cache, app, external = self.weak_inputs()
                external["/first"].exports = export("_value", 1, flags)
                _, result = loader.plan(cache, app, external, None, 4096, "load-order")
                self.assertEqual(result["weak_choices"]["_value"]["provider"], "/second")

    def test_missing_weak_import_is_null_but_required_lookup_fails(self):
        cache, app, external = self.weak_inputs(missing=True, weak_import=True)
        images, _ = loader.plan(cache, app, external, None, 4096, "load-order")
        client = next(image for image in images if image.path == "/client")
        self.assertEqual(unpack_from("<Q", client.data, 0x18008)[0], 0)
        cache, app, external = self.weak_inputs(missing=True)
        with self.assertRaisesRegex(ValueError, "unresolved standalone imports"):
            loader.plan(cache, app, external, None, 4096, "load-order")

    def test_bad_page_and_slide_metadata_are_rejected(self):
        for page in (0, 8192, 12288, 131072):
            with self.subTest(page=page), self.assertRaisesRegex(ValueError, "page size"):
                loader.plan(*inputs(), None, page, "unique")
        cache, app, external = inputs()
        cache.slide_versions = {5: 1}
        with self.assertRaisesRegex(ValueError, "slide fixups"):
            loader.plan(cache, app, external, None, 4096, "unique")

    def test_misaligned_cache_and_overlap_are_rejected(self):
        for address, size, message in ((0x1C0000001, 4096, "unaligned"),
                                       (0x1C0000000, 4095, "unaligned"),
                                       (0x100000000, 4096, "overlap")):
            cache, app, external = inputs()
            cache.mappings = (audit.Mapping(b"", 0, address, size, 0, 1),)
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, message):
                loader.plan(cache, app, external, None, 4096, "unique")

    def test_writable_executable_maximum_is_rejected(self):
        cache, app, external = inputs()
        framework = external[staging.SPRINGBOARD]
        raw = bytearray(framework.data)
        pack_into("<I", raw, 32 + 56, 7)
        mutated = audit.Image(bytes(raw))
        mutated.exports = framework.exports
        external[staging.SPRINGBOARD] = mutated
        with self.assertRaisesRegex(ValueError, "protection"):
            loader.plan(cache, app, external, None, 4096, "unique")

    def test_protection_failure_and_bad_bind_unwind_all_mappings(self):
        cache, app, external = inputs()
        images, _ = loader.plan(cache, app, external, None, 4096, "unique")
        for memory in (FakeMemory(fail_protect=2), FakeMemory(corrupt_word=True)):
            with self.subTest(memory=memory), self.assertRaises((ValueError, OSError)):
                with memory:
                    loader.materialize(cache, images, memory, [])
            self.assertTrue(memory.closed)
            self.assertEqual(memory.blocks, {})

    def test_zero_fill_overrides_dirty_file_bytes(self):
        cache, app, external = inputs()
        external[staging.SPRINGBOARD] = bss_framework()
        images, _ = loader.plan(cache, app, external, None, 4096, "unique")
        with FakeMemory() as memory:
            result = loader.materialize(cache, images, memory, [])
            self.assertEqual(result["zero_filled_bytes"], 16)
            self.assertEqual(bytes(memory.span(0x110010000, 64)), b"x" * 16 + b"\0" * 16 + b"x" * 32)

    def test_zero_fill_outside_its_segment_is_rejected(self):
        cache, app, external = inputs()
        external[staging.SPRINGBOARD] = bss_framework(4096)
        with self.assertRaisesRegex(ValueError, "section VM range"):
            loader.plan(cache, app, external, None, 4096, "unique")

    def test_zero_fill_cannot_erase_a_staged_fixup(self):
        cache, app, external = inputs()
        raw = bytearray(app.data)
        end = 32 + unpack_from("<I", raw, 20)[0]
        # Add a zerofill section to the existing __DATA command, moving only
        # load commands in unused __TEXT padding, never the fixup file bytes.
        raw[256:end + 80] = raw[176:end]
        pack_into("<I", raw, 20, end - 32 + 80)
        pack_into("<I", raw, 108, 152)
        pack_into("<I", raw, 168, 1)
        raw[176:256] = pack("<16s16s2Q8I", b"__bss", b"__DATA", 0x18008, 8,
                            0, 3, 0, 0, 1, 0, 0, 0)
        with self.assertRaisesRegex(ValueError, "fixup overlaps zero-fill"):
            loader.plan(cache, audit.Image(bytes(raw)), external, None, 4096, "unique")

    def test_profile_checks_size_and_hash_before_parsing(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "sample"
            with patch.dict(loader.PROFILE, {"sample": (4, sha256(b"good").hexdigest())}, clear=True):
                path.write_bytes(b"bad")
                with self.assertRaisesRegex(ValueError, "size mismatch"):
                    loader.verified_inputs(root)
                path.write_bytes(b"nope")
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    loader.verified_inputs(root)
                path.write_bytes(b"good")
                self.assertEqual(loader.verified_inputs(root), {"sample": path})

    def test_unknown_policy_never_publishes_a_prepared_image(self):
        cache, app, external = inputs()
        published = []
        with self.assertRaisesRegex(ValueError, "weak lookup policy"):
            staging.stage(cache, app, external, None, weak_policy="guess",
                          prepared=lambda *row: published.append(row))
        self.assertEqual(published, [])

    def test_cli_does_not_offer_an_execution_mode(self):
        result = subprocess.run([sys.executable, loader.__file__, "--input-root", ".", "--execute"],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("unrecognized arguments", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


@unittest.skipUnless(sys.platform in ("linux", "android") and ctypes.sizeof(ctypes.c_void_p) == 8,
                     "native data mappings require 64-bit Linux/Android")
class NativeTests(unittest.TestCase):
    def test_execution_protections_are_refused_before_syscall(self):
        with loader.NativeMemory() as memory:
            address = 0x500000000
            memory.reserve(address, memory.page)
            for mode in (4, 5, 6, 7):
                with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, "non-executable"):
                    memory.protect(address, memory.page, mode)

    def test_partial_native_mapping_failure_releases_previous_images(self):
        if os.sysconf("SC_PAGE_SIZE") > 16384:
            self.skipTest("synthetic fixture supports 4/16 KiB host pages")
        cache, app, external = inputs(16384)
        images, _ = loader.plan(cache, app, external, None, os.sysconf("SC_PAGE_SIZE"), "unique")

        class RefusedSecondMapping(loader.NativeMemory):
            def reserve(self, *args, **kwargs):
                if self.owned:
                    raise OSError("injected second reservation refusal")
                return super().reserve(*args, **kwargs)

        memory = RefusedSecondMapping()
        with self.assertRaisesRegex(OSError, "second reservation"):
            with memory:
                loader.materialize(cache, images, memory, [])
        self.assertEqual(memory.owned, [])

    def test_real_mappings_readback_permissions_and_release(self):
        if os.sysconf("SC_PAGE_SIZE") > 16384:
            self.skipTest("synthetic fixture supports 4/16 KiB host pages")
        cache, app, external = inputs(16384)
        page = os.sysconf("SC_PAGE_SIZE")
        images, _ = loader.plan(cache, app, external, None, page, "unique")
        with loader.NativeMemory() as memory:
            result = loader.materialize(cache, images, memory, [])
            self.assertTrue(result["permissions_verified"])
            self.assertEqual(result["executable_pages"], 0)
            addresses = list(memory.owned)
        self.assertEqual(memory.owned, [])
        libc = ctypes.CDLL(None, use_errno=True)
        libc.mincore.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
        libc.mincore.restype = ctypes.c_int
        for address, _ in addresses:
            resident = ctypes.c_ubyte()
            self.assertEqual(libc.mincore(address, page, ctypes.byref(resident)), -1)

    def test_fixed_mapping_collision_preserves_existing_memory(self):
        with loader.NativeMemory() as first, loader.NativeMemory() as second:
            address, size = 0x500000000, first.page
            first.reserve(address, size)
            first.protect(address, size, 3)
            first.write(address, bytearray(b"keep"))
            first.protect(address, size, 1)
            with self.assertRaises(OSError):
                second.reserve(address, size)
            self.assertEqual(second.owned, [])
            self.assertEqual(first.digest(address, 4), sha256(b"keep").digest())

    def test_file_backed_cache_remains_readonly_and_unchanged(self):
        with TemporaryFile() as source, loader.NativeMemory() as memory:
            page = memory.page
            raw = b"cache-data".ljust(page, b"x")
            source.write(raw)
            source.flush()
            cache, app, external = inputs(16384)
            cache.mappings = (audit.Mapping(b"", 0, 0x1C0000000, page, 0, 5),)
            images, _ = loader.plan(cache, app, external, None, page, "unique")
            result = loader.materialize(cache, images, memory, [source.fileno()])
            self.assertEqual(result["cache_regions"], 1)
            self.assertEqual(memory.digest(0x1C0000000, page), sha256(raw).digest())
            source.seek(0)
            self.assertEqual(source.read(), raw)


if __name__ == "__main__":
    unittest.main()
