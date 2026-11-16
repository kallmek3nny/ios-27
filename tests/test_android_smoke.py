"""Offline Android smoke contract tests; every subprocess call is mocked."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('android_smoke', ROOT / 'tools/android-smoke.py')
smoke = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(smoke)


def result_text(mode='success'):
    timeout_ms, passed = smoke.MODES[mode]
    return (f'Fixture SHA-256 verified\nSupervised mode: {mode}, deadline {timeout_ms} ms\n'
            f'Mach-O validated; host page size 4096 bytes\nStatic internal pointer rebases: 1\n'
            f'{passed}\n\nLauncher exit: 0')


class FakeAdb:
    def __init__(self, mode='success', text=None, failure=None):
        self.mode = mode
        self.text = result_text(mode) if text is None else text
        self.failure = failure
        self.calls = []
        self.launches = 0
        self.cleared = False

    def __call__(self, command, *, capture_output, timeout, check):
        if command[:3] != ['fake-adb', '-s', 'private-test-serial']:
            raise AssertionError('Unexpected ADB executable or serial')
        if not capture_output or timeout != 30:
            raise AssertionError('ADB calls must be captured and bounded')
        parts = command[3:]
        self.calls.append(parts)
        code, stdout, stderr = 0, b'', b''
        if parts[:2] == ['shell', 'getprop']:
            stdout = b'synthetic-property\n'
        elif parts == ['shell', 'getconf', 'PAGE_SIZE']:
            stdout = b'4096\n'
        elif parts[0] == 'install':
            stdout = b'Failure' if self.failure == 'install' else b'Success\n'
        elif parts == ['shell', 'am', 'force-stop', 'org.ios27.host']:
            self.cleared = False
        elif parts == ['shell', 'pidof', 'org.ios27.host']:
            code = 0 if self.failure == 'stop' else 1
        elif parts == ['shell', 'run-as', 'org.ios27.host', 'rm', '-f', 'files/last-result.txt']:
            self.cleared = True
        elif parts[:3] == ['shell', 'am', 'start']:
            expected = ['shell', 'am', 'start', '-W', '-n', 'org.ios27.host/.MainActivity',
                        '--ez', 'run_fixture', 'true', '--es', 'fixture_mode', self.mode]
            if parts != expected or not self.cleared:
                raise AssertionError('Launch must select the mode after clearing stale results')
            self.launches += 1
            if self.failure == 'launch':
                stderr = b'Error: Activity not started'
        elif parts == ['shell', 'run-as', 'org.ios27.host', 'cat', 'files/last-result.txt']:
            if not self.launches:
                raise AssertionError('Result read before a fresh launch')
            if self.failure == 'hang':
                code = 1
            elif self.failure == 'adb-timeout':
                raise subprocess.TimeoutExpired(command, timeout)
            else:
                stdout = self.text.encode()
        elif parts == ['exec-out', 'screencap', '-p']:
            stdout = b'not-png' if self.failure == 'screenshot' else b'\x89PNG\r\n\x1a\nsynthetic'
        else:
            raise AssertionError('Unexpected ADB operation: ' + repr(parts))
        if check and code:
            raise subprocess.CalledProcessError(code, command, stdout, stderr)
        return subprocess.CompletedProcess(command, code, stdout, stderr)


class ResultContract(unittest.TestCase):
    def test_all_exact_mode_results(self):
        for mode in smoke.MODES:
            with self.subTest(mode=mode):
                self.assertTrue(smoke.valid_result(result_text(mode), mode))
                self.assertTrue(smoke.valid_result(result_text(mode).replace('\n', '\r\n'), mode))

    def test_mode_and_outcome_mismatches(self):
        for mode in smoke.MODES:
            for other in smoke.MODES:
                if other == mode:
                    continue
                with self.subTest(mode=mode, other=other):
                    text = result_text(other)
                    self.assertFalse(smoke.valid_result(text, mode))
                    timeout_ms, _ = smoke.MODES[mode]
                    # Even the right mode announcement cannot bless a wrong outcome.
                    wrong_pass = text.replace(text.splitlines()[1],
                                              f'Supervised mode: {mode}, deadline {timeout_ms} ms')
                    self.assertFalse(smoke.valid_result(wrong_pass, mode))

    def test_reject_partial_conflicting_or_failed_results(self):
        for mode in smoke.MODES:
            text = result_text(mode)
            for old, new in (
                    ('Launcher exit: 0', 'Launcher exit: 01'),
                    ('Launcher exit: 0', 'Launcher exit: 5'),
                    ('Launcher exit: 0', 'Launcher exit: 77'),
                    ('Launcher exit: 0', 'Launcher exit: 139'),
                    ('Launcher exit: 0', ''),
                    ('Fixture SHA-256 verified', ''),
                    ('Fixture SHA-256 verified', 'Fixture SHA-256 verified\nFixture SHA-256 verified'),
                    ('Static internal pointer rebases: 1', 'Static internal pointer rebases: 0'),
                    ('Static internal pointer rebases: 1', 'Static internal pointer rebases: 1junk'),
                    (smoke.MODES[mode][1], smoke.MODES[mode][1] + ' unexpected'),
                    (text.splitlines()[1], text.splitlines()[1] + '0'),
                    (text.splitlines()[1], '')):
                with self.subTest(mode=mode, replacement=new):
                    self.assertFalse(smoke.valid_result(text.replace(old, new), mode))
            for extra in ('FAIL: setup error', 'UNSUPPORTED: ARM64 required',
                          smoke.MODES[mode][1], 'Launcher exit: 5', text.splitlines()[1]):
                with self.subTest(mode=mode, extra=extra):
                    self.assertFalse(smoke.valid_result(text + '\n' + extra, mode))

    def test_pass_strings_match_actual_runner_and_activity(self):
        header = (ROOT / 'android/app/src/main/cpp/runner_modes.h').read_text()
        activity = (ROOT / 'android/app/src/main/java/org/ios27/host/MainActivity.java').read_text()
        for mode, (_, expected) in smoke.MODES.items():
            self.assertIn('"' + expected + '"', header)
            self.assertIn('"' + mode + '"', activity)
            # Activity's success PASS is the existing abbreviated prefix.
            prefix = 'PASS: native ARM64 Mach-O returned 42.' if mode == 'success' else expected
            self.assertIn('"' + prefix + '"', activity)
        self.assertIn('passed = process.exitValue() == 0 && report.contains(pass);', activity)
        self.assertIn('if (!BuildConfig.DEBUG ||', activity)


class SmokeFlow(unittest.TestCase):
    def run_smoke(self, fake, mode=None, screenshot=False, error=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            apk = root / 'synthetic.apk'
            apk.write_bytes(b'host-test-only')
            output = root / 'reports/result.json'
            args = ['--adb', 'fake-adb', '--serial', 'private-test-serial',
                    '--apk', str(apk), '--output', str(output)]
            if mode is not None:
                args += ['--mode', mode]
            if screenshot:
                args += ['--screenshot', str(root / 'screen.png')]
            with patch.object(smoke.subprocess, 'run', side_effect=fake), \
                    patch.object(smoke.time, 'sleep'), \
                    patch.object(smoke.time, 'monotonic', side_effect=range(1000)), \
                    patch('sys.stdout', new_callable=io.StringIO) as console:
                if error:
                    with self.assertRaises(error):
                        smoke.main(args)
                else:
                    smoke.main(args)
            report = json.loads(output.read_text())
            self.assertEqual(report['apk_sha256'], hashlib.sha256(b'host-test-only').hexdigest())
            self.assertNotIn('private-test-serial', output.read_text())
            self.assertNotIn('private-test-serial', console.getvalue())
            if screenshot and not error:
                self.assertTrue((root / 'screen.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n'))
            if error:
                self.assertFalse(report['passed'])
                self.assertNotIn('PASS: two fresh', console.getvalue())
            return report

    def test_default_success_still_launches_twice(self):
        fake = FakeAdb()
        report = self.run_smoke(fake)
        self.assertTrue(report['passed'])
        self.assertEqual(report['mode'], 'success')
        self.assertEqual(report['runs'], [result_text()] * 2)
        self.assertEqual(fake.launches, 2)
        self.assertEqual(sum(parts[:1] == ['install'] for parts in fake.calls), 1)

    def test_explicit_modes_launch_twice(self):
        for mode in smoke.MODES:
            with self.subTest(mode=mode):
                fake = FakeAdb(mode)
                report = self.run_smoke(fake, mode)
                self.assertTrue(report['passed'])
                self.assertEqual(report['mode'], mode)
                self.assertEqual(report['runs'], [result_text(mode)] * 2)
                self.assertEqual(fake.launches, 2)

    def test_bad_modes_rejected_before_any_adb(self):
        for mode in ('', 'loop', 'Trap', 'deadline ', 'unknown', 'success;reboot'):
            with self.subTest(mode=mode), patch.object(smoke.subprocess, 'run') as adb, \
                    patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit) as caught:
                smoke.main(['--serial', 'not-used', '--apk', 'missing.apk',
                            '--output', 'not-written.json', '--mode', mode])
            self.assertEqual(caught.exception.code, 2)
            adb.assert_not_called()

    def test_wrong_outcome_stops_after_first_launch(self):
        for mode in ('trap', 'deadline', 'raw-syscall'):
            with self.subTest(mode=mode):
                fake = FakeAdb(mode, text=result_text())
                report = self.run_smoke(fake, mode, error=RuntimeError)
                self.assertEqual(len(report['runs']), 1)
                self.assertEqual(fake.launches, 1)

    def test_crash_is_not_an_expected_fault(self):
        fake = FakeAdb('trap', text=result_text('trap').replace('Launcher exit: 0', 'Launcher exit: 139'))
        self.run_smoke(fake, 'trap', error=RuntimeError)
        self.assertEqual(fake.launches, 1)

    def test_install_and_launch_failures(self):
        for failure in ('install', 'launch'):
            with self.subTest(failure=failure):
                fake = FakeAdb(failure=failure)
                report = self.run_smoke(fake, error=RuntimeError)
                self.assertEqual(report['runs'], [])

    def test_previous_process_must_stop_before_cleanup(self):
        fake = FakeAdb(failure='stop')
        self.run_smoke(fake, error=RuntimeError)
        self.assertEqual(fake.launches, 0)
        self.assertFalse(fake.cleared)
        self.assertEqual(sum(parts[1:2] == ['pidof'] for parts in fake.calls), 20)

    def test_missing_result_deadline_is_bounded(self):
        fake = FakeAdb('deadline', failure='hang')
        report = self.run_smoke(fake, 'deadline', error=RuntimeError)
        self.assertEqual(report['runs'], [])
        self.assertEqual(fake.launches, 1)
        self.assertLess(len(fake.calls), 30)

    def test_adb_timeout_leaves_failure_report(self):
        self.run_smoke(FakeAdb(failure='adb-timeout'), error=subprocess.TimeoutExpired)

    def test_optional_screenshot(self):
        self.assertTrue(self.run_smoke(FakeAdb(), screenshot=True)['passed'])

    def test_invalid_screenshot_cannot_publish_pass(self):
        self.run_smoke(FakeAdb(failure='screenshot'), screenshot=True, error=RuntimeError)


if __name__ == '__main__':
    unittest.main()
