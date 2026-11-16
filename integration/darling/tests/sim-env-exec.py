#!/usr/bin/env python3
"""Exercise environment/argument transfer and rejection before exec."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--program', required=True, type=Path)
args = parser.parse_args()
program = args.program.resolve(strict=True)
with tempfile.TemporaryDirectory(prefix='ios27-sim-env-') as temporary:
    root = Path(temporary)
    environment = root / 'sim.env'
    environment.write_bytes(b'IOS27_A=value with spaces\r\nIOS27_B=x=y\nIOS27_EMPTY=\n')
    witness = ('import os,sys; assert os.environ["IOS27_A"]=="value with spaces"; '
               'assert os.environ["IOS27_B"]=="x=y"; assert os.environ["IOS27_EMPTY"]==""; '
               'assert sys.argv[1:]==["one","two words"]; print("ENV_EXEC_OK")')
    result = subprocess.run([str(program), str(environment), sys.executable, '-c', witness, 'one', 'two words'],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0 and result.stdout.strip() == 'ENV_EXEC_OK', result
    marker = root / 'executed'
    forbidden = ('from pathlib import Path; Path(' + repr(str(marker)) + ').write_text("executed")')
    invalid = [b'NO_EQUALS\n', b'bad-key=x\n', b'__mldr_sockpath=x\n', b'LD_PRELOAD=x\n',
               b'LD_LIBRARY_PATH=x\n', b'KEY=x\x00hidden\n', b'KEY=' + b'x' * 8192,
               b'KEY=x\n' * 257, b'x' * 65537]
    for content in invalid:
        environment.write_bytes(content)
        result = subprocess.run([str(program), str(environment), sys.executable, '-c', forbidden],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 3 and not marker.exists(), result
print('10 environment/exec contract checks passed')
