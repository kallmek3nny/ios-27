# ios-27 development handoff | phase 7 complete -> phase 8

**Updated:** 2026-10-26  
**Repository:** https://github.com/kallmek3nny/ios-27  
**Verified phase 7 merge:** [PR #8](https://github.com/kallmek3nny/ios-27/pull/8), main commit e9491cfb1b95de6bf6573e89e47fd014c6f3b97b  
**CI:** exact PR #8 head 3d6f5b33e0324a51d3026d5eac19f48d67af99e3 passed Ubuntu 24.04 x86_64 and ARM64 build, tests and CLI usage. Reconfirm main/PRs before continuing.

## Actual goal

Run **Apple's real iOS 27 UI**, SpringBoard, UIKit/SwiftUI/CoreAnimation, Liquid Glass and iOS apps natively on ARM64 above Android on a locked Galaxy S25+. Not an Android theme and not CPU emulation. Feasibility remains unknown; CPU instruction compatibility alone does not solve Apple system frameworks, arm64e/PAC, Mach/XNU, entitlements, graphics or services.

## Critical Samsung safety

- Never change/flash bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, firmware, antirollback, verified boot or partitions.
- Never bypass Odin/D2 restrictions or weaken recovery.
- No persistent root, privileged filesystem writes, SELinux policy changes, exploit deployment, boot hooks or kernel modules.
- DirtyFrag/KernelSU/root are optional and not needed now. Any future root experiment requires review and explicit opt-in, without persistent changes. Reboots do not undo previously made persistent writes.
- Run only reviewed, trusted synthetic fixtures in unprivileged userspace. Do not run unknown code through the unsandboxed freestanding test runner.
- Never distribute proprietary Apple framework binaries, firmware or unauthorized decrypted application payloads.
- DirtyFrag background: https://github.com/mitschud/DirtyFrag.

## Merged milestones

| Phase | PR | What really works |
| --- | --- | --- |
| 1 | #1 | Thin ARM64 Mach-O parser and restricted native freestanding fixture |
| 2 | #2 | Bounded synthetic ARM64/Darwin syscall probe |
| 3 | #3 | FAT/FAT64 and import/framework inventory |
| 4 | #4 | Guest-virtual dyld path token resolution |
| 5 | #5 | Export-trie metadata parsing |
| 6 | #6 | Classic pointer binds; synthetic native ARM64 cross-image call returns 42 |
| 7 | [#8](https://github.com/kallmek3nny/ios-27/pull/8) | Bounded modern chained fixups, PTR_64_OFFSET format 6, import format 1, single-start 4K/16K pages, atomic copy-only bind/rebase, tested x86/ARM64 native fixtures |

Phase 7 files: include/ios27/chained.h, src/chained.c, tests/test_chained.c, docs/phase7-chained-fixups.md. The code rejects arm64e authenticated pointers, compressed symbols, multistarts, weak imports and unknown pointer encodings; it is **not** dyld or a real iOS process.

## Phase 8: substantive next work

1. Isolated **native multi-image** ARM64 loader taking only explicit, trusted, synthetic Mach-O fixture bytes; no ambient host-file lookup or Apple binary redistribution.
2. Bounded dependency graph by dylib ordinal/name; reject missing, duplicate and cycling dependencies. Explicit image lifecycles: parsed, mapped, relocated, ready, retired.
3. Validate addresses and map safe userspace segments with W^X, no RWX. Roll back all mappings and staged relocation buffers on error.
4. Wire the existing export trie and classic/chained fixup resolution across multiple synthetic images. Reject reexports/stub resolvers until genuinely supported.
5. Add x86_64 parser/lifecycle tests and ARM64 native inter-image call tests; test 4 KiB and 16 KiB independent of actual runner VM page size.
6. After mapping, add a separately testable **Darwin Mach-port/message compatibility** boundary in ordinary userspace. Do not claim real XNU, UIKit or SpringBoard until tested.

No general ARM64e/PAC support, dyld shared cache, ObjC/Swift, broad Darwin libsystem/pthreads, GPU compositor, SpringBoard/BackBoard, liquid glass or app lifecycle exists. No Galaxy on-device validation. Overall project is **roughly ~1% as a speculative order-of-magnitude estimate**, not measured completion.

## Workflow

- Work exclusively in this repo. Recheck latest main SHA and open PRs before coding.
- Every shell, clone, build and test command has an **explicit hard timeout**; no unbounded polling.
- Preserve -Wall -Wextra -Werror -Wpedantic. Push meaningful code, tests and docs.
- Verify exact frozen PR head CI on Ubuntu x86_64 and ARM64, verify main has not moved, then safely merge with expected-head SHA.
- Never mistake Linux ARM64 CI for Samsung compatibility.

On a trusted Linux host:

    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --output-on-failure
