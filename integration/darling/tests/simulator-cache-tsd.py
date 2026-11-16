#!/usr/bin/env python3
"""Sanitizer checks for the actual mapped-cache TSD adapter."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--source', type=Path, help='patched Darling root')
    sources.add_argument('--fetch-pinned', action='store_true')
    parser.add_argument('--cxx', default='clang++')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ios27-cache-tsd-') as temporary:
        work = Path(temporary)
        if args.fetch_pinned:
            spec = importlib.util.spec_from_file_location('tsd_inputs', HERE / 'simulator-thread-registers.py')
            inputs = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = inputs
            spec.loader.exec_module(inputs)
            source = work / 'source'
            inputs.fetch_pinned(source, [('src/external/dyld', 'src/simulator_cache_tsd.h')])
        else:
            source = args.source.resolve(strict=True)
        header = source / 'src/external/dyld/src/simulator_cache_tsd.h'
        for directory, name in [('mach-o', 'loader.h'), ('libkern', 'OSCacheControl.h')]:
            (work / directory).mkdir()
            (work / directory / name).write_text('#pragma once\n')
        binary = work / 'test'
        subprocess.run([args.cxx, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(work),
                        '-DSIMULATOR_CACHE_TSD_HEADER="' + str(header) + '"',
                        str(HERE / 'simulator-cache-tsd.cpp'), '-o', str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
