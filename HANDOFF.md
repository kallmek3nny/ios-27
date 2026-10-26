# ios-27 development handoff | phase 10 complete -> phase 11

**Snapshot:** 2026-10-26  
**Repository:** https://github.com/kallmek3nny/ios-27  
**Last confirmed main:** `8263d7abaa71c7bbf3730fc79ed6ffeff24830d7`  
**Phase 10 merges:** [#15](https://github.com/kallmek3nny/ios-27/pull/15) -> `f690f8129b60a1f0a4de699888f04f988f064f86`; [#16](https://github.com/kallmek3nny/ios-27/pull/16) -> `8263d7abaa71c7bbf3730fc79ed6ffeff24830d7`. Their exact frozen PR heads `3f1195a9a12c8cd36bb3f55cbe7129dd3893fbf3` and `d72c0256b753ad5d0b719f1179ab83d2f11b6de3` both passed Ubuntu 24.04 x86_64 AND ARM64 GitHub CI with strict warnings and tests, then were squash-merged with expected-head SHA. Reverify current main/PRs before more work.

## Goal

Run the **actual official Apple iOS 27 user interface**, genuine SpringBoard, Control Center, lockscreen, UIKit/SwiftUI/CoreAnimation and Liquid Glass, and iOS apps on a locked-bootloader Galaxy S25+ above the unchanged Android Linux kernel. No themed launcher or general CPU emulation. This is an **unproven research objective**, not a functioning Apple UI port. Generic ARM64 instruction execution doesn't supply arm64e/PAC semantics, dyld shared caches, proprietary frameworks, Apple's entitlements, XNU/Mach, Objective-C/Swift services, Apple GPU compositor or hardware integration.

## Hard recovery and root safety

- NEVER flash or modify Samsung bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, firmware, partition layout, antirollback, verified boot or secure boot.
- NEVER interfere with Odin/download mode or bypass D2 restrictions. Never weaken recovery or deploy exploits.
- Never require root, DirtyFrag/KernelSU, custom modules, persistent SELinux changes, boot hooks or privileged writes. DirtyFrag soft root is optional and NOT used; a reboot does not reverse persistent changes previously written while rooted.
- Run only reviewable, unprivileged userspace tests on trusted synthetic/local fixtures. Native execution primitives are **not sandboxed** and must not run untrusted Mach-O binaries.
- Do not bundle or redistribute proprietary Apple binaries, firmware or unauthorized decrypted apps.
- If revisiting device assumptions, recheck https://github.com/mitschud/DirtyFrag and its releases before any device-specific recommendation.

## Proven merged phases

| Phase | PR | What was actually verified |
| --- | --- | --- |
| 1 | #1 | thin ARM64 Mach-O parser, freestanding native fixture runner |
| 2 | #2 | bounded synthetic Darwin SVC probe |
| 3 | #3 | FAT/FAT64 ARM64 inventory and dylib/framework import metadata |
| 4 | #4 | guest-virtual dyld path token resolution |
| 5 | #5 | bounded export trie parsing |
| 6 | #6 | classic bind planning, native ARM64 synthetic cross-image call |
| 7 | #8 | bounded dyld chained pointer format 6, 4KiB/16KiB page fixups and copy-only atomics |
| 8 | #10 | explicit synthetic multi-image graph with W^X maps, classic/chained inter-image relocation, native ARM64 cross-image calls |
| 9 | #12, #13 | bounded synthetic Mach-style message queues/owner rights, COPY_SEND/MOVE_SEND, quota and in-flight capacity |
| 10 | [#15](https://github.com/kallmek3nny/ios-27/pull/15), [#16](https://github.com/kallmek3nny/ios-27/pull/16) | separate per-task port name spaces with collisions/stale-handle checks; atomic send-once request/reply and translated copy/move descriptors; checked offset-based guest byte-arena send/receive facade; 11 total CTest suites pass on x86_64 and ARM64 |

## Phase 10 code and true limits

- `include/ios27/mach_v2.h`, `src/mach_v2.c`, `tests/test_mach_v2.c`: a **new versioned** synthetic API alongside the unchanged phase 9 global-name compatibility API. Up to 8 simulated task ids, 32 port objects and 64 names per task, bounded 4-message FIFOs, 256-byte messages. Each task's local names advance monotonically and resolve only within that task. Two tasks can have numeric port name 100 referring to entirely different objects.
- Supports remote `MOVE_SEND_ONCE` (18), outgoing local `MAKE_SEND_ONCE` (21), one-port COPY_SEND/MOVE_SEND descriptors, task-local alias translation, reserved in-flight rights, checked send ownership and deterministic poll-only queue errors. A successful one-use reply consumes its capability; invalid/full-queue sends leave rights unchanged. Receive-short-buffer errors don't dequeue.
- `include/ios27/mach_guest.h`, `src/mach_guest.c`, `tests/test_mach_guest.c`: narrow fixture-only arena adapter with bounds-checked uint64 offsets/lengths, isolated 256-byte staging and rejection of guest-arena/metadata aliasing. Tested end-to-end synthetic request/reply and malformed offsets.
- Full test suite at latest Phase 10 head: 11 suites, including all earlier loader/native-call tests. CI tested ordinary x86_64 and ARM64 Linux only; **not** the Samsung or any Apple framework.
- This is not XNU, actual Mach kernel traps, a sandbox, a process-isolated IPC namespace, real os task authentication, service bootstrap, trailers, notifications, OOL transfers, pthread workqueues, send-once descriptors, authentic blocking/timeouts or signed iOS app launch.
- Nothing yet runs Apple's actual SpringBoard, Liquid Glass or iOS applications.

## Phase 11: concrete next coding

1. **Security and lifecycle hardening:** test task teardown, port death, queued rights cancellation, orphaned send-once reply rights, naming overflow, count saturation, cross-task descriptor exploits. Extend deterministic property/corpus tests without weakening -Werror.
2. **Darwin-facing ABI boundary:** define an explicit, very narrow Mach trap vocabulary (with reference to public XNU headers where legal) that marshals registers and guest-memory offsets into the phase 10 fixture implementation. Reject every unsupported syscall; no arbitrary passthrough to Android/Linux or read/write host memory.
3. **Native legally redistributable Darwin sample:** where toolchains permit, build a small arm64 Mach-O from source with an actual Darwin target (rather than hand-writing synthetic instruction bytes) and integrate it into the safe isolated loader. Never claim framework compatibility from that fixture.
4. Keep native multi-image W^X mapping and image lifecycles robust; test real 16KiB-host constraints separately. Don't silently bypass PAC/arm64e requirements.
5. Perform documented technical feasibility research on Apple ObjC/Swift runtime, private framework/decrypted binary availability, dyld shared cache and Metal/CoreAnimation compatibility before promising genuine iOS UI.

## Process

- Work exclusively in `kallmek3nny/ios-27`, inspect current main/open PRs and don't duplicate concurrent branches.
- Every shell/build/configure/test/clone command has an **explicit hard timeout**. No indefinite polling loops.
- Preserve `-Wall -Wextra -Werror -Wpedantic` for all code/tests. Add meaningful tests/docs to every PR.
- Verify BOTH x86_64 and ARM64 CI on the exact frozen PR head; recheck if main moves and merge using expected-head SHA only.
- Linux ARM64 runner success is not on-phone validation.

Trusted Linux verification:

    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure

**Progress:** working genuine iOS UI on Android = 0%; overall goal about ~1% as a speculative order-of-magnitude estimate, not a measurable accomplishment rate.
