#!/usr/bin/env python3
"""Sanitizer checks for the actual bounded spawn-policy data handoff."""
import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
sources = parser.add_mutually_exclusive_group(required=True)
sources.add_argument('--source', type=Path, help='patched XNU root')
sources.add_argument('--fetch-pinned', action='store_true', help='reconstruct the new header from maintained patches')
parser.add_argument('--cc', default='clang')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='ios27-spawn-policy-') as temporary:
    work = Path(temporary)
    if args.fetch_pinned:
        source = work / 'source'
        source.mkdir()
        manifest = json.loads((HERE.parent / 'source-patches.json').read_text())
        relative = 'libsyscall/wrappers/darling_spawn_policy.h'
        for entry in manifest['patches']:
            if entry['directory'] != 'src/external/xnu':
                continue
            patch = HERE.parent / entry['path']
            if relative in re.findall(r'^\+\+\+ b/(.+)$', patch.read_text(), re.MULTILINE):
                subprocess.run(['git', '-C', str(source), 'apply', '--include=' + relative, str(patch)], check=True, timeout=15)
        expected = manifest['files']['src/external/xnu/' + relative]
        if sha256((source / relative).read_bytes()).hexdigest() != expected:
            raise ValueError('spawn policy header hash mismatch')
        print('Maintained spawn policy header hash matches', flush=True)
    else:
        source = args.source.resolve(strict=True)
    binary = work / 'test'
    subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(source / 'libsyscall/wrappers'),
                    str(Path(__file__).with_suffix('.c')), '-o', str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
