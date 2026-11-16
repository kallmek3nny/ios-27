#!/usr/bin/env python3
"""Sanitizer contracts for the actual bounded asynchronous kqueue adapter."""
import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent


def fetch_pinned(root):
    inputs = json.loads((HERE / 'kevent-workqueue-inputs.json').read_text())
    manifest = json.loads((HERE.parent / 'source-patches.json').read_text())
    if inputs['darling_revision'] != manifest['base_revision']:
        raise ValueError('workqueue input revision does not match the source patches')
    entry = inputs['source']
    url = 'https://raw.githubusercontent.com/' + '/'.join(
        (entry['repository'], entry['revision'], entry['path']))
    with urlopen(url, timeout=30) as response:
        data = response.read(2 * 1024 * 1024)
    if sha256(data).hexdigest() != entry['sha256']:
        raise ValueError('workqueue header hash mismatch')
    destination = root / entry['path']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    paths = inputs['patched_files']
    for patch in manifest['patches']:
        if patch['directory'] != 'src/external/xnu':
            continue
        filename = HERE.parent / patch['path']
        changed = set(re.findall(r'^(?:\+\+\+ b/|--- a/)(.+)$', filename.read_text(), re.MULTILINE))
        if changed.intersection(paths):
            subprocess.run(['git', '-C', str(root), 'apply',
                            *['--include=' + path for path in paths], str(filename)], check=True, timeout=15)
    for relative in paths:
        expected = manifest['files']['src/external/xnu/' + relative]
        if sha256((root / relative).read_bytes()).hexdigest() != expected:
            raise ValueError('patched workqueue hash mismatch: ' + relative)
    print('Pinned workqueue header and both patched hashes match', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--source', type=Path, help='patched XNU source root')
    sources.add_argument('--fetch-pinned', action='store_true')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ios27-kevent-workqueue-') as temporary:
        work = Path(temporary)
        if args.fetch_pinned:
            root = work / 'source'
            fetch_pinned(root)
        else:
            root = args.source.resolve(strict=True)
        source = root / 'darling/src/libsystem_kernel/emulation'
        header = source / 'include/xnu_syscall/bsd/impl/bsdthread/workq_kernreturn.h'
        implementation = source / 'src/xnu_syscall/bsd/impl/kqueue/kevent_workq.c'
        # Only platform includes are replaced. The actual adapter and delivery
        # declaration are compiled; syscalls and thread creation are fixtures.
        (work / 'sys').mkdir()
        (work / 'sys/event.h').write_text('#pragma once\n')
        extracted = work / 'adapter.inc'
        extracted.write_text(re.sub(r'^#include[^\n]*\n', '', implementation.read_text(), flags=re.MULTILINE))
        binary = work / 'test'
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(work),
                        '-DWORKQUEUE_HEADER="' + str(header) + '"',
                        '-DKEVENT_WORKQUEUE_BLOCK="' + str(extracted) + '"',
                        str(HERE / 'kevent-workqueue.c'), '-o', str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
