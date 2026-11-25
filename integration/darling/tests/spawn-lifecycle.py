#!/usr/bin/env python3
"""Sanitizer contracts for the actual external-libSystem spawn lifecycle."""
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
    inputs = json.loads((HERE / 'spawn-lifecycle-inputs.json').read_text())
    manifest = json.loads((HERE.parent / 'source-patches.json').read_text())
    if inputs['darling_revision'] != manifest['base_revision']:
        raise ValueError('spawn input revision does not match the source patches')
    entry = inputs['source']
    url = 'https://raw.githubusercontent.com/' + '/'.join(
        (entry['repository'], entry['revision'], entry['path']))
    with urlopen(url, timeout=30) as response:
        data = response.read(2 * 1024 * 1024)
    if sha256(data).hexdigest() != entry['sha256']:
        raise ValueError('spawn callback input hash mismatch')
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
    for path in paths:
        expected = manifest['files']['src/external/xnu/' + path]
        if sha256((root / path).read_bytes()).hexdigest() != expected:
            raise ValueError('patched spawn source hash mismatch: ' + path)
    print(f'Pinned spawn input and {len(paths)} patched hashes match', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--source', type=Path, help='patched XNU root')
    sources.add_argument('--fetch-pinned', action='store_true')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ios27-spawn-lifecycle-') as temporary:
        work = Path(temporary)
        if args.fetch_pinned:
            source = work / 'source'
            fetch_pinned(source)
        else:
            source = args.source.resolve(strict=True)
        emulation = source / 'darling/src/libsystem_kernel/emulation'
        process = emulation / 'src/xnu_syscall/bsd/impl/process'
        (work / 'mach').mkdir()
        (work / 'mach/mach.h').write_text('#pragma once\n#include <stdint.h>\n'
            'typedef unsigned int mach_port_t;\ntypedef int kern_return_t;\n'
            'typedef int boolean_t;\ntypedef uintptr_t voucher_mach_msg_state_t;\n')
        (work / 'mach/message.h').write_text('#pragma once\ntypedef struct { unsigned int bits; } mach_msg_header_t;\n')
        (work / 'mach/mach_types.h').write_text('#pragma once\n')
        implementation = work / 'selector.inc'
        implementation.write_text(re.sub(r'^#include[^\n]*\n', '',
            (process / 'spawn_lifecycle.c').read_text(), flags=re.MULTILINE))
        binary = work / 'test'
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I' + str(work), '-I' + str(process), '-I' + str(source / 'libsyscall/wrappers'),
                        '-I' + str(emulation / 'include/xnu_syscall/bsd/impl/process'),
                        '-DSPAWN_LIFECYCLE_IMPLEMENTATION="' + str(implementation) + '"',
                        str(HERE / 'spawn-lifecycle.c'), '-o', str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
