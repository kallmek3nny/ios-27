#!/usr/bin/env python3
"""Check Darling's Clang-version query with real native and cross compilers."""

import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", required=True, type=Path)
    args = parser.parse_args()
    module = args.module.resolve(strict=True)
    for tool in ("cmake", "clang", "clang++", "gcc"):
        if not shutil.which(tool):
            parser.error(f"missing test tool: {tool}")
    with tempfile.TemporaryDirectory(prefix="darling-compiler-version-") as temp:
        root = Path(temp)
        passed = 0
        for language, driver in (("c", "clang"), ("cpp", "clang++")):
            cross = root / f"cross-{language}"
            cross.write_text("#!/bin/sh\nexec " + shlex.quote(shutil.which(driver))
                             + " --target=aarch64-linux-android30 --sysroot=/nonexistent \"$@\"\n")
            cross.chmod(0o700)
            malformed = root / "malformed-compiler"
            malformed.write_text("#!/bin/sh\nprintf 'not a compiler version\\n'\n")
            malformed.chmod(0o700)
            cases = (("native", shutil.which(driver), 11, True),
                     ("cross-no-sdk", str(cross), 11, True),
                     ("too-old", shutil.which(driver), 999, False),
                     ("non-clang", shutil.which("gcc"), 11, False),
                     ("malformed", str(malformed), 11, False))
            for name, compiler, minimum, expected in cases:
                directory = root / f"{language}-{name}"
                directory.mkdir()
                script = directory / "check.cmake"
                script.write_text(f'include("{module.as_posix()}")\n'
                                  f'clang_version_check("{compiler}" "{language}" {minimum})\n')
                result = subprocess.run(["cmake", "-P", str(script)], cwd=directory,
                                        capture_output=True, text=True, timeout=35)
                assert (result.returncode == 0) == expected, f"{language}/{name}: {result.stdout}{result.stderr}"
                assert not (directory / f"clang_{language}_major").exists(), "query linked a target executable"
                passed += 1
        print(f"{passed} compiler-version checks passed, including SDK-free Android cross compilers")


if __name__ == "__main__":
    main()
