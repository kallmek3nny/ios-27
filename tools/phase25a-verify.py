#!/usr/bin/env python3
"""Stand-alone phase25a real linked arm64 ObjC2 verification; no phase25 siblings.

Usage: timeout 180s python3 tools/phase25a-verify.py
       timeout 180s python3 tools/phase25a-verify.py --sanitize

Requires upstream LLVM's Mach-O-capable ld64.lld, not the Swift fork that
rejects linking platform macOS. Missing tools are a HARD failure, never skipped.
"""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from tempfile import TemporaryDirectory
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from phase25a_bind import Registry, BindError, inspect, parse_image

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ('root', 'branch', 'sibling', 'category', 'leaf', 'category2')
GRAPH = {
    'root': (),
    'branch': ('root',),
    'sibling': ('root',),
    'category': ('branch',),
    'leaf': ('branch', 'sibling'),
    'category2': ('root', 'leaf'),
}


def run(cmd: list[str], timeout: int = 60) -> None:
    print('+', ' '.join(cmd), flush=True)
    child = subprocess.run(cmd, check=False, timeout=timeout, cwd=ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                                'UBSAN_OPTIONS': 'halt_on_error=1'})
    if child.stdout:
        print(child.stdout[-8000:], flush=True)
    if child.returncode:
        raise RuntimeError(f'command failed ({child.returncode}): {cmd}')


def choose(*names: str) -> str:
    for name in names:
        result = shutil.which(name)
        if result:
            return result
    raise RuntimeError(f'none of these required tools found: {names!r}')


def link_fixtures(temp: Path, clang: str, linker: str, optimize: int) -> dict[str, bytes]:
    produced: dict[str, Path] = {}
    data = {}
    for name in FIXTURES:
        obj = temp / f'phase25a-{name}-{optimize}.o'
        lib = temp / f'lib25{optimize}-{name}.dylib'
        run([clang, '--target=arm64-apple-macos11', '-fobjc-runtime=macosx-11.0',
             '-fno-objc-arc', f'-O{optimize}', '-Wall', '-Wextra', '-Werror',
             '-c', str(ROOT / 'tests' / f'phase25a-{name}.m'), '-o', str(obj)], 30)
        libs = [str(produced[dep]) for dep in GRAPH[name]]
        run([linker, '-arch', 'arm64', '-dylib', '-platform_version', 'macos',
             '11.0', '11.0', '-fixup_chains', '-undefined', 'dynamic_lookup',
             '-install_name', f'@rpath/{lib.name}', str(obj), *libs, '-o', str(lib)], 30)
        produced[name] = lib
        data[name] = lib.read_bytes()
        if len(data[name]) < 1024:
            raise AssertionError(f'linked fixture {name} suspiciously small')
        print(f'linked genuine {name}: {len(data[name])} bytes', flush=True)
    return data


def verify_concurrent_registration(data: dict[str, bytes]) -> None:
    """Race identical registration against conflicting global class identities."""
    registry = Registry()
    with ThreadPoolExecutor(max_workers=12) as executor:
        bases = list(executor.map(lambda _: registry.register('root', data['root']),
                                  range(96)))
    assert len(set(bases)) == 1
    assert set(registry.classes) == {'Phase25Root'}
    assert set(registry.images) == {'root'}
    generation = registry.generation

    def duplicate(index: int) -> bool:
        try:
            registry.register(f'duplicate-{index}', data['root'])
        except BindError as exc:
            assert 'duplicate class' in str(exc)
            return True
        raise AssertionError('accepted duplicate class from a second image')

    with ThreadPoolExecutor(max_workers=12) as executor:
        assert all(executor.map(duplicate, range(48)))
    assert registry.generation == generation and set(registry.images) == {'root'}

    corrupt = bytearray(data['root'])
    corrupt[0] = 0
    try:
        registry.register('invalid-macho', bytes(corrupt))
    except BindError:
        pass
    else:
        raise AssertionError('invalid image committed despite failed preflight')
    assert registry.generation == generation
    registry.unload('root')
    assert registry.classes == {} and registry.images == {}
    print('concurrent registration=96; duplicate classes rejected=48; '
          'atomic invalid-image rollback=passed', flush=True)


def exercise(data: dict[str, bytes], rounds: int) -> None:
    verify_concurrent_registration(data)
    registry = Registry()
    for name in FIXTURES:
        registry.register(name, data[name], GRAPH[name])
        assert registry.register(name, data[name], GRAPH[name]) == registry.images[name].base
    assert set(registry.classes) == {
        'Phase25Root', 'Phase25Branch', 'Phase25Sibling', 'Phase25Leaf'
    }
    assert len(registry.images) == 6
    assert registry.lookup('Phase25Branch', 'score') is not None
    assert registry.lookup('Phase25Sibling', 'score') is not None
    assert registry.lookup('Phase25Leaf', 'branchValue') is not None
    assert registry.lookup('Phase25Branch', 'categoryValue') is not None
    assert registry.lookup('Phase25Root', 'overrideValue') is not None
    assert registry.lookup('Phase25Root', 'identity', class_method=True) is not None
    # Compiler-linked categories must replace existing method selectors, not
    # merely append new methods. Subclasses with no override inherit them.
    assert registry.lookup('Phase25Root', 'score').owner == 'Phase25Overrides'
    assert registry.lookup('Phase25Sibling', 'score').owner == 'Phase25Overrides'
    assert registry.lookup('Phase25Branch', 'score').owner == 'Phase25Extras'
    assert registry.lookup('Phase25Leaf', 'score').owner == 'Phase25Extras'
    assert registry.classes['Phase25Root'].protocols == ('Phase25Protocol',)
    assert '_value' in registry.classes['Phase25Root'].ivars
    assert len(registry.selectors) >= 6
    base = registry.images['root'].base
    assert base != registry.images['branch'].base
    assert registry.classes['Phase25Branch'].parent == 'Phase25Root'
    assert registry.classes['Phase25Leaf'].parent == 'Phase25Branch'
    try:
        registry.unload('root')
    except BindError:
        pass
    else:
        raise AssertionError('unloaded a live dependent class')
    # 160,000+ concurrent class-method and selector resolution operations.
    selectors = [('Phase25Root', 'score'), ('Phase25Leaf', 'score'),
                 ('Phase25Branch', 'categoryValue'), ('Phase25Leaf', 'leafValue'),
                 ('Phase25Root', 'overrideValue')]
    def reader(n: int) -> int:
        found = 0
        for index in range(n):
            cls, sel = selectors[index % len(selectors)]
            method = registry.lookup(cls, sel)
            if method is None or method.imp & 3:
                raise AssertionError('missing or invalid relocated IMP')
            found += 1
        return found
    with ThreadPoolExecutor(max_workers=8) as executor:
        assert sum(executor.map(reader, [20000] * 8)) == 160000
    # Registered image must be immutable and identity cannot be hijacked.
    corrupted = bytearray(data['root'])
    corrupted[0] ^= 1
    try:
        registry.register('root', bytes(corrupted), ())
    except BindError:
        pass
    else:
        raise AssertionError('accepted mismatched image generation')
    # Each mutation goes through the same strict Mach-O and ObjC relocation
    # checks; valid mutations can be accepted, but may not leak registry state.
    source = data['branch']
    denied = 0
    for i in range(rounds):
        attempt = bytearray(source)
        at = (i * 104729 + i * i * 31) % len(attempt)
        attempt[at] ^= 1 << (i % 8)
        try:
            candidate = parse_image('fuzz', bytes(attempt), 0x600000000, ('root',))
            inspect(candidate, 9)
        except (BindError, UnicodeDecodeError):
            denied += 1
    assert denied > 0, 'mutation corpus failed to exercise invalid metadata'
    print(f'relocated method lookups=160000; malformed mutations={rounds}, '
          f'rejected={denied}; selectors={len(registry.selectors)}', flush=True)
    # Teardown is reverse dependency/registration order, retaining no stale IMP.
    for name in reversed(FIXTURES):
        registry.unload(name)
        # Retiring a category must invalidate its method overlay immediately.
        if name == 'category2':
            assert registry.lookup('Phase25Root', 'score').owner == 'Phase25Root'
            assert registry.lookup('Phase25Sibling', 'score').owner == 'Phase25Root'
        if name == 'category':
            assert registry.lookup('Phase25Branch', 'score').owner == 'Phase25Branch'
    assert not registry.classes and not registry.images
    registry.register('root', data['root'])
    assert registry.images['root'].base != base, 'reused retired guest address'
    registry.unload('root')
    assert not registry.selectors


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    options = parser.parse_args()
    clang = choose('clang-18', 'clang-19', 'clang-17', 'clang')
    linker = choose('ld64.lld-18', 'ld64.lld-19', 'ld64.lld')
    cc = choose('clang-18', 'clang-19', 'clang')
    with TemporaryDirectory(prefix='phase25a-') as d:
        temporary = Path(d)
        for opt in (0, 2):
            files = link_fixtures(temporary, clang, linker, opt)
            app = temporary / f'preflight-{opt}'
            flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Wpedantic',
                     '-Werror', '-Iinclude']
            if options.sanitize:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            run([cc, *flags, 'src/phase25a_inspect.c',
                 'tests/phase25a-native.c', '-o', str(app)], 60)
            paths = [str(temporary / f'lib25{opt}-{name}.dylib') for name in FIXTURES]
            run([str(app), *paths], 120)
            exercise(files, 800)
    print('phase25a: genuine compiler-linked arm64 objc2 dylibs, relocations, '
          'class identity, category overrides, sanitizer preflight: PASSED', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, BindError, AssertionError) as exc:
        print(f'PHASE25A FAIL: {exc}', file=sys.stderr, flush=True)
        sys.exit(1)
