#!/usr/bin/env python3
"""Compile/link the original SDK-free ARM64 service-table fixture.

Uses the existing phase19a linker profile without widening its accepted
sections or relocation types. No Apple SDK, binary download or emulation.
"""
import argparse
import pathlib
import runpy
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    parser.add_argument("--linker", required=True)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    support = runpy.run_path(str(ROOT / "tools/phase19a-build.py"))
    command = support["command"]
    args.out.unlink(missing_ok=True)
    obj = args.out.with_suffix(".o")
    command([args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-O2", "-Wall", "-Wextra",
             "-Werror", "-Wpedantic", "-I", str(ROOT / "include"), "-c",
             str(ROOT / "tests/fixtures/service-bridge.c"), "-o", str(obj)])
    if args.linker != "none":
        common = [args.linker, "-arch", "arm64", "-platform_version", "macos",
                  "13.0", "13.0", "-e", "_main", "-fatal_warnings",
                  "-segprot", "__DATA_CONST", "r--", "r--", "-o", str(args.out)]
        try:
            command(common + [str(obj)])
        except RuntimeError as exc:
            if "This version of lld does not support linking for platform" not in str(exc):
                raise
            command(common + [str(support["no_platform_command"](obj))])
    else:
        command([sys.executable, str(ROOT / "tools/phase19a-link-fallback.py"),
                 str(obj), str(args.out)])
    support["inspect"](args.out)
    print(f"service bridge: original linked ARM64 MH_EXECUTE: {args.out}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        sys.exit(f"service bridge build: {exc}")
