#!/usr/bin/env python3
"""Check the actual native mach_msg2 adapter under ASan/UBSan."""
import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
sources = parser.add_mutually_exclusive_group(required=True)
sources.add_argument('--source', type=Path)
sources.add_argument('--fetch-pinned', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='ios27-mach-msg2-') as temporary:
    work = Path(temporary)
    if args.fetch_pinned:
        source = work / 'source'
        source.mkdir()
        integration = Path(__file__).resolve().parents[1]
        manifest = json.loads((integration / 'source-patches.json').read_text())
        relative = 'libsyscall/mach/darling_mach_msg2.h'
        for entry in manifest['patches']:
            if entry['directory'] != 'src/external/xnu':
                continue
            patch = integration / entry['path']
            if relative in re.findall(r'^\+\+\+ b/(.+)$', patch.read_text(), re.MULTILINE):
                subprocess.run(['git', '-C', str(source), 'apply', '--include=' + relative, str(patch)], check=True)
        if sha256((source / relative).read_bytes()).hexdigest() != manifest['files']['src/external/xnu/' + relative]:
            raise ValueError('Mach message adapter hash mismatch')
    else:
        source = args.source.resolve(strict=True)
    binary = work / 'test'
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-DMACH_MSG2_HEADER="' + str(source / 'libsyscall/mach/darling_mach_msg2.h') + '"',
                    str(Path(__file__).with_suffix('.c')), '-o', str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
