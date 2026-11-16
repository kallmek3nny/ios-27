#!/usr/bin/env python3
"""Verify host dyld's actual simulator argument-vector construction."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True, help='patched dyld source directory')
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
source = (args.source / 'src/dyld2.cpp').read_text()
start = '\tconst char** argvParams = argv;'
end = '\t// <rdar://problem/5077374> have host dyld detach macOS shared cache'
assert source.count(start) == source.count(end) == 1
block = source[source.index(start):source.index(end)]
with tempfile.TemporaryDirectory(prefix='ios27-simulator-args-') as temporary:
    work = Path(temporary)
    extracted = work / 'arguments.inc'
    extracted.write_text(block)
    subprocess.run([args.cxx, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-DSIMULATOR_ARGS_BLOCK="' + str(extracted) + '"',
                    str(Path(__file__).with_suffix('.cpp')), '-o', str(work / 'test')], check=True, timeout=60)
    subprocess.run([str(work / 'test')], check=True, timeout=30)
