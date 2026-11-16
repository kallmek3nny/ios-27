#!/usr/bin/env python3
"""Synthetic v4 cache patch metadata; no Apple bytes or guest execution."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
from struct import pack_into, unpack_from
from tempfile import TemporaryFile
from types import SimpleNamespace
import sys
import unittest

import test_springboard_load as fixture


spec = spec_from_file_location("cache_platform", Path(__file__).resolve().parents[1] / "tools/cache-platform-overrides.py")
patcher = module_from_spec(spec)
spec.loader.exec_module(patcher)


def sample():
    base, client, metadata = 0x100000, 0x200000, 0x300000
    data = bytearray(4096)
    pool = bytearray(4096)
    pack_into("<QQ", data, 0, base + 128, base + 128)
    counts = (2, 1, 1, 1, 1, 8, 2, 1, 1)
    widths = (16, 8, 12, 12, 8, 1, 8, 12, 16)
    offsets = []
    cursor = 152
    for count, width in zip(counts, widths):
        cursor = (cursor + 7) & -8
        offsets.append(cursor)
        cursor += count * width
    fields = [value for offset, count in zip(offsets, counts) for value in (metadata + offset, count)]
    pack_into("<II18Q", pool, 0, 4, 0, *fields)
    pack_into("<4I", pool, offsets[0], 0, 1, 0, 1)
    pack_into("<2I", pool, offsets[1], 128, 0)
    pack_into("<3I", pool, offsets[2], 1, 0, 1)
    pack_into("<3I", pool, offsets[3], 0, 0, 1)
    pack_into("<2I", pool, offsets[4], 4096, 0)
    pool[offsets[5]:offsets[5] + 8] = b"_memcpy\0"
    pack_into("<2I", pool, offsets[6], 0, 1)
    pack_into("<3I", pool, offsets[7], 0, 0, 1)
    pack_into("<Q2I", pool, offsets[8], client + 4096 + 8 - base, 0, 0)

    class Cache:
        paths = {patcher.PLATFORM: base, "/client": client}
        image_addresses = [base, client]
        region = (base, 0x300000)
        patch_span = (metadata, len(pool))
        mappings = [SimpleNamespace(address=client + 4096, size=len(data), protection=3)]

        def locate(self, address, size):
            for start, buffer in ((metadata, pool), (client + 4096, data)):
                if start <= address and address + size <= start + len(buffer):
                    return buffer, address - start
            raise ValueError("synthetic address outside cache")

        def image(self, address):
            if address == base:
                return SimpleNamespace(export_is_host_placeholder=lambda flags, offset: flags == 0 and offset == 128)
            if address == client:
                return SimpleNamespace(segments={b"__DATA": (client + 4096, len(data), 0, len(data), 3)})
            raise ValueError("unknown synthetic image")

    return Cache(), pool, data, offsets


class PatchTests(unittest.TestCase):
    def test_client_and_coalesced_got_pointers(self):
        cache, pool, data, _ = sample()
        original = bytes(pool), bytes(data)
        patches = patcher.plan(cache, {0x100080: 0x400080})
        self.assertEqual([p["address"] for p in patches], [0x201000, 0x201008])
        self.assertEqual([p["kind"] for p in patches], ["client", "got"])
        self.assertTrue(all(p["original"] == 0x100080 and p["target"] == 0x400080 for p in patches))
        self.assertEqual(original, (bytes(pool), bytes(data)))

    def test_bad_version_arrays_indices_and_name(self):
        edits = [(0, 3), (8, 0), (16, 3)]
        for offset, value in edits:
            cache, pool, _, _ = sample()
            pack_into("<I", pool, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                patcher.plan(cache, {0x100080: 0x400080})
        for which, field, value in ((2, 0, 2), (3, 0, 1), (3, 4, 9), (1, 4, 99)):
            cache, pool, _, offsets = sample()
            pack_into("<I", pool, offsets[which] + field, value)
            with self.subTest(which=which, field=field), self.assertRaises(ValueError):
                patcher.plan(cache, {0x100080: 0x400080})

    def test_pac_tags_addends_and_got_padding_are_refused(self):
        for flags in (1, 2, 1 << 9):
            cache, pool, _, offsets = sample()
            pack_into("<I", pool, offsets[4] + 4, flags)
            with self.subTest(flags=flags), self.assertRaisesRegex(ValueError, "authenticated, tagged or interior"):
                patcher.plan(cache, {0x100080: 0x400080})
        cache, pool, _, offsets = sample()
        pack_into("<I", pool, offsets[8] + 12, 1)
        with self.assertRaisesRegex(ValueError, "authenticated, tagged or interior"):
            patcher.plan(cache, {0x100080: 0x400080})

    def test_stale_unaligned_and_executable_pointer_locations_are_refused(self):
        cache, _, data, _ = sample()
        pack_into("<Q", data, 0, 0x123456)
        with self.assertRaisesRegex(ValueError, "differs"):
            patcher.plan(cache, {0x100080: 0x400080})
        cache, pool, _, offsets = sample()
        pack_into("<I", pool, offsets[4], 4097)
        with self.assertRaisesRegex(ValueError, "unaligned"):
            patcher.plan(cache, {0x100080: 0x400080})
        cache, _, _, _ = sample()
        cache.mappings[0].protection = 5
        with self.assertRaisesRegex(ValueError, "non-executable"):
            patcher.plan(cache, {0x100080: 0x400080})

    def test_weak_flag_and_duplicate_reference_are_valid(self):
        cache, pool, _, offsets = sample()
        pack_into("<I", pool, offsets[4] + 4, 0x100)
        pack_into("<Q", pool, offsets[8], 0x101000)
        self.assertEqual(len(patcher.plan(cache, {0x100080: 0x400080})), 1)

    @unittest.skipUnless(sys.platform in ("linux", "android"), "native private mappings require Linux")
    def test_native_private_cache_patch_and_cleanup(self):
        cache, app, external = fixture.inputs(16384)
        loader = fixture.loader
        with TemporaryFile() as file, loader.NativeMemory() as memory:
            source = bytearray(memory.page)
            pack_into("<Q", source, 16, 0x12340)
            file.write(source); file.flush()
            cache.mappings = (fixture.audit.Mapping(source, 0, 0x1C0000000, memory.page, 0, 1),)
            cache.locate = lambda address, size: (source, address - 0x1C0000000)
            images, _ = loader.plan(cache, app, external, None, memory.page, "unique")
            patch = {"address": 0x1C0000010, "original": 0x12340, "target": 0x110008AFC}
            result = loader.materialize(cache, images, memory, [file.fileno()], cache_patches=[patch])
            self.assertEqual(memory.word(patch["address"]), patch["target"])
            self.assertEqual(result["cache_platform_pointers_replaced"], 1)
            file.seek(0)
            self.assertEqual(unpack_from("<Q", file.read(), 16)[0], 0x12340)
        self.assertEqual(memory.owned, [])


if __name__ == "__main__":
    unittest.main()
