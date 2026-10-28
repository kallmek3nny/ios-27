#!/usr/bin/env python3
"""Build and verify four genuine Clang/LLD ARM64 Darwin MH_DYLIBs.

No Apple SDK or proprietary binaries. Each subprocess has a hard timeout;
the link stubs are only build-time symbol declarations, not loaded at runtime.
"""
import argparse
import pathlib
import struct
import shutil
import subprocess
import sys


def stub(path, name, symbols):
    path.write_text(
        "--- !tapi-tbd-v3\n"
        "archs: [ arm64 ]\n"
        "platform: macosx\n"
        "install-name: '" + name + "'\n"
        "current-version: 1.0.0\n"
        "compatibility-version: 1.0.0\n"
        "exports:\n"
        "  - archs: [ arm64 ]\n"
        "    symbols: [ " + ", ".join(symbols) + " ]\n"
        "...\n", encoding="ascii")


def run(command):
    completed = subprocess.run(command, capture_output=True, text=True,
                               timeout=30, check=False)
    if completed.returncode:
        raise RuntimeError("failed: " + repr(command) + "\\n" +
                           completed.stdout + completed.stderr)


def strip_platform_command(path):
    """LLD Swift-build workaround, restricted to MH_OBJECT metadata."""
    data = bytearray(path.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated Clang object")
    magic, cpu, _, typ, count, cmdbytes = struct.unpack_from("<6I", data)
    if (magic, cpu, typ) != (0xFEEDFACF, 0x0100000C, 1) or cmdbytes > len(data) - 32:
        raise ValueError("not an ARM64 Darwin MH_OBJECT")
    end, cursor = 32 + cmdbytes, 32
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("truncated load command")
        cmd, size = struct.unpack_from("<II", data, cursor)
        if size < 8 or size % 8 or cursor + size > end:
            raise ValueError("invalid load command")
        if cmd in (0x32, 0x24):
            data[cursor:end - size] = data[cursor + size:end]
            data[end - size:end] = bytes(size)
            struct.pack_into("<II", data, 16, count - 1, cmdbytes - size)
            result = path.with_suffix(".link.o")
            result.write_bytes(data)
            return result
        cursor += size
    raise ValueError("Clang object missing platform command")


def link(linker, options, obj, *extra):
    args = [linker, "-arch", "arm64", "-platform_version",
            "macos", "13.0", "13.0", "-fatal_warnings"] + options
    suffix = [str(x) for x in extra]
    try:
        run(args + [str(obj)] + suffix)
    except RuntimeError as error:
        if "This version of lld does not support linking for platform macOS" not in str(error):
            raise
        run(args + [str(strip_platform_command(obj))] + suffix)


def check_dylib(path):
    data = path.read_bytes()
    if len(data) < 32 or len(data) > 4 * 1024 * 1024:
        raise ValueError("invalid linked dylib length")
    magic, cpu, subtype, typ, count, size = struct.unpack_from("<6I", data)
    if (magic, cpu, subtype & 255, typ) != (0xFEEDFACF, 0x0100000C, 0, 6):
        raise ValueError("not an ARM64 MH_DYLIB")
    if count > 64 or size > len(data) - 32:
        raise ValueError("invalid linked dylib command count")
    cursor = 32
    commands = []
    for _ in range(count):
        if cursor + 8 > 32 + size:
            raise ValueError("truncated linked load command")
        command, length = struct.unpack_from("<II", data, cursor)
        if length < 8 or length % 8 or cursor + length > 32 + size:
            raise ValueError("invalid linked command length")
        commands.append(command)
        cursor += length
    if cursor != 32 + size or 0x80000034 not in commands or 0x80000033 not in commands:
        raise ValueError("linked dylib missing real chained fixups or exports")
    return commands


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--linker", default="ld64.lld")
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--core", default="build/libios27_core.a")
    parser.add_argument("--output-dir", default="build/phase19b")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    output = pathlib.Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not shutil.which(args.linker):
        candidates = ["ld64.lld", "ld64.lld-20", "ld64.lld-19",
                      "ld64.lld-18", "ld64.lld-17"]
        args.linker = next((x for x in candidates if shutil.which(x)), "")
        if not args.linker:
            raise ValueError("ld64.lld not available after installing LLVM lld")
    paths = {}
    stubs = {}
    exports = {
        "base": ["_phase19b_base_number", "_phase19b_base_calculate"],
        "leaf": ["_phase19b_leaf_multiplier", "_phase19b_leaf_value"],
        "middle": ["_phase19b_middle_offset", "_phase19b_middle_value"],
        "root": ["_phase19b_root_seed", "_phase19b_root_value"],
    }
    names = {k: "@rpath/" + k.title() + ".dylib" for k in exports}
    dependencies = {
        "base": [],
        "leaf": ["base"],
        "middle": ["leaf", "base"],
        "root": ["middle", "leaf", "base"],
    }
    for stem in exports:
        obj = output / ("phase19b-" + stem + ".o")
        run([args.clang, "--target=arm64-apple-darwin",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-fPIC", "-O2", "-c",
             str(root / "tests/fixtures" / ("phase19b-" + stem + ".c")),
             "-o", str(obj)])
        dylib = output / ("phase19b-" + stem + ".dylib")
        options = ["-dylib", "-install_name", names[stem],
                   "-o", str(dylib)]
        if stem == "root":
            options += ["-rpath", "@loader_path/Plugins",
                        "-rpath", "@loader_path/Frameworks"]
        link(args.linker, options, obj, *(stubs[x] for x in dependencies[stem]))
        commands = check(dylib)
        if dependencies[stem] and 0x0c not in commands:
            raise ValueError("missing linked LC_LOAD_DYLIB for " + stem)
        paths[stem] = dylib
        link_stub = output / ("phase19b-" + stem + ".tbd")
        stub(link_stub, names[stem], exports[stem])
        stubs[stem] = link_stub
    executable = output / "phase19b-verify"
    core = pathlib.Path(args.core).resolve()
    if not core.is_file():
        raise ValueError("missing core library: " + str(core))
    run([args.host_cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-Wpedantic", "-I", str(root / "include"),
         str(root / "tests/fixtures/phase19b-verify.c"),
         str(core), "-o", str(executable)])
    run([str(executable), str(paths["root"]), str(paths["middle"]),
         str(paths["leaf"]), str(paths["base"])])
    print("phase19b: four real arm64 Darwin dylibs compiled, linked and verified")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase19b: " + str(exc))
