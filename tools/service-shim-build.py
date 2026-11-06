#!/usr/bin/env python3
"""Audit and link two real SDK-free Darwin objects: named client + static shim.

No unresolved host/Apple symbols are accepted. This requires LLVM ld64.lld;
the older one-object fallback profile is deliberately not broadened.
"""
import argparse
import hashlib
import json
import pathlib
import runpy
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
IMPORTS = {"_getpid", "_open", "_read", "_write", "_close", "_lseek",
           "_mmap", "_mprotect", "_munmap", "___error",
           "_malloc", "_calloc", "_realloc", "_free"}


def need(condition, message):
    if not condition:
        raise ValueError(message)


def symbols(path, expected_kind):
    data = path.read_bytes()
    need(32 <= len(data) <= 2 * 1024 * 1024, "invalid object/image size")
    magic, cpu, subtype, kind, count, length = struct.unpack_from("<6I", data)
    need((magic, cpu, subtype & 255, kind) ==
         (0xFEEDFACF, 0x0100000C, 0, expected_kind), "not plain ARM64 Mach-O")
    need(count <= 128 and length <= len(data) - 32, "invalid command bounds")
    cursor, end, table = 32, 32 + length, None
    for _ in range(count):
        need(cursor + 8 <= end, "truncated command")
        command, size = struct.unpack_from("<II", data, cursor)
        need(size >= 8 and size % 8 == 0 and size <= end - cursor, "bad command")
        if command == 2:
            need(size == 24 and table is None, "invalid/duplicate symbol table")
            table = struct.unpack_from("<4I", data, cursor + 8)
        cursor += size
    need(cursor == end and table is not None, "missing symbol table")
    symoff, count, stroff, strsize = table
    need(count <= 4096 and symoff + 16 * count <= len(data) and
         strsize <= 65536 and stroff + strsize <= len(data), "symbol table bounds")
    undefined, defined = set(), set()
    for i in range(count):
        nameoff, flags, _, _, value = struct.unpack_from("<IBBHQ", data, symoff + 16 * i)
        if flags & 0xE0:
            continue
        need(nameoff < strsize, "invalid symbol name index")
        raw = data[stroff + nameoff:stroff + strsize]
        need(b"\0" in raw, "unterminated symbol name")
        name = raw.split(b"\0", 1)[0].decode("ascii")
        if flags & 0x0E == 0:
            need(flags & 1 and value == 0, "unsupported common/local undefined symbol")
            undefined.add(name)
        elif flags & 1:
            defined.add(name)
    return data, undefined, defined


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    parser.add_argument("--linker", required=True)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    args = parser.parse_args()
    need(args.linker != "none", "the named service shim requires LLVM ld64.lld")
    support = runpy.run_path(str(ROOT / "tools/phase19a-build.py"))
    command = support["command"]
    args.out.unlink(missing_ok=True)
    args.manifest.unlink(missing_ok=True)
    inputs = [ROOT / "tests/fixtures/service-shim-client.c", ROOT / "src/darwin_shim.c"]
    objects = [args.out.with_name("service-shim-client.o"),
               args.out.with_name("service-shim-runtime.o")]
    for source, obj in zip(inputs, objects):
        command([args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
                 "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables",
                 "-fno-asynchronous-unwind-tables", "-O2", "-Wall", "-Wextra",
                 "-Werror", "-Wpedantic", "-I", str(ROOT / "include"), "-I",
                 str(ROOT / "src"), "-c", str(source), "-o", str(obj)])
    _, imported, client_exports = symbols(objects[0], 1)
    _, shim_imports, shim_exports = symbols(objects[1], 1)
    need(imported == IMPORTS, f"client import allowlist mismatch: {sorted(imported)}")
    need(client_exports == {"_main"}, "unexpected client export")
    need(not shim_imports, f"shim needs unsupported runtime imports: {sorted(shim_imports)}")
    need(shim_exports == IMPORTS | {"_ios27_shim_binding"}, "unexpected shim export set")
    # Android NDK ships the unified LLVM linker rather than ld64.lld.
    flavor = ["-flavor", "darwin"] if pathlib.Path(args.linker).name in ("ld.lld", "ld.lld.exe") else []
    common = [args.linker, *flavor, "-arch", "arm64", "-platform_version", "macos",
              "13.0", "13.0", "-e", "_main", "-fatal_warnings",
              "-segprot", "__DATA_CONST", "r--", "r--", "-o", str(args.out)]
    try:
        command(common + [str(obj) for obj in objects])
    except RuntimeError as exc:
        if "This version of lld does not support linking for platform" not in str(exc):
            raise
        command(common + [str(support["no_platform_command"](obj)) for obj in objects])
    support["inspect"](args.out)
    data, remaining, exports = symbols(args.out, 2)
    need(not remaining and IMPORTS <= exports, "unresolved or missing linked service symbols")
    args.manifest.write_text(json.dumps({
        "format": "ios27-static-service-shim-v1", "entry": "_main",
        "entry_arguments": ["argc", "argv", "envp", "apple"],
        "client_imports": sorted(imported), "shim_object_definitions": sorted(shim_exports),
        "unresolved_linked_symbols": sorted(remaining),
        "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in [*inputs, ROOT / "src/darwin_shim_internal.h",
                                       ROOT / "include/ios27/darwin_shim.h",
                                       ROOT / "include/ios27/service_bindings.h",
                                       ROOT / "include/ios27/service_bridge.h"]},
        "executable_sha256": hashlib.sha256(data).hexdigest(),
    }, indent=2) + "\n", encoding="utf-8")
    print("named shim: audited 14 client imports, no shim runtime imports, linked 4-argument LC_MAIN")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, struct.error, subprocess.TimeoutExpired) as exc:
        sys.exit(f"service shim build: {exc}")
