#!/usr/bin/env python3
"""Phase 24a: genuine Clang/LLVM Mach-O fixtures and bounded host verification."""
import argparse
import os
import pathlib
import struct
import subprocess
import sys

SOURCES = {
    "core": """
__attribute__((visibility("default"))) int phase24a_core_value = 7;
__attribute__((visibility("default"))) int phase24a_core(void) {
    return phase24a_core_value;
}
__attribute__((visibility("default"))) int phase24a_choice(void) { return 11; }
""",
    "math": """
extern int phase24a_core(void);
__attribute__((visibility("default"))) int phase24a_math(void) {
    return phase24a_core() + 5;
}
""",
    "data": """
extern int phase24a_core_value;
__attribute__((visibility("default"))) int phase24a_data(void) {
    return phase24a_core_value * 2;
}
""",
    "left": """
extern int phase24a_math(void);
extern int phase24a_data(void);
__attribute__((visibility("default"))) int phase24a_left(void) {
    return phase24a_math() + phase24a_data();
}
""",
    "right": """
extern int phase24a_core(void);
extern int phase24a_data(void);
__attribute__((visibility("default"))) int phase24a_right(void) {
    return phase24a_core() + phase24a_data();
}
__attribute__((weak, visibility("default")))
int phase24a_choice(void) { return 2; }
""",
    "front": """
extern int phase24a_left(void);
extern int phase24a_right(void);
__attribute__((visibility("default"))) int phase24a_front(void) {
    return phase24a_left() + phase24a_right();
}
""",
    "app1": """
extern int phase24a_front(void);
int main(void) { return phase24a_front() + 1; }
""",
    "app2": """
extern int phase24a_right(void);
int main(void) { return phase24a_right() + 2; }
""",
}
EXPORTS = {
    "core": ["_phase24a_core_value", "_phase24a_core", "_phase24a_choice"],
    "math": ["_phase24a_math"],
    "data": ["_phase24a_data"],
    "left": ["_phase24a_left"],
    "right": ["_phase24a_right", "_phase24a_choice"],
    "front": ["_phase24a_front"],
}
DEPS = {
    "core": [],
    "math": ["core"],
    "data": ["core"],
    "left": ["math", "data"],
    "right": ["data", "core"],
    "front": ["left", "right"],
    "app1": ["front"],
    "app2": ["right"],
}
ORDER = ["core", "math", "data", "left", "right", "front", "app1", "app2"]
HOST_ARGS = ["front", "left", "right", "math", "data", "core"]


def run(argv, timeout, cwd=None, env=None):
    args = [str(arg) for arg in argv]
    result = subprocess.run(args, cwd=cwd, env=env, timeout=timeout,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, check=False)
    if result.returncode:
        raise RuntimeError("command failed: " + repr(args) +
                           "\nexit=" + str(result.returncode) + "\n" +
                           result.stdout[-12000:])
    if result.stdout.strip():
        print(result.stdout[-12000:])


def strip_platform_command(path):
    data = bytearray(path.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated original object")
    magic, cpu, _, kind, count, size = struct.unpack_from("<6I", data)
    if (magic, cpu, kind) != (0xfeedfacf, 0x0100000c, 1):
        raise ValueError("not a real arm64 compiler object")
    cursor, end = 32, 32 + size
    if end > len(data):
        raise ValueError("load commands outside object")
    for _ in range(count):
        if cursor + 8 > end:
            break
        cmd, length = struct.unpack_from("<II", data, cursor)
        if length < 8 or length % 8 or cursor + length > end:
            break
        if cmd in (0x32, 0x24):
            data[cursor:end - length] = data[cursor + length:end]
            data[end - length:end] = bytes(length)
            struct.pack_into("<II", data, 16, count - 1, size - length)
            dest = path.with_suffix(".link.o")
            dest.write_bytes(data)
            return dest
        cursor += length
    raise ValueError("unsupported clang object platform metadata")


def link(linker, output, obj, options, dependencies, build):
    cmd = [linker, "-arch", "arm64", "-platform_version", "macos",
           "13.0", "13.0", "-fatal_warnings", "-fixup_chains"] + options + [
           "-o", output, obj] + [build / (d + ".tbd") for d in dependencies]
    try:
        run(cmd, 30)
    except RuntimeError as exc:
        if "does not support linking for platform macOS" not in str(exc):
            raise
        cmd[cmd.index(obj)] = strip_platform_command(obj)
        run(cmd, 30)


def validate_linked(path, kind, expected_dependencies):
    content = path.read_bytes()
    if len(content) < 32 or len(content) > 4 * 1024 * 1024:
        raise ValueError("invalid linked image length")
    magic, cpu, _, actual_kind, count, commands_size = struct.unpack_from(
        "<6I", content)
    if (magic, cpu, actual_kind) != (0xfeedfacf, 0x0100000c, kind):
        raise ValueError("not linked arm64 macho kind=" + str(kind))
    end, cursor, imports, exports, fixups = 32 + commands_size, 32, [], 0, 0
    if end > len(content) or count > 128:
        raise ValueError("load commands exceed image")
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("truncated command")
        command, size = struct.unpack_from("<II", content, cursor)
        if size < 8 or size % 8 or cursor + size > end:
            raise ValueError("invalid load command")
        if command == 0xc:
            offset = struct.unpack_from("<I", content, cursor + 8)[0]
            if offset < 24 or offset >= size:
                raise ValueError("invalid dependency string offset")
            raw = content[cursor + offset: cursor + size].split(b"\0", 1)
            if len(raw) != 2:
                raise ValueError("unterminated import")
            imports.append(raw[0].decode("ascii"))
        if command in (0x80000033, 0x80000022, 0x22):
            exports += 1
        if command in (0x80000034, 0x80000022, 0x22):
            fixups += 1
        cursor += size
    if cursor != end or imports != ["@rpath/" + d + ".dylib"
                                    for d in expected_dependencies]:
        raise ValueError("unexpected import ordinal/names: " + repr(imports))
    if kind == 6 and (exports == 0 or fixups == 0):
        raise ValueError("linked dylib lacks export/fixup metadata")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="build/phase24a")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--ld", default="ld64.lld")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    build = pathlib.Path(args.output_dir).resolve()
    build.mkdir(parents=True, exist_ok=True)
    for name in ORDER:
        source = build / (name + ".c")
        obj = build / (name + ".o")
        source.write_text(SOURCES[name], encoding="ascii")
        run([args.clang, "--target=arm64-apple-darwin",
             "-ffreestanding", "-fPIC", "-fno-builtin", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-Wall", "-Wextra", "-Werror", "-O2", "-c", source, "-o", obj], 30)
        if name in EXPORTS:
            (build / (name + ".tbd")).write_text(
                "--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n"
                "install-name: '@rpath/" + name + ".dylib'\n"
                "current-version: 1.0.0\ncompatibility-version: 1.0.0\n"
                "exports:\n  - archs: [ arm64 ]\n    symbols: [ " +
                ", ".join(EXPORTS[name]) + " ]\n...\n", encoding="ascii")
            output = build / (name + ".dylib")
            opts = ["-dylib", "-install_name", "@rpath/" + name + ".dylib",
                    "-rpath", "@loader_path/."]
            kind = 6
        else:
            output = build / name
            opts = ["-e", "_main", "-rpath", "@executable_path/../lib"]
            kind = 2
        link(args.ld, output, obj, opts, DEPS[name], build)
        validate_linked(output, kind, DEPS[name])
        print("linked ARM64 Darwin", name, "filetype", kind,
              "dependency edges", len(DEPS[name]))
    cmake = build / "cmake"
    flags = "-fsanitize=address,undefined -fno-omit-frame-pointer" if args.sanitize else ""
    run(["cmake", "-S", root, "-B", cmake, "-DIOS27_BUILD_TESTS=OFF",
         "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_C_FLAGS=" + flags], 120)
    run(["cmake", "--build", cmake, "--parallel", "2"], 300)
    host = build / "phase24a-integration"
    cflags = ["-std=c11", "-D_GNU_SOURCE", "-O1", "-g", "-pthread",
              "-Wall", "-Wextra", "-Werror", "-Wpedantic"]
    if args.sanitize:
        cflags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    run([args.cc] + cflags + ["-I", root / "include",
                              root / "src/phase24a_registry.c",
                              root / "tests/phase24a-integration.c",
                              cmake / "libios27_core.a", "-o", host], 120)
    env = os.environ.copy()
    if args.sanitize:
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1"
    run([host] + [build / (n + ".dylib") for n in HOST_ARGS], 180, env=env)
    print("verification complete: compiler-produced six dylibs and two "
          "executables; x86 models, arm64 executes trusted mapped functions")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase24a verification failed: " + str(exc))
