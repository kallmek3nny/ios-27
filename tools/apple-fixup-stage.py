#!/usr/bin/env python3
"""Stage iOS Simulator chained fixups in private, non-executable file copies."""

import argparse
from collections import Counter
from contextlib import ExitStack
from functools import lru_cache, partial
from hashlib import sha256
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
from struct import pack_into, unpack_from
import sys


spec = spec_from_file_location("apple_cache_audit", Path(__file__).with_name("apple-cache-audit.py"))
audit = module_from_spec(spec)
sys.modules[spec.name] = audit
spec.loader.exec_module(audit)

ALIGN = 0x4000
SPRINGBOARD = "/System/Library/PrivateFrameworks/SpringBoard.framework/SpringBoard"
WEAK_DEFINES = 0x8000
WEAK_EXPORT = 0x04
PLATFORM_MEMORY_INSTALL_NAME = "/ios27/libplatform-memory.dylib"
PLATFORM_HOST = "/usr/lib/system/libsystem_platform.dylib"
PLATFORM_MEMORY_SYMBOLS = frozenset("_" + name for name in (
    "bzero", "memcmp", "memcpy", "memmove", "memset", "strcmp", "strcpy",
    "strlen", "strncmp", "strncpy", "strnlen", "strstr"))


def validate_platform_memory(image):
    """Narrow stateless original-source replacement profile, never a whole libSystem."""
    audit.check(image.cache is None and image.kind == 6 and image.platform == 7
                and image.install_name == PLATFORM_MEMORY_INSTALL_NAME
                and len(image.data) <= 1024 * 1024,
                "invalid platform-memory replacement profile")
    audit.check(not image.dependencies and not image.classic_bind_bytes
                and not image.initializers and not image.objc_load_candidates,
                "platform-memory replacement must have no runtime dependencies")
    audit.check(not image.chained_imports(), "platform-memory replacement has imports")
    _, binds, rebases = image.chain_sites(0)
    audit.check(not binds and not rebases, "platform-memory replacement has pointer fixups")
    audit.check(all(not size or protection == 5 or (name == b"__LINKEDIT" and protection == 1)
                    for name, (_, size, _, _, protection) in image.segments.items()),
                "platform-memory replacement must be stateless read-only code")
    code = [section for section in image.sections if section.segment == b"__TEXT"
            and section.flags & 0x80000400 and section.flags & 0xFF == 0]
    for section in code:
        vm, _, file_offset, file_size, protection = image.segments[section.segment]
        audit.check(protection == 5 and section.address >= vm
                    and section.file_offset == file_offset + section.address - vm,
                    "platform-memory code file/VM mismatch")
        audit.interval(section.address - vm, section.size, file_size, "platform-memory code")
        audit.interval(section.file_offset, section.size, len(image.data), "platform-memory code bytes")
    exports = {}
    text = image.segments[b"__TEXT"][0]
    for symbol in PLATFORM_MEMORY_SYMBOLS:
        entry = image.export(symbol)
        audit.check(entry and entry[0] == 0 and entry[2] is None,
                    f"missing regular platform-memory export: {symbol}")
        address = text + entry[1]
        audit.check(address % 4 == 0 and any(s.address <= address and address + 4 <= s.address + s.size
                                           for s in code),
                    f"platform-memory export is outside code: {symbol}")
        exports[symbol] = entry[1]
    return exports


def contains_vm(image, address):
    return any(start <= address < start + size and protection & 1
               for start, size, _, _, protection in image.segments.values())


def layout(images, external, cache):
    addresses = {"<app>": 0x100000000, SPRINGBOARD: 0x110000000}
    cursor = 0x120000000
    spans = []
    for path in ["<app>", *sorted(external)]:
        image = images[path]
        text = image.segments[b"__TEXT"][0]
        span = 0
        segments = []
        for name, (vm, size, offset, file_size, protection) in image.segments.items():
            if name == b"__PAGEZERO":
                continue
            audit.check(vm >= text and not ((protection & 6) == 6), "invalid staged segment")
            audit.interval(offset, file_size, len(image.data), "staged segment")
            if size:
                segments.append((vm, vm + size))
            span = max(span, vm - text + size)
        segments.sort()
        audit.check(all(end <= start for (_, end), (start, _) in zip(segments, segments[1:])),
                    "overlapping staged VM segments")
        span = (span + ALIGN - 1) & -ALIGN
        if path not in addresses:
            addresses[path] = cursor
            cursor += span
        base = addresses[path]
        spans.append((base, base + span))
    spans.sort()
    audit.check(all(end <= next_start for (_, end), (next_start, _) in zip(spans, spans[1:]))
                and spans[-1][1] <= cache.region[0], "standalone VM layout overlaps")
    return addresses


def rebase(image, base, fmt, raw):
    preferred = image.segments[b"__TEXT"][0]
    high = (raw >> 36) & 0xFF
    low = raw & ((1 << 36) - 1)
    target = low + preferred if fmt == 6 else low
    audit.check(contains_vm(image, target), "rebase target outside readable image")
    return (base + target - preferred) | (high << 56)


def bound_address(result, addresses):
    path, image, flags, offset = result
    kind = flags & 3
    if kind == 2:
        return offset, None, None
    audit.check(kind in (0, 1), "unsupported export kind")
    text = image.segments[b"__TEXT"][0]
    if image.cache:
        image.cache.locate(text + offset, 1)
        return text + offset, image, text
    audit.check(contains_vm(image, text + offset), "export target outside readable image")
    return addresses[path] + offset, image, addresses[path]


def stage(cache, app, external, open_standalone, *, weak_policy="unique", prepared=None,
          platform_memory=None):
    audit.check(weak_policy in ("unique", "load-order"), "unsupported weak lookup policy")
    replacement_exports = validate_platform_memory(platform_memory) if platform_memory is not None else {}
    external = dict(external)
    resolver = audit.Resolver(cache, app, external, open_standalone)
    images, closure = resolver.closure()
    audit.check(SPRINGBOARD in external, "SpringBoard.framework missing from standalone inputs")
    audit.check(not any(item["required"] for item in closure["missing"]),
                "required dependencies are missing from the staging inputs")
    if platform_memory is not None:
        audit.check(PLATFORM_MEMORY_INSTALL_NAME not in images and PLATFORM_MEMORY_INSTALL_NAME not in external,
                    "platform-memory replacement conflicts with the image catalog")
        images[PLATFORM_MEMORY_INSTALL_NAME] = platform_memory
        external[PLATFORM_MEMORY_INSTALL_NAME] = platform_memory
    addresses = layout(images, external, cache)
    weak_choices = {}

    @lru_cache(maxsize=None)
    def weak_target(symbol):
        strong, weak = {}, {}
        for path, image in images.items():
            if not image.flags & WEAK_DEFINES:
                continue
            if weak_policy == "load-order":
                # Match dyld's shallow weak-definition search: the first
                # definition wins unless a later strong definition overrides
                # it. This explicit research load order is NOT a claim that
                # cache weak-override patches or initializers have run.
                # apple-oss-distributions/dyld fd8d0c4d, Loader.cpp:3001-3031.
                exported = image.export(symbol)
                result = ((path, image, exported[0], exported[1])
                          if exported and not exported[0] & (8 | 0x10) else None)
            else:
                result = resolver.resolve(path, symbol)
            if result:
                matches = weak if result[2] & WEAK_EXPORT else strong
                matches[(result[0], result[3])] = result
        matches = strong or weak
        selected = (next(iter(matches.values()))
                    if matches and (len(matches) == 1 or weak_policy == "load-order") else None)
        if selected:
            weak_choices[symbol] = {"provider": selected[0], "export_offset": selected[3],
                                    "candidate_count": len(strong) + len(weak),
                                    "strong": not bool(selected[2] & WEAK_EXPORT)}
        return selected, len(matches)

    rows = {}
    for path in ("<app>", *sorted(external)):
        image = images[path]
        audit.check(not image.classic_bind_bytes, f"classic binds are outside this stage: {path}")
        imports = image.chained_imports()
        records = []
        _, binds, rebase_count = image.chain_sites(len(imports), records)
        if path == "<app>":
            audit.interval(0x18008, 8, len(image.data), "SpringBoard entry chained bind")
            entry = [raw for offset, _, raw in records if offset == 0x18008]
            audit.check(len(entry) == 1 and entry[0] >> 63,
                        "missing SpringBoard entry chained bind")
            raw = entry[0]
            ordinal, weak, symbol, addend = imports[raw & 0xFFFFFF]
            audit.check(ordinal > 0 and image.dependencies[ordinal - 1].path == SPRINGBOARD
                        and not weak and symbol == "_SBSystemAppMain" and addend == 0
                        and ((raw >> 24) & 0xFF) == 0, "unexpected SpringBoard entry import")
        use = Counter(index for _, index, _ in binds)
        interior_targets = {index for _, index, addend in binds if addend}
        targets, deferred = {}, []
        host_placeholders = []
        host_replacements = []
        weak_matches = coalesced_matches = 0
        for index, (ordinal, weak, symbol, addend) in enumerate(imports):
            if index not in use:
                continue
            candidates = None
            if ordinal == -3:
                result, candidates = weak_target(symbol)
                weak_matches += result is not None and candidates == 1
                coalesced_matches += result is not None and candidates > 1
            elif ordinal >= -1:
                provider = (image.dependencies[ordinal - 1].path if ordinal > 0 else
                            path if ordinal == 0 else "<app>")
                result = resolver.resolve(provider, symbol)
            else:
                result = None
            if result:
                is_placeholder = result[1].export_is_host_placeholder(result[2], result[3])
                if is_placeholder and result[0] == PLATFORM_HOST and symbol in replacement_exports:
                    audit.check(not addend and index not in interior_targets,
                                "platform-memory override requires a whole-function pointer")
                    host_replacements.append({"symbol": symbol, "provider": result[0],
                                              "replacement": PLATFORM_MEMORY_INSTALL_NAME,
                                              "sites": use[index]})
                    result = (PLATFORM_MEMORY_INSTALL_NAME, platform_memory, 0, replacement_exports[symbol])
                    is_placeholder = False
                target, _, _ = bound_address(result, addresses)
                targets[index] = (target, addend)
                if is_placeholder:
                    host_placeholders.append({"symbol": symbol, "provider": result[0],
                                              "sites": use[index]})
            elif weak and (ordinal >= -1 or (ordinal == -3 and weak_policy == "load-order")):
                targets[index] = None
            else:
                deferred.append({"symbol": symbol, "ordinal": ordinal,
                                 "sites": use[index], "candidates": candidates})

        row = {"vm_base": addresses[path], "bind_sites": len(binds),
               "rebase_sites": rebase_count, "unique_weak_targets": weak_matches,
                "load_order_coalesced_targets": coalesced_matches}
        if host_placeholders:
            row["host_placeholder_imports"] = host_placeholders
        if host_replacements:
            row["host_replacements"] = host_replacements
        rows[path] = row
        if deferred:
            row["deferred"] = deferred
            continue

        expected = {}
        for offset, fmt, raw in records:
            audit.check(offset not in expected, "duplicate staged fixup slot")
            if raw >> 63:
                target = targets[raw & 0xFFFFFF]
                inline = (raw >> 24) & 0xFF
                inline = inline - 256 if inline >= 128 else inline
                value = 0 if target is None else target[0] + target[1] + inline
            else:
                value = rebase(image, addresses[path], fmt, raw)
            audit.check(0 <= value < (1 << 64), "staged pointer exceeds 64 bits")
            expected[offset] = value
        copy = bytearray(image.data)
        for offset, value in expected.items():
            pack_into("<Q", copy, offset, value)
        audit.check(all(unpack_from("<Q", copy, offset)[0] == value
                        for offset, value in expected.items()), "staged pointer readback mismatch")
        row["verified_fixups"] = len(expected)
        row["original_sha256"] = sha256(image.data).hexdigest()
        row["staged_sha256"] = sha256(copy).hexdigest()
        if path == "<app>":
            row["staged_app_bind"] = unpack_from("<Q", copy, 0x18008)[0]
            audit.check(row["staged_app_bind"] == addresses[SPRINGBOARD] + 0x8AFC,
                        "unexpected SpringBoard entry bind")
        if prepared is not None:
            prepared(path, image, addresses[path], copy)
    return {
        "scope": "24A434 private non-executable fixup staging",
        "weak_lookup_policy": weak_policy,
        "weak_choices": weak_choices,
        "load_order": list(images),
        "host_runtime": audit.host_runtime_requirements(images, resolver),
        "host_replacement_import_entries": sum(len(row.get("host_replacements", [])) for row in rows.values()),
        "host_replacement_bind_sites": sum(item["sites"] for row in rows.values()
                                           for item in row.get("host_replacements", [])),
        "cached_prebound_references_replaced": False,
        "host_placeholder_import_entries": sum(len(row.get("host_placeholder_imports", []))
                                                for row in rows.values()),
        "host_placeholder_bind_sites": sum(item["sites"] for row in rows.values()
                                           for item in row.get("host_placeholder_imports", [])),
        "images": rows,
        "verified_fixups": sum(row.get("verified_fixups", 0) for row in rows.values()),
        "staged_images": sum("verified_fixups" in row for row in rows.values()),
        "deferred_images": [path for path, row in rows.items() if "deferred" in row],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--framework", type=Path, required=True)
    parser.add_argument("--runtime-root", type=Path, required=True)
    parser.add_argument("--weak-policy", choices=("unique", "load-order"), default="unique",
                        help="unique-only by default; explicit research load-order coalescing is not runtime initialization")
    args = parser.parse_args()
    try:
        with ExitStack() as stack:
            cache = audit.Cache(stack, args.cache)
            app = audit.Image(audit.mapped_file(stack, args.app))
            framework = audit.Image(audit.mapped_file(stack, args.framework))
            audit.check(app.kind == 2 and app.platform == 7 and app.entry_offset == 0x618
                        and framework.kind == 6 and framework.platform == 7
                        and framework.install_name == SPRINGBOARD,
                        "expected the audited Simulator SpringBoard app and framework")
            root = args.runtime_root.resolve(strict=True)
            audit.check(root.is_dir(), "invalid runtime root")
            report = stage(cache, app, {SPRINGBOARD: framework}, partial(audit.standalone, stack, root),
                           weak_policy=args.weak_policy)
    except (OSError, ValueError) as error:
        parser.exit(1, f"fixup stage failed: {error}\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
