#!/usr/bin/env python3
"""Verify isolated startup and compiler-linked PR fixtures without false execution claims."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PINNED = {
    102: "70d7d9826a034b3076a8c066d81aa085dbced285",
    103: "ae4bdd578843",  # expanded by github CI's full SHA lease
    104: "7c434d616cf2",  # expanded by github CI's full SHA lease
}
REQUIRED = {
    102: ["include/ios27/phase25c_lifetime.h", "src/phase25c_lifetime.c"],
    104: ["include/ios27/phase25d_lifecycle.hpp", "src/phase25d_lifecycle.cpp"],
    103: ["tools/phase25e-build.py", "tools/phase25e-inspect.py",
          "tests/phase25e-fixtures.m", "tests/phase25e-integration.py"],
}

def run(command: list[str], timeout: int = 180, cwd: Path | None = None) -> str:
    try:
        completed = subprocess.run(command, cwd=cwd, check=False, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"timed out: {command[0]}") from exc
    if completed.returncode:
        raise RuntimeError(f"failed ({completed.returncode}): {command}\n{completed.stdout[-5000:]}")
    print(completed.stdout.strip(), flush=True)
    return completed.stdout

def verify_source(work: Path, revisions: dict[int, str]) -> None:
    repo = ROOT
    for pr, paths in REQUIRED.items():
        rev = revisions[pr]
        for path in paths:
            destination = work / "upstream" / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            # git-show only reads detached frozen objects and cannot mutate main.
            content = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=repo,
                                     capture_output=True, timeout=30, check=True).stdout
            if not content:
                raise RuntimeError(f"empty upstream file: pr{pr}/{path}")
            if destination.exists() and destination.read_bytes() != content:
                raise RuntimeError(f"upstream path collision: {path}")
            destination.write_bytes(content)

def compile_startup(work: Path, mode: str) -> None:
    cc = shutil.which("clang") or shutil.which("cc")
    cxx = shutil.which("clang++") or shutil.which("c++")
    if not cc or not cxx:
        raise RuntimeError("both C and C++ compilers required")
    flags = ["-O1", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
    if mode == "sanitize":
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    incl = ["-I", str(ROOT / "include"), "-I", str(work / "upstream/include")]
    outputs = work / "objects"
    outputs.mkdir(parents=True, exist_ok=True)
    source_c = [ROOT / "src/phase27a_startup.c",
                work / "upstream/src/phase25c_lifetime.c"]
    object_files = []
    for n, source in enumerate(source_c):
        dest = outputs / f"c{n}.o"
        run([cc, "-std=c11", *flags, *incl, "-pthread", "-c", str(source),
             "-o", str(dest)], 60)
        object_files.append(str(dest))
    cpp_source = work / "upstream/src/phase25d_lifecycle.cpp"
    cpp_object = outputs / "lifecycle.o"
    run([cxx, "-std=c++17", *flags, *incl, "-pthread", "-c", str(cpp_source),
         "-o", str(cpp_object)], 60)
    dest = outputs / "phase27a-startup"
    run([cc, "-std=c11", *flags, *incl, "-pthread",
         str(ROOT / "tests/phase27a-startup.c"), str(object_files[0]),
         "-o", str(dest)], 60)
    run([str(dest)], 30)
    integrated = outputs / "phase27a-host-integration"
    run([cxx, "-std=c++17", *flags, *incl, "-pthread",
         str(ROOT / "tests/phase27a-host-integration.cpp"),
         *object_files, str(cpp_object), "-o", str(integrated)], 90)
    run([str(integrated)], 30)

def verify_linked(work: Path) -> None:
    linker = shutil.which("ld64.lld") or shutil.which("ld64.lld-18")
    clang = shutil.which("clang") or shutil.which("clang-18")
    if not clang or not linker:
        raise RuntimeError("clang and LLVM Mach-O linker required")
    upstream = work / "upstream"
    linked = upstream / "build/phase25e"
    run([sys.executable, "tools/phase25e-build.py", "--output", str(linked),
         "--rounds", "4"], 300, upstream)
    run([sys.executable, "tests/phase25e-integration.py", "-v"], 180, upstream)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("normal","sanitize"), default="normal")
    parser.add_argument("--linked", action="store_true")
    parser.add_argument("--work", type=Path, default=ROOT / "build/phase27a")
    parser.add_argument("--pr102", required=True)
    parser.add_argument("--pr103", required=True)
    parser.add_argument("--pr104", required=True)
    args = parser.parse_args()
    revisions = {102: args.pr102, 103: args.pr103, 104: args.pr104}
    if not all(revisions[n].startswith(PINNED[n]) for n in revisions):
        raise RuntimeError("frozen upstream SHA mismatch; refusing unreviewed PR revision")
    args.work.mkdir(parents=True, exist_ok=True)
    verify_source(args.work, revisions)
    compile_startup(args.work, args.mode)
    if args.linked:
        verify_linked(args.work)
    print(json.dumps({"phase":"27a", "startup_host": "passed", "linked_metadata":
                      "inspected" if args.linked else "not_run",
                      "native_darwin_app_executed": False,
                      "claimable_phase27a_complete": False}, sort_keys=True))
    return 0

if __name__ == "__main__":
    sys.exit(main())