#!/usr/bin/env python3
"""Link six real compiler-produced ARM64 Darwin ObjC dylibs and three apps.

This validates files, imports and fixups; native execution is separate.
All subprocesses are bounded. A platform-command workaround changes only the
MH_OBJECT load-command table for older ld64.lld, never ObjC method bodies.
"""
from __future__ import annotations
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINKER = os.getenv('PHASE25B_LINKER') or next(
    (p for p in ('ld64.lld', 'ld64.lld-20', 'ld64.lld-19',
                 'ld64.lld-18', 'ld64.lld-17') if shutil.which(p)), '')
LIBS = {
    'alpha': (), 'beta': ('alpha',), 'gamma': ('alpha',),
    'delta': ('beta', 'alpha'), 'epsilon': ('alpha',),
    'zeta': ('beta', 'alpha'),
}
APPS = {'one': ('alpha',), 'two': ('beta', 'alpha'),
        'three': ('gamma', 'alpha')}
EXPORTS = {
    name: (f'_OBJC_CLASS_$_P25{class_name}',
           f'_OBJC_METACLASS_$_P25{class_name}')
    for name, class_name in {
        'alpha': 'Root', 'beta': 'Child', 'gamma': 'Gamma',
        'delta': 'Grandchild', 'epsilon': 'Epsilon', 'zeta': 'Zeta'
    }.items()
}


def run(cmd: list[str]) -> None:
    res = subprocess.run(cmd, cwd=ROOT, check=False, timeout=30,
                         capture_output=True, text=True)
    if res.stdout and ('phase25b-native' in cmd[0] or 'phase25b-extract.py' in ' '.join(cmd)):
        print(res.stdout.strip(), flush=True)
    if res.returncode:
        raise RuntimeError(f'command failed {cmd!r}\n{res.stdout}{res.stderr}')


def commands(data: bytes, kind: int) -> list[int]:
    if len(data) < 32 or len(data) > 4_194_304:
        raise ValueError('malformed size')
    magic, cpu, subtype, filetype, ncmds, sz, flags, reserved = struct.unpack_from('<8I', data)
    if magic != 0xfeedfacf or cpu != 0x0100000c or filetype != kind or reserved or ncmds > 96 or sz > len(data)-32:
        raise ValueError('malformed arm64 Mach-O header')
    out, pos, end = [], 32, 32 + sz
    for _ in range(ncmds):
        if pos + 8 > end: raise ValueError('truncated commands')
        op, length = struct.unpack_from('<II', data, pos)
        if length < 8 or length % 8 or length > end-pos:
            raise ValueError('invalid load command')
        out.append(op)
        pos += length
    if pos != end: raise ValueError('trailing load commands')
    return out


def strip_platform(path: pathlib.Path) -> pathlib.Path:
    data = bytearray(path.read_bytes())
    n, size = struct.unpack_from('<II', data, 16)
    pos, end = 32, 32 + size
    for op in commands(data, 1):
        actual, length = struct.unpack_from('<II', data, pos)
        assert actual == op
        if op in (0x32, 0x24):
            data[pos:end-length] = data[pos+length:end]
            data[end-length:end] = bytes(length)
            struct.pack_into('<II', data, 16, n-1, size-length)
            patched = path.with_suffix('.link.o')
            patched.write_bytes(data)
            commands(data, 1)
            return patched
        pos += length
    raise ValueError('platform metadata not found')


def tbd(path: pathlib.Path, name: str, exports: tuple[str, ...]) -> None:
    path.write_text('--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n'
        f'install-name: "@rpath/{name}.dylib"\n'
        'current-version: 1.0.0\ncompatibility-version: 1.0.0\n'
        'exports:\n  - archs: [ arm64 ]\n'
        '    symbols: [ ' + ', '.join(f"'{s}'" for s in exports) + ' ]\n...\n')


def link(ins: list[pathlib.Path], output: pathlib.Path, dylib: bool) -> None:
    argv = [LINKER, '-arch', 'arm64', '-platform_version', 'macos',
            '13.0', '13.0', '-undefined', 'dynamic_lookup',
            '-fixup_chains', '-o', str(output)]
    argv += (['-dylib', '-install_name', '@rpath/' + output.name] if dylib else
             ['-e', '_main', '-rpath', '@executable_path'])
    try:
        run(argv + [str(x) for x in ins])
    except RuntimeError as exc:
        if 'This version of lld does not support linking for platform macOS' not in str(exc):
            raise
        run(argv + [str(strip_platform(x) if x.suffix == '.o' else x) for x in ins])


def main() -> None:
    if not LINKER: raise SystemExit('ld64.lld missing')
    with tempfile.TemporaryDirectory(prefix='phase25b-link-') as temp:
        out = pathlib.Path(temp)
        stubs: dict[str, pathlib.Path] = {}
        linked: dict[str, pathlib.Path] = {}
        for lib, deps in LIBS.items():
            obj = out / f'{lib}.o'
            run([os.getenv('PHASE25B_CLANG', 'clang'), '--target=arm64-apple-darwin',
                 '-fobjc-runtime=macosx-11.0', '-fno-objc-arc',
                 '-fno-stack-protector', '-fPIC', '-O2', '-Werror', '-Wall',
                 '-Wextra', '-c', str(ROOT/'tests'/f'phase25b-lib{lib}.m'),
                 '-o', str(obj)])
            target = out / f'{lib}.dylib'
            link([obj] + [stubs[dep] for dep in deps], target, True)
            cmd = commands(target.read_bytes(), 6)
            if deps and (0x0c not in cmd or 0x80000034 not in cmd):
                raise ValueError(f'missing dylib binding/fixups for {lib}')
            linked[lib] = target
            stubs[lib] = out / f'{lib}.tbd'
            tbd(stubs[lib], lib, EXPORTS[lib])
        for app, deps in APPS.items():
            obj = out / f'app{app}.o'
            run([os.getenv('PHASE25B_CLANG', 'clang'), '--target=arm64-apple-darwin',
                 '-fobjc-runtime=macosx-11.0', '-fno-objc-arc',
                 '-fno-stack-protector', '-O2', '-Werror', '-Wall',
                 '-Wextra', '-c', str(ROOT/'tests'/f'phase25b-app{app}.m'),
                 '-o', str(obj)])
            target = out / f'app{app}'
            link([obj] + [stubs[dep] for dep in deps], target, False)
            cmd = commands(target.read_bytes(), 2)
            if 0x0c not in cmd or 0x80000034 not in cmd:
                raise ValueError(f'missing executable binding/fixups for {app}')
        native_dir = out / 'native'
        native_dir.mkdir()
        # Extract only reloc-free, two-instruction compiler-produced methods.
        for lib in ('alpha', 'beta', 'gamma'):
            run(['python3', str(ROOT/'tools'/'phase25b-extract.py'),
                 str(linked[lib]), str(native_dir)])
        if os.getenv('PHASE25B_NATIVE_BIN'):
            run([os.environ['PHASE25B_NATIVE_BIN'], str(native_dir)])
        print('phase25b: 6 compiler-linked arm64 Darwin ObjC dylibs, 3 executables')
        print('phase25b: linked-app code is NOT claimed executable under Linux')

if __name__ == '__main__':
    main()
