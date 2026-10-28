#!/usr/bin/env python3
"""Independent -Werror compiled native and Clang-produced ARM64 Darwin checks."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src/libsystem21h_lifecycle.c'
TESTS = ('native', 'cancel', 'races')


def execute(args, seconds=120, env=None):
    print('+', ' '.join(map(str, args)), flush=True)
    completed = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, timeout=seconds, check=False, env=env)
    if completed.returncode:
        raise RuntimeError('exit {}: {}'.format(completed.returncode, completed.stdout))
    if completed.stdout.strip():
        print(completed.stdout.strip(), flush=True)


def main():
    started = time.monotonic()
    cc = os.environ.get('CC', 'cc')
    clang = shutil.which('clang')
    if not shutil.which(cc) or not clang:
        raise RuntimeError('a C compiler and Clang are required')
    common = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
              '-pthread', '-I' + str(ROOT / 'include')]
    with tempfile.TemporaryDirectory(prefix='phase21h-') as tmp:
        scratch = Path(tmp)
        for optimization in ('-O0', '-O2'):
            for name in TESTS:
                exe = scratch / ('phase21h-' + name + optimization)
                execute([cc, *common, optimization, str(SRC),
                         str(ROOT / 'tests' / ('phase21h-' + name + '.c')),
                         '-o', str(exe)], 120)
                execute([str(exe)], 180)
            rollover = scratch / ('rollover' + optimization)
            execute([cc, *common, optimization, '-DIOS27_21H_MAX_GENERATION=3',
                     str(SRC), str(ROOT / 'tests/phase21h-rollover.c'),
                     '-o', str(rollover)], 120)
            execute([str(rollover)], 180)
            injected = scratch / ('fault-source' + optimization + '.o')
            execute([cc, *common, optimization,
                     '-Dpthread_cond_init=ios27_21h_test_cond_init',
                     '-c', str(SRC), '-o', str(injected)], 120)
            faulty = scratch / ('fault' + optimization)
            execute([cc, *common, optimization, str(injected),
                     str(ROOT / 'tests/phase21h-fault.c'), '-o', str(faulty)], 120)
            execute([str(faulty)], 180)
        if os.environ.get('PHASE21H_SANITIZE') == '1':
            for name in TESTS:
                exe = scratch / ('sanitized-' + name)
                execute([clang, *common, '-O1', '-g', '-fno-omit-frame-pointer',
                         '-fsanitize=address,undefined', str(SRC),
                         str(ROOT / 'tests' / ('phase21h-' + name + '.c')),
                         '-o', str(exe)], 120)
                execute([str(exe)], 180,
                        dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                             UBSAN_OPTIONS='halt_on_error=1'))
        output = scratch / 'phase21h-darwin-arm64.o'
        execute([clang, '--target=arm64-apple-darwin', '-ffreestanding',
                 '-fno-builtin', '-fno-stack-protector', '-O2', '-Werror',
                 '-I' + str(ROOT / 'include'), '-c',
                 str(ROOT / 'tests/phase21h-darwin-arm64.c'), '-o', str(output)], 60)
        generated = output.read_bytes()
        if generated[:4] != bytes.fromhex('cffaedfe'):
            raise RuntimeError('expected compiled ARM64 Mach-O MH_OBJECT magic')
        for name in (b'_ios27_21h_object_create\x00', b'_ios27_21h_lock\x00',
                     b'_ios27_21h_unlock\x00', b'_ios27_21h_object_close\x00',
                     b'_ios27_21h_object_destroy\x00'):
            if name not in generated:
                raise RuntimeError('missing compiled Darwin callsite: ' + repr(name))
        print('phase21h: real ARM64 Darwin Mach-O relocation symbols checked')
    print('phase21h: compiled checks passed; elapsed {:.2f}s'.format(time.monotonic() - started))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        sys.exit('phase21h failure: {}'.format(exc))
