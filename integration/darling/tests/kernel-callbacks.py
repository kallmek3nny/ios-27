#!/usr/bin/env python3
"""Host ASan/UBSan checks for the actual patched libkernel callback ABI."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True, help='patched XNU source directory')
parser.add_argument('--cc', default='clang')
args = parser.parse_args()
headers = args.source.resolve(strict=True) / 'libsyscall/wrappers'
with tempfile.TemporaryDirectory(prefix='ios27-kernel-callbacks-') as temporary:
    work = Path(temporary)
    (work / 'mach').mkdir()
    # Only unrelated Mach argument types are declared here. Both callback
    # layout and normalization implementation come from the patched source.
    (work / 'mach/mach.h').write_text('#pragma once\n#include <stdint.h>\n'
        'typedef unsigned int mach_port_t;\ntypedef int kern_return_t;\n'
        'typedef int boolean_t;\ntypedef uintptr_t voucher_mach_msg_state_t;\n')
    (work / 'mach/message.h').write_text('#pragma once\ntypedef struct { unsigned int bits; } mach_msg_header_t;\n')
    (work / 'mach/mach_types.h').write_text('#pragma once\n')
    subprocess.run([args.cc, '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(work), '-I' + str(headers),
                    str(Path(__file__).with_suffix('.c')), '-o', str(work / 'test')], check=True, timeout=60)
    subprocess.run([str(work / 'test')], check=True, timeout=30)
