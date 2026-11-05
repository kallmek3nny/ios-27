# phase 27c: deterministic software rendering and frame submission

## implementation / scope

This is a **CPU reference pipeline**, not a claim of Metal, genuine IOSurface,
Vulkan or a running Android graphics host. It is intentionally layered on the
verified draft phase26c contract in PR #111: `p26_device`, generation-checked
handles, buffer/image/surface resources, retained commands, ordered upload and
RGBA/BGRA blit, completed CPU fences and immutable presentation frames. The
phase27c PR is stacked on its branch; it cannot be merged independently until
that foundation lands.

## frame ABI for agents 4 and 5

`include/ios27/phase27c_renderer.h` exposes `p27c_frame` ABI version 1.
Fields:

- `abi_version = 1`
- `image.width / height`: 1..8192 pixels, held constant per renderer
- `image.format`: RGBA8 or BGRA8, four **straight-alpha** 8-bit channels
- `image.stride`: multiple of 16 bytes, at least width * 4 bytes
- `frame`: generation-safe immutable snapshot handle, owned by the receipt
- `ready`: the existing phase26c *already-completed CPU fence* (queue and
  monotonic sequence), **not** an Android sync-fence FD or GPU semaphore
- `visible_hash`: 64-bit FNV-1a over little-endian width, height and format
  (four bytes each) followed by visible row bytes excluding padding

The consumer calls `p27c_frame_read` with a destination pointer, capacity and
row stride, then `p27c_frame_release`. An Android host can copy the RGBA8
rows directly into a CPU-mapped RGBA8888 `AHardwareBuffer`, with independently
validated NDK usage/locking; this module does not allocate a GPU surface,
transfer hardware-fence FDs, or export raw internal pointers. Keep the receipt
valid until the copy is complete. The renderer returns `P26_BUSY` on destroy
while a frame or mutable drawing map remains outstanding. No renderer destroy
may race entering public APIs.

`p27c_begin` clears all backing padding and visible pixels. `p27c_rect`
and `p27c_upload_rgba` blend source-over in deterministic integer arithmetic.
`p27c_end` unlocks staging, records and submits a real phase26c buffer upload
and image blit, polls its fence, presents a distinct immutable frame, and
returns its exact hash. The backend enforces checked bounds, allocation
quotas (including snapshots), 128-flight cap and stale-frame rejection.
The format conversion occurs through the existing phase26c blit command.

## evidence and commands

Run:
```sh
timeout 180s bash tools/phase27c-verify.sh
P27C_SANITIZE=1 timeout 180s bash tools/phase27c-verify.sh
```

The independent pixel fixtures expect:
- RGBA8 4x2 hash: `06c3261fef3963a7`
- BGRA8 4x2 hash: `52f13f4c4681c790`
- Starting pixel (RGBA): `0a 14 1e ff`
- 50% red-purple overlay: `69 0a 41 ff`
- Overlay plus 50% green: `34 69 20 ff`

The tests compare all 32 visible bytes, then run 10,000 full render/read/release
cycles in **each** format, read immutable frames concurrently with four
threads, verify 128 outstanding frames and safe busy teardown, and exercise
quota and bounds failures. CI runs strict `-Werror` C11 with ASan/UBSan on
x86_64 and arm64. A successful CI result verifies CPU behavior on its runners,
not Samsung hardware, Android NDK/driver behavior or genuine Apple frameworks.

## interfaces / ownership

All changes are isolated to phase27c-owned paths. There are no edits to
CoreAnimation layer engine or Android lifecycle files. Agent 4 can call
the rectangle/upload commands or add a separate adapter; agent 5 can consume
receipts using `p27c_frame_read` without depending on compositor internals.
