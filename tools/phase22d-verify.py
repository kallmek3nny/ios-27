#!/usr/bin/env python3
"""Independent, bounded phase 22d compilation, runtime and Mach-O checks."""
from __future__ import annotations
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SANITIZE = os.getenv("PHASE22D_SANITIZE") == "1"

def run(args: list[str], timeout: int = 120) -> None:
    print("+", " ".join(map(str, args)), flush=True)
    completed = subprocess.run(
        [str(a) for a in args], cwd=ROOT, timeout=timeout,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        check=False, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                          "UBSAN_OPTIONS": "halt_on_error=1"}
    )
    if completed.stdout:
        print(completed.stdout[-12000:], flush=True)
    if completed.returncode:
        raise RuntimeError(f"command exited {completed.returncode}: {args}")

def tool(name: str) -> str:
    result = shutil.which(name)
    if not result:
        raise RuntimeError(f"required compiler missing: {name}")
    return result

def check_macho(data: bytes) -> None:
    if len(data) < 32:
        raise AssertionError("truncated Mach-O")
    magic, cpu, subtype, filetype, commands, nbytes, flags, reserved = (
        struct.unpack_from("<8I", data)
    )
    if magic != 0xFEEDFACF or cpu != 0x0100000C or subtype & 0xFF not in (0, 2) or filetype != 1:
        raise AssertionError("not an arm64 Darwin MH_OBJECT")
    if not commands or nbytes > len(data) - 32 or reserved:
        raise AssertionError("invalid load command table")
    offset = 32
    for _ in range(commands):
        if offset + 8 > len(data):
            raise AssertionError("truncated load command")
        _, size = struct.unpack_from("<II", data, offset)
        if size < 8 or size % 8 or offset + size > 32 + nbytes:
            raise AssertionError("invalid load command size")
        offset += size
    if offset != 32 + nbytes:
        raise AssertionError("trailing load commands")
    for text in (
        b"__objc_classlist", b"__objc_catlist", b"__objc_selrefs",
        b"__objc_methname", b"_objc_msgSend", b"_objc_msgSendSuper2",
        b"Phase22dRoot", b"Phase22dChild"
    ):
        if text not in data:
            raise AssertionError(f"missing compiler evidence: {text!r}")

def main() -> None:
    clang = tool("clang")
    gcc = tool("gcc") if not SANITIZE else None
    flags = ["-std=c11", "-O1" if SANITIZE else "-O2",
             "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             "-pthread", "-Iinclude"]
    if SANITIZE:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="phase22d-") as temporary:
        temp = pathlib.Path(temporary)
        fixture = ROOT / "tests/phase22d-native-fixture.m"
        for opt in ("0", "2"):
            native = temp / f"objc-arm64-O{opt}.o"
            run([clang, "-target", "arm64-apple-darwin",
                 "-fobjc-runtime=macosx-11.0",
                 f"-O{opt}", "-Wall", "-Wextra", "-Werror", "-c",
                 str(fixture), "-o", str(native)], 90)
            data = native.read_bytes()
            check_macho(data)
            for compiler in [clang] + ([gcc] if gcc else []):
                tag = pathlib.Path(compiler).name
                app = temp / f"stress-{tag}-O{opt}"
                run([compiler, *flags, "src/phase22d_runtime.c",
                     "tests/phase22d-stress.c", "-o", str(app)], 120)
                run([str(app)], 180)
                smoke = temp / f"metadata-{tag}-O{opt}"
                run([compiler, *flags, "src/phase22d_runtime.c",
                     "src/phase22d_metadata.c", "src/objc_metadata.c",
                     "tests/phase22d-metadata-smoke.c", "-o", str(smoke)], 120)
                run([str(smoke), str(native)], 180)
    print("phase22d standalone verification passed", flush=True)

if __name__ == "__main__":
    main()
