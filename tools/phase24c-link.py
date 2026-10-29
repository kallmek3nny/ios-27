#!/usr/bin/env python3
"""Build real Clang+LLD ARM64 Darwin ObjC2 dylibs and executables.

They are linked artifacts, not execution evidence. The platform command
workaround is applied only to bounded MH_OBJECT input (as in phase19b).
"""
from __future__ import annotations
import os
import pathlib
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIBS = {
    "alpha": (),
    "beta": ("alpha",),
    "gamma": ("alpha",),
    "delta": ("beta", "alpha"),
    "epsilon": ("alpha",),
    "zeta": ("alpha",),
}
APPS = {"one": ("alpha",), "two": ("beta", "alpha"),
        "three": ("gamma", "alpha")}


def run(cmd: list[str]) -> None:
    result = subprocess.run(cmd, cwd=ROOT, check=False, timeout=30,
                            text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{cmd!r}\n{result.stdout}{result.stderr}")


def read_commands(path: pathlib.Path, kind: int) -> list[int]:
    data = path.read_bytes()
    if len(data) < 32 or len(data) > 4_194_304:
        raise ValueError(f"invalid file size: {path}")
    magic, cpu, subtype, filetype, count, nbytes, flags, reserved = (
        struct.unpack_from('<8I', data))
    if (magic, cpu, filetype) != (0xfeedfacf, 0x0100000c, kind):
        raise ValueError(f"not expected arm64 Mach-O kind {kind}: {path}")
    if count > 96 or nbytes > len(data) - 32 or reserved:
        raise ValueError("invalid command range")
    cursor, end, commands = 32, 32+nbytes, []
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("truncated load commands")
        code, size = struct.unpack_from('<II', data, cursor)
        if size < 8 or size % 8 or size > end - cursor:
            raise ValueError("invalid load command size")
        commands.append(code)
        cursor += size
    if cursor != end:
        raise ValueError("trailing command bytes")
    return commands


def remove_build_version(path: pathlib.Path) -> pathlib.Path:
    data = bytearray(path.read_bytes())
    commands = read_commands(path, 1)
    ncmds, total = struct.unpack_from('<II', data, 16)
    end, cursor = 32 + total, 32
    for cmd in commands:
        found, size = struct.unpack_from('<II', data, cursor)
        if found != cmd:
            raise ValueError("command mismatch")
        if cmd in (0x32, 0x24):
            data[cursor:end-size] = data[cursor+size:end]
            data[end-size:end] = bytes(size)
            struct.pack_into('<II', data, 16, ncmds-1, total-size)
            target = path.with_suffix('.link.o')
            target.write_bytes(data)
            read_commands(target, 1)
            return target
        cursor += size
    raise ValueError("no platform metadata to remove")


def stub(path: pathlib.Path, name: str, symbols: tuple[str, ...]) -> None:
    path.write_text(
        "--- !tapi-tbd-v3\n"
        "archs: [ arm64 ]\n"
        "platform: macosx\n"
        "install-name: '@rpath/" + name + ".dylib'\n"
        "current-version: 1.0.0\n"
        "compatibility-version: 1.0.0\n"
        "exports:\n"
        "  - archs: [ arm64 ]\n"
        "    symbols: [ " + ", ".join("'"+x+"'" for x in symbols) + " ]\n"
        "...\n", encoding='ascii')


EXPORTS = {
    'alpha': ('_OBJC_CLASS_$_Phase24cRoot', '_OBJC_METACLASS_$_Phase24cRoot'),
    'beta': ('_OBJC_CLASS_$_Phase24cChild', '_OBJC_METACLASS_$_Phase24cChild'),
    'gamma': ('_phase24c_gamma_anchor',),
    'delta': ('_OBJC_CLASS_$_Phase24cLeaf', '_OBJC_METACLASS_$_Phase24cLeaf'),
    'epsilon': ('_phase24c_epsilon_anchor',),
    'zeta': ('_OBJC_CLASS_$_Phase24cHolder', '_OBJC_METACLASS_$_Phase24cHolder'),
}


def link(inputs: list[pathlib.Path], out: pathlib.Path, kind: str) -> None:
    args = [os.getenv('PHASE24C_LINKER', 'ld64.lld'), '-arch', 'arm64',
            '-platform_version', 'macos', '13.0', '13.0',
            '-undefined', 'dynamic_lookup', '-fixup_chains',
            '-o', str(out)]
    if kind == 'lib':
        args += ['-dylib', '-install_name', '@rpath/' + out.name]
    else:
        args += ['-e', '_main', '-rpath', '@executable_path']
    prefix = list(args)
    args += [str(x) for x in inputs]
    try:
        run(args)
    except RuntimeError as error:
        if 'This version of lld does not support linking for platform macOS' not in str(error):
            raise
        inputs = [remove_build_version(x) if x.suffix == '.o' else x
                  for x in inputs]
        run(prefix + [str(x) for x in inputs])


def main() -> None:
    with tempfile.TemporaryDirectory(prefix='phase24c-link-') as tmp:
        outdir = pathlib.Path(tmp)
        linked: dict[str, pathlib.Path] = {}
        stubs: dict[str, pathlib.Path] = {}
        for name, deps in LIBS.items():
            obj = outdir / (name+'.o')
            run([os.getenv('PHASE24C_CLANG', 'clang'),
                 '--target=arm64-apple-darwin', '-fobjc-runtime=macosx-11.0',
                 '-Werror', '-Wall', '-Wextra', '-fno-objc-arc',
                 '-fno-stack-protector', '-fPIC', '-O2', '-c',
                 str(ROOT/'tests'/('phase24c-lib'+name+'.m')),
                 '-o', str(obj)])
            target = outdir / (name+'.dylib')
            link([obj] + [stubs[x] for x in deps], target, 'lib')
            commands = read_commands(target, 6)
            if deps and (0x0c not in commands or 0x80000034 not in commands):
                raise ValueError(f"missing dependency or chained fixups in {name}")
            linked[name] = target
            path_stub = outdir / (name+'.tbd')
            stub(path_stub, name, EXPORTS[name])
            stubs[name] = path_stub
        for name, deps in APPS.items():
            obj = outdir / (name+'.o')
            run([os.getenv('PHASE24C_CLANG', 'clang'),
                 '--target=arm64-apple-darwin', '-fobjc-runtime=macosx-11.0',
                 '-Werror', '-Wall', '-Wextra', '-fno-objc-arc',
                 '-fno-stack-protector', '-O2', '-c',
                 str(ROOT/'tests'/('phase24c-app'+name+'.m')),
                 '-o', str(obj)])
            target = outdir / ('app'+name)
            link([obj] + [stubs[x] for x in deps], target, 'app')
            commands = read_commands(target, 2)
            if 0x0c not in commands or 0x80000034 not in commands:
                raise ValueError(f"missing dependency/fixups in app {name}")
        print('phase24c: 6 genuine ARM64 Darwin ObjC MH_DYLIBs and 3 MH_EXECUTEs linked')
        print('phase24c: linking does not prove runtime execution')


if __name__ == '__main__':
    main()
