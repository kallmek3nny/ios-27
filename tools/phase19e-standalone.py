#!/usr/bin/env python3
"""Independent original-source ObjC2 inspection + native runtime tests.

No SDK, no Apple binaries, no emulation, no dependency on project CMake.
Clang builds a genuine ARM64 Darwin MH_OBJECT containing ObjC2 metadata.
All subprocess calls are bounded. AArch64 hosts also execute verified,
relocation-free compiler-generated instance method instructions in W^X pages.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMPONENTS = [ROOT / "src" / name for name in (
    "objc_runtime.c", "objc_metadata.c", "objc_bridge.c", "libsystem_user.c")]


def run(args, timeout=25, env=None):
    result = subprocess.run(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            timeout=timeout, check=False, env=env)
    if result.returncode:
        raise RuntimeError(f"{args[0]} returned {result.returncode}:\n{result.stdout}")
    print(result.stdout.strip())


def main():
    clang = next((shutil.which(exe) for exe in
                  ("clang", "clang-20", "clang-19", "clang-18")
                  if shutil.which(exe)), None)
    cc = os.environ.get("CC", "cc")
    if not clang or not shutil.which(cc):
        raise RuntimeError("clang and a native C compiler are required")
    with tempfile.TemporaryDirectory(prefix="phase19e-") as td:
        path = pathlib.Path(td)
        obj = path / "classes.o"
        run([clang, "--target=arm64-apple-darwin", "-fobjc-runtime=macosx-13.0",
             "-fno-objc-arc", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-Werror", "-O1", "-c",
             str(ROOT / "tests/fixtures/phase19e-objc-classes.m"),
             "-o", str(obj)])
        common = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                  "-I" + str(ROOT / "include")]
        test = str(ROOT / "tests/phase19e-standalone.c")
        for opt in ("-O0", "-O2"):
            exe = path / ("native" + opt.replace("-", ""))
            run([cc, *common, opt, *map(str, COMPONENTS), test,
                 "-o", str(exe)], timeout=60)
            run([str(exe), str(obj)], timeout=40)
        if os.environ.get("PHASE19E_SANITIZE") == "1":
            exe = path / "sanitized"
            run([cc, *common, "-O1", "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", *map(str, COMPONENTS),
                 test, "-o", str(exe)], timeout=60)
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                       UBSAN_OPTIONS="halt_on_error=1")
            run([str(exe), str(obj)], timeout=40, env=env)
    print("phase19e: all independent tests passed")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as exc:
        sys.exit(f"phase19e: {exc}")
