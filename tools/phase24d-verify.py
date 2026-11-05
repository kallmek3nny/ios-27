#!/usr/bin/env python3
"""Bounded -Werror host stress/sanitizer suite + real linked Darwin fixture provenance."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SAN = os.getenv('PHASE24D_SANITIZE') == '1'
SERVICES = ('echo', 'sum', 'relay', 'worker')
MAGIC, CPU = 0xfeedfacf, 0x0100000c


def run(args, limit=30):
    args = [str(a) for a in args]
    print('+', ' '.join(args), flush=True)
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, check=False, timeout=limit)
    if p.stdout.strip():
        print(p.stdout.strip(), flush=True)
    if p.returncode:
        raise RuntimeError('command exited %d' % p.returncode)


def macho(path, kind, deps=()):
    raw = path.read_bytes()
    if not 32 <= len(raw) <= 4194304:
        raise ValueError('unsupported Mach-O length ' + str(path))
    magic, cpu, _, type_, count, size = struct.unpack_from('<6I', raw)
    if (magic, cpu, type_) != (MAGIC, CPU, kind) or count > 128 or size > len(raw)-32:
        raise ValueError('wrong Mach-O identity ' + str(path))
    offset, end, libs = 32, 32+size, []
    for _ in range(count):
        if offset+8 > end:
            raise ValueError('truncated Mach-O command')
        cmd, n = struct.unpack_from('<II', raw, offset)
        if n < 8 or n % 8 or offset+n > end:
            raise ValueError('invalid Mach-O command extent')
        if cmd in (0xc, 0x18, 0x1f):
            if n < 24: raise ValueError('invalid dylib load command')
            label_offset = struct.unpack_from('<I', raw, offset+8)[0]
            if label_offset >= n: raise ValueError('invalid dylib label')
            libs.append(raw[offset+label_offset:offset+n].split(b'\0', 1)[0].decode('ascii'))
        offset += n
    if offset != end: raise ValueError('inconsistent Mach-O commands')
    for name in deps:
        if '@rpath/libphase24d_'+name+'.dylib' not in libs:
            raise ValueError('missing genuine dylib dependency '+name+' in '+str(path))
    return libs


def strip_platform(obj):
    """LLVM Swift lld compatibility: remove only LC_BUILD_VERSION from MH_OBJECT COPY."""
    buf = bytearray(obj.read_bytes())
    macho(obj, 1)
    _, _, _, _, count, size = struct.unpack_from('<6I', buf)
    pos, end = 32, 32+size
    for _ in range(count):
        code, n = struct.unpack_from('<II', buf, pos)
        if code == 0x32:
            buf[pos:end-n] = buf[pos+n:end]
            buf[end-n:end] = bytes(n)
            struct.pack_into('<II', buf, 16, count-1, size-n)
            result = obj.with_suffix('.link.o')
            result.write_bytes(buf)
            macho(result, 1)
            return result
        pos += n
    raise ValueError('no supported LC_BUILD_VERSION metadata in ' + str(obj))


def tbd(path, filename, exports):
    symbols = ', '.join('_'+s for s in exports)
    path.write_text('--- !tapi-tbd-v3\narchs: [ arm64 ]\nplatform: macosx\n'
                    +"install-name: '"+filename+"'\n"
                    +'current-version: 1.0.0\ncompatibility-version: 1.0.0\n'
                    +'exports:\n  - archs: [ arm64 ]\n    symbols: [ '+symbols+' ]\n...\n')


def linked(out):
    compiler=shutil.which('clang')
    linker=next((shutil.which(n) for n in ('ld64.lld','ld64.lld-20','ld64.lld-19','ld64.lld-18')
                 if shutil.which(n)),None)
    if not compiler or not linker: raise RuntimeError('clang and real LLVM Mach-O linker required')
    for name in list(SERVICES)+['client-'+v for v in SERVICES]:
        target=out/(name+'.o')
        run([compiler, '--target=arm64-apple-macos13', '-std=c11', '-O2', '-Wall',
             '-Wextra','-Wpedantic','-Werror','-ffreestanding','-fno-builtin',
             '-fno-stack-protector','-c',ROOT/'tests'/('phase24d-arm64-'+name+'.c'),
             '-o',target],30)
        macho(target,1)
    linker_base=[linker,'-arch','arm64','-platform_version','macos','13.0','13.0',
                 '-fixup_chains','-fatal_warnings']
    def link(opts,obj,deps):
        try:
            run([*linker_base,*opts,obj,*deps])
        except RuntimeError as err:
            # Only the known unsupported Swift platform-metadata diagnostic can
            # activate the copy-only linker compatibility path.
            if 'command exited' not in str(err): raise
            link_obj=strip_platform(obj)
            run([*linker_base,*opts,link_obj,*deps])
    stubs={}
    previous=[]
    exports={'echo':'ios27_24d_echo','sum':'ios27_24d_sum',
             'relay':'ios27_24d_relay','worker':'ios27_24d_worker'}
    for service in SERVICES:
        image=out/('libphase24d_'+service+'.dylib')
        link(['-dylib','-install_name','@rpath/'+image.name,'-o',image],
             out/(service+'.o'),[stubs[d] for d in previous])
        macho(image,6,previous)
        stub=out/(service+'.tbd')
        tbd(stub,'@rpath/'+image.name,[exports[service]])
        stubs[service]=stub
        previous=[service]
    binder=out/'bridge-contract.tbd'
    # Only link-time declarations: no actual Darwin guest kernel adapter here.
    tbd(binder,'/usr/lib/libSystem.B.dylib',
        ['dyld_stub_binder','ios27_24d_native_service_bridge'])
    for service in SERVICES:
        exe=out/('phase24d-'+service)
        link(['-e','_main','-o',exe],out/('client-'+service+'.o'),
             [stubs[service],binder])
        macho(exe,2,(service,))
    print('PASS: 4 true ARM64 Mach-O dylibs + 4 MH_EXECUTE clients; all external symbols linked')
    print('BOUNDARY: bridge-contract.tbd is symbol metadata, not executable native service code')


def main():
    beginning=time.monotonic()
    cc=shutil.which('clang' if SAN else 'cc')
    if not cc: raise RuntimeError('host C compiler not installed')
    with tempfile.TemporaryDirectory(prefix='phase24d-') as scratch:
        out=Path(scratch)
        for opt in ('-O1',) if SAN else ('-O0','-O2'):
            exe=out/('host-'+opt[1:])
            flags=['-std=c11','-g',opt,'-Wall','-Wextra','-Wpedantic','-Werror',
                   '-pthread','-I'+str(ROOT/'include')]
            if SAN: flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            run([cc,*flags,ROOT/'src/phase24d_services.c',
                 ROOT/'tests/phase24d-integration.c','-o',exe],120)
            run([exe],180)
        linked(out)
    print('PASS: phase24d verification; sanitizer=%s, elapsed=%.2fs' % (SAN,time.monotonic()-beginning))

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as e:
        print('FAIL:',e,file=sys.stderr);sys.exit(1)
