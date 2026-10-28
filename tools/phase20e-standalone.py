#!/usr/bin/env python3
"""Bounded, independent phase20e host runtime and Darwin ARM64 compile checks."""
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FILES = [ROOT / 'src/libsystem20e_services.c',
         ROOT / 'tests/phase20e-darwin-fixture.c',
         ROOT / 'tests/phase20e-standalone.c']
CC = os.environ.get('CC', 'clang')
CLANG = os.environ.get('DARWIN_CLANG', 'clang')
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-I', str(ROOT/'include')]

def run(args, deadline):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str,args)), check=True, timeout=deadline, cwd=ROOT)

def main():
    if not shutil.which(CC) or not shutil.which(CLANG):
        raise RuntimeError('C11 compiler and Clang are required')
    with tempfile.TemporaryDirectory(prefix='phase20e-') as tmp:
        base=pathlib.Path(tmp)
        for opt in ('-O0','-O2'):
            exe=base/('test'+opt)
            run([CC,*FLAGS,opt,*FILES,'-o',exe], 60)
            run([exe], 40)
        if os.environ.get('PHASE20E_SANITIZE') == '1':
            exe=base/'sanitized'
            run([CC,*FLAGS,'-O1','-g','-fsanitize=address,undefined', '-fno-omit-frame-pointer',*FILES,'-o',exe],60)
            run([exe],60)
        macho=base/'phase20e-arm64-darwin.o'
        run([CLANG,'--target=arm64-apple-darwin','-ffreestanding','-fno-builtin',
             '-fno-stack-protector','-O2','-Wall','-Wextra','-Werror','-Wpedantic',
             '-I',ROOT/'include','-c',ROOT/'tests/phase20e-darwin-fixture.c', '-o',macho],60)
        data=macho.read_bytes()
        if len(data)<32 or struct.unpack_from('<II',data)!=(0xfeedfacf,0x0100000c):
            raise RuntimeError('fixture is not Clang-generated ARM64 Mach-O MH_OBJECT')
        if struct.unpack_from('<I',data,12)[0]!=1:
            raise RuntimeError('fixture must be Mach-O object file')
        for required in (b'_ios27_20e_open',b'_ios27_20e_write',b'_ios27_20e_mmap',
                         b'_ios27_20e_calloc',b'_ios27_20e_mprotect'):
            if required not in data:raise RuntimeError('missing ARM64 external symbol: '+repr(required))
        print('phase20e: ARM64 Darwin compiler-generated integration object verified')

if __name__ == '__main__':
    try:main()
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        print('phase20e FAILED:',exc,file=sys.stderr)
        sys.exit(1)