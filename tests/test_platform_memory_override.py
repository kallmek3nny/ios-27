#!/usr/bin/env python3
"""Synthetic integration tests for explicit platform-memory import replacement."""

from struct import pack, pack_into, unpack_from
from dataclasses import replace
import unittest

import test_apple_cache_audit as fixtures


stage = fixtures.staging
audit = stage.audit


def uleb(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def export_trie(symbols):
    names = sorted(symbols)
    terminals = [uleb(0) + uleb(symbols[name]) for name in names]
    leaves = [uleb(len(terminal)) + terminal + b"\0" for terminal in terminals]
    root_size = 2
    for _ in range(5):
        cursor = root_size
        entries = []
        for name, leaf in zip(names, leaves):
            entries.append(name.encode() + b"\0" + uleb(cursor))
            cursor += len(leaf)
        root = b"\0" + bytes([len(names)]) + b"".join(entries)
        if len(root) == root_size:
            return root + b"".join(leaves)
        root_size = len(root)
    raise AssertionError("synthetic trie sizing failed")


def replacement():
    section = pack("<16s16s2Q8I", b"__text", b"__TEXT", 512, 256, 512, 2, 0, 0, 0x80000400, 0, 0, 0)
    text = fixtures.command(0x19, pack("<16s4Q4I", b"__TEXT", 0, 4096, 0, 4096, 5, 5, 1, 0) + section)
    header = fixtures.macho(6, text, fixtures.dylib(0xD, stage.PLATFORM_MEMORY_INSTALL_NAME),
                            fixtures.command(0x32, pack("<4I", 7, 0, 0, 0)))
    image = audit.Image(header.ljust(4096, b"\0"))
    symbols = {name: 512 + 4 * i for i, name in enumerate(sorted(stage.PLATFORM_MEMORY_SYMBOLS))}
    image.exports = export_trie(symbols)
    return image, symbols


def inputs(symbol="_memcpy", provider=stage.PLATFORM_HOST, content=bytes(16)):
    _, app, _ = fixtures.staging_inputs()
    framework = audit.Image(fixtures.macho(6, fixtures.segment(b"__TEXT", 0, 0, 0x10000, 5),
                            fixtures.dylib(0xD, stage.SPRINGBOARD), fixtures.dylib(0xC, "/consumer"))
                            .ljust(0x10000, b"\0"))
    framework.exports = fixtures.trie("_SBSystemAppMain", b"\0\xfc\x95\x02")
    consumer = fixtures.host_consumer(symbol, provider)
    return (fixtures.host_placeholder(symbol=symbol, name=provider, content=content), app,
            {stage.SPRINGBOARD: framework, "/consumer": consumer})


class OverrideTests(unittest.TestCase):
    def test_explicit_replacement_changes_actual_staged_pointer(self):
        library, symbols = replacement()
        cache, app, external = inputs()
        original = external["/consumer"].data
        prepared = {}
        result = stage.stage(cache, app, external, None, platform_memory=library,
                             prepared=lambda path, image, base, data: prepared.update({path: data}))
        expected = result["images"][stage.PLATFORM_MEMORY_INSTALL_NAME]["vm_base"] + symbols["_memcpy"]
        self.assertEqual(unpack_from("<Q", prepared["/consumer"], 4104)[0], expected)
        self.assertEqual(result["host_replacement_import_entries"], 1)
        self.assertEqual(result["host_replacement_bind_sites"], 1)
        self.assertEqual(result["host_placeholder_import_entries"], 0)
        self.assertFalse(result["cached_prebound_references_replaced"])
        self.assertEqual(external["/consumer"].data, original)
        self.assertNotIn(stage.PLATFORM_MEMORY_INSTALL_NAME, external)

    def test_replacements_are_opt_in(self):
        result = stage.stage(*inputs(), None)
        self.assertEqual(result["host_replacement_import_entries"], 0)
        self.assertEqual(result["host_placeholder_import_entries"], 1)

    def test_missing_apis_remain_unimplemented(self):
        library, _ = replacement()
        result = stage.stage(*inputs(symbol="_os_unfair_lock_lock"), None, platform_memory=library)
        self.assertEqual(result["host_placeholder_bind_sites"], 1)
        self.assertEqual(result["host_replacement_bind_sites"], 0)

    def test_same_name_from_wrong_host_is_not_replaced(self):
        library, _ = replacement()
        result = stage.stage(*inputs(provider=fixtures.HOST_LIBRARY), None, platform_memory=library)
        self.assertEqual(result["host_placeholder_bind_sites"], 1)
        self.assertEqual(result["host_replacement_bind_sites"], 0)

    def test_nonplaceholder_export_is_not_replaced(self):
        library, _ = replacement()
        result = stage.stage(*inputs(content=b"\xc0\x03\x5f\xd6" + bytes(12)), None, platform_memory=library)
        self.assertEqual(result["host_placeholder_bind_sites"], 0)
        self.assertEqual(result["host_replacement_bind_sites"], 0)

    def test_bad_provider_profile_fails_before_any_staging(self):
        for field, value in (("platform", 1), ("kind", 2), ("install_name", stage.PLATFORM_HOST),
                             ("dependencies", [audit.Dependency("load", "/other")]), ("initializers", 1)):
            with self.subTest(field=field):
                library, _ = replacement()
                setattr(library, field, value)
                calls = []
                with self.assertRaises(ValueError):
                    stage.stage(*inputs(), None, platform_memory=library, prepared=lambda *args: calls.append(args))
                self.assertEqual(calls, [])

    def test_every_export_must_be_regular_file_backed_code(self):
        library, symbols = replacement()
        del symbols["_memcpy"]
        library.exports = export_trie(symbols)
        with self.assertRaisesRegex(ValueError, "missing regular"):
            stage.validate_platform_memory(library)
        library, symbols = replacement()
        symbols["_memcpy"] = 4096
        library.exports = export_trie(symbols)
        with self.assertRaisesRegex(ValueError, "outside code"):
            stage.validate_platform_memory(library)

    def test_replacement_cannot_collide_with_existing_catalog(self):
        library, _ = replacement()
        cache, app, external = inputs()
        external[stage.PLATFORM_MEMORY_INSTALL_NAME] = library
        with self.assertRaisesRegex(ValueError, "conflicts"):
            stage.stage(cache, app, external, None, platform_memory=library)

    def test_provider_code_section_must_be_file_backed(self):
        library, _ = replacement()
        library.sections[0] = replace(library.sections[0], file_offset=513)
        with self.assertRaisesRegex(ValueError, "file/VM mismatch"):
            stage.validate_platform_memory(library)

    def test_interior_function_pointer_is_not_retargeted(self):
        library, _ = replacement()
        cache, app, external = inputs()
        data = bytearray(external["/consumer"].data)
        pack_into("<Q", data, 4104, (1 << 63) | (1 << 24))
        external["/consumer"] = audit.Image(bytes(data))
        with self.assertRaisesRegex(ValueError, "whole-function pointer"):
            stage.stage(cache, app, external, None, platform_memory=library)


if __name__ == "__main__":
    unittest.main()
