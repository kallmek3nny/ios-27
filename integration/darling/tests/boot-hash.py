#!/usr/bin/env python3
"""ASan/UBSan and independent-process checks of mldr executable boot identity."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True, help='patched Darling source root')
parser.add_argument('--host-prefix', type=Path)
parser.add_argument('--cc', default='clang')
args = parser.parse_args()
source = args.source.resolve(strict=True) / 'src/startup/mldr'
env = dict(os.environ)
if args.host_prefix:
    prefix = args.host_prefix.resolve(strict=True)
    env.update(CPATH=str(prefix / 'include') + ':' + str(prefix / 'include/x86_64-linux-gnu'),
               LIBRARY_PATH=str(prefix / 'lib/x86_64-linux-gnu'),
               LD_LIBRARY_PATH=str(prefix / 'lib/x86_64-linux-gnu'))
with tempfile.TemporaryDirectory(prefix='ios27-boot-hash-') as temporary:
    work = Path(temporary)
    data = work / 'data'
    data.mkdir(mode=0o700)
    subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(source),
                    str(Path(__file__).with_suffix('.c')), str(source / 'boot_hash.c'), '-lcrypto',
                    '-o', str(work / 'test')], env=env, check=True, timeout=60)
    subprocess.run([str(work / 'test'), str(data)], env=env, check=True, timeout=30)
