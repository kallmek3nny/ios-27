#!/usr/bin/env python3
"""Run a real optimized, cross-pthread fiber migration contract."""
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
sources.add_argument('--source', type=Path, help='patched darlingserver root')
sources.add_argument('--fetch-pinned', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='ios27-fiber-tls-') as temporary:
    work = Path(temporary)
    if args.fetch_pinned:
        source = work / 'source'
        source.mkdir()
        manifest = json.loads((HERE.parent / 'source-patches.json').read_text())
        relative = 'include/darlingserver/fiber-tls.hpp'
        for entry in manifest['patches']:
            if entry['directory'] != 'src/external/darlingserver':
                continue
            patch = HERE.parent / entry['path']
            if relative in re.findall(r'^\+\+\+ b/(.+)$', patch.read_text(), re.MULTILINE):
                subprocess.run(['git', '-C', str(source), 'apply', '--include=' + relative, str(patch)], check=True)
        if sha256((source / relative).read_bytes()).hexdigest() != manifest['files']['src/external/darlingserver/' + relative]:
            raise ValueError('fiber TLS header hash mismatch')
    else:
        source = args.source.resolve(strict=True)
    binary = work / 'test'
    subprocess.run(['clang++', '-std=c++17', '-O3', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-I' + str(source / 'include'), str(HERE / 'fiber-tls.cpp'), '-o', str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=15)
