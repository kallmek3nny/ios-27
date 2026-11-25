#!/usr/bin/env python3
"""Acquire an explicitly selected Apple simulator asset outside the repository.

TLS verification stays enabled. Apple's private service root is loaded only
into this request context, from Apple's public PKI endpoint. This tool never
executes, installs, packages or commits the downloaded Apple material.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import ssl
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
INDEX = 'https://devimages-cdn.apple.com/downloads/xcode/simulators/index2.dvtdownloadableindex'
ROOT_CA = 'https://www.apple.com/appleca/AppleIncRootCertificate.cer'
ASSETS = 'https://gdmf.apple.com/v2/assets'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def apple_url(url):
    parsed = urllib.parse.urlsplit(url)
    require(parsed.scheme == 'https' and parsed.port in (None, 443) and
            not parsed.username and not parsed.password and
            parsed.hostname in {'devimages-cdn.apple.com', 'www.apple.com',
                                'gdmf.apple.com', 'updates.cdn-apple.com'},
            'Unexpected Apple download origin')
    return url


def read_small(url, *, context=None, data=None):
    request = urllib.request.Request(apple_url(url), data=data,
                                    headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, context=context, timeout=30) as response:
        apple_url(response.url)
        blob = response.read(8 * 1024 * 1024 + 1)
        require(len(blob) <= 8 * 1024 * 1024, 'Oversized metadata response')
        return blob


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def asset_fields(asset):
    url = apple_url(asset['__BaseURL'] + asset['__RelativePath'])
    size = asset['_DownloadSize']
    digest = base64.b64decode(asset['_Measurement-SHA256'], validate=True)
    require(type(size) is int and 0 < size <= 16 * 1024**3,
            'Unsupported archive size')
    require(len(digest) == 32 and asset.get('Architectures') == ['arm64'],
            'Expected an ARM64 asset with a SHA-256 measurement')
    return url, size, digest.hex()


def download(url, destination, size, expected):
    partial = destination.with_suffix(destination.suffix + '.part')
    if destination.exists():
        require(destination.stat().st_size == size, 'Existing archive size mismatch')
        with destination.open('rb') as stream:
            require(hashlib.file_digest(stream, 'sha256').hexdigest() == expected,
                    'Existing archive checksum mismatch')
        return
    offset = partial.stat().st_size if partial.exists() else 0
    require(offset <= size, 'Partial archive exceeds expected size')
    require(shutil.disk_usage(destination.parent).free > size - offset + 1024**3,
            'Insufficient disk space for archive plus 1 GiB reserve')
    digest = hashlib.sha256()
    if offset:
        with partial.open('rb') as stream:
            while block := stream.read(4 * 1024 * 1024):
                digest.update(block)
    if offset < size:
        request = urllib.request.Request(apple_url(url), headers={
            'Range': f'bytes={offset}-{size - 1}', 'Accept-Encoding': 'identity'})
        with urllib.request.urlopen(request, timeout=45) as response:
            apple_url(response.url)
            require(response.status == 206 and
                    response.headers.get('Content-Range') == f'bytes {offset}-{size - 1}/{size}',
                    'Server did not honor the exact resume range')
            with partial.open('ab') as stream:
                last = 0.0
                while block := response.read(4 * 1024 * 1024):
                    require(len(block) <= size - offset, 'Archive exceeds expected size')
                    stream.write(block)
                    digest.update(block)
                    offset += len(block)
                    if time.monotonic() - last >= 10:
                        print(f'Downloaded {offset}/{size} ({100 * offset / size:.1f}%)', flush=True)
                        last = time.monotonic()
    require(offset == size, 'Incomplete download; rerun to resume')
    require(digest.hexdigest() == expected, 'Archive SHA-256 mismatch; partial retained for inspection')
    os.replace(partial, destination)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--build', required=True)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    require(re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,2}', args.version) and
            re.fullmatch(r'[A-Za-z0-9]{3,24}', args.build), 'Invalid version/build')
    directory = args.directory.resolve()
    require(not directory.is_relative_to(ROOT), 'Apple inputs must stay outside the repository')
    directory.mkdir(parents=True, exist_ok=True)
    index_bytes = read_small(INDEX)
    index = json.loads(index_bytes) if index_bytes.lstrip().startswith(b'{') else plistlib.loads(index_bytes)
    matching = [entry for entry in index['downloadables']
                if entry.get('simulatorVersion') == {'buildUpdate': args.build, 'version': args.version}
                and entry.get('architectures') == ['arm64']
                and entry.get('platform') == 'com.apple.platform.iphoneos']
    require(len(matching) == 1, 'Build/version is absent or ambiguous in Apple index')
    (directory / 'apple-simulator-index').write_bytes(index_bytes)
    der = read_small(ROOT_CA)
    pem = ssl.DER_cert_to_PEM_cert(der)
    context = ssl.create_default_context()
    context.load_verify_locations(cadata=pem)
    request = {'ClientVersion': 2, 'AssetAudience': '02d8e57e-dd1c-4090-aa50-b4ed2aef0062',
               'AssetType': 'com.apple.MobileAsset.iOSSimulatorRuntime',
               'RequestedBuild': args.build}
    response = read_small(ASSETS, context=context, data=json.dumps(request).encode())
    # Pallas returns a JWS payload. HTTPS authenticates acquisition; this is
    # payload decoding, not a claim of independent JWS signature validation.
    if response.count(b'.') == 2:
        response = base64.urlsafe_b64decode(response.split(b'.')[1] + b'===')
    metadata = json.loads(response)
    assets = [a for a in metadata.get('Assets', [])
              if a.get('Build') == args.build and a.get('SimulatorVersion') == args.version
              and a.get('Architectures') == ['arm64']]
    require(len(assets) == 1, 'Apple asset response did not identify one exact build')
    asset = assets[0]
    url, size, expected = asset_fields(asset)
    # The raw asset response includes archive key material: keep it local,
    # outside Git, and omit it from reports and console output.
    atomic_json(directory / 'ios27-mobileasset.json', metadata)
    destination = directory / ('ios-' + args.version + '-' + args.build + '-arm64.aar')
    report = {'index_url': INDEX, 'asset_service': ASSETS, 'version': args.version,
              'build': args.build, 'architectures': ['arm64'], 'archive_url': url,
              'download_size': size, 'expected_sha256': expected,
              'archive_path': str(destination), 'download_verified': False}
    atomic_json(directory / 'source-report.json', report)
    print(json.dumps(report), flush=True)
    if args.download:
        download(url, destination, size, expected)
        report['download_verified'] = True
        atomic_json(directory / 'source-report.json', report)
        print('Apple archive downloaded and SHA-256 verified: ' + str(destination), flush=True)


if __name__ == '__main__':
    main()
