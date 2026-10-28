#!/usr/bin/env python3
"""Phase 21e independent native stress + SDK-free Darwin Clang object checks."""
import os
from pathlib import Path
import shutil
import subprocess
import struct
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
CANCEL_RACE = ROOT / 'tests/phase21e-cancel-races.c'
ROLLBACK = ROOT / 'tests/phase21e-allocation-rollback.c'
OWNERSHIP = ROOT / 'tests/phase21e-resource-ownership.c'
MACHO = ROOT / 'tests/phase21e-arm64-darwin.c'


def run(command, seconds=60, env=None):
    print('+', ' '.join(map(str, command)), flush=True)
    cp = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, check=False, timeout=seconds, env=env)
    if cp.returncode:
        raise RuntimeError(f'exit {cp.returncode}:\n{cp.stdout}')
    if cp.stdout.strip():
        print(cp.stdout.strip(), flush=True)


def inspect_arm64_macho(path):
    """Inspect the actual compiler-emitted object and undefined relocatable APIs."""
    data = path.read_bytes()
    if len(data) < 32:
        raise RuntimeError('truncated Mach-O header')
    magic, cpu, subtype, file_type, command_count, command_bytes, flags, reserved = \
        struct.unpack_from('<IiiIIIII', data)
    if (magic, cpu, file_type) != (0xfeedfacf, 0x0100000c, 1):
        raise RuntimeError('not an ARM64 Mach-O object')
    if not 1 <= command_count <= 64 or command_bytes > len(data)-32:
        raise RuntimeError('invalid ARM64 Mach-O load commands')
    offset = 32
    symbol_table = None
    for _ in range(command_count):
        if offset+8 > len(data):
            raise RuntimeError('truncated ARM64 Mach-O command')
        cmd, size = struct.unpack_from('<II',data,offset)
        if size<8 or offset+size>len(data):
            raise RuntimeError('out-of-bounds ARM64 Mach-O command')
        if cmd==2 and size>=24:
            symbol_table=struct.unpack_from('<IIII',data,offset+8)
        offset+=size
    if offset > 32+command_bytes or symbol_table is None:
        raise RuntimeError('missing or inconsistent ARM64 Mach-O symbol table')
    symoff,nsyms,stroff,strsize=symbol_table
    if nsyms>8192 or symoff+nsyms*16>len(data) or stroff+strsize>len(data):
        raise RuntimeError('out-of-bounds ARM64 Mach-O string/symbol table')
    unresolved=set()
    for i in range(nsyms):
        strx,type_,section,desc,value=struct.unpack_from('<IBBHQ',data,symoff+i*16)
        if strx>=strsize: continue
        name=data[stroff+strx:stroff+strsize].split(b'\x00',1)[0].decode('ascii','replace')
        if (type_&0x0e)==0 and name.startswith('_ios27_21e_'):
            unresolved.add(name)
    needed={'_ios27_21e_attach','_ios27_21e_key_create',
            '_ios27_21e_setspecific','_ios27_21e_cancel_register',
            '_ios27_21e_cleanup_push','_ios27_21e_cancel_checkpoint',
            '_ios27_21e_cleanup_pop'}
    if not needed.issubset(unresolved):
        raise RuntimeError(f'ARM64 Mach-O API relocations missing: {needed-unresolved}')
    print(f'phase21e: ARM64 Mach-O verified with {len(needed)} real undefined API callsites')


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
            for name, source in (('cross-runtime',CROSS), ('key-race',RACE), ('cancel-races',CANCEL_RACE), ('ownership',OWNERSHIP)):
                target = td / (name+optimization)
                run([cc,*opts,optimization,str(SOURCE),str(source),'-o',str(target)],120)
                run([str(target)],180)
            rollback = td / ('rollback'+optimization)
            run([cc,*opts,optimization,'-Dcalloc=ios27_21e_test_calloc',
                 str(SOURCE),str(ROLLBACK),'-o',str(rollback)],120)
            run([str(rollback)],180)
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
            for name, source in (('cross-runtime',CROSS), ('key-race',RACE), ('cancel-races',CANCEL_RACE), ('ownership',OWNERSHIP)):
                target = td / (name+'-sanitized')
                run([clang,*opts,'-O1','-g','-fno-omit-frame-pointer',
                     '-fsanitize=address,undefined',str(SOURCE),str(source),
                     '-o',str(target)],120)
                run([str(target)],180,dict(os.environ,
                    ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                    UBSAN_OPTIONS='halt_on_error=1'))
            rollback = td / 'rollback-sanitized'
            run([clang,*opts,'-O1','-g','-fno-omit-frame-pointer',
                 '-fsanitize=address,undefined',
                 '-Dcalloc=ios27_21e_test_calloc',str(SOURCE),str(ROLLBACK),
                 '-o',str(rollback)],120)
            run([str(rollback)],180,dict(os.environ,
                ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1'))
        obj = td / 'phase21e-darwin-arm64.o'
        run([clang, '--target=arm64-apple-darwin', '-ffreestanding',
             '-fno-builtin', '-fno-stack-protector', '-std=c11', '-Wall',
             '-Wextra', '-Wpedantic', '-Werror', '-O2',
             '-I' + str(ROOT / 'include'), '-c', str(MACHO), '-o', str(obj)], 120)
        inspect_arm64_macho(obj)
        print('phase21e: Clang-generated ARM64 Mach-O callsite verified')
    print(f'phase21e: all standalone checks passed; elapsed {time.monotonic()-beginning:.2f}s')

if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
        sys.exit(f'phase21e FAILED: {exc}')
