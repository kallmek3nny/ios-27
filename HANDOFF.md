# ios-27 development handoff | phase 13 complete -> phase 14

**Updated:** 2026-10-26
**Repo:** https://github.com/kallmek3nny/ios-27
**Last verified main:** 3f3a12d2b15903d085076a5649cd27cdd9f43b6b
**Phase 13:** [PR #22](https://github.com/kallmek3nny/ios-27/pull/22), merged as 3f3a12d2b15903d085076a5649cd27cdd9f43b6b.
Exact frozen PR head aa14543c0f06675c87cf45dd0dc50f9a6d5043f3 passed **both Ubuntu 24.04 x86_64 and ARM64 CI** run 40390271857, with 14 CTest suites. Recheck current main and open PRs before coding.

## Non-negotiable goal and current reality

Research running Apple's **actual official iOS 27 SpringBoard, UIKit/SwiftUI/CoreAnimation Liquid Glass, Control Center, lock screen and genuine iOS applications** above Android on a locked-bootloader Samsung Galaxy S25+. No imitated launcher or CPU emulation.

**No actual Apple UI or iOS app currently runs.** We have synthetic ELF/Linux host tests, compiler-produced ARM64 Darwin unlinked object files, and limited Mach-O mapping and userspace Mach-message compatibility. ARM64 CPU instructions alone are not enough to reproduce Apple's arm64e/PAC, dyld caches, code signing/entitlements, XNU/Mach/libsystem, ObjC/Swift, proprietary services, graphics and GPU integration. Real official UI feasibility on this locked Samsung remains unproven. Overall ~1% is only a speculative order-of-magnitude, not a measured progress metric.

## Hard device and legal safety

- NEVER touch or flash bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, firmware, partition table, secure boot, verified boot or antirollback.
- NEVER bypass Odin/download-mode D2 safeguards, install device exploits, modify SELinux/kernel or weaken recovery.
- DirtyFrag/KernelSU root is optional and NOT used. Do not modify persistent root state or privileged boot-time hooks. A reboot cannot undo prior persistent writes.
- Only unprivileged ordinary Linux/Android userspace, trusted reviewed fixtures. The native Mach-O runner is **NOT a sandbox**: never execute untrusted binaries.
- Do not bundle or redistribute proprietary Apple SDK, framework blobs, firmware or unauthorized decrypted executables. Review https://github.com/mitschud/DirtyFrag before making any phone-specific safety assertions.
- ARM64 Linux CI success is NOT Galaxy S25+ validation. No on-device tests have been run.

## Proven merged phases

| Phase | PR | Verified result |
| --- | --- | --- |
| 1 | #1 | thin ARM64 Mach-O parser and freestanding synthetic native runner |
| 2 | #2 | deny-by-default synthetic Darwin SVC probe |
| 3 | #3 | FAT/FAT64 ARM64 slice and dylib inventory |
| 4 | #4 | guest-virtual dyld path tokens |
| 5 | #5 | bounded Mach-O export trie |
| 6 | #6 | classic dyld bind planning and native synthetic ARM64 cross-image call |
| 7 | #8 | subset of dyld chained fixups and 4K/16K chain-page declarations |
| 8 | #10 | explicit synthetic image dependency graph, private W^X maps, cross-image native calls |
| 9 | #12, #13 | bounded synthetic Mach-like queues, send rights, atomic quota reservations |
| 10 | #15, #16 | task-local Mach port namespaces, send-once replies, bounded guest arena |
| 11 | #18 | synthetic task teardown, fixture-only register selector interface |
| 12 | #20 | genuine Clang-produced SDK-free ARM64 Darwin MH_OBJECT section/nlist inspection; native relocation-free call returns 42 |
| 13 | [#22](https://github.com/kallmek3nny/ios-27/pull/22) | two genuine Clang-produced Darwin MH_OBJECT files, real external ARM64_RELOC_BRANCH26 validation/patching; native ARM64 cross-object call returns **43** |

## Phase 13 precise scope

- Files: include/ios27/darwin_branch26.h, src/darwin_branch26.c, tests/fixtures/arm64_darwin_consumer.c, tests/test_compiled_darwin_reloc.c, docs/phase13-clang-branch26.md, CMakeLists.txt.
- Clang independently compiles an SDK-free original provider object defining _fixture_entry (returns 42), plus a consumer calling _fixture_entry and adding 1. Compiler emits real Mach-O `ARM64_RELOC_BRANCH26` data.
- Inspector requires MH_OBJECT, ARM64_ALL subtype, macOS build version, bounded nlist and string tables, exact undefined _fixture_entry and defined _fixture_consumer, precisely one external PC-relative long BRANCH26 relocation on an unpatched BL instruction. Rejects arm64e/PAC, unsupported symbols, malformed offsets and other relocations.
- Patch routine validates alignment and signed BL branch range and updates only staged writable bytes after checks. Negative tests verify rollback and malformed metadata rejection.
- On ARM64 Linux, tests copy the **trusted** separately compiled code sections into a private mapping, patch external call, switch RW to RX (no RWX) and execute the consumer returning 43 without CPU emulation. On x86_64, only validation and displacement tests run. All 14 CTest suites passed on both CI architectures.
- **Not a real linked Darwin executable or iOS app.** A local attempt with installed Clang 17 / ld64.lld rejected macOS linkage with 'This version of lld does not support linking for platform macOS'. Do not claim a Mach-O MH_EXECUTE, a Darwin process, or Apple framework execution.

## Phase 14 substantive next steps

1. Find/install/use a reproducible **open-source Darwin Mach-O linker** supporting AArch64 on both CI runners. Build a legitimate SDK-free freestanding MH_EXECUTE from original C/assembly; inspect actual LC_MAIN, load commands, segment protections, CPU/platform flags and entrypoint. Never fabricate a linked executable to claim success.
2. If a real ARM64 linker cannot be made available, expand fail-closed real compiler-object relocations (e.g. PAGE21/PAGEOFF12 or ABS64) and measured native cross-object data references, with tight bounds, real C fixtures and deterministic corruption tests. Do not substitute synthetic bytes.
3. Improve mapped executable loader compatibility only after analyzing actual linker output; maintain W^X, untrusted binary rejection and clean rollback. Verify actual 16K host-page behavior separately.
4. Add deterministic bounded Mach rights/teardown property tests with stale names, in-flight transfers and queue saturation.
5. Produce evidence-based feasibility review of Apple arm64e/PAC, dyld shared caches/closures, XNU services, ObjC/Swift, code signing, licensing and Metal/CoreAnimation, avoiding exaggerated claims about real Apple UI readiness.

## Workflow

- Only change kallmek3nny/ios-27. Check latest main, open PRs and concurrent changes before work.
- Every shell/build/test/clone/install/link command has **explicit hard timeout**. No indefinite polling loops.
- Keep -Wall -Wextra -Werror -Wpedantic. Do not weaken warnings/tests to pass CI.
- Push meaningful code/tests/docs, open PR, require **both x86_64 and ARM64 CI on frozen exact head**, verify main hasn't moved, then merge with expected_head_sha lease.
- Never infer Samsung on-device compatibility from ARM64 CI.

Reproduce on trusted Linux:

    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure