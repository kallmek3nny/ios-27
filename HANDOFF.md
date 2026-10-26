# ios-27 development handoff | phase 9 complete -> phase 10

**Updated:** 2026-10-26  
**Repository:** https://github.com/kallmek3nny/ios-27  
**Verified main after phase 9:** f0a5353ffdbbccb4904739c4aa0ee7a71553c724

**Merged development:** [#12](https://github.com/kallmek3nny/ios-27/pull/12) phase 9 synthetic Mach IPC merged as ea1a4edfb1ef00690a1b24f41c5ff96f970cb3e2. [#13](https://github.com/kallmek3nny/ios-27/pull/13) phase 9 rights-capacity hardening merged as f0a5353ffdbbccb4904739c4aa0ee7a71553c724. Both PR heads passed strict GitHub Actions tests on Ubuntu x86_64 and ARM64. Recheck current main, open PRs and CI when resuming; do not assume nothing moved.

## Non-negotiable goal

Research **Apple's actual official iOS 27 UI, genuine SpringBoard, UIKit/SwiftUI/CoreAnimation Liquid Glass, Control Center, lockscreen and real iOS apps** running above Android on Galaxy S25+ with a locked bootloader. No launcher imitation and no general CPU emulation. This remains an *unproven research goal*; none of the tested fixtures is an actual Apple app. ARM64 CPU instruction compatibility does not supply arm64e/PAC, XNU/Mach semantics, dyld shared cache, Apple frameworks, entitlements or graphics services.

## Hard phone safety

- Never flash or modify bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, firmware, partitions, antirollback, secure boot or verified boot.
- Never alter Odin/download mode or bypass Samsung D2 restrictions. Do not weaken recovery or exploit the phone.
- Never require persistent root, SELinux policy changes, kernel modules, boot-time hooks or privileged writes. DirtyFrag/KernelSU are optional **and not used**. A reboot does not undo any previous persistent writes made while rooted.
- Do everything on unprivileged ordinary Linux/Android userspace first; no privileged device state changes. Review https://github.com/mitschud/DirtyFrag for any new Samsung-specific risk.
- Use only controlled, redistributable test fixtures. Do not distribute proprietary Apple frameworks, Apple firmware or unauthorized decrypted app payloads. Native code execution tests are **not sandboxed**: never run untrusted Mach-O code.

## Proven merged phases

| Phase | PR | Verified implementation |
| --- | --- | --- |
| 1 | #1 | thin ARM64 Mach-O parser and freestanding native fixture runner |
| 2 | #2 | bounded synthetic Darwin SVC probe |
| 3 | #3 | FAT/FAT64 and import inventory |
| 4 | #4 | guest-virtual dyld path tokens |
| 5 | #5 | dyld export trie metadata |
| 6 | #6 | classic pointer binds and synthetic ARM64 native inter-image call |
| 7 | #8 | bounded dyld chained fixups, PTR_64_OFFSET subset, 4K/16K pages |
| 8 | #10 | explicit 8-image Mach-O DAG with cross-image relocation, private mappings, W^X and native synthetic calls |
| 9 | [#12](https://github.com/kallmek3nny/ios-27/pull/12), [#13](https://github.com/kallmek3nny/ios-27/pull/13) | userspace Mach-style 24-byte headers and 12-byte port descriptors, 8 synthetic task IDs, 32 ports, 4-message queues, 256-byte bounded messages, send-right isolation, atomic COPY_SEND/MOVE_SEND transfers, rights reservation and deterministic error tests |

## Phase 9 source and proof

- `include/ios27/mach_ipc.h`, `src/mach_ipc.c`, `tests/test_mach_ipc.c`, `docs/phase9-mach-ipc.md`.
- A **private, single-process** synthetic Mach message space. Receive rights are owned by simulated task IDs; grants and transfers are checked. Duplicate, stale, unauthorized rights and unsupported complex descriptors are rejected. A one-descriptor COPY_SEND keeps sender rights; MOVE_SEND consumes them **only after successful enqueue**. Pending rights reserve recipient capacity. Short buffers do not dequeue. Queues are bounded and message delivery order tested.
- Header/descriptor sizes and selected dispositions follow public XNU definitions, but this is **not actual Darwin Mach IPC**. No guest trap forwarding, kernel ports, global service registry, task isolation, send-once, OOL memory, trailers, reply ports, voucher ports, authentic Mach timeouts, or Apple framework behavior.
- On Ubuntu ARM64 CI, this is compiled and tested as native host C; it is not an iOS application or an on-phone test. Phase 8 synthetic cross-image ARM64 native calls remain in the test suite.
- Exact frozen PR head #12 `1a0a121fb14f0ccf89ecc7cbeb88d882e247f85e` and #13 `ba7c21a5dd8b40e19c28bf9c34893b996fd4a3d9` passed both Ubuntu CI jobs before expected-head squash merges.

## Phase 10: next concrete coding

1. Replace global port IDs with *per-task port-name tables* that translate scoped names into underlying objects. Test ownership, stale handles, naming collisions, and proper send-right moves/copies across namespaces.
2. Implement bounded send-once rights and reply-port semantics with explicit handoff, one-time consumption and teardown tests. Preserve transactional queueing and receiver capacity.
3. Add a **separate** narrow Darwin-facing C ABI facade that can marshal/unmarshal tested message headers, with deny-by-default unsupported syscalls and no arbitrary Linux syscall forwarding. Do not claim real XNU/Mach compatibility.
4. Add malformed message corpus tests, limits, ownership and rollback invariants for both x86_64 and native ARM64. Reproduce 4K/16K host/dyld assumptions, respecting W^X and no RWX windows.
5. Evaluate a *legally compiled, redistributable* real Darwin sample binary only once execution contracts are explicit. Do not assume arm64e/Apple system frameworks can work just because synthetic fixtures pass.
6. Prioritize a reality check for actual Apple framework runtime/Metal/graphics feasibility before promising SpringBoard or Liquid Glass.

## Process and reproducibility

- Work exclusively on `kallmek3nny/ios-27`; avoid duplicate branches and PRs and check main for concurrent changes.
- Explicit hard timeout on **every** shell/configure/build/test/clone command; no indefinite waiting/polling loops.
- Keep `-Wall -Wextra -Werror -Wpedantic`; never suppress failures or loosen tests.
- Push meaningful code/tests/docs; open PR; require exact frozen head x86_64 and ARM64 success, recheck main, merge with expected PR head SHA only.
- Linux ARM64 CI is not proof of Galaxy S25+ or official iOS compatibility.

Trusted Linux commands:

    timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure

**Progress:** full official iOS UI running on Android remains at 0%. Overall project ~1% *speculative order of magnitude*, not measured completion.
