#!/usr/bin/env python3
"""Convert phase27d's strict 32x24 premultiplied RGBA fixtures to lossless PNG.
No third-party dependencies, no image resampling, no platform font rendering.
"""
import pathlib
import struct
import sys
import zlib

W, H = 32, 24


def chunk(kind: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))


def convert(raw: pathlib.Path) -> pathlib.Path:
    rgba = raw.read_bytes()
    if len(rgba) != W * H * 4:
        raise ValueError(f"{raw}: invalid rgba size {len(rgba)}")
    scanlines = b"".join(
        b"\0" + rgba[y * W * 4:(y + 1) * W * 4]
        for y in range(H)
    )
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(scanlines, level=9)) +
           chunk(b"IEND", b""))
    result = raw.with_suffix(".png")
    result.write_bytes(png)
    return result


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: phase27d-reference.py /path/to/frame-directory")
    directory = pathlib.Path(sys.argv[1])
    frames = sorted(directory.glob("frame-*.rgba"))
    if len(frames) != 3:
        raise SystemExit(f"expected 3 frames, got {len(frames)}")
    for frame in frames:
        print(convert(frame))
