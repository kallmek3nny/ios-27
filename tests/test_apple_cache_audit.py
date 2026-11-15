#!/usr/bin/env python3
"""Offline coverage for the SpringBoard cache auditor."""

from contextlib import ExitStack
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
from struct import pack, pack_into
import json
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest


spec = spec_from_file_location("apple_cache_audit",
                               Path(__file__).resolve().parents[1] / "tools/apple-cache-audit.py")
audit = module_from_spec(spec)
sys.modules[spec.name] = audit
spec.loader.exec_module(audit)
spec = spec_from_file_location("apple_fixup_stage",
                               Path(__file__).resolve().parents[1] / "tools/apple-fixup-stage.py")
staging = module_from_spec(spec)
spec.loader.exec_module(staging)


def command(cmd, payload):
    return pack("<II", cmd, 8 + len(payload)) + payload


def segment(name, address, offset, size, protection):
    return command(0x19, pack("<16s4Q4I", name, address, size, offset, size,
                              protection, protection, 0, 0))


def dylib(cmd, name):
    name = name.encode("ascii") + b"\0"
    return command(cmd, pack("<4I", 24, 0, 0, 0) + name + b"\0" * (-len(name) % 8))


def macho(kind, *commands):
    payload = b"".join(commands)
    return pack("<8I", 0xFEEDFACF, 0x0100000C, 0, kind, len(commands), len(payload), 0, 0) + payload


def trie(name, terminal):
    edge = name.encode("ascii") + b"\0"
    return b"\0\1" + edge + bytes([len(edge) + 3, len(terminal)]) + terminal + b"\0"


def cache_part(base, uuid):
    data = bytearray(4096)
    data[:16] = audit.CACHE_MAGIC
    data[88:104] = uuid
    pack_into("<2I", data, 16, 512, 1)
    pack_into("<I", data, 216, 7)
    pack_into("<2Q", data, 224, base, 8192)
    pack_into("<3Q2I", data, 512, base, 4096, 0, 5, 5)
    return data


def staging_inputs(symbol="_SBSystemAppMain", data_address=0x10000):
    starts = pack("<4I", 3, 0, 16, 0) + pack("<IHHQIHH", 24, 4096, 6, 0x18000, 0, 1, 8)
    imports = 28 + len(starts)
    fixups = pack("<7I", 0, 28, imports, imports + 4, 1, 1, 0)
    fixups += starts + pack("<I", 1) + symbol.encode("ascii") + b"\0"
    app_header = macho(2, segment(b"__TEXT", 0, 0, 0x18000, 5),
                       segment(b"__DATA", 0x18000, 0x18000, 4096, 3),
                       segment(b"__LINKEDIT", 0x19000, 0x19000, len(fixups), 1),
                       dylib(0xC, staging.SPRINGBOARD),
                       command(0x80000034, pack("<II", 0x19000, len(fixups))))
    app_data = bytearray(app_header.ljust(0x19000, b"\0") + fixups)
    pack_into("<Q", app_data, 0x18008, 1 << 63)
    app = staging.audit.Image(bytes(app_data))
    framework_header = macho(6, segment(b"__TEXT", 0, 0, 0x10000, 5),
                             segment(b"__DATA", data_address, 0x10000, 4096, 3),
                             dylib(0xD, staging.SPRINGBOARD))
    framework = staging.audit.Image(framework_header.ljust(0x11000, b"\0"))
    framework.exports = trie(symbol, b"\0\xfc\x95\x02")  # ULEB128 0x8afc
    cache = type("EmptyCache", (), {"paths": {}, "region": (0x180000000, 0x100000)})()
    return cache, app, {staging.SPRINGBOARD: framework}


HOST_LIBRARY = "/usr/lib/system/libsystem_kernel.dylib"


def host_placeholder(symbol="_getpid", content=bytes(16), name=HOST_LIBRARY, section_offset=1024):
    base = 0x1C0000000
    section = pack("<16s16s2Q8I", b"__text", b"__TEXT", base + 1024, 16,
                   section_offset, 2, 0, 0, 0x80000400, 0, 0, 0)
    text = command(0x19, pack("<16s4Q4I", b"__TEXT", base, 4096, 0, 4096, 5, 5, 1, 0) + section)
    raw = bytearray(macho(6, text, dylib(0xD, name)).ljust(4096, b"\0"))
    raw[1024:1040] = content

    class FixtureCache:
        paths = {name: base}
        region = (base, 4096)

        def locate(self, address, size):
            audit.interval(address - base, size, len(raw), "synthetic cache range")
            return bytes(raw), address - base

        def image(self, address):
            if address != base:
                raise ValueError("unknown synthetic cache image")
            return self.library

    cache = FixtureCache()
    cache.library = audit.Image(bytes(raw), cache=cache)
    cache.library.exports = trie(symbol, b"\0\x80\x08")  # export offset 1024
    return cache


def host_consumer(symbol="_getpid", provider=HOST_LIBRARY):
    starts = pack("<4I", 3, 0, 16, 0) + pack("<IHHQIHH", 24, 4096, 6, 4096, 0, 1, 8)
    imports = 28 + len(starts)
    fixups = pack("<7I", 0, 28, imports, imports + 4, 1, 1, 0)
    fixups += starts + pack("<I", 1) + symbol.encode("ascii") + b"\0"
    header = macho(6, segment(b"__TEXT", 0, 0, 4096, 5),
                   segment(b"__DATA", 4096, 4096, 4096, 3),
                   segment(b"__LINKEDIT", 8192, 8192, len(fixups), 1),
                   dylib(0xC, provider),
                   command(0x80000034, pack("<II", 8192, len(fixups))))
    raw = bytearray(header.ljust(8192, b"\0") + fixups)
    pack_into("<Q", raw, 4104, 1 << 63)
    return audit.Image(bytes(raw))


class AuditTests(unittest.TestCase):
    def test_zero_filled_host_export_is_not_an_implementation(self):
        cache = host_placeholder()
        image = cache.library
        self.assertEqual(image.export("_getpid"), (0, 1024, None))
        self.assertEqual(image.host_placeholder_ranges, ((0x1C0000400, 0x1C0000410),))
        self.assertTrue(image.export_is_host_placeholder(0, 1024))
        self.assertFalse(image.export_is_host_placeholder(2, 1024))  # absolute symbol
        self.assertFalse(image.export_is_host_placeholder(0, 1040))
        consumer = host_consumer()
        resolver = audit.Resolver(cache, consumer, {})
        census, _, _, matched = resolver.bind_census("<app>", consumer)
        self.assertEqual(census["import_entries_by_result"], {"host_placeholder": 1})
        self.assertEqual(len(matched), 1)  # still a static export match
        self.assertEqual(census["unresolved_by_provider"][0]["result"], "host_placeholder")
        images, _ = resolver.closure()
        requirements = audit.host_runtime_requirements(images, resolver)
        self.assertFalse(requirements["execution_verified"])
        self.assertEqual(requirements["zero_filled_host_libraries"][0]["zero_code_bytes"], 16)
        self.assertEqual(requirements["required_api_probes"][0]["backing"], "zero_filled_host_placeholder")

    def test_nonzero_or_unrelated_code_does_not_match_host_placeholder_profile(self):
        image = host_placeholder(content=b"\xc0\x03\x5f\xd6" + bytes(12)).library
        self.assertEqual(image.host_placeholder_ranges, ())
        unrelated = host_placeholder(name="/fixture.dylib").library
        self.assertEqual(unrelated.host_placeholder_ranges, ())
        standalone = audit.Image(host_placeholder().library.data)
        self.assertEqual(standalone.host_placeholder_ranges, ())

    def test_host_placeholder_section_requires_file_vm_correspondence(self):
        cache = host_placeholder(section_offset=1028)
        with self.assertRaisesRegex(audit.AuditError, "file/VM mismatch"):
            _ = cache.library.host_placeholder_ranges

    def test_cross_part_linkedit_and_identity(self):
        main = cache_part(0x100000, b"m" * 16)
        sub = cache_part(0x101000, b"s" * 16)
        pack_into("<2I", main, 392, 544, 1)
        pack_into("<16sQ32s", main, 544, b"s" * 16, 4096, b".01")
        pack_into("<2I", main, 448, 600, 1)
        pack_into("<3Q2I", main, 600, 0x100400, 0, 0, 632, 0)
        main[632:639] = b"/cache\0"
        exports = trie("_entry", b"\0\x12")
        header = macho(6, segment(b"__TEXT", 0x100400, 1024, 512, 5),
                       segment(b"__LINKEDIT", 0x101400, 17, len(exports), 1),
                       command(0x80000033, pack("<II", 17, len(exports))))
        main[1024:1024 + len(header)] = header
        sub[1024:1024 + len(exports)] = exports
        with TemporaryDirectory() as directory:
            path = Path(directory) / "cache"
            subpath = path.with_suffix(".01")
            path.write_bytes(main)
            subpath.write_bytes(sub)
            with ExitStack() as stack:
                cache = audit.Cache(stack, path)
                image = cache.image(cache.paths["/cache"])
                self.assertEqual(image.export("_entry"), (0, 0x12, None))
                self.assertIsNone(image.export("_absent"))
                with self.assertRaisesRegex(audit.AuditError, "cache VM span"):
                    cache.locate(0x100FFF, 2)
            with subpath.open("r+b") as file:
                file.seek(88)
                file.write(b"z")
            with ExitStack() as stack, self.assertRaisesRegex(audit.AuditError, "UUID mismatch"):
                audit.Cache(stack, path)

    def test_reexport_alias_and_required_path_promotion(self):
        start = segment(b"__TEXT", 0, 0, 512, 5)
        app = audit.Image(macho(2, start, dylib(0x80000018, "/A"), dylib(0xC, "/B")))
        image_a = audit.Image(macho(6, start, dylib(0xC, "/missing"), dylib(0x8000001F, "/B")))
        image_b = audit.Image(macho(6, start, dylib(0xC, "/A"), dylib(0x80000018, "/optional")))
        image_a.exports = trie("_alias", b"\x08\x02_original\0")
        image_b.exports = trie("_original", b"\0\x10")
        cache = type("EmptyCache", (), {"paths": {}})()
        resolver = audit.Resolver(cache, app, {"/A": image_a, "/B": image_b})
        _, closure = resolver.closure()
        self.assertEqual([(row["install_name"], row["required"]) for row in closure["missing"]],
                         [("/missing", True), ("/optional", False)])
        self.assertEqual(resolver.resolve("/A", "_alias"), ("/B", image_b, 0, 16))
        self.assertEqual(resolver.resolve("/A", "_original"), ("/B", image_b, 0, 16))

    def test_chained_bind_and_rebase(self):
        names = b"_entry\0"
        starts = pack("<4I", 3, 0, 16, 0) + pack("<IHHQIHH", 24, 4096, 6, 4096, 0, 1, 8)
        import_offset = 28 + len(starts)
        fixups = pack("<7I", 0, 28, import_offset, import_offset + 4, 1, 1, 0)
        fixups += starts + pack("<I", 1) + names
        header = macho(2, segment(b"__TEXT", 0, 0, 4096, 5),
                       segment(b"__DATA", 4096, 4096, 4096, 3),
                       segment(b"__LINKEDIT", 8192, 8192, len(fixups), 1),
                       dylib(0xC, "/provider"),
                       command(0x80000034, pack("<II", 8192, len(fixups))))
        data = bytearray(header.ljust(8192, b"\0") + fixups)
        pack_into("<QQ", data, 4104, (1 << 63) | (2 << 51), 0x80)
        image = audit.Image(bytes(data))
        self.assertEqual(image.chained_imports(), [(1, False, "_entry", 0)])
        self.assertEqual(image.chain_sites(1), ({6: 1}, [(4104, 0, 0)], 1))
        pack_into("<Q", data, 4104, (1 << 63) | (1 << 51))
        with self.assertRaisesRegex(audit.AuditError, "overlapping chained slots"):
            audit.Image(bytes(data)).chain_sites(1)
        pack_into("<I", data, 8192 + import_offset, 2)
        with self.assertRaisesRegex(audit.AuditError, "ordinal outside dylib table"):
            audit.Image(bytes(data)).chained_imports()

    def test_chained_import_addends_and_reserved_bits(self):
        for kind, row, expected in ((2, pack("<Ii", 0x1FF, -17), (-1, True, "_entry", -17)),
                                    (3, pack("<Qq", 0x1FFFE, -1234), (-2, True, "_entry", -1234))):
            with self.subTest(kind=kind):
                fixups = pack("<7I", 0, 28, 32, 32 + len(row), 1, kind, 0)
                fixups += pack("<I", 0) + row + b"_entry\0"
                header = macho(2, segment(b"__TEXT", 0, 0, 4096, 5),
                               segment(b"__LINKEDIT", 4096, 4096, len(fixups), 1),
                               command(0x80000034, pack("<II", 4096, len(fixups))))
                data = bytearray(header.ljust(4096, b"\0") + fixups)
                self.assertEqual(audit.Image(bytes(data)).chained_imports(), [expected])
                if kind == 3:
                    pack_into("<Q", data, 4096 + 32, 0x1FFFE | (1 << 17))
                    with self.assertRaisesRegex(audit.AuditError, "reserved chained import bits"):
                        audit.Image(bytes(data)).chained_imports()

    def test_export_trie_bounds(self):
        with self.assertRaisesRegex(audit.AuditError, "overflowing"):
            audit.trie_lookup(b"\x81" * 10, "_entry")
        with self.assertRaisesRegex(audit.AuditError, "child outside trie"):
            audit.trie_lookup(b"\0\1_e\0\x7f", "_entry")
        with self.assertRaisesRegex(audit.AuditError, "bounded string"):
            audit.trie_lookup(trie("_entry", b"\x08\x01bad"), "_entry")

    def test_duplicate_dyld_commands_are_rejected(self):
        start = segment(b"__TEXT", 0, 0, 512, 5)
        info = command(0x80000022, pack("<10I", 0, 0, 0, 8, 0, 0, 0, 0, 0, 0))
        empty_info = command(0x22, pack("<10I", *([0] * 10)))
        exports = command(0x80000033, pack("<II", 0, 0))
        for commands in ((info, empty_info), (empty_info, info), (exports, exports)):
            with self.subTest(commands=commands):
                with self.assertRaisesRegex(audit.AuditError, "duplicate dyld"):
                    audit.Image(macho(2, start, *commands))

    def test_export_trie_command_takes_precedence_over_dyld_info(self):
        start = segment(b"__TEXT", 0, 0, 512, 5)
        info = command(0x80000022, pack("<10I", *([0] * 8), 128, 8))
        exports = command(0x80000033, pack("<II", 256, 16))
        for commands in ((info, exports), (exports, info)):
            with self.subTest(commands=commands):
                self.assertEqual(audit.Image(macho(2, start, *commands)).export_span, (256, 16))

    def test_export_terminal_extent_and_child_table(self):
        for terminal in (b"\0\x12\0", b"\x10\x12\x34\0", b"\x08\x01other\0extra"):
            with self.subTest(terminal=terminal):
                with self.assertRaisesRegex(audit.AuditError, "export terminal extent"):
                    audit.trie_lookup(trie("_entry", terminal), "_entry")
        for data, message in ((b"\x02\0\x12", "missing export trie child count"),
                              (b"\x02\0\x12\1", "bounded string"),
                              (b"\x02\0\x12\1_e\0\0", "child outside trie")):
            with self.subTest(data=data):
                with self.assertRaisesRegex(audit.AuditError, message):
                    audit.trie_lookup(data, "")
        self.assertEqual(audit.trie_lookup(trie("_entry", b"\x10\x12\x34"), "_entry"),
                         (0x10, 0x12, None))

    def test_cli_reports_synthetic_closure_and_rejects_malformed_app(self):
        main = cache_part(0x100000, b"m" * 16)
        pack_into("<2I", main, 448, 600, 1)
        pack_into("<3Q2I", main, 600, 0x100400, 0, 0, 632, 0)
        main[632:640] = b"/cached\0"
        header = macho(6, segment(b"__TEXT", 0x100400, 1024, 512, 5))
        main[1024:1024 + len(header)] = header
        start = segment(b"__TEXT", 0, 0, 512, 5)
        platform = command(0x32, pack("<4I", 7, 0, 0, 0))
        with TemporaryDirectory() as directory:
            root = Path(directory)
            cachepath, apppath = root / "cache", root / "app"
            cachepath.write_bytes(main)
            apppath.write_bytes(macho(2, start, platform, dylib(0xC, "/cached")).ljust(512, b"\0"))
            args = [sys.executable, str(Path(audit.__file__)), "--cache", str(cachepath),
                    "--app", str(apppath)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report["dependency_closure"]["cached_paths"], 1)
            self.assertEqual(report["dependency_closure"]["missing"], [])
            self.assertEqual(report["standalone_images"]["<app>"]["chained_bind_sites"], 0)
            exports = command(0x80000033, pack("<II", 0, 0))
            apppath.write_bytes(macho(2, start, platform, exports, exports).ljust(512, b"\0"))
            result = subprocess.run(args, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("audit failed: duplicate dyld export command", result.stderr)
            self.assertNotIn("Traceback", result.stderr)

    def test_runtime_root_uses_verified_install_name(self):
        with TemporaryDirectory() as directory, ExitStack() as stack:
            root = Path(directory).resolve()
            data = macho(6, segment(b"__TEXT", 0, 0, 512, 5),
                         command(0x32, pack("<4I", 7, 0, 0, 0)),
                         dylib(0xD, "/provider"))
            (root / "provider").write_bytes(data)
            (root / "wrong").write_bytes(data)
            self.assertEqual(audit.standalone(stack, root, "/provider").install_name, "/provider")
            self.assertIsNone(audit.standalone(stack, root, "/../provider"))
            self.assertIsNone(audit.standalone(stack, root, "@rpath/provider"))
            with self.assertRaisesRegex(audit.AuditError, "unexpected platform, file type or install name"):
                audit.standalone(stack, root, "/wrong")


class StagingTests(unittest.TestCase):
    def test_nonexecuting_stage_reports_unimplemented_host_targets(self):
        _, app, _ = staging_inputs()
        framework = audit.Image(macho(6, segment(b"__TEXT", 0, 0, 0x10000, 5),
                                      dylib(0xD, staging.SPRINGBOARD),
                                      dylib(0xC, "/consumer")).ljust(0x10000, b"\0"))
        framework.exports = trie("_SBSystemAppMain", b"\0\xfc\x95\x02")
        consumer = host_consumer()
        before = consumer.data
        report = staging.stage(host_placeholder(), app,
                               {staging.SPRINGBOARD: framework, "/consumer": consumer}, None)
        self.assertEqual(report["deferred_images"], [])  # NX byte staging still works
        self.assertEqual(report["host_placeholder_import_entries"], 1)
        self.assertEqual(report["host_placeholder_bind_sites"], 1)
        self.assertEqual(report["images"]["/consumer"]["host_placeholder_imports"],
                         [{"symbol": "_getpid", "provider": HOST_LIBRARY, "sites": 1}])
        self.assertEqual(consumer.data, before)

    def test_stages_genuine_bind_without_mutating_inputs(self):
        cache, app, external = staging_inputs()
        original = app.data
        report = staging.stage(cache, app, external, None)
        self.assertEqual(report["verified_fixups"], 1)
        self.assertEqual(report["staged_images"], 2)
        self.assertEqual(report["deferred_images"], [])
        self.assertEqual(report["images"]["<app>"]["staged_app_bind"], 0x110008AFC)
        self.assertNotEqual(report["images"]["<app>"]["original_sha256"],
                            report["images"]["<app>"]["staged_sha256"])
        self.assertEqual(app.data, original)

    def test_rejects_absent_entry_bind_even_when_literal_matches(self):
        cache, _, external = staging_inputs()
        for size in (4096, 0x19000):
            with self.subTest(size=size):
                header = macho(2, segment(b"__TEXT", 0, 0, size, 5),
                               dylib(0xC, staging.SPRINGBOARD))
                data = bytearray(header.ljust(size, b"\0"))
                if size > 0x18008:
                    pack_into("<Q", data, 0x18008, 0x110008AFC)
                with self.assertRaisesRegex(staging.audit.AuditError, "entry chained bind"):
                    staging.stage(cache, staging.audit.Image(bytes(data)), external, None)

    def test_rejects_wrong_entry_symbol_at_expected_address(self):
        cache, app, external = staging_inputs(symbol="_not_the_entry")
        with self.assertRaisesRegex(staging.audit.AuditError, "entry import"):
            staging.stage(cache, app, external, None)

    def test_rejects_overlapping_image_segments(self):
        cache, app, external = staging_inputs(data_address=0x8000)
        with self.assertRaisesRegex(staging.audit.AuditError, "overlapping staged VM segments"):
            staging.stage(cache, app, external, None)


if __name__ == "__main__":
    unittest.main()
