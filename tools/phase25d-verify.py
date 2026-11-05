#!/usr/bin/env python3
"""Strict, bounded phase25d runtime and ARM64 Darwin provenance verifier."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
CLANG = shutil.which("clang")
CXX = shutil.which("clang++") or shutil.which("g++")
LINKER = shutil.which("ld64.lld")
if not CLANG or not CXX:
    sys.exit("phase25d: clang and a C++17 compiler required")

def run(argv, limit=120, require=True):
    result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                            timeout=limit, check=False)
    if require and result.returncode:
        raise RuntimeError("command failed (rc=%d): %s\n%s" %
                           (result.returncode, " ".join(map(str, argv)),
                            result.stderr[-3000:]))
    return result

with tempfile.TemporaryDirectory(prefix="phase25d-") as directory:
    out = Path(directory)
    sanitized = os.getenv("PHASE25D_SANITIZE", "0") == "1"
    cflags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
              "-pthread", "-Iinclude", "-g"]
    if sanitized:
        cflags += ["-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    else:
        cflags += ["-O2"]
    binary = out / "phase25d-runtime"
    run([CXX, *cflags, "src/phase25d_lifecycle.cpp",
         "tests/phase25d-runtime.cpp", "-o", str(binary)], 120)
    result = run([str(binary)], 180)
    print(result.stdout, end="")

    objc_flags = ["-target", "arm64-apple-macos13",
                  "-fobjc-runtime=macosx-13.0", "-fno-objc-arc",
                  "-Wall", "-Wextra", "-Werror", "-c"]
    libs, apps = [], []
    for index in range(6):
        src = "tests/phase25d-lib%d.m" % index
        obj = out / ("lib%d.o" % index)
        run([CLANG, *objc_flags, src, "-o", str(obj)], 30)
        raw = obj.read_bytes()
        if raw[:4] != bytes.fromhex("cffaedfe") or raw[4:8] != bytes.fromhex("0c000001"):
            raise RuntimeError("not a compiler-produced arm64 Darwin MH_OBJECT: " + src)
        if b"__objc_classlist" not in raw or b"__objc_methname" not in raw:
            raise RuntimeError("missing ObjC2 class/method metadata in " + src)
        libs.append(obj)
    for index in range(3):
        src = "tests/phase25d-app%d.m" % index
        obj = out / ("app%d.o" % index)
        run([CLANG, *objc_flags, src, "-o", str(obj)], 30)
        raw = obj.read_bytes()
        if raw[:4] != bytes.fromhex("cffaedfe") or b"_objc_msgSend" not in raw:
            raise RuntimeError("missing ARM64 compiler-generated objc_msgSend in " + src)
        apps.append(obj)
    print("six compiler-generated Darwin ObjC2 library MH_OBJECTs and three app MH_OBJECTs: verified")

    # Link attempt is evidence, never conceal an unsupported cross-linker.
    linked_libraries = 0
    linked_apps = 0
    link_error = ""
    if LINKER:
        for index, obj in enumerate(libs):
            dylib = out / ("lib%d.dylib" % index)
            proc = run([LINKER, "-arch", "arm64", "-platform_version",
                        "macos", "13.0", "13.0", "-dylib", "-undefined",
                        "dynamic_lookup", "-o", str(dylib), str(obj)], 30, False)
            if proc.returncode:
                link_error = proc.stderr.strip().splitlines()[0] if proc.stderr.strip() else "ld64 failure"
                break
            if dylib.read_bytes()[:4] != bytes.fromhex("cffaedfe"):
                raise RuntimeError("linked library is not Mach-O: " + str(dylib))
            linked_libraries += 1
        if linked_libraries == 6:
            for index, obj in enumerate(apps):
                exe = out / ("app%d" % index)
                proc = run([LINKER, "-arch", "arm64", "-platform_version",
                            "macos", "13.0", "13.0", "-execute",
                            "-undefined", "dynamic_lookup", "-e", "_main",
                            "-o", str(exe), str(obj)], 30, False)
                if proc.returncode:
                    link_error = proc.stderr.strip().splitlines()[0] if proc.stderr.strip() else "app link failure"
                    break
                linked_apps += 1
    else:
        link_error = "ld64.lld not installed"
    print("linked Darwin images: %d dylibs, %d programs" % (linked_libraries, linked_apps))
    if link_error:
        print("linker limitation: " + link_error)
    if os.getenv("PHASE25D_REQUIRE_LINK", "0") == "1" and (linked_libraries != 6 or linked_apps != 3):
        sys.exit("required Darwin linker not available or linking incomplete")
    print("phase25d PASS on %s (sanitizers=%s); actual native Darwin lifecycle callbacks: 0 verified" %
          (platform.machine(), sanitized))
