#!/usr/bin/env python3
"""ASan/UBSan contract for the actual pthread workgroup callback bridge."""
import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import tempfile
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent


def fetch_pinned(root):
    inputs = json.loads((HERE / 'pthread-workgroup-inputs.json').read_text())
    manifest = json.loads((HERE.parent / 'source-patches.json').read_text())
    if inputs['darling_revision'] != manifest['base_revision']:
        raise ValueError('workgroup input revision does not match the source patches')
    entry = inputs['source']
    url = 'https://raw.githubusercontent.com/' + '/'.join(
        (entry['repository'], entry['revision'], entry['path']))
    with urlopen(url, timeout=30) as response:
        data = response.read(2 * 1024 * 1024)
    if sha256(data).hexdigest() != entry['sha256']:
        raise ValueError('workgroup input hash mismatch')
    destination = root / entry['path']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    for patch in manifest['patches']:
        if patch['directory'] == 'src/external/libpthread':
            subprocess.run(['git', '-C', str(root), 'apply', str(HERE.parent / patch['path'])],
                           check=True, timeout=15)
    for relative in ('src/pthread.c', 'private/pthread/workgroup_private.h'):
        expected = manifest['files']['src/external/libpthread/' + relative]
        if sha256((root / relative).read_bytes()).hexdigest() != expected:
            raise ValueError('patched workgroup hash mismatch: ' + relative)
    print('Pinned workgroup source and both patched hashes match', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--source', type=Path, help='patched libpthread source directory')
    sources.add_argument('--fetch-pinned', action='store_true')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ios27-pthread-workgroup-') as temporary:
        work = Path(temporary)
        if args.fetch_pinned:
            source = work / 'source'
            fetch_pinned(source)
        else:
            source = args.source.resolve(strict=True)
        text = (source / 'src/pthread.c').read_text()
        begin, end = '// DARLING_WORKGROUP_CALLBACKS_BEGIN', '// DARLING_WORKGROUP_CALLBACKS_END'
        if text.count(begin) != 1 or text.count(end) != 1:
            raise ValueError('expected one workgroup callback block')
        block = work / 'workgroup.inc'
        block.write_text(text.split(begin, 1)[1].split(end, 1)[0])
        # Only platform type/annotation declarations are replaced. The table
        # layout and both exported function bodies come from patched sources.
        (work / 'pthread').mkdir()
        (work / 'pthread/pthread.h').write_text(
            '#pragma once\n#define SPI_AVAILABLE(...)\n'
            'typedef struct test_thread *pthread_t;\n'
            'typedef struct test_attr pthread_attr_t;\n')
        (work / 'os').mkdir()
        (work / 'os/workgroup.h').write_text(
            '#pragma once\ntypedef struct test_workgroup *os_workgroup_t;\n')
        binary = work / 'test'
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-Wno-nullability-extension', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I' + str(work), '-I' + str(source / 'private'),
                        '-DPTHREAD_WORKGROUP_BLOCK="' + str(block) + '"',
                        str(HERE / 'pthread-workgroup.c'), '-o', str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)
        subprocess.run([str(binary), 'future'], check=True, timeout=30)


if __name__ == '__main__':
    main()
