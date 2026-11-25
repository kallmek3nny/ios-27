#!/usr/bin/env python3
"""Extract pinned Android libraries into a private cross-build prefix on Linux."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
from urllib.parse import urljoin, urlsplit
from urllib.request import urlopen


def prepare(args):
    manifest_path = Path(__file__).resolve().parent.parent / "integration/darling/termux-packages.json"
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    root = args.output.resolve()
    marker = root / "manifest.sha256"
    digest = sha256(manifest_bytes).hexdigest()
    if root.exists() and any(root.iterdir()):
        if not marker.is_file() or marker.read_text().strip() != digest:
            raise ValueError("output belongs to another dependency configuration")
    root.mkdir(parents=True, exist_ok=True)
    marker.write_text(digest + "\n")
    downloads = root / "downloads"
    downloads.mkdir(exist_ok=True)

    def download(package):
        path = PurePosixPath(package["path"])
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("package path must be relative to its repository")
        url = urljoin(manifest["repository"], str(path))
        if urlsplit(url).scheme != "https":
            raise ValueError("package download requires HTTPS")
        archive = downloads / path.name
        if archive.is_file():
            content = archive.read_bytes()
        else:
            with urlopen(url, timeout=60) as response:
                if urlsplit(response.url).scheme != "https":
                    raise ValueError("package redirect left HTTPS")
                content = response.read(package["size"] + 1)
        if len(content) != package["size"] or sha256(content).hexdigest() != package["sha256"]:
            raise ValueError(f"size/hash mismatch for {package['name']}")
        if not archive.exists():
            archive.write_bytes(content)
        return archive

    with ThreadPoolExecutor(max_workers=4) as workers:
        archives = list(workers.map(download, manifest["packages"]))
    extracted = root / "root"
    for archive in archives:
        subprocess.run(["dpkg-deb", "--extract", str(archive), str(extracted)], check=True, timeout=30)
    prefix = extracted / "data/data/com.termux/files/usr"
    for name in ("crypto", "xml2", "ucontext", "android-posix-semaphore"):
        library = prefix / "lib" / f"lib{name}.so"
        data = library.read_bytes()
        if data[:6] != b"\x7fELF\x02\x01" or int.from_bytes(data[18:20], "little") != 183:
            raise ValueError(f"{library} is not little-endian AArch64 ELF64")
    print(f"Verified {len(archives)} package hashes and native ARM64 libraries.")
    print(f"Dependency prefix: {prefix}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    try:
        prepare(parser.parse_args())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(f"Android dependencies: {error}")
