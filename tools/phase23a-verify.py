#!/usr/bin/env python3
"""Phase 23a: real compiler-produced ARM64 Darwin dylibs + host/native tests.

No Apple SDK, host filesystem access from the loader, or guest constructors.
Every child command has a hard timeout, and all linked fixtures are inspected.
"""
import argparse
import pathlib
import runpy
import subprocess
import sys

FIXTURES = {
    "base": """
__attribute__((visibility("default"))) int phase23a_base_number = 7;
__attribute__((visibility("default"))) int phase23a_base(void) {
    return phase23a_base_number;
}
__attribute__((visibility("default"))) int phase23a_choice(void) { return 9; }
""",
    "mid": """
extern int phase23a_base(void);
__attribute__((visibility("default"))) int phase23a_mid(void) {
    return phase23a_base() + 3;
}
""",
    "util": """
extern int phase23a_base_number;
__attribute__((visibility("default"))) int phase23a_util(void) {
    return phase23a_base_number * 2;
}
__attribute__((weak, visibility("default")))
int phase23a_choice(void) { return 3; }
""",
    "facade": """
extern int phase23a_mid(void);
extern int phase23a_util(void);
__attribute__((visibility("default"))) int phase23a_facade(void) {
    return phase23a_mid() + phase23a_util();
}
""",
    "top": """
extern int phase23a_facade(void);
extern int phase23a_base(void);
__attribute__((visibility("default"))) int phase23a_top(void) {
    return phase23a_facade() + phase23a_base();
}
""",
}
EXPORTS = {
    "base": ["_phase23a_base", "_phase23a_base_number", "_phase23a_choice"],
    "mid": ["_phase23a_mid"],
    "util": ["_phase23a_util", "_phase23a_choice"],
    "facade": ["_phase23a_facade"],
    "top": ["_phase23a_top"],
}
DEPS = {
    "base": [],
    "mid": ["base"],
    "util": ["base"],
    "facade": ["mid", "util"],
    "top": ["facade", "base"],
}
ORDER = ["base", "mid", "util", "facade", "top"]


def run(argv, seconds):
    completed = subprocess.run(
        [str(v) for v in argv], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, timeout=seconds, check=False)
    if completed.returncode:
        raise RuntimeError("failed exit=" + str(completed.returncode) +
                           " command=" + repr(argv) + "\n" + completed.stdout)
    if completed.stdout.strip():
        print(completed.stdout.strip())


def stub(path, lib):
    path.write_text(
        "--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n"
        "install-name: '" + lib + ".dylib'\n"
        "current-version: 1.0.0\ncompatibility-version: 1.0.0\n"
        "exports:\n  - archs: [ arm64 ]\n    symbols: [ " +
        ", ".join(EXPORTS[lib]) + " ]\n...\n", encoding="ascii")


def main():
    argp = argparse.ArgumentParser()
    argp.add_argument("--output-dir", default="build/phase23a")
    argp.add_argument("--clang", default="clang")
    argp.add_argument("--ld", default="ld64.lld")
    argp.add_argument("--cc", default="cc")
    argp.add_argument("--sanitize", action="store_true")
    args = argp.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    dest = pathlib.Path(args.output_dir).resolve()
    dest.mkdir(parents=True, exist_ok=True)
    helpers = runpy.run_path(str(root / "tools/phase17b-build.py"))
    link, check = helpers["link"], helpers["check_dylib"]

    for lib in ORDER:
        source = dest / ("phase23a-" + lib + ".c")
        obj = dest / ("phase23a-" + lib + ".o")
        source.write_text(FIXTURES[lib], encoding="ascii")
        run([args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fPIC", "-fno-builtin", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-Wall", "-Wextra", "-Werror", "-O2", "-c", source,
             "-o", obj], 30)
        stub(dest / ("phase23a-" + lib + ".tbd"), lib)
        options = ["-dylib", "-install_name", lib + ".dylib",
                   "-o", str(dest / (lib + ".dylib"))]
        additional = [dest / ("phase23a-" + dep + ".tbd")
                      for dep in DEPS[lib]]
        link(args.ld, options, obj, *additional)
        cmds = check(dest / (lib + ".dylib"))
        actual_deps = cmds.count(0x0c)
        if actual_deps != len(additional):
            raise ValueError(lib + ": expected " + str(len(additional)) +
                             " linked dependencies, got " + str(actual_deps))
        print("linked genuine ARM64 MH_DYLIB:", lib, "deps:", actual_deps)

    cmake_dir = dest / "cmake"
    opts = ["-DCMAKE_BUILD_TYPE=Debug", "-DIOS27_BUILD_TESTS=OFF"]
    ccflags = ["-std=c11", "-D_GNU_SOURCE", "-O1", "-g", "-pthread",
               "-Wall", "-Wextra", "-Werror", "-Wpedantic"]
    if args.sanitize:
        flags = "-fsanitize=address,undefined -fno-omit-frame-pointer"
        opts += ["-DCMAKE_C_FLAGS=" + flags]
        ccflags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    run(["cmake", "-S", root, "-B", cmake_dir] + opts, 120)
    run(["cmake", "--build", cmake_dir, "--parallel", "2"], 300)
    executable = dest / "phase23a-integration"
    run([args.cc] + ccflags +
        ["-I", root / "include", root / "src/phase23a_loader.c",
         root / "tests/phase23a-integration.c",
         cmake_dir / "libios27_core.a", "-o", executable], 120)
    run([executable] + [dest / (lib + ".dylib")
                       for lib in ("top", "facade", "util", "mid", "base")], 180)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit("phase23a verification failed: " + str(error))
