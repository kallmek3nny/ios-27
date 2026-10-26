# ios-27 development handoff | phase 6 complete -> phase 7

**Snapshot date:** 2026-10-26  
**Repository:** https://github.com/kallmek3nny/ios-27  
**Verified pre-handoff main commit:** `64206299fc7c176711f6763764ba11a0c7d131d9` (phase 6 squash merge)  
**State at handoff:** PRs #1-#6 merged, no open PRs when checked; all phase 6 PR CI jobs succeeded on Ubuntu x86_64 and Ubuntu ARM64. Recheck the repo head, branches, PRs and CI before beginning work.

## The goal, without substitutions

The user wants **Apple's actual, official iOS 27 UI and iOS apps**, including genuine SpringBoard, Control Center, lock screen, UIKit/SwiftUI/CoreAnimation and Liquid Glass, **running over Android** on a Samsung Galaxy S25+ with a **locked bootloader**. The requested architecture uses native ARM64 CPU execution whenever compatible, not general CPU emulation or a visually similar Android launcher.

This is a **research target**, not a currently working port and not guaranteed to be achievable. Generic AArch64 instruction compatibility alone does not supply Apple's kernel interfaces, arm64e/PAC semantics, Apple framework binaries, Metal/CoreAnimation compositor, entitlements, services or hardware integration.

**Do not substitute a themed launcher, simulated Liquid Glass, or another Android skin for the official Apple UI.** Do not claim progress toward the requested UI from a merely pretty mockup.

## Device and recovery safety: hard requirements

- **Do not change or flash the bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, partition tables, device firmware, secure boot, or antirollback state.**
- **Do not modify Odin/download mode, attempt to bypass Samsung's D2 restriction, or weaken verified boot or recovery.** DirtyFrag documentation discusses a D2/Odin restriction on some newer Samsung firmware/configurations; treat this as a recovery-risk warning, not proof of a universally bricked bootloader.
- Do not require root, DirtyFrag, KernelSU or kernel module changes in the baseline implementation. The user allows exploring optional root **only if genuinely needed**, but any such experiment must be opt-in, isolated, reviewed, and nonpersistent; no privileged write operations. On-device testing should start unprivileged.
- DirtyFrag's soft root is temporary, but **a reboot cannot reverse persistent changes previously made while rooted**.
- No boot-time hooks, persistent SELinux modifications, exploit deployment, privileged partitions, or unsigned flash images. Keep all build/CI steps on ordinary Linux or userspace Termux and only execute trusted, reviewed synthetic native tests.
- Do not distribute Apple firmware, proprietary Apple framework images or unauthorized decrypted app payloads. Use lawful inputs and local fixtures.
- Primary background: https://github.com/mitschud/DirtyFrag and https://github.com/mitschud/DirtyFrag/releases. Recheck upstream documentation before revisiting any device-related assumption.

## Actual implementation, through phase 6

| Phase | PR | What was actually implemented | What it does NOT prove |
| --- | --- | --- | --- |
| 1 | [#1](https://github.com/kallmek3nny/ios-27/pull/1) | C11 thin ARM64 Mach-O parser/validator; restricted ARM64 in-process freestanding test loader | No iOS framework/app support |
| 2 | [#2](https://github.com/kallmek3nny/ios-27/pull/2) | A tiny bounded ARM64 instruction probe; deny-by-default Darwin-style SVC handling for basic identity syscalls and virtual exit | Not broad instruction emulation, XNU, or Mach IPC |
| 3 | [#3](https://github.com/kallmek3nny/ios-27/pull/3) | FAT/FAT64 ARM64 slice selection and import/framework inventory; `audit-ios` | Metadata inspection does not load Apple frameworks |
| 4 | [#4](https://github.com/kallmek3nny/ios-27/pull/4) | Guest-only `@rpath`, `@loader_path`, `@executable_path` resolution | Not actual dyld loading |
| 5 | [#5](https://github.com/kallmek3nny/ios-27/pull/5) | Bounded dyld export-trie decoding, reexport/stub symbol metadata; `symbols` | Does not resolve all dependencies or execute iOS |
| 6 | [#6](https://github.com/kallmek3nny/ios-27/pull/6) | Classic dyld pointer bind-opcode planning + atomic fixups on a caller-owned file buffer; `binds`; synthetic native ARM64 cross-image binding/invocation | Not Apple's dyld, modern chained fixups, or a real iOS process |

**Most substantial executable milestone:** An ARM64 CI test binds the `_answer` symbol from a separate synthetic Mach-O provider into a synthetic Mach-O consumer and makes a native cross-image call that returns `42`. This is **real native ARM64 execution of hand-built test components on an ARM64 Linux runner**, not execution of an iOS app and not validation on the Samsung device.

**Last verified test state:** the exact phase 6 PR head `388212ad3a9f6c72bffbf97c3b749f6d1ccb72ca` passed both Ubuntu 24.04 and Ubuntu 24.04 ARM jobs with `-Wall -Wextra -Werror -Wpedantic`, then merged as `64206299fc7c176711f6763764ba11a0c7d131d9`.

### Repository map

- `src/macho.c`, `include/ios27/macho.h`: thin ARM64 Mach-O validation and segment metadata.
- `src/runner.c`: intentionally limited freestanding native ARM64 execution.
- `src/darwin_probe.c`: synthetic Darwin SVC micro-interpreter and deny-by-default dispatch.
- `src/inventory.c`: FAT/FAT64 ARM64 slice choice, dylib imports, framework counts.
- `src/dyld_paths.c`: **guest-virtual** install-name path resolver.
- `src/exports.c`: dyld export-trie symbol discovery.
- `src/binds.c`: classic dyld pointer binds, validation and copy-only fixups.
- `src/main.c`: CLI (`inspect`, `probe-darwin`, `audit-ios`, `resolve-dyld`, `symbols`, `binds`, plus the opt-in restricted `run-freestanding`).
- `tests/test_*.c`: parser, probe, inventory, paths, export trie, and bind/cross-image tests.
- `.github/workflows/ci.yml`: Ubuntu x86_64 and ARM64 build/tests.
- `docs/official-ios-ui-roadmap.md`: the explicit **real Apple UI**, not theme, goal.
- `docs/architecture.md`: root/boot safety and earlier milestones (some roadmap wording predates later phases; prefer this handoff and current code for up-to-date state).

Build/test on a trusted Linux developer runner:

```sh
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --output-on-failure
```

On Android/Termux use non-root compilation first. Avoid executing untrusted binaries with `run-freestanding --unsafe`; it is **not sandboxed**.

## Not implemented or validated

- Full dyld loader, native multi-image lifecycle, ordinary iOS binaries.
- **Modern dyld chained fixups**, dyld shared cache, rebases, lazy/weak binds, relocation varieties, arm64e authenticated pointers, runtime code-signing contracts.
- Mach ports/messages, Darwin threading/workqueues/pthreads, launchd and process supervision, broadly compatible Darwin syscalls.
- Objective-C/Swift runtime, Foundation, UIKit, SwiftUI, CoreAnimation, IOSurface, Metal-to-Android-GPU rendering.
- BackBoard/FrontBoard/SpringBoard services, actual SpringBoard, Liquid Glass, iOS app lifecycle, Apple account services, App Store apps.
- No Android UI integration or end-to-end Galaxy S25+ compatibility test. CI is ordinary Linux only.
- No proof that official iOS 27 UI is possible on this device. Treat overall ~1% as a **speculative order-of-magnitude estimate**, not a validated metric.

## Next agent: do real coding immediately (phase 7)

**First:** confirm `main` HEAD, open PRs and latest CI. Avoid duplicating any branch or milestone already developed since this handoff. Keep all changes exclusively in `kallmek3nny/ios-27`.

**Primary task:** implement a **bounded, safety-first subset of modern `LC_DYLD_CHAINED_FIXUPS`** in the existing native loader tooling. Prefer a coherent slice that supports a specific nonauthenticated arm64 pointer format in *synthetic, self-contained Mach-O test images*, with production-sized bounds checks, and clearly reject other formats.

Suggested concrete acceptance criteria:

1. Parse fixup header, segment/page starts, import table, symbol strings, and selected pointer chain formats **without touching any host files or privileged addresses**.
2. Decode authenticated versus nonauthenticated pointer formats; **explicitly reject arm64e PAC/auth binds and unsupported formats**, don't silently mis-handle them.
3. Handle per-page chain walks only within declared file-backed/writable segment boundaries, with cycle/step, count, arithmetic and offset limits.
4. Resolve symbols through an explicit fixture-only resolver, build a fixup plan, and apply it **atomically to a private buffer** after all checks pass. Unsupported relocations must fail closed.
5. Add regression tests for valid rebases/binds, malformed offsets/ordinals/ULEBs, overflow, bad symbols and wrong page sizes. Test 4KiB/16KiB assumptions separately from the host page size.
6. If the implementation and tests are stable, extend the existing synthetic multi-image AArch64 native-call test to a modern chained-fixup provider/consumer pair. **Do not mistake that for running genuine iOS apps.**

After phase 7, prioritize an isolated native multi-image mapper with relocations, W^X permissions and explicit per-image lifecycle; then a meaningful Darwin/Mach ABI boundary and a genuinely compiled, redistributable Darwin sample binary. Evaluate the real graphics/framework feasibility with evidence before promising SpringBoard integration. Keep the actual user goal visible while selecting each milestone.

## Branch, CI and merge discipline

- Branch from freshly verified `main`; commit meaningful code, tests, and docs together. Push work in meaningful chunks and open a PR.
- Every invoked shell/build/test/configure/clone command gets an **explicit hard timeout**. Avoid indefinite loops and unbounded polling. Preserve strict compiler warnings and `-Werror`.
- Verify x86_64 and ARM64 CI on the **exact, frozen PR head**. Diagnose/fix failures; do not weaken the checks. Do not claim on-device tests from an ARM CI runner.
- Check for concurrent `main` movement, review new commits before merging, and merge using the expected PR head SHA/lease. Do not claim a merge until GitHub confirms it.
- Keep reports concise with actual merged PR numbers, commit IDs, tested results, remaining blockers, and realistic estimates rather than synthetic milestone percentages.
- Respect device safety even if optional root is available; **no phone flashing or boot-chain experimentation**.

## Short next-agent starter

> Continue real coding on https://github.com/kallmek3nny/ios-27. Read `HANDOFF.md`, `docs/official-ios-ui-roadmap.md`, and current code. Phase 6 merged at `6420629`; verify current main/PRs first. Implement phase 7: bounded modern dyld chained fixups with atomic in-memory binding/rebasing and rigorous x86_64/ARM64 CI tests. Use native ARM64 test execution where applicable. Goal is the genuine Apple iOS 27 UI/apps above Android, not an Android-style clone; feasibility remains uncertain. **Never touch Samsung bootloader, firmware, partitions, Odin/D2 recovery, or persistent root state.** Keep all shell commands time-bounded, push meaningful code, get exact-head CI green and merge only safely. Give a short verified rundown.
