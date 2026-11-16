#!/usr/bin/env python3
"""Build the original stateless platform-memory dylib and a real Darwin client."""

import argparse
from hashlib import sha256
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
spec = spec_from_file_location("platform_memory_stage", ROOT / "tools/apple-fixup-stage.py")
stage = module_from_spec(spec)
spec.loader.exec_module(stage)
audit = stage.audit


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError(f"compiler/linker failed: {result.stdout}{result.stderr}")


def imports(image):
    data = image.data
    cursor, result = 32, set()
    for _ in range(audit.unpack("I", data, 16)[0]):
        command, size = audit.unpack("II", data, cursor)
        if command == 2:
            start, count, strings, length = audit.unpack("4I", data, cursor + 8, cursor + size)
            audit.interval(start, count * 16, len(data), "nlist table")
            end = audit.interval(strings, length, len(data), "nlist strings")
            for i in range(count):
                name, kind, _, _, value = audit.unpack("IBBHQ", data, start + 16 * i)
                if not kind & 0xE0 and kind & 0xF == 1 and value == 0:
                    result.add(audit.cstring(data, strings + name, end))
        cursor += size
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--linker", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    common = [args.clang, "--target=arm64-apple-ios14.0-simulator", "-ffreestanding", "-fno-builtin",
              "-fno-stack-protector", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
              "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-I", str(ROOT / "include")]
    run([*common, "-DIOS27_PLATFORM_EXPORT_DARWIN=1", "-c", str(ROOT / "src/platform_memory.c"),
         "-o", str(out / "platform-memory.o")])
    run([*common, "-c", str(ROOT / "tests/fixtures/platform-memory-main.c"),
         "-o", str(out / "platform-memory-main.o")])
    linker = [args.linker]
    if Path(args.linker).name in ("ld.lld", "ld.lld.exe"):
        linker += ["-flavor", "darwin"]
    linker += ["-arch", "arm64", "-platform_version", "ios-simulator", "14.0", "14.0",
               "-fixup_chains", "-fatal_warnings"]
    library = out / "PlatformMemory.dylib"
    client = out / "PlatformMemoryClient"
    run([*linker, "-dylib", "-install_name", stage.PLATFORM_MEMORY_INSTALL_NAME,
         "-o", str(library), str(out / "platform-memory.o")])
    run([*linker, "-e", "_main", "-o", str(client), str(out / "platform-memory-main.o"), str(library)])
    provider_image = audit.Image(library.read_bytes())
    exports = stage.validate_platform_memory(provider_image)
    audit.check(not imports(provider_image), "platform library has unresolved runtime imports")
    client_image = audit.Image(client.read_bytes())
    audit.check(imports(client_image) == stage.PLATFORM_MEMORY_SYMBOLS, "client must import all twelve routines")
    sources = ("src/platform_memory.c", "include/ios27/platform_memory.h",
               "tests/fixtures/platform-memory-main.c", "tools/platform-memory-build.py")
    report = {
        "scope": "original compiler-linked ARM64 Simulator platform-memory replacement",
        "install_name": stage.PLATFORM_MEMORY_INSTALL_NAME,
        "provider_sha256": sha256(provider_image.data).hexdigest(),
        "client_sha256": sha256(client_image.data).hexdigest(),
        "exports": dict(sorted(exports.items())),
        "provider_runtime_imports": [],
        "source_sha256": {path: sha256((ROOT / path).read_bytes()).hexdigest() for path in sources},
    }
    (out / "PlatformMemory.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("linked original platform-memory dylib: 12 exports, no runtime imports; client imports all 12")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit(f"platform-memory build failed: {error}")
