#!/usr/bin/env python3
"""Phase 23a: real compiler-produced ARM64 Darwin dylibs + host/native tests.

No Apple SDK, host filesystem access from the loader, or guest constructors.
Every child command has a hard timeout, and all linked fixtures are inspected.
"""
import argparse
import pathlib
import struct
import subprocess
import sys

FIXTURES = {
    "base": """
__attribute__((visibility("default"))) int phase23a_base_number = 7;
__attribute__((visibility("default"))) int phase23a_base(void) {
    return phase23a_base_number;
}
__attribute__((visibility("default"))) int phase23a_choice(void) { return 9; }
""",
    "mid": """
extern int phase23a_base(void);
__attribute__((visibility("default"))) int phase23a_mid(void) {
    return phase23a_base() + 3;
}
""",
    "util": """
extern int phase23a_base_number;
__attribute__((visibility("default"))) int phase23a_util(void) {
    return phase23a_base_number * 2;
}
__attribute__((weak, visibility("default")))
int phase23a_choice(void) { return 3; }
""",
    "facade": """
extern int phase23a_mid(void);
extern int phase23a_util(void);
__attribute__((visibility("default"))) int phase23a_facade(void) {
    return phase23a_mid() + phase23a_util();
}
""",
    "top": """
extern int phase23a_facade(void);
extern int phase23a_base(void);
__attribute__((visibility("default"))) int phase23a_top(void) {
    return phase23a_facade() + phase23a_base();
}
""",
}
EXPORTS = {
    "base": ["_phase23a_base", "_phase23a_base_number", "_phase23a_choice"],
    "mid": ["_phase23a_mid"],
    "util": ["_phase23a_util", "_phase23a_choice"],
    "facade": ["_phase23a_facade"],
    "top": ["_phase23a_top"],
}
DEPS = {
    "base": [],
    "mid": ["base"],
    "util": ["base"],
    "facade": ["mid", "util"],
    "top": ["facade", "base"],
}
ORDER = ["base", "mid", "util", "facade", "top"]


def run(argv, seconds):
    completed = subprocess.run(
        [str(v) for v in argv], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, timeout=seconds, check=False)
    if completed.returncode:
        raise RuntimeError("failed exit=" + str(completed.returncode) +
                           " command=" + repr(argv) + "\n" + completed.stdout)
    if completed.stdout.strip():
        print(completed.stdout.strip())


def stub(path, lib):
    path.write_text(
        "--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n"
        "install-name: '" + lib + ".dylib'\n"
        "current-version: 1.0.0\ncompatibility-version: 1.0.0\n"
        "exports:\n  - archs: [ arm64 ]\n    symbols: [ " +
        ", ".join(EXPORTS[lib]) + " ]\n...\n", encoding="ascii")



def strip_platform_command(obj):
    """Remove one original Clang MH_OBJECT platform record for old Swift LLD."""
    data = bytearray(obj.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated compiler object")
    magic, cpu, _, kind, count, size = struct.unpack_from("<6I", data)
    if (magic, cpu, kind) != (0xfeedfacf, 0x0100000c, 1) or size > len(data) - 32:
        raise ValueError("invalid compiler Mach-O object")
    end, cursor = 32 + size, 32
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("truncated object commands")
        cmd, length = struct.unpack_from("<II", data, cursor)
        if length < 8 or length % 8 or cursor + length > end:
            raise ValueError("invalid object command")
        if cmd in (0x32, 0x24):
            data[cursor:end - length] = data[cursor + length:end]
            data[end - length:end] = bytes(length)
            struct.pack_into("<II", data, 16, count - 1, size - length)
            patched = obj.with_suffix(".link.o")
            patched.write_bytes(data)
            return patched
        cursor += length
    raise ValueError("object has no removable platform command")


def link(linker, options, obj, *dependencies):
    base = [linker, "-arch", "arm64", "-platform_version",
            "macos", "13.0", "13.0", "-fatal_warnings"] + options
    extra = [str(p) for p in dependencies]
    try:
        run(base + [str(obj)] + extra, 30)
    except RuntimeError as exception:
        if "does not support linking for platform macOS" not in str(exception):
            raise
        run(base + [str(strip_platform_command(obj))] + extra, 30)


def check(path):
    image = path.read_bytes()
    if len(image) < 32 or len(image) > 4 * 1024 * 1024:
        raise ValueError("invalid linked dylib length")
    magic, cpu, subtype, kind, count, cmdbytes = struct.unpack_from("<6I", image)
    if (magic, cpu, subtype & 255, kind) != (0xfeedfacf, 0x0100000c, 0, 6):
        raise ValueError("not compiler-linked arm64 MH_DYLIB")
    if count > 64 or cmdbytes > len(image) - 32:
        raise ValueError("linked load commands outside file")
    cursor, cmds = 32, []
    for _ in range(count):
        if cursor + 8 > 32 + cmdbytes:
            raise ValueError("truncated linked load command")
        cmd, size = struct.unpack_from("<II", image, cursor)
        if size < 8 or size % 8 or cursor + size > 32 + cmdbytes:
            raise ValueError("invalid linked load command")
        cmds.append(cmd)
        cursor += size
    if cursor != 32 + cmdbytes or not any(cmd in cmds for cmd in (0x80000034, 0x80000022, 0x22)) or not any(cmd in cmds for cmd in (0x80000033, 0x80000022, 0x22)):
        raise ValueError("missing real supported dyld fixup/export metadata: " + repr([hex(x) for x in cmds]))
    return cmds


def main():
    argp = argparse.ArgumentParser()
    argp.add_argument("--output-dir", default="build/phase23a")
    argp.add_argument("--clang", default="clang")
    argp.add_argument("--ld", default="ld64.lld")
    argp.add_argument("--cc", default="cc")
    argp.add_argument("--sanitize", action="store_true")
    args = argp.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    dest = pathlib.Path(args.output_dir).resolve()
    dest.mkdir(parents=True, exist_ok=True)

    for lib in ORDER:
        source = dest / ("phase23a-" + lib + ".c")
        obj = dest / ("phase23a-" + lib + ".o")
        source.write_text(FIXTURES[lib], encoding="ascii")
        run([args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fPIC", "-fno-builtin", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-Wall", "-Wextra", "-Werror", "-O2", "-c", source,
             "-o", obj], 30)
        stub(dest / ("phase23a-" + lib + ".tbd"), lib)
        options = ["-dylib", "-install_name", lib + ".dylib",
                   "-o", str(dest / (lib + ".dylib"))]
        additional = [dest / ("phase23a-" + dep + ".tbd")
                      for dep in DEPS[lib]]
        link(args.ld, options, obj, *additional)
        cmds = check(dest / (lib + ".dylib"))
        actual_deps = cmds.count(0x0c)
        if actual_deps != len(additional):
            raise ValueError(lib + ": expected " + str(len(additional)) +
                             " linked dependencies, got " + str(actual_deps))
        print("linked genuine ARM64 MH_DYLIB:", lib, "deps:", actual_deps)

    cmake_dir = dest / "cmake"
    opts = ["-DCMAKE_BUILD_TYPE=Debug", "-DIOS27_BUILD_TESTS=OFF"]
    ccflags = ["-std=c11", "-D_GNU_SOURCE", "-O1", "-g", "-pthread",
               "-Wall", "-Wextra", "-Werror", "-Wpedantic"]
    if args.sanitize:
        flags = "-fsanitize=address,undefined -fno-omit-frame-pointer"
        opts += ["-DCMAKE_C_FLAGS=" + flags]
        ccflags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    run(["cmake", "-S", root, "-B", cmake_dir] + opts, 120)
    run(["cmake", "--build", cmake_dir, "--parallel", "2"], 300)
    executable = dest / "phase23a-integration"
    run([args.cc] + ccflags +
        ["-I", root / "include", root / "src/phase23a_loader.c",
         root / "tests/phase23a-integration.c",
         cmake_dir / "libios27_core.a", "-o", executable], 120)
    run([executable] + [dest / (lib + ".dylib")
                       for lib in ("top", "facade", "util", "mid", "base")], 180)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit("phase23a verification failed: " + str(error))
