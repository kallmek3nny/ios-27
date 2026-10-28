#!/usr/bin/env python3
"""Reproducible SDK-free Clang ARM64 Mach-O ObjC metadata integration checks.
Run at the repository root: timeout 180s python3 tools/phase20a-standalone.py
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CLANG = os.environ.get("CLANG", "clang")
SAN = os.environ.get("PHASE20A_SANITIZE", "0") == "1"


def run(argv, timeout=30):
    subprocess.run([str(x) for x in argv], check=True, timeout=timeout)


with tempfile.TemporaryDirectory(prefix="ios27-phase20a-") as temp:
    build = Path(temp)
    sources = ["graph", "external", "category"]
    for optimize in ("0", "2"):
        objects = []
        for name in sources:
            obj = build / f"{name}-{optimize}.o"
            run([CLANG, "--target=arm64-apple-macos11", "-fobjc-runtime=macosx-11.0",
                 "-Werror", "-O" + optimize, "-c",
                 ROOT / "tests" / f"phase20a-{name}.m", "-o", obj])
            objects.append(obj)
        exe = build / ("verify-" + optimize)
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-O1", "-g", "-I", ROOT / "include"]
        if SAN:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        run([CLANG, *flags, ROOT / "src/objc20a_reader.c",
             ROOT / "src/objc20a_registry.c", ROOT / "tests/phase20a-validate.c",
             "-o", exe], timeout=60)
        run([exe, *objects], timeout=60)
print("phase20a: all independent O0/O2 fixture suites passed")
