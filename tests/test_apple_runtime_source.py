import base64
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('apple_source', ROOT / 'tools/apple-runtime-source.py')
source = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(source)
URL = 'https://updates.cdn-apple.com/test/runtime.aar'


class AcquisitionBoundaries(unittest.TestCase):
    def test_origins(self):
        self.assertEqual(source.apple_url(URL), URL)
        for url in ('http://updates.cdn-apple.com/a', 'https://apple.com.evil.test/a',
                    'https://user@updates.cdn-apple.com/a', 'https://updates.cdn-apple.com:444/a',
                    'file:///tmp/runtime.aar', 'https://github.com/runtime.aar'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                source.apple_url(url)

    def test_asset_bounds(self):
        valid = {'__BaseURL': 'https://updates.cdn-apple.com/', '__RelativePath': 'test/runtime.aar',
                 '_DownloadSize': 16, '_Measurement-SHA256': base64.b64encode(bytes(32)).decode(),
                 'Architectures': ['arm64']}
        self.assertEqual(source.asset_fields(valid), (URL, 16, '00' * 32))
        for key, value in (('_DownloadSize', True), ('_DownloadSize', 0),
                           ('_DownloadSize', 17 * 1024**3), ('Architectures', ['x86_64']),
                           ('_Measurement-SHA256', base64.b64encode(bytes(31)).decode()),
                           ('__BaseURL', 'https://evil.test/')):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                source.asset_fields(dict(valid, **{key: value}))

    def test_repository_destination_refused_before_network(self):
        with patch('sys.argv', ['tool', '--directory', str(ROOT / 'out/apple'),
                                '--version', '27.0', '--build', '24A434']), \
                patch.object(source, 'read_small') as network:
            with self.assertRaises(ValueError):
                source.main()
            network.assert_not_called()

    def test_existing_archive_is_verified_without_network(self):
        blob = b'AEA1-complete'
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'runtime.aar'
            destination.write_bytes(blob)
            with patch.object(source.urllib.request, 'urlopen') as network:
                source.download(URL, destination, len(blob), hashlib.sha256(blob).hexdigest())
                with self.assertRaises(ValueError):
                    source.download(URL, destination, len(blob), '00' * 32)
                network.assert_not_called()
            self.assertEqual(destination.read_bytes(), blob)

    def test_resume_range_and_checksum_commit(self):
        blob = b'AEA1-verified-archive'
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'runtime.aar'
            partial = destination.with_suffix('.aar.part')
            partial.write_bytes(blob[:4])
            response = io.BytesIO(blob[4:])
            response.status = 206
            response.url = URL
            response.headers = {'Content-Range': f'bytes 4-{len(blob)-1}/{len(blob)}'}
            with patch.object(source.urllib.request, 'urlopen', return_value=response) as network, \
                    patch.object(source.shutil, 'disk_usage', return_value=types.SimpleNamespace(free=2*1024**3)), \
                    patch('builtins.print'):
                source.download(URL, destination, len(blob), hashlib.sha256(blob).hexdigest())
                self.assertEqual(network.call_args.args[0].get_header('Range'), f'bytes=4-{len(blob)-1}')
            self.assertEqual(destination.read_bytes(), blob)
            self.assertFalse(partial.exists())

    def test_bad_range_preserves_partial(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'runtime.aar'
            partial = destination.with_suffix('.aar.part')
            partial.write_bytes(b'AEA1')
            response = io.BytesIO(b'replacement')
            response.status = 200
            response.url = URL
            response.headers = {}
            with patch.object(source.urllib.request, 'urlopen', return_value=response), \
                    patch.object(source.shutil, 'disk_usage', return_value=types.SimpleNamespace(free=2*1024**3)), \
                    self.assertRaises(ValueError):
                source.download(URL, destination, 12, '00' * 32)
            self.assertEqual(partial.read_bytes(), b'AEA1')
            self.assertFalse(destination.exists())


if __name__ == '__main__':
    unittest.main()
