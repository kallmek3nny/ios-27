#!/usr/bin/env python3
"""Install and exercise only the original ios-27 debug host on a named device.

No root, device configuration changes, external guest imports or Apple code.
Reports intentionally omit the device serial. Requires an authorized ADB device.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--serial', required=True)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--apk', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--screenshot', type=Path)
    args = parser.parse_args()

    def adb(*parts, check=True):
        return subprocess.run([args.adb, '-s', args.serial, *parts],
                              capture_output=True, timeout=30, check=check)

    def prop(name):
        return adb('shell', 'getprop', name).stdout.decode().strip()

    report = {'apk_sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest(),
              'model': prop('ro.product.model'), 'sdk': prop('ro.build.version.sdk'),
              'abi': prop('ro.product.cpu.abi'),
              'page_size': adb('shell', 'getconf', 'PAGE_SIZE').stdout.decode().strip(),
              'runs': [], 'passed': False}
    try:
        installed = adb('install', '-r', str(args.apk))
        if b'Success' not in installed.stdout:
            raise RuntimeError('APK installation did not report success')
        for _ in range(2):
            adb('shell', 'am', 'force-stop', 'org.ios27.host')
            # Wait for process death before removing the old result; a
            # finishing previous worker must not publish a stale completion.
            for attempt in range(20):
                if adb('shell', 'pidof', 'org.ios27.host', check=False).returncode:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError('Previous app process did not stop')
            adb('shell', 'run-as', 'org.ios27.host', 'rm', '-f', 'files/last-result.txt')
            launch = adb('shell', 'am', 'start', '-W', '-n',
                         'org.ios27.host/.MainActivity', '--ez', 'run_fixture', 'true')
            if b'Error:' in launch.stdout or b'Error:' in launch.stderr:
                raise RuntimeError('Activity launch failed')
            deadline = time.monotonic() + 16
            while time.monotonic() < deadline:
                result = adb('shell', 'run-as', 'org.ios27.host', 'cat',
                             'files/last-result.txt', check=False)
                if result.returncode == 0:
                    break
                time.sleep(0.25)
            else:
                raise RuntimeError('No fresh result before device deadline')
            text = result.stdout.decode('utf-8', errors='replace')
            report['runs'].append(text)
            if ('PASS: native ARM64 Mach-O returned 42.' not in text or
                    'Launcher exit: 0' not in text or 'Fixture SHA-256 verified' not in text or
                    not re.search(r'Static internal pointer rebases: [1-9][0-9]*', text)):
                raise RuntimeError('Native execution failed: ' + text)
        report['passed'] = True
        if args.screenshot:
            # Native execution may finish before Android's launch animation.
            time.sleep(1)
            png = adb('exec-out', 'screencap', '-p').stdout
            if not png.startswith(b'\x89PNG\r\n\x1a\n'):
                raise RuntimeError('Device did not return a PNG screenshot')
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            args.screenshot.write_bytes(png)
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS: two fresh native ARM64 executions; report: ' + str(args.output))


if __name__ == '__main__':
    main()
