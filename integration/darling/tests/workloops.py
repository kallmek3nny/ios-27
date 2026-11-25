#!/usr/bin/env python3
"""Sanitizer checks for the actual native workloop and kevent adapters."""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--source', type=Path, help='patched XNU root')
    sources.add_argument('--fetch-pinned', action='store_true')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ios27-workloops-') as temporary:
        work = Path(temporary)
        if args.fetch_pinned:
            spec = importlib.util.spec_from_file_location('kevent_inputs', HERE / 'kevent-workqueue.py')
            inputs = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = inputs
            spec.loader.exec_module(inputs)
            root = work / 'source'
            inputs.fetch_pinned(root)
        else:
            root = args.source.resolve(strict=True)
        source = root / 'darling/src/libsystem_kernel/emulation'
        (work / 'sys').mkdir()
        (work / 'sys/event.h').write_text('#pragma once\n')
        flags = []
        for name, macro in [('kevent_workq', 'KEVENT_WORKQUEUE_BLOCK'), ('kevent_id', 'WORKLOOP_BLOCK')]:
            text = (source / 'src/xnu_syscall/bsd/impl/kqueue' / (name + '.c')).read_text()
            extracted = work / (name + '.inc')
            extracted.write_text(re.sub(r'^#include[^\n]*\n', '', text, flags=re.MULTILINE))
            flags.append('-D' + macro + '="' + str(extracted) + '"')
        header = source / 'include/xnu_syscall/bsd/impl/bsdthread/workq_kernreturn.h'
        binary = work / 'test'
        subprocess.run([args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(work),
                        '-DWORKQUEUE_HEADER="' + str(header) + '"', *flags,
                        str(HERE / 'workloops.c'), '-o', str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
