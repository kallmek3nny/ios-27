# ios-27 development handoff | phase 8 complete -> phase 9

**Updated:** 2026-10-26  
**Repo:** https://github.com/kallmek3nny/ios-27  
**Verified phase 8:** [PR #10](https://github.com/kallmek3nny/ios-27/pull/10) merged into main as fadbaf2bfb3313edac0bbc330d5cd28348090198. Exact PR head 50bf52e413e25f0e098cf6b3880f73016b8619ce passed Ubuntu 24.04 x86_64 and arm64 CI with strict warnings and bounded commands. Check latest main and PRs before continuing.

## Goal and current reality

Research native ARM64 execution of **actual official iOS 27 SpringBoard, UIKit/SwiftUI/CoreAnimation, Liquid Glass, and iOS apps** above Android on a locked-bootloader Galaxy S25+. No imitation launcher and no general CPU emulation. The real Apple UI is **not** running and feasibility is unproven. iOS proprietary frameworks, XNU/Mach interfaces, arm64e/PAC, entitlements, Apple GPU and system services remain unsolved. A synthetic native test returning 42 does NOT mean an iOS app runs. Treat overall roughly ~1% as speculative order-of-magnitude, not a validated percentage.

## Safety: hard boundaries

- Never change/flash bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, partitions, firmware, secure boot, antirollback or recovery paths.
- Never bypass Odin/D2 restrictions, weaken verified boot or persistently modify root/SELinux/kernel state.
- DirtyFrag, KernelSU and temporary root are optional, NOT used in baseline; root can create non-revertible changes. No root/privileged writes or exploit deployment.
- Use isolated ordinary Android/Linux userspace, trusted synthetic non-Apple fixtures, and legally redistributable source only. The native test loader is **not sandboxed**, so do NOT execute untrusted Mach-O binaries.
- Never distribute Apple proprietary framework binaries or decrypted firmware/app payloads.
- If reviewing device safety details, check https://github.com/mitschud/DirtyFrag and its releases for updated warnings.

## Proven merged work

| Phase | PR | Actual result |
| --- | --- | --- |
| 1 | #1 | thin ARM64 Mach-O validation and restricted native fixture runner |
| 2 | #2 | bounded synthetic Darwin SVC instruction probe |
| 3 | #3 | FAT/FAT64 slice selection and imported framework inventory |
| 4 | #4 | guest-virtual dyld path tokens |
| 5 | #5 | bounded export trie decoding |
| 6 | #6 | classic dyld binds, synthetic native ARM64 cross-image call |
| 7 | #8 | subset of dyld chained fixups, PTR_64_OFFSET, bounded 4K/16K page fixups and atomic copy-only relocation |
| 8 | [#10](https://github.com/kallmek3nny/ios-27/pull/10) | explicit 8-image dependency DAG, deterministic cross-image import resolution, private anonymous VM mappings, staged classic/chained fixups, W^X code/data protections, rollback, native ARM64 multi-image calls; x86_64+arm64 CI passed |

**Phase 8 files:** include/ios27/images.h, src/images.c, tests/test_images.c, docs/phase8-native-images.md. The loader accepts only caller-supplied thin ARM64 buffers and exact LC_LOAD_DYLIB dependency names. It validates cycles, duplicates, mapped segment bounds, page alignment, exported regular executable symbols and classic/chained relocation formats. All images reserve VM before symbol resolution; copy-only staging completes before segment data is committed. No RWX mappings; gaps inaccessible; failures unmap everything. The ARM64 Linux CI test exercises both classic/chained synthetic consumer→provider native calls returning 42. 4K/16K **dyld chain** page cases are tested, not a physical Samsung 16K kernel mapping.

**Not yet validated:** actual iOS 27 binaries, dyld shared cache, PAC/arm64e ABI, load from real dylib path caches, objc/swift, Mach message system, real Darwin libsystem/pthreads, Metal/CoreAnimation, SpringBoard, Liquid Glass, Galaxy S25+ userspace execution. Host 16K shared-page layout conflicts intentionally fail closed.

## Phase 9 substantive engineering tasks

1. Design **scoped, testable Darwin/Mach message ABI contract** in ordinary userspace using declared message headers/descriptors and bounded buffers. Match open Darwin headers where licensing permits; keep own implementation. Do not claim XNU.
2. Add a port-rights model with correct owner isolation, send/receive references, checked transfer/deallocation and explicit limits. Include intentionally invalid/destructive message tests and cleanup.
3. Implement bounded synchronous message exchange through a fixture-local queue (not global host ports), with deterministic timeout/error semantics; no host privilege or IPC service takeover.
4. Write strict x86_64 and ARM64 tests including native ARM64 synthetic code calling through a stable compatibility C ABI boundary where feasible.
5. Tighten image loader with more malformed-segment/fuzz corpus and host 16K-alignment testing where accessible. Independent lifecycle, reexports/weak bindings and dyld shared caches must remain unsupported until real tests exist.
6. Evaluate a legitimately compiled, redistributable Darwin test executable if one can be obtained legally; do not substitute synthetic Mach messages for proof of real Apple compatibility.

## Exact workflow

- Work only on kallmek3nny/ios-27. Confirm newest main/PRs and avoid duplicating concurrent work.
- Every shell/configure/build/test/clone command has an explicit hard timeout. No indefinite polling loops.
- Maintain -Wall -Wextra -Werror -Wpedantic. Include meaningful implementation, regression tests, documentation.
- Branch from inspected main; push, PR; verify x86_64 and ARM64 CI on **exact frozen PR head**; recheck main and merge using expected_head_sha only if both pass.
- Do not claim on-phone tests from Linux ARM CI.

## Reproduce on trusted Linux

    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
