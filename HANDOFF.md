# ios-27 development handoff | phase 12 verified -> phase 13

**updated:** october 26, 2026  
**repo:** https://github.com/kallmek3nny/ios-27  
**verified phase 12 code merge:** [pr #20](https://github.com/kallmek3nny/ios-27/pull/20), main sha `83f9f312799129ad58e8079811e644c3841ff77c`. exact frozen pr head `99d4b4bdcafc3a0f307686dc5e29359c8ff88956` passed ubuntu 24.04 x86_64 and ARM64 github actions, 13 CTest suites including the compiler-produced Darwin object test. check latest main and open PRs before continuing.

## ultimate goal, with truthful limitations

research native arm64 execution of **Apple's actual official iOS 27 SpringBoard, UIKit/SwiftUI/CoreAnimation-backed Liquid Glass, Control Center, lock screen, and real iOS apps** above the existing Android/Linux userspace on a **locked-bootloader Galaxy S25+**, with no lookalike launcher and no CPU instruction emulation.

**The genuine iOS UI and applications do not run at all.** ARM64 instruction similarity alone does not reproduce Apple's arm64e/PAC requirements, XNU/Mach, private framework/system services, dyld caches and closures, Objective-C/Swift ABI, entitlements/signing, GPU/IOSurface/Metal/CoreAnimation, or secure device services. Locked-device feasibility is not established. Overall project completion around ~1% is a *speculative order-of-magnitude*, not a measured percentage.

## non-negotiable Samsung safety

- never flash or write bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, firmware, partition metadata, secure/verified boot or antirollback.
- never bypass Odin/download-mode D2 safeguards, modify recovery paths, install persistent root/KernelSU/DirtyFrag changes, exploit the device, patch SELinux or install kernel modules.
- DirtyFrag soft root is **optional**, never required by this code. Reboot cannot undo persistent changes made during root.
- only unprivileged userspace work with reviewed, legally redistributable test fixtures. The native executor is NOT a sandbox: **never run untrusted Mach-O binaries**.
- never ship Apple proprietary framework blobs, decrypted firmware or unauthorized decrypted apps. Device-specific warnings: https://github.com/mitschud/DirtyFrag.
- no on-device Galaxy validation so far.

## proven work

| phase | PR | tested scope |
| --- | --- | --- |
| 1 | #1 | bounded thin ARM64 Mach-O parser and native freestanding fixture runner |
| 2 | #2 | deny-by-default synthetic Darwin SVC probe |
| 3 | #3 | FAT/FAT64 ARM64 imports and inventory |
| 4 | #4 | guest-virtual dyld path handling |
| 5 | #5 | bounded export trie parser |
| 6 | #6 | classic bind fixture and native ARM64 synthetic cross-image call |
| 7 | #8 | narrow chained fixups, unauthenticated 64_OFFSET and 4K/16K chain pages |
| 8 | #10 | synthetic multi-image mapping, dependency DAG, W^X and native calls |
| 9 | #12, #13 | synthetic Mach-style FIFO send-rights/quotas |
| 10 | #15, #16 | task-local names, send-once replies, bounded guest byte arena |
| 11 | #18 | transactional task teardown and synthetic register-selector ABI |
| 12 | [#20](https://github.com/kallmek3nny/ios-27/pull/20) | **actual Clang-produced** SDK-free ARM64 Darwin Mach-O MH_OBJECT, inspected by parser and executed as trusted relocation-free section on Linux ARM64 without CPU emulation |

## phase 12 implementation and exact claim

- `tests/fixtures/arm64_darwin_fixture.c`: original small freestanding C source, no SDK or Apple binaries.
- CMake compiles this source on BOTH CI hosts via installed `clang --target=arm64-apple-darwin -c` with **30s timeout**, producing an **unlinked relocatable Mach-O MH_OBJECT** in the build directory.
- `include/ios27/darwin_object.h` and `src/darwin_object.c`: strictly bounded inspection of ARM64_ALL object headers, `LC_BUILD_VERSION` macOS, `__TEXT,__text` sections, nlist string/symbol data and external `_fixture_entry` offset. Reject relocations, undefined symbols, arm64e/PAC subtypes, malformed bounds or unsupported layouts.
- `tests/test_compiled_darwin_object.c`: negative mutation tests on filetype, CPU subtype, platform, relocations, string table, symbol table; x86_64 CI inspects metadata only. On ARM64 Linux CI, copy just the **verified, relocation-free trusted Clang-produced `__text`**, seal anonymous mapping RW->RX, cache sync, and natively call `_fixture_entry`, returning 42. This is no CPU emulation.
- verified in PR #20 workflow run `40387501782`, with **13 total CTest suites green on x86_64 and ARM64**. Existing synthetic multi-image and Mach IPC tests still pass.
- **NOT** a linked Darwin executable, native iOS app, genuine UIKit, arm64e, code signing or process loader. A local test of the available `ld64.lld` 17 failed to link for macOS/iOS with "This version of lld does not support linking for platform macOS/iOS". Do not silently claim linker support or ship a handcrafted image as a compiler-linked binary.
- The MH_OBJECT may contain RWX **metadata** (not actual runtime segment permissions). It is not passed into the ordinary executable loader, and the ARM64 test maps only trusted section bytes RW then RX (never RWX).

## phase 13: highest-priority next engineering

1. Find and validate a real *open-source* Darwin/Mach-O linker flow capable of producing a **linked freestanding ARM64 Mach-O MH_EXECUTE** from owned sources. Confirm correct entry/segment/load-command forms on both CI architectures. No proprietary SDK redistribution, forged signing or Apple private assets.
2. Implement fail-closed inspection/support for the actual linker's LC_MAIN vs LC_UNIXTHREAD, DYLINKER and load-command patterns; separate genuine linked-file metadata support from synthetic fixtures. Keep W^X, no unsupported arm64e/PAC, dyld shared cache, or sandbox claims.
3. If no available real linker can produce compliant output, build rigorous **object relocation and nlist** tests with compiler-produced relocation-bearing MH_OBJECT objects, without calling this a linked process.
4. Add deterministic fuzz/property tests for Mach port rights, queued grants, stale handles, and atomic teardown. Do not weaken strict warnings/tests.
5. Conduct an evidence-based feasibility gate for real Apple frameworks, ObjC/Swift, arm64e/PAC, dyld shared cache, entitlements and Metal/CoreAnimation on Linux/Android. Resist interpreting fake/synthetic UI as progress toward official SpringBoard.

## workflow

- work exclusively on `kallmek3nny/ios-27`; inspect current main SHA and open PRs before changes, and check for concurrent modifications.
- every shell, clone, build, test, compiler and linker command must have an **explicit hard timeout**. No open-ended polling.
- preserve `-Wall -Wextra -Werror -Wpedantic`. Ship meaningful code, tests and docs. Never suppress a failing test to make CI green.
- merge only when the **frozen PR head** passed both ubuntu x86_64 and ARM64 jobs, main hasn't moved unexpectedly, and the GitHub merge uses the `expected_head_sha` lease.
- do not mistake Linux ARM64 CI for Samsung on-device operation.

trusted Linux verification:

    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
