# phase 14b: guest-memory mach ipc result ABI (2026-10-27)

## scope and collision avoidance

Owned paths: `src/mach_guest.c`, `include/ios27/mach_guest.h`, `tests/test_mach_guest.c`, and this unique document. Started from main `3e3cf85bf1b3bd0247efd81036d33542ce6acee5`. PR #24 already edits the separate `mach_v2` source/tests, so this branch intentionally never touches them. The phase 14a and 14c agents' paths, build targets, CMake, CI, and shared handoff remain untouched.

## implementation

`ios27_mach_guest_call_offset` exposes a strictly bounded guest-virtual u32 result slot for the existing synthetic Mach send and receive operations. The slot is disjoint from the wire input/output span, fully inside the caller-owned arena, with no arbitrary guest-provided host result pointer. The byte order is explicit little-endian, irrespective of host alignment. Reuse of `ios27_mach_guest_call` guarantees that successful sends or dequeues are completed before committing the length slot and that a too-large receive leaves its queued message intact. All other failures are side-effect-free on guest memory. No arbitrary syscalls, host filesystem access, Linux Mach forwarding, or privilege changes.

## regressions

Existing `bounded-mach-guest-memory` target now covers a real synthetic request/reply send-once roundtrip with length slots; all-or-nothing failure and stale send-once rejection; full and partial slot overlap, integer edge cases and bad operations; undersized receive/required-length preservation; a full queue and queue-order preservation after timeout. Existing `-Wall -Wextra -Werror -Wpedantic` stays unchanged.

## reality check

The in-process task and port model is a deterministic *fixture*, **not** XNU, an iOS service bus, a real SpringBoard IPC implementation, or a working iOS framework loader. No device, Samsung bootloader, firmware, partitions, kernel, Odin/D2, or persistent root modifications are involved.
