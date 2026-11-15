#!/usr/bin/env python3
"""Check patched Darling shared-cache signatures using native host sanitizers.

Requires Linux host C/C++ compilers, OpenSSL and libxml2 development files.
Optional --cache files are read-only inputs for signature metadata checks.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='patched darlingserver source directory')
    parser.add_argument('--host-prefix', type=Path, help='private usr prefix for host development packages')
    parser.add_argument('--cache', action='append', type=Path, default=[])
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--cxx', default='clang++')
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    tests = Path(__file__).resolve().parent
    env = dict(os.environ, CC=args.cc, CXX=args.cxx, SANITIZE='address,undefined')
    if args.host_prefix:
        prefix = args.host_prefix.resolve(strict=True)
        library = str(prefix / 'lib/x86_64-linux-gnu')
        env.update(PATH=str(prefix / 'bin') + ':' + env['PATH'],
                   CPATH=str(prefix / 'include') + ':' + str(prefix / 'include/x86_64-linux-gnu'),
                   LIBRARY_PATH=library, LD_LIBRARY_PATH=library,
                   PKG_CONFIG_PATH=library + '/pkgconfig', PKG_CONFIG_SYSROOT_DIR=str(prefix.parent))

    def run(command):
        subprocess.run(list(map(str, command)), cwd=source, env=env, check=True, timeout=120)

    common = ['-g', '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
              '-I' + str(source / 'include'), '-I' + str(source / 'internal-include'),
              '-I' + str(source / 'duct-tape/include'), '-idirafter', str(source / 'duct-tape/xnu/osfmk'),
              '-idirafter', str(source / 'duct-tape/xnu/bsd')]
    xml = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libxml-2.0'],
                                             env=env, text=True, timeout=15))
    with tempfile.TemporaryDirectory(prefix='ios27-cache-signature-') as temporary:
        output = Path(temporary)
        run([args.cc, '-std=gnu11', *common, '-Wall', '-Wextra', '-Werror',
             tests / 'shared-cache-signature.c', source / 'src/codesign/dc_codesign.c',
             '-lcrypto', '-o', output / 'parser'])
        run([output / 'parser', output / 'synthetic-cache'])
        objects = []
        for name, filename in [('codesign', 'src/codesign/dc_codesign.c'),
                               ('entitlements', 'src/codesign/dc_entitlements.c'), ('csr', 'duct-tape/src/csr.c')]:
            target = output / (name + '.o')
            run([args.cc, '-std=gnu11', *common, *[v for v in xml if v.startswith('-I')],
                 '-c', source / filename, '-o', target])
            objects.append(target)
        run([args.cxx, '-std=c++17', *common, tests / 'shared-cache-policy.cpp',
             source / 'src/code-signing.cpp', *objects, '-lcrypto', *xml, '-o', output / 'policy'])
        run([output / 'policy', output / 'synthetic-cache', *[p.resolve(strict=True) for p in args.cache]])
    for topic in ('codesign', 'code_signing'):
        run(['bash', source / 'tests/run-host.sh', topic])


if __name__ == '__main__':
    main()
