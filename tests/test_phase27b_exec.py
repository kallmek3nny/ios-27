#!/usr/bin/env python3
"""Inspect on x86_64; execute the compiler-linked MH_EXECUTE only on arm64."""
import hashlib
import json
import pathlib
import platform
import struct
import subprocess
import sys

def main():
    if len(sys.argv) != 5:
        raise ValueError("expected runner, service loader, executable, manifest")
    runner, helper, executable, manifest = map(pathlib.Path, sys.argv[1:])
    image = executable.read_bytes()
    metadata = json.loads(manifest.read_text(encoding="utf-8"))
    if metadata.get("format") != "ios27-phase27b-static-libsystem-v1":
        raise ValueError("unexpected ABI manifest")
    if metadata.get("unresolved_linked_symbols") != []:
        raise ValueError("unresolved imports")
    if hashlib.sha256(image).hexdigest() != metadata.get("executable_sha256"):
        raise ValueError("image hash mismatch")
    if len(image) < 512 or len(image) > 4 * 1024 * 1024:
        raise ValueError("invalid linked image size")
    magic, cpu, _sub, kind, count, size = struct.unpack_from("<6I", image)
    if (magic, cpu, kind) != (0xFEEDFACF, 0x0100000C, 2):
        raise ValueError("not compiler-linked arm64 MH_EXECUTE")
    if count > 100 or size > len(image) - 32:
        raise ValueError("invalid load commands")
    position = 32
    entry_found = False
    for _ in range(count):
        if position + 8 > 32 + size:
            raise ValueError("truncated command")
        command, length = struct.unpack_from("<II", image, position)
        if length < 8 or length % 8 or position + length > 32 + size:
            raise ValueError("command bounds")
        if command == 0x80000028:
            entry_found = True
        position += length
    if position != 32 + size or not entry_found:
        raise ValueError("LC_MAIN missing or command table incomplete")
    if platform.machine().lower() not in ("aarch64", "arm64"):
        print("phase27b: x86_64 parsed and audited; native execution skipped")
        return 77
    command = [str(runner), "run-exec", str(helper), str(executable), "--unsafe"]
    completed = subprocess.run(command, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, timeout=25)
    if completed.returncode or "exec ARM64 entry returned 42" not in completed.stdout:
        raise RuntimeError("ARM64 native fixture failed: %s, %s" %
                           (completed.returncode, completed.stdout[-2500:]))
    print("phase27b: real native arm64 LC_MAIN allocated, formatted and roundtripped virtual file")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit("phase27b verification: %s" % error)
