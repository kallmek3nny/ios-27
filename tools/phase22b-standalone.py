#!/usr/bin/env python3
"""Independent phase22b compiled native + SDK-free Darwin checks.
Every compile and execution has a hard wall-clock timeout.
"""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CC = os.environ.get("CC", "clang")
SAN = os.environ.get("PHASE22B_SANITIZE") == "1"


def run(argv, timeout=120, **kw):
    print("+", " ".join(map(str, argv)), flush=True)
    subprocess.run(list(map(str, argv)), check=True, timeout=timeout, **kw)


def main():
    if not shutil.which(CC):
        raise RuntimeError(f"compiler missing: {CC}")
    with tempfile.TemporaryDirectory(prefix="ios27-phase22b-") as tmp:
        exe = Path(tmp) / "integration"
        opts = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-pthread", "-O1" if SAN else "-O2"]
        if SAN:
            opts += ["-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
        run([CC, *opts, "-I" + str(ROOT / "include"),
             ROOT / "src/phase22b_syscalls.c", ROOT / "src/libsystem20e_services.c",
             ROOT / "tests/phase22b-integration.c", "-o", exe], timeout=120)
        env = os.environ.copy()
        if SAN:
            env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run([exe], timeout=120, env=env)
        if shutil.which("clang"):
            obj = Path(tmp) / "darwin.o"
            run(["clang", "--target=arm64-apple-darwin", "-ffreestanding",
                 "-fno-stack-protector", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wpedantic", "-I" + str(ROOT / "include"), "-c",
                 ROOT / "tests/phase22b-darwin-fixture.c", "-o", obj], timeout=120)
            data = obj.read_bytes()
            assert data[:4] == b"\xcf\xfa\xed\xfe", "not a little-endian Mach-O object"
            assert data[4:8] == b"\x0c\x00\x00\x01", "not ARM64"
            assert b"_ios27_22b_dispatch" in data, "missing native dispatcher relocation"
            print("darwin fixture: real clang-produced arm64 MH_OBJECT with dispatcher relocation")
        print("verified native architecture:", platform.machine(), "asan+ubsan:" , SAN)


if __name__ == "__main__":
    main()