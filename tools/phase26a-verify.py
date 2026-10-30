#!/usr/bin/env python3
"""Phase26a standalone: native-host stress plus compiler-linked ARM64 Darwin graphs.

All subprocesses have explicit hard timeouts. No unmerged dependencies, Apple
SDK, root, bootloader, or host-library pass-through. The x86_64 run inspects
Mach-O and exercises the host model; only ARM64 Linux executes loaded images.
"""
import os
from pathlib import Path
import runpy
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0xFEEDFACF
ARM64 = 0x0100000C
ABI = ("malloc", "calloc", "realloc", "free", "memcpy", "memmove",
       "memset", "memcmp", "strlen", "strnlen", "strcmp", "strncmp",
       "strcpy", "strncpy", "strdup", "getenv", "setenv", "unsetenv")


def run(args, limit=120):
    args = [str(x) for x in args]
    print("+", " ".join(args), flush=True)
    done = subprocess.run(args, cwd=ROOT, check=False, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=limit, env={**os.environ,
                          "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                          "UBSAN_OPTIONS": "halt_on_error=1"})
    if done.stdout:
        print(done.stdout[-9000:], flush=True)
    if done.returncode:
        raise RuntimeError(f"exit {done.returncode}: {args}")
    return done.stdout


def stub(path, install, symbols):
    path.write_text("--- !tapi-tbd-v3\n"
                    "archs: [ arm64 ]\n"
                    "platform: macosx\n"
                    f"install-name: '{install}'\n"
                    "current-version: 1.0.0\n"
                    "compatibility-version: 1.0.0\n"
                    "exports:\n"
                    "  - archs: [ arm64 ]\n"
                    "    symbols: [ " + ", ".join(symbols) + " ]\n"
                    "...\n", encoding="ascii")


def check(path, kind, dependency=False):
    data = path.read_bytes()
    if len(data) < 512 or len(data) > 4 * 1024 * 1024:
        raise ValueError(f"size invalid: {path}")
    header = struct.unpack_from("<8I", data)
    if header[0] != MAGIC or header[1] != ARM64 or header[3] != kind:
        raise ValueError(f"not linked Darwin arm64 Mach-O: {path}")
    count, cmds_size = header[4], header[5]
    if not count or count > 80 or cmds_size > len(data) - 32:
        raise ValueError("malformed load command count/size")
    commands = []
    pos = 32
    for _ in range(count):
        if pos + 8 > len(data):
            raise ValueError("truncated load command")
        op, size = struct.unpack_from("<II", data, pos)
        if size < 8 or size % 8 or pos + size > 32 + cmds_size:
            raise ValueError("invalid load command bounds")
        commands.append(op)
        pos += size
    if pos != 32 + cmds_size:
        raise ValueError("unconsumed load commands")
    if dependency and 0xC not in commands:
        raise ValueError("expected real LC_LOAD_DYLIB")
    if kind == 2 and 0x80000028 not in commands:
        raise ValueError("expected compiler-linked LC_MAIN")
    if kind == 6 and 0x80000033 not in commands:
        raise ValueError("expected exports trie")
    return data


def main():
    clang = shutil.which("clang")
    ld = shutil.which("ld64.lld") or shutil.which("ld64.lld-18")
    cc = os.environ.get("CC", "cc")
    if not clang or not ld or not shutil.which(cc):
        raise RuntimeError("clang, ld64.lld, and native C compiler required")
    helper = runpy.run_path(str(ROOT / "tools/phase17b-build.py"))
    link = helper["link"]  # Verified main-only helper, each link has 30s timeout.
    with tempfile.TemporaryDirectory(prefix="phase26a-") as td:
        out = Path(td)
        host_flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                      "-Werror", "-fno-builtin", "-Iinclude"]
        host = out / "phase26a-host"
        run([cc, *host_flags, "src/phase26a_libc.c",
             "tests/phase26a-host.c", "-o", host], 120)
        run([host], 180)
        if os.getenv("PHASE26A_SANITIZE") == "1":
            sanitized = out / "phase26a-asan"
            run([clang, *host_flags, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined",
                 "src/phase26a_libc.c", "tests/phase26a-host.c",
                 "-o", sanitized], 120)
            run([sanitized], 180)
        darwin = ["--target=arm64-apple-darwin", "-ffreestanding",
                  "-fno-builtin", "-fno-stack-protector",
                  "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                  "-fPIC", "-Wall", "-Wextra", "-Werror", "-Iinclude"]
        provider_obj = out / "provider.o"
        provider = out / "phase26a-c.dylib"
        run([clang, *darwin, "-O0", "-DPHASE26A_DARWIN_EXPORT",
             "-c", "src/phase26a_libc.c", "-o", provider_obj], 120)
        link(ld, ["-dylib", "-install_name", provider.name,
                  "-o", str(provider)], provider_obj)
        check(provider, 6)
        provider_stub = out / "phase26a-c.tbd"
        stub(provider_stub, provider.name, ["_" + symbol for symbol in ABI])
        clients = []
        for case in range(5):
            obj = out / f"client{case}.o"
            lib = out / f"client{case}.dylib"
            run([clang, *darwin, "-O2", f"-DPHASE26A_CASE={case}",
                 "-c", "tests/phase26a-client.c", "-o", obj], 120)
            link(ld, ["-dylib", "-install_name", "phase26a-client.dylib",
                      "-o", str(lib)], obj, provider_stub)
            data = check(lib, 6, True)
            if b"phase26a-c.dylib" not in data:
                raise ValueError("missing libc dependency")
            clients.append(lib)
        executables = []
        for case in range(2):
            client_stub = out / f"phase26a-client{case}.tbd"
            stub(client_stub, "phase26a-client.dylib",
                 [f"_phase26a_case_{case}"])
            obj = out / f"exe{case}.o"
            exe = out / f"exe{case}"
            run([clang, *darwin, "-O2", f"-DPHASE26A_CASE={case}",
                 "-c", "tests/phase26a-executable.c", "-o", obj], 120)
            link(ld, ["-e", "_main", "-o", str(exe)],
                 obj, client_stub)
            check(exe, 2, True)
            executables.append(exe)
        print("phase26a: 6 Clang/LLD-linked ARM64 Darwin dylibs and 2 executables", flush=True)
        build = out / "core"
        run(["cmake", "-S", str(ROOT), "-B", str(build),
             "-DIOS27_BUILD_TESTS=OFF", "-DCMAKE_BUILD_TYPE=Debug"], 120)
        run(["cmake", "--build", str(build), "--parallel", "2"], 300)
        native = out / "phase26a-native"
        run([cc, *host_flags, "tests/phase26a-native.c",
             build / "libios27_core.a", "-o", native], 120)
        for case in range(5):
            args = [native, provider, clients[case]]
            if case < 2:
                args.append(executables[case])
            args.append(f"_phase26a_case_{case}")
            run(args, 180)
        print("phase26a: standalone verification passed", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        sys.exit(f"phase26a failed: {exc}")
