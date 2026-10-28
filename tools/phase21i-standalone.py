#!/usr/bin/env python3
"""Independent phase21i host+Mach-O checks, without modifying shared CMake."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def run(cmd, timeout_seconds):
    print("+", " ".join(map(str, cmd)), flush=True)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, timeout=timeout_seconds, check=False)
    if r.stdout.strip():
        print(r.stdout, flush=True)
    if r.returncode:
        raise RuntimeError(f"command exited {r.returncode}")

def main():
    cc = os.environ.get("CC", "cc")
    clang = shutil.which("clang")
    if not shutil.which(cc) or not clang:
        raise RuntimeError("cc and clang required")
    with tempfile.TemporaryDirectory(prefix="phase21i-") as temp:
        dest = Path(temp)
        flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 "-pthread", "-I" + str(ROOT / "include")]
        sanitizers = os.environ.get("PHASE21I_SANITIZE") == "1"
        if sanitizers:
            flags.extend(["-fsanitize=address,undefined",
                          "-fno-omit-frame-pointer", "-g"])
        for optimization in (("-O1",) if sanitizers else ("-O0", "-O2")):
            for source in ("phase21i-native.c", "phase21i-recycle.c",
                           "phase21i-arm64-args.c", "phase21i-fuzz.c",
                           "phase21i-attrs.c", "phase21i-capacity.c",
                           "phase21i-access.c", "phase21i-destroy-races.c"):
                exe = dest / (source + optimization)
                run([cc, *flags, optimization,
                     str(ROOT / "src/libsystem21c_sync.c"),
                     str(ROOT / "src/libsystem21i_abi.c"),
                     str(ROOT / "src/libsystem21i_arm64.c"),
                     str(ROOT / "src/libsystem21i_errno.c"),
                     str(ROOT / "tests" / source),
                     "-o", str(exe)], 120)
                run([str(exe)], 120)
        for optimize in ("-O0", "-O2"):
            target = dest / ("arm64-darwin" + optimize + ".o")
            run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
                 "-fno-builtin", "-fno-stack-protector", "-Werror", "-Wall",
                 "-Wextra", "-Wpedantic", optimize, "-c",
                 str(ROOT / "tests/phase21i-darwin-client.c"),
                 "-o", str(target)], 60)
            run([sys.executable, str(ROOT / "tools/phase21i-macho.py"),
                 str(target)], 30)
            run([sys.executable, str(ROOT / "tools/phase21i-macho.py"),
                 "--attrs", str(target)], 30)
            threaded = dest / ("arm64-darwin-threads" + optimize + ".o")
            run([clang, "--target=arm64-apple-darwin", "-ffreestanding",
                 "-fno-builtin", "-fno-stack-protector", "-Werror", "-Wall",
                 "-Wextra", "-Wpedantic", optimize, "-c",
                 str(ROOT / "tests/phase21i-darwin-threads.c"),
                 "-o", str(threaded)], 60)
            run([sys.executable, str(ROOT / "tools/phase21i-macho.py"),
                 "--threads", str(threaded)], 30)
    print("phase21i: standalone checks passed", flush=True)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit(f"phase21i failed: {exc}")
