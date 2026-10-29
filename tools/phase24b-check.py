#!/usr/bin/env python3
"""Bounded build and verification for standalone phase24b.

Uses genuine SDK-free clang/ld64.lld ARM64 Darwin MH_DYLIB fixtures.
Host C callbacks are an explicitly MODELED lifecycle, never guest execution.
"""
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "phase24b"
MAGIC, ARM64, DYLIB = 0xFEEDFACF, 0x0100000C, 6


def bounded(argv, seconds, **kwargs):
    started = time.monotonic()
    result = subprocess.run(argv, cwd=ROOT, timeout=seconds, capture_output=True,
                            text=True, check=False, **kwargs)
    if result.returncode:
        raise RuntimeError(f"failed {argv} ({result.returncode})\n"
                           f"{result.stdout}\n{result.stderr}")
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    print(f"phase24b: command {pathlib.Path(argv[0]).name} "
          f"completed in {time.monotonic()-started:.3f}s")
    return result


def find_linker():
    for tool in ("ld64.lld", "ld64.lld-21", "ld64.lld-20", "ld64.lld-19",
                 "ld64.lld-18", "ld64.lld-17", "ld64.lld-16", "ld64.lld-15"):
        path = shutil.which(tool)
        if path:
            return path
    raise RuntimeError("LLVM ld64.lld is required; no synthetic linker fallback")


def remove_platform_load_command(path):
    """Older ld64.lld rejects a Clang LC_BUILD_VERSION in MH_OBJECT."""
    b = bytearray(path.read_bytes())
    if len(b) < 32:
        raise ValueError("short object")
    magic, cpu, _, typ, count, cmdbytes = struct.unpack_from("<6I", b)
    if (magic, cpu, typ) != (MAGIC, ARM64, 1):
        raise ValueError("invalid clang Mach-O MH_OBJECT")
    cursor, end = 32, 32 + cmdbytes
    if end > len(b) or count > 128:
        raise ValueError("invalid Mach-O commands")
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("bad load command")
        cmd, size = struct.unpack_from("<II", b, cursor)
        if size < 8 or size % 8 or cursor + size > end:
            raise ValueError("bad load command length")
        if cmd in (0x32, 0x24):
            b[cursor:end-size] = b[cursor+size:end]
            b[end-size:end] = bytes(size)
            struct.pack_into("<II", b, 16, count - 1, cmdbytes - size)
            output = path.with_suffix(".no-platform.o")
            output.write_bytes(b)
            return output
        cursor += size
    raise ValueError("no platform command in object")


def link_dylib(linker, obj, path, dependencies):
    args = [linker, "-arch", "arm64", "-platform_version",
            "macos", "11.0", "11.0", "-fatal_warnings",
            "-no_fixup_chains", "-U", "dyld_stub_binder", "-dylib",
            "-install_name", path.name, "-o", str(path)]
    try:
        bounded(args + [str(obj)] + [str(x) for x in dependencies], 45)
    except RuntimeError as error:
        if "This version of lld does not support linking for platform macOS" not in str(error):
            raise
        bounded(args + [str(remove_platform_load_command(obj))] +
                [str(x) for x in dependencies], 45)


def inspect(path, minimum_dependencies):
    b = path.read_bytes()
    if len(b) < 32 or len(b) > 4 * 1024 * 1024:
        raise ValueError(f"invalid dylib file size: {path}")
    magic, cpu, _, typ, count, size = struct.unpack_from("<6I", b)
    if (magic, cpu, typ) != (MAGIC, ARM64, DYLIB):
        raise ValueError(f"not an arm64 MH_DYLIB: {path}")
    end, cursor = 32 + size, 32
    if end > len(b) or count > 128:
        raise ValueError("invalid linked dylib header")
    sections, dependencies = set(), 0
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("truncated command")
        cmd, width = struct.unpack_from("<II", b, cursor)
        if width < 8 or width % 8 or cursor + width > end:
            raise ValueError("malformed command")
        if cmd in (0xC, 0x18, 0x1F, 0x23):
            dependencies += 1
        if cmd == 0x19:
            if width < 72:
                raise ValueError("truncated segment")
            nsects, = struct.unpack_from("<I", b, cursor + 64)
            if 72 + nsects * 80 > width:
                raise ValueError("truncated sections")
            for j in range(nsects):
                pos = cursor + 72 + j * 80
                name = b[pos:pos+16].split(b"\0", 1)[0]
                sect_size, = struct.unpack_from("<Q", b, pos + 40)
                if name in (b"__mod_init_func", b"__mod_term_func") and sect_size >= 8:
                    sections.add(name)
        cursor += width
    if cursor != end or sections != {b"__mod_init_func", b"__mod_term_func"}:
        raise ValueError(f"missing authentic init/term sections: {path}")
    if dependencies < minimum_dependencies:
        raise ValueError(f"missing expected linked dependency: {path}")
    print(f"phase24b: {path.name}, {len(b)} bytes, {dependencies} "
          "linked dependencies, constructor/terminator sections present")


def fixtures():
    clang = shutil.which(os.environ.get("CLANG", "clang"))
    if not clang:
        raise RuntimeError("clang with arm64-apple-darwin support required")
    linker = find_linker()
    libraries = []
    for i in range(6):
        source = ROOT / "tests" / f"phase24b-lib{i}.c"
        obj = BUILD / f"phase24b-lib{i}.o"
        lib = BUILD / f"phase24b-lib{i}.dylib"
        bounded([clang, "--target=arm64-apple-darwin", "-std=c11",
                 "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                 "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                 "-fPIC", "-O2", "-c", str(source), "-o", str(obj)], 45)
        selected = [] if i == 0 else (
            [libraries[2], libraries[3]] if i == 4 else
            [libraries[1], libraries[4]] if i == 5 else
            [libraries[-1]])
        link_dylib(linker, obj, lib, selected)
        inspect(lib, len(selected))
        libraries.append(lib)
    return libraries


def verify(libraries, sanitized):
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not cc:
        raise RuntimeError("C compiler required")
    exe = BUILD / ("phase24b-sanitized" if sanitized else "phase24b-host")
    command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
               "-pthread", "-g", "-I", str(ROOT / "include")]
    if sanitized:
        command += ["-O1", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer"]
    else:
        command += ["-O2"]
    command += [str(ROOT / "src" / "phase24b_lifecycle.c"),
                str(ROOT / "tests" / "phase24b-native.c"), "-o", str(exe)]
    bounded(command, 120)
    bounded([str(exe)] + [str(p) for p in libraries], 180,
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                 "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    libs = fixtures()
    verify(libs, False)
    if os.environ.get("PHASE24B_SANITIZE") == "1":
        verify(libs, True)
    print(f"phase24b: verification wall-clock: {time.monotonic()-started:.2f}s")
    print("phase24b: actual compiled Darwin guest callbacks executed: ZERO")
    print("phase24b: no validated relocator+mapper+Darwin ABI entrypoint "
          "has been integrated; don't classify host models as guest execution")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit(f"phase24b verify failed: {exc}")
