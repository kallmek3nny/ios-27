#!/usr/bin/env python3
"""Phase 24e: compile and execute trusted linked Darwin applications.

No Apple SDK, Apple dyld, or synthetic native execution. Every child
process has a hard timeout. MH_EXECUTE startup is reported unsupported unless
the merged runtime genuinely supports a validated entry bridge.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/phase24e-fixtures.c"
FIXTURES = ("base", "math", "logic", "state", "pipeline", "ctor")
APPS = ("calc", "state", "pipeline")
EXPORTS = {
    "base": ("_phase24e_base_value", "_phase24e_base_inc"),
    "math": ("_phase24e_math_eval",),
    "logic": ("_phase24e_logic_eval",),
    "state": ("_phase24e_state_counter", "_phase24e_state_next"),
    "pipeline": ("_phase24e_pipeline_eval",),
    "ctor": ("_phase24e_ctor_state", "_phase24e_ctor_value"),
}
DEPS = {
    "base": (),
    "math": ("base",),
    "logic": ("math",),
    "state": ("base",),
    "pipeline": ("logic", "state"),
    "ctor": (),
}
APP_DEPS = {
    "calc": ("base", "math", "logic"),
    "state": ("state", "base"),
    "pipeline": ("pipeline", "state"),
}
MAGIC = 0xFEEDFACF
CPU = 0x0100000C


def command(args, seconds=30, env=None):
    result = subprocess.run([str(x) for x in args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            timeout=seconds, check=False, env=env)
    if result.returncode:
        raise RuntimeError("command failed ({}): {}\n{}".format(
            result.returncode, " ".join(map(str, args)), result.stdout))
    return result.stdout


def strip_object_platform(path):
    data = bytearray(path.read_bytes())
    if len(data) < 32:
        raise ValueError("truncated Darwin object")
    magic, cpu, _, kind, count, size = struct.unpack_from("<6I", data)
    if (magic, cpu, kind) != (MAGIC, CPU, 1) or size > len(data) - 32:
        raise ValueError("not a compiler-produced ARM64 MH_OBJECT")
    cursor, end = 32, 32 + size
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("bad object load commands")
        op, length = struct.unpack_from("<II", data, cursor)
        if length < 8 or length % 8 or cursor + length > end:
            raise ValueError("bad object load command size")
        if op in (0x24, 0x32):
            data[cursor:end - length] = data[cursor + length:end]
            data[end - length:end] = bytes(length)
            struct.pack_into("<II", data, 16, count - 1, size - length)
            patched = path.with_name(path.stem + ".link.o")
            patched.write_bytes(data)
            return patched
        cursor += length
    raise ValueError("compiler object lacks platform command")


def linked_file(path, expected_type):
    data = path.read_bytes()
    if len(data) < 32 or len(data) > 4 * 1024 * 1024:
        raise ValueError("invalid linked Mach-O byte length")
    magic, cpu, _, kind, commands, size = struct.unpack_from("<6I", data)
    if (magic, cpu, kind) != (MAGIC, CPU, expected_type) or commands > 64:
        raise ValueError("wrong compiler-linked Mach-O type")
    if size > len(data) - 32:
        raise ValueError("truncated linked Mach-O")
    p, found = 32, []
    for _ in range(commands):
        if p + 8 > 32 + size:
            raise ValueError("truncated linked load command")
        op, n = struct.unpack_from("<II", data, p)
        if n < 8 or n % 8 or p + n > 32 + size:
            raise ValueError("invalid linked load command")
        found.append(op)
        p += n
    if p != 32 + size or 0x80000033 not in found:
        raise ValueError("linked image lacks export trie")
    if expected_type == 2 and 0x80000028 not in found:
        raise ValueError("compiler-linked app lacks LC_MAIN")
    return found


def stub(path, name, exports):
    path.write_text(
        "--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n"
        "install-name: '" + name + "'\ncurrent-version: 1.0.0\n"
        "compatibility-version: 1.0.0\nexports:\n"
        "  - archs: [ arm64 ]\n    symbols: [ " +
        ", ".join(exports) + " ]\n...\n", encoding="ascii")


def linker_call(ld, options, obj, deps):
    command_prefix = [ld, "-arch", "arm64", "-platform_version",
                      "macos", "13.0", "13.0", "-fatal_warnings"]
    try:
        command(command_prefix + options + [obj] + deps)
    except RuntimeError as err:
        if "does not support linking for platform macOS" not in str(err):
            raise
        command(command_prefix + options +
                [strip_object_platform(Path(obj))] + deps)


def build_fixtures(out, clang, ld):
    stubs, outputs = {}, {}
    for name in FIXTURES:
        obj = out / (name + ".o")
        lib = out / (name + ".dylib")
        command([clang, "--target=arm64-apple-darwin", "-std=c11",
                 "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                 "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                 "-fPIC", "-O2", "-D", "PHASE24E_" + name.upper(),
                 "-c", SOURCE, "-o", obj])
        install_name = "@rpath/" + name.title() + ".dylib"
        linker_call(ld, ["-dylib", "-fixup_chains",
                         "-install_name", install_name,
                         "-rpath", "@loader_path/.", "-o", lib], obj,
                    [stubs[x] for x in DEPS[name]])
        commands = linked_file(lib, 6)
        if DEPS[name] and 0x0c not in commands:
            raise ValueError("compiled dylib missing LC_LOAD_DYLIB")
        tbd = out / (name + ".tbd")
        stub(tbd, install_name, EXPORTS[name])
        stubs[name] = tbd
        outputs[name] = lib
    for name in APPS:
        obj = out / ("app-" + name + ".o")
        exe = out / ("app-" + name)
        command([clang, "--target=arm64-apple-darwin", "-std=c11",
                 "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                 "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                 "-fPIC", "-O2", "-D", "PHASE24E_APP_" + name.upper(),
                 "-c", SOURCE, "-o", obj])
        linker_call(ld, ["-e", "_main", "-fixup_chains",
                         "-rpath", "@executable_path/Frameworks",
                         "-o", exe], obj,
                    [stubs[x] for x in APP_DEPS[name]])
        linked_file(exe, 2)
        outputs["app-" + name] = exe
    return outputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", default="build/libios27_core.a")
    parser.add_argument("--output-dir", default="build/phase24e")
    parser.add_argument("--runs", type=int, default=100)
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.runs <= 200:
        raise ValueError("--runs must be 1..200")
    t0 = time.monotonic()
    clang = shutil.which("clang")
    ld = next((shutil.which(name) for name in
               ("ld64.lld", "ld64.lld-20", "ld64.lld-19",
                "ld64.lld-18", "ld64.lld-17", "ld64.lld-16")
               if shutil.which(name)), None)
    cc = shutil.which("clang" if args.sanitizers else os.environ.get("CC", "cc"))
    if not clang or not ld or not cc:
        raise RuntimeError("clang, ld64.lld and a host C compiler required")
    core = Path(args.core).resolve()
    if not core.is_file():
        raise RuntimeError("missing unchanged ios27_core archive: " + str(core))
    out = Path(args.output_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixtures = build_fixtures(out, clang, ld)
    test = out / "phase24e-check"
    flags = ["-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
             "-Wpedantic", "-O1", "-g", "-I" + str(ROOT / "include")]
    if args.sanitizers:
        flags += ["-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
    command([cc, *flags, ROOT / "src/phase24e_application.c",
             ROOT / "tests/phase24e-check.c", core, "-o", test], 120)
    env = os.environ.copy()
    if args.sanitizers:
        env.update(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    order = ["base", "math", "logic", "state", "pipeline", "ctor",
             "app-calc", "app-state", "app-pipeline"]
    output = command([test, str(args.runs)] +
                     [fixtures[x] for x in order], 180, env)
    records = [json.loads(line) for line in output.splitlines()
               if line.startswith("{")]
    if len(records) != 6:
        raise RuntimeError("missing application/fuzz/unsupported-ABI test reports: " + output)
    result = {"fixtures": {"dylibs": len(FIXTURES),
                           "applications": len(APPS)},
              "host": os.uname().machine, "sanitizers": args.sanitizers,
              "cases": records, "seconds": round(time.monotonic() - t0, 3)}
    (out / "phase24e-report.json").write_text(
        json.dumps(result, sort_keys=True, indent=2) + "\n",
        encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired,
            struct.error) as exc:
        sys.exit("phase24e: " + str(exc))
