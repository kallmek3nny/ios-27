#!/usr/bin/env python3
"""Compile/link three real arm64 Darwin dylibs without Apple SDK or host dyld.

All child commands use a bounded subprocess timeout from phase17b-build.py.
"""
import argparse
import pathlib
import runpy
import sys


def tbd(path, name, symbols):
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
    p = argparse.ArgumentParser()
    p.add_argument("--clang", default="clang")
    p.add_argument("--linker", default="ld64.lld")
    p.add_argument("--output-dir", default="build/phase18b")
    args = p.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    helper = runpy.run_path(str(root / "tools/phase17b-build.py"))
    run, link, check = (helper[name] for name in
                        ("run", "link", "check_dylib"))
    output = pathlib.Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    objects = {}
    for stem in ("leaf", "middle", "top"):
        source = root / "tests" / "fixtures" / ("phase18b-" + stem + ".c")
        obj = output / ("phase18b-" + stem + ".o")
        run([args.clang, "--target=arm64-apple-darwin", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-fPIC", "-O2",
             "-c", str(source), "-o", str(obj)])
        objects[stem] = obj
    leaf = output / "phase18b-leaf.dylib"
    middle = output / "phase18b-middle.dylib"
    top = output / "phase18b-top.dylib"
    leaf_name, middle_name, top_name = (
        "@rpath/Leaf.dylib", "@rpath/Middle.dylib", "@rpath/Top.dylib")
    link(args.linker, ["-dylib", "-install_name", leaf_name,
                       "-o", str(leaf)], objects["leaf"])
    leaf_stub = output / "phase18b-leaf.tbd"
    tbd(leaf_stub, leaf_name, ["_phase18b_number", "_phase18b_leaf"])
    link(args.linker, ["-dylib", "-install_name", middle_name,
                       "-rpath", "@loader_path/../Frameworks",
                       "-o", str(middle)], objects["middle"], leaf_stub)
    middle_stub = output / "phase18b-middle.tbd"
    tbd(middle_stub, middle_name, ["_phase18b_middle", "_phase18b_adjustment"])
    link(args.linker, ["-dylib", "-install_name", top_name,
                       "-rpath", "@loader_path/Plugins",
                       "-rpath", "@loader_path/Frameworks",
                       "-o", str(top)],
         objects["top"], middle_stub, leaf_stub)
    for lib in (leaf, middle, top):
        commands = check(lib)
        if lib != leaf and (0xcu not in commands or 0x8000001cu not in commands):
            raise ValueError("linked dylib missing real LC_LOAD_DYLIB/LC_RPATH: " +
                             str(lib))
        print("phase18b linked:", lib, "commands:", len(commands))
    print("phase18b: three genuine Mach-O dylibs; no Apple SDK or dylib bytes")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        sys.exit("phase18b build: " + str(exc))
