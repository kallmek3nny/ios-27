#!/usr/bin/env python3
"""Link genuine SDK-free ARM64 Darwin libc + named BSD shim into MH_EXECUTE.

Audit compiler imports and definitions before linking, disallow unresolved
symbols and record source hashes. No fallback to undefined Apple libSystem.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "tools" / "service-shim-build.py"
SOURCES = (
    "tests/fixtures/phase27b-libsystem.c",
    "src/darwin_shim.c",
    "src/phase26a_libc.c",
    "src/phase27b_bsd_shim.c",
    "src/phase27b_extras.c",
)
ALLOWED = {
    "_malloc", "_calloc", "_realloc", "_free", "_memcpy", "_memmove",
    "_memset", "_memcmp", "_strlen", "_strnlen", "_strcmp", "_strncmp",
    "_strcpy", "_strncpy", "_strdup", "_snprintf", "_strtol", "_strerror",
    "_getenv", "_setenv", "_unsetenv", "_qsort", "_bsearch", "_atexit",
    "_ios27_27b_run_exit_handlers", "_open", "_openat", "_read", "_write",
    "_pread", "_pwrite", "_close", "_lseek", "_fstat", "_fcntl",
    "_gettimeofday", "_getpid", "_mmap", "_mprotect", "_munmap", "___error",
}
MUST_CALL = {
    "_malloc", "_calloc", "_realloc", "_free", "_memcpy", "_memmove",
    "_snprintf", "_qsort", "_bsearch", "_atexit", "_openat", "_pread",
    "_pwrite", "_write", "_read", "_fstat", "_fcntl", "_gettimeofday",
    "_getpid", "_close", "_lseek", "___error",
}


def need(ok, message):
    if not ok:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    parser.add_argument("--linker", required=True)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()
    need(args.linker != "none", "LLVM ld64.lld is mandatory for phase27b")
    base = runpy.run_path(str(BASE))
    support = runpy.run_path(str(ROOT / "tools/phase19a-build.py"))
    symbols = base["symbols"]
    command = support["command"]
    objects = [args.out.with_name("phase27b-%d.o" % i)
               for i in range(len(SOURCES))]
    args.out.unlink(missing_ok=True)
    args.manifest.unlink(missing_ok=True)
    for i, (source, obj) in enumerate(zip(SOURCES, objects)):
        flags = ["-DPHASE26A_DARWIN_EXPORT"] if i == 2 else []
        command([
            args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
            "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables",
            "-fno-asynchronous-unwind-tables", "-O0" if i == 2 else "-O2",
            "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
            *flags, "-c", str(ROOT / source), "-o", str(obj)
        ])
    imported = set()
    exported = set()
    report = {}
    for source, obj in zip(SOURCES, objects):
        _, unresolved, definitions = symbols(obj, 1)
        imported.update(unresolved)
        exported.update(definitions)
        report[source] = {
            "imports": sorted(unresolved), "definitions": sorted(definitions)
        }
    need(report[SOURCES[0]]["definitions"] == ["_main"],
         "fixture must define only ordinary _main")
    need(set(report[SOURCES[0]]["imports"]) <= ALLOWED,
         "unexpected fixture imports")
    need(MUST_CALL <= set(report[SOURCES[0]]["imports"]),
         "missing required named fixture calls")
    need(imported <= exported,
         "unresolved imports across objects: %s" % sorted(imported - exported))
    need(ALLOWED <= exported,
         "missing requested named definitions: %s" % sorted(ALLOWED - exported))
    need("_ios27_shim_binding" in exported,
         "service binding slot absent")
    common = [
        args.linker, "-arch", "arm64", "-platform_version", "macos",
        "13.0", "13.0", "-e", "_main", "-fatal_warnings",
        "-segprot", "__DATA_CONST", "r--", "r--", "-o", str(args.out),
    ]
    try:
        command(common + [str(obj) for obj in objects])
    except RuntimeError as exc:
        if "This version of lld does not support linking for platform" not in str(exc):
            raise
        command(common + [str(support["no_platform_command"](obj)) for obj in objects])
    support["inspect"](args.out)
    data, remaining, definitions = symbols(args.out, 2)
    need(not remaining and ALLOWED <= definitions and "_main" in definitions,
         "linked symbol resolution failed")
    args.manifest.write_text(json.dumps({
        "format": "ios27-phase27b-static-libsystem-v1",
        "target": "arm64-apple-darwin", "entry": "_main",
        "entry_arguments": ["argc", "argv", "envp", "apple"],
        "client_imports": report[SOURCES[0]]["imports"],
        "unresolved_linked_symbols": sorted(remaining),
        "objects": report,
        "source_sha256": {
            source: hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
            for source in SOURCES
        },
        "executable_sha256": hashlib.sha256(data).hexdigest(),
    }, indent=2) + "\n", encoding="utf-8")
    print("phase27b: audited compiler-linked ARM64 named libc and BSD symbols")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase27b build: %s" % exc)
