#!/usr/bin/env python3
"""Standalone bounded test driver; never modifies the shared root CMake graph."""
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(os.environ.get("PHASE22E_OUT", ROOT / ".phase22e-build"))
OUT.mkdir(parents=True, exist_ok=True)
CC = os.environ.get("CC", "clang")
SANITIZE = os.environ.get("PHASE22E_SANITIZE", "")
FLAGS = ["-std=c11", "-O1" if SANITIZE else "-O2", "-g", "-Wall", "-Wextra",
         "-Wpedantic", "-Werror", "-pthread", "-I" + str(ROOT / "include"),
         "-I" + str(ROOT / "tests")]
if SANITIZE:
    if SANITIZE not in ("address,undefined", "thread", "undefined"):
        sys.exit("unsupported sanitizer configuration")
    FLAGS += ["-fsanitize=" + SANITIZE, "-fno-omit-frame-pointer"]

def run(cmd, seconds):
    print("$", " ".join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, timeout=seconds)

print("host:", platform.system(), platform.machine(), "sanitizer:", SANITIZE or "none", flush=True)
run([sys.executable, ROOT / "tools" / "phase22e-generate.py", "--check"], 30)
for name in ("stress", "negative", "ordering"):
    exe = OUT / (name + ("-san" if SANITIZE else ""))
    run([CC, *FLAGS, ROOT / "src" / "phase22e_ipc.c",
         ROOT / "tests" / ("phase22e-" + name + ".c"), "-o", exe], 300)
    run([exe], 180)
