#!/usr/bin/env python3
"""Phase 21e independent native stress + SDK-free Darwin Clang object checks."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/libsystem21e_tls.c'
TEST = ROOT / 'tests/phase21e-stress.c'
EDGE = ROOT / 'tests/phase21e-teardown.c'
ISOLATION = ROOT / 'tests/phase21e-runtime-isolation.c'
ORDER = ROOT / 'tests/phase21e-destructor-order.c'
CROSS = ROOT / 'tests/phase21e-cross-runtime-thread-exit.c'
RACE = ROOT / 'tests/phase21e-racing-deletion.c'
MACHO = ROOT / 'tests/phase21e-arm64-darwin.c'


def run(command, seconds=60, env=None):
    print('+', ' '.join(map(str, command)), flush=True)
    cp = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, check=False, timeout=seconds, env=env)
    if cp.returncode:
        raise RuntimeError(f'exit {cp.returncode}:\n{cp.stdout}')
    if cp.stdout.strip():
        print(cp.stdout.strip(), flush=True)


def main():
    beginning = time.monotonic()
    cc = os.environ.get('CC', 'cc')
    clang = shutil.which('clang')
    if not shutil.which(cc) or not clang:
        raise RuntimeError('native compiler and clang required')
    opts = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
            '-pthread', '-I' + str(ROOT / 'include')]
    with tempfile.TemporaryDirectory(prefix='phase21e-') as scratch:
        td = Path(scratch)
        for optimization in ('-O0', '-O2'):
            exe = td / ('stress' + optimization)
            run([cc, *opts, optimization, str(SOURCE), str(TEST), '-o', str(exe)], 120)
            run([str(exe)], 180)
            edge = td / ('teardown' + optimization)
            run([cc, *opts, optimization, str(SOURCE), str(EDGE), '-o', str(edge)], 120)
            run([str(edge)], 180)
            isolation = td / ('isolation' + optimization)
            run([cc, *opts, optimization, str(SOURCE), str(ISOLATION), '-o', str(isolation)], 120)
            run([str(isolation)], 180)
            order = td / ('nested' + optimization)
            run([cc, *opts, optimization, str(SOURCE), str(ORDER), '-o', str(order)], 120)
            run([str(order)], 180)
            for name, source in (('cross-runtime',CROSS), ('key-race',RACE)):
                target = td / (name+optimization)
                run([cc,*opts,optimization,str(SOURCE),str(source),'-o',str(target)],120)
                run([str(target)],180)
        if os.environ.get('PHASE21E_SANITIZE') == '1':
            exe = td / 'sanitized'
            run([clang, *opts, '-O1', '-g', '-fno-omit-frame-pointer',
                 '-fsanitize=address,undefined', str(SOURCE), str(TEST),
                 '-o', str(exe)], 120)
            run([str(exe)], 180, dict(os.environ,
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1'))
            edge = td / 'teardown-sanitized'
            run([clang, *opts, '-O1', '-g', '-fno-omit-frame-pointer',
                 '-fsanitize=address,undefined', str(SOURCE), str(EDGE),
                 '-o', str(edge)], 120)
            run([str(edge)], 180, dict(os.environ,
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1'))
            isolation = td / 'isolation-sanitized'
            run([clang, *opts, '-O1', '-g', '-fno-omit-frame-pointer',
                 '-fsanitize=address,undefined', str(SOURCE), str(ISOLATION),
                 '-o', str(isolation)], 120)
            run([str(isolation)], 180, dict(os.environ,
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1'))
            order = td / 'nested-sanitized'
            run([clang, *opts, '-O1', '-g', '-fno-omit-frame-pointer',
                 '-fsanitize=address,undefined', str(SOURCE), str(ORDER),
                 '-o', str(order)], 120)
            run([str(order)], 180, dict(os.environ,
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1'))
            for name, source in (('cross-runtime',CROSS), ('key-race',RACE)):
                target = td / (name+'-sanitized')
                run([clang,*opts,'-O1','-g','-fno-omit-frame-pointer',
                     '-fsanitize=address,undefined',str(SOURCE),str(source),
                     '-o',str(target)],120)
                run([str(target)],180,dict(os.environ,
                    ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                    UBSAN_OPTIONS='halt_on_error=1'))
        obj = td / 'phase21e-darwin-arm64.o'
        run([clang, '--target=arm64-apple-darwin', '-ffreestanding',
             '-fno-builtin', '-fno-stack-protector', '-std=c11', '-Wall',
             '-Wextra', '-Wpedantic', '-Werror', '-O2',
             '-I' + str(ROOT / 'include'), '-c', str(MACHO), '-o', str(obj)], 120)
        if obj.read_bytes()[:4] != bytes.fromhex('cffaedfe'):
            raise RuntimeError('expected 64-bit Mach-O object')
        print('phase21e: Clang-generated ARM64 Mach-O callsite verified')
    print(f'phase21e: all standalone checks passed; elapsed {time.monotonic()-beginning:.2f}s')

if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
        sys.exit(f'phase21e FAILED: {exc}')
