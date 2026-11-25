#!/usr/bin/env python3
"""Build original C constructor fixtures with Clang and LLVM's real Mach-O linker."""

import argparse
from hashlib import sha256
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
from struct import unpack_from
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
spec = spec_from_file_location("initializer_link_helpers", ROOT / "tools/phase19b-build.py")
helpers = module_from_spec(spec)
spec.loader.exec_module(helpers)


def check(path, kind, expected):
    data = path.read_bytes()
    if len(data) < 32 or len(data) > 1024 * 1024:
        raise ValueError("unexpected fixture size")
    magic, cpu, sub, actual, count, size = unpack_from("<6I", data)
    if (magic, cpu, sub, actual) != (0xFEEDFACF, 0x0100000C, 0, kind) or size > len(data) - 32:
        raise ValueError("invalid ARM64 compiler-linked fixture")
    cursor, initializers, legacy = 32, 0, 0
    for _ in range(count):
        command, length = unpack_from("<II", data, cursor)
        if length < 8 or length % 8 or cursor + length > 32 + size:
            raise ValueError("invalid fixture command")
        if command == 0x19:
            sections, = unpack_from("<I", data, cursor + 64)
            if length != 72 + sections * 80:
                raise ValueError("invalid fixture sections")
            for i in range(sections):
                section = cursor + 72 + 80 * i
                flags, = unpack_from("<I", data, section + 64)
                section_size, = unpack_from("<Q", data, section + 40)
                if flags & 255 == 0x16:
                    if data[section:section + 16].rstrip(b"\0") != b"__init_offsets" or section_size % 4:
                        raise ValueError("invalid modern initializer table")
                    initializers += section_size // 4
                legacy += flags & 255 == 9
        cursor += length
    if cursor != 32 + size or initializers != expected or legacy:
        raise ValueError(f"linker did not emit {expected} modern initializer offsets")
    return {"sha256": sha256(data).hexdigest(), "initializers": initializers, "filetype": actual}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--linker", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name in ("provider", "main"):
        helpers.run([args.clang, "--target=arm64-apple-darwin", "-ffreestanding", "-fno-builtin",
                     "-fno-stack-protector", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                     "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O2", "-fPIC", "-c",
                     str(ROOT / f"tests/fixtures/initializer-{name}.c"), "-o", str(output / f"{name}.o")])
    provider = output / "Provider.dylib"
    entry = output / "Main"
    helpers.link(args.linker, ["-dylib", "-fixup_chains", "-install_name", "/Fixture/Provider.dylib",
                               "-o", str(provider)], output / "provider.o")
    helpers.link(args.linker, ["-fixup_chains", "-e", "_main", "-o", str(entry)],
                 output / "main.o", provider)
    report = {"scope": "original SDK-free compiler-linked C initialization",
              "provider": check(provider, 6, 2), "executable": check(entry, 2, 1)}
    (output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("linked original ARM64 provider (2 constructors) and main (1 constructor)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(f"initializer fixture build failed: {error}")
