#!/usr/bin/env python3
"""Build and verify four genuine Clang/LLD ARM64 Darwin MH_DYLIBs.

No Apple SDK or proprietary binaries. Each subprocess has a hard timeout;
the link stubs are only build-time symbol declarations, not loaded at runtime.
"""
import argparse
import pathlib
import runpy
import subprocess
import sys


def stub(path, name, symbols):
    path.write_text(
        "--- !tapi-tbd-v3\n"
        "archs: [ arm64 ]\n"
        "platform: macosx\n"
        "install-name: '" + name + "'\n"
        "current-version: 1.0.0\n"
        "compatibility-version: 1.0.0\n"
        "exports:\n"
        "  - archs: [ arm64 ]\n"
        "    symbols: [ " + ", ".join(symbols) + " ]\n"
        "...\n", encoding="ascii")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--linker", default="ld64.lld")
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--core", default="build/libios27_core.a")
    parser.add_argument("--output-dir", default="build/phase19b")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    output = pathlib.Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    helpers = runpy.run_path(str(root / "tools/phase17b-build.py"))
    run = helpers["run"]
    link = helpers["link"]
    check = helpers["check_dylib"]
    paths = {}
    stubs = {}
    exports = {
        "base": ["_phase19b_base_number", "_phase19b_base_calculate"],
        "leaf": ["_phase19b_leaf_multiplier", "_phase19b_leaf_value"],
        "middle": ["_phase19b_middle_offset", "_phase19b_middle_value"],
        "root": ["_phase19b_root_seed", "_phase19b_root_value"],
    }
    names = {k: "@rpath/" + k.title() + ".dylib" for k in exports}
    dependencies = {
        "base": [],
        "leaf": ["base"],
        "middle": ["leaf", "base"],
        "root": ["middle", "leaf", "base"],
    }
    for stem in exports:
        obj = output / ("phase19b-" + stem + ".o")
        run([args.clang, "--target=arm64-apple-darwin",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-fPIC", "-O2", "-c",
             str(root / "tests/fixtures" / ("phase19b-" + stem + ".c")),
             "-o", str(obj)])
        dylib = output / ("phase19b-" + stem + ".dylib")
        options = ["-dylib", "-install_name", names[stem],
                   "-o", str(dylib)]
        if stem == "root":
            options += ["-rpath", "@loader_path/Plugins",
                        "-rpath", "@loader_path/Frameworks"]
        link(args.linker, options, obj, *(stubs[x] for x in dependencies[stem]))
        commands = check(dylib)
        if dependencies[stem] and 0xcu not in commands:
            raise ValueError("missing linked LC_LOAD_DYLIB for " + stem)
        paths[stem] = dylib
        link_stub = output / ("phase19b-" + stem + ".tbd")
        stub(link_stub, names[stem], exports[stem])
        stubs[stem] = link_stub
    executable = output / "phase19b-verify"
    core = pathlib.Path(args.core).resolve()
    if not core.is_file():
        raise ValueError("missing core library: " + str(core))
    run([args.host_cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-Wpedantic", "-I", str(root / "include"),
         str(root / "tests/fixtures/phase19b-verify.c"),
         str(core), "-o", str(executable)])
    run([str(executable), str(paths["root"]), str(paths["middle"]),
         str(paths["leaf"]), str(paths["base"])])
    print("phase19b: four real arm64 Darwin dylibs compiled, linked and verified")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit("phase19b: " + str(exc))
