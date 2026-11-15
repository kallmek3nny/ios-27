#!/usr/bin/env python3
"""Check the actual simulator TSD adapter's word encodings, bounds and W^X transitions.

Requires Linux host C/C++ compilers. Synthetic ARM64 words are data only.
--fetch-pinned downloads three hash-checked open-source files and reapplies the
relevant maintained patches; it does not require a simulator runtime or phone.
"""
import argparse
from collections import defaultdict
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent


def fetch_pinned(root):
    inputs = json.loads((HERE / 'thread-register-inputs.json').read_text())
    patches = json.loads((HERE.parent / 'source-patches.json').read_text())
    if inputs['darling_revision'] != patches['base_revision']:
        raise ValueError('thread-register input revision does not match the source patches')
    groups = defaultdict(list)
    for entry in inputs['files']:
        url = 'https://raw.githubusercontent.com/' + '/'.join(
            (entry['repository'], entry['revision'], entry['path']))
        with urlopen(url, timeout=30) as response:
            data = response.read(2 * 1024 * 1024)
        if sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('source hash mismatch: ' + entry['path'])
        destination = root / entry['directory'] / entry['path']
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        groups[entry['directory']].append(entry['path'])
    for entry in patches['patches']:
        paths = groups.get(entry['directory'])
        if not paths:
            continue
        patch = HERE.parent / entry['path']
        changed = set(re.findall(r'^(?:\+\+\+ b/|--- a/)(.+)$', patch.read_text(), re.MULTILINE))
        if not changed.intersection(paths):
            continue
        subprocess.run(['git', '-C', str(root / entry['directory']), 'apply',
                        *['--include=' + path for path in paths], str(patch)], check=True, timeout=15)
    for entry in inputs['files']:
        relative = Path(entry['directory']) / entry['path']
        expected = patches['files'][relative.as_posix()]
        if sha256((root / relative).read_bytes()).hexdigest() != expected:
            raise ValueError('patched source hash mismatch: ' + str(relative))
    print('Pinned TSD inputs: all three original and patched source hashes match', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument('--source', type=Path, help='patched Darling source root')
    source_group.add_argument('--fetch-pinned', action='store_true')
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--cxx', default='clang++')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ios27-simulator-tsd-') as temporary:
        output = Path(temporary)
        if args.fetch_pinned:
            root = output / 'source'
            fetch_pinned(root)
        else:
            root = args.source.resolve(strict=True)
        source = (root / 'src/external/dyld/src/dyld2.cpp').read_text()
        marker = '// dyld_sim is mapped here rather than by mldr'
        if source.count(marker) != 1:
            raise ValueError('expected one simulator TSD adaptation block')
        position = source.index(marker)
        start = source.rindex('#if defined(DARLING) && __arm64__', 0, position)
        end = source.index('\n#endif', position) + len('\n#endif')
        extracted = output / 'simulator-tsd.inc'
        extracted.write_text(source[start:end] + '\n')
        flags = ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all', '-I' + str(root / 'src/startup/mldr')]
        subprocess.run([args.cc, '-std=gnu11', *flags, str(root / 'tests/mldr/tsd_rewrite.c'),
                        '-o', str(output / 'words')], check=True, timeout=60)
        subprocess.run([str(output / 'words')], check=True, timeout=30)
        subprocess.run([args.cxx, '-std=c++17', *flags, '-DSIMULATOR_TSD_BLOCK="' + str(extracted) + '"',
                        str(HERE / 'simulator-thread-registers.cpp'), '-o', str(output / 'mapping')], check=True, timeout=60)
        subprocess.run([str(output / 'mapping')], check=True, timeout=30)


if __name__ == '__main__':
    main()
