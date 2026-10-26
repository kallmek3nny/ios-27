# ios-27 development handoff | phase 11 complete -> phase 12

**updated:** october 26, 2026  
**repo:** https://github.com/kallmek3nny/ios-27  
**phase 11 merge:** [pr #18](https://github.com/kallmek3nny/ios-27/pull/18), main commit `62d5db1a381d447a4ceb280f40afaf64f94542fa`. frozen pr head `30528b9528bcba6b07b79d251ad11269cdcc1f58` passed both ubuntu 24.04 x86_64 and arm64 github actions jobs, including 12 ctest suites. safely merged using github's expected-head sha. check current main/open prs again before doing anything.

## actual goal and critical honesty

research native arm64 execution of the **official apple ios 27 springboard, uikit/swiftui/coreanimation liquid glass, control center, lock screen and real ios applications** above android on a locked samsung galaxy s25+, without a lookalike launcher or cpu emulation.

**none of those apple interfaces or apps run yet.** all proven executable examples are synthetic arm64 fixture programs or host-native tests. apple arm64e/pac, xnu mach services and pthread interfaces, code-signing/entitlements, dyld shared cache and apple binary licensing, objective-c/swift runtime, coreanimation/metal rendering and hardware service dependencies remain unresolved. native generic arm64 cpu compatibility is necessary but very far from sufficient. feasibility on a locked samsung remains unproven. `~1%` overall is purely a speculative order-of-magnitude estimate, not a measurable completion percentage.

## absolute samsung safety boundary

- never touch, write or flash bootloader, boot, init_boot, vendor_boot, vbmeta, dtbo, recovery, partition layout, firmware, secure boot, verified boot or antirollback.
- never bypass samsung odin/download-mode d2 restrictions, weaken recovery, install exploits or persistent privileged hooks.
- root / dirtyfrag / kernelsu are **optional and never required**. do not change persistent root state, selinux policy, kernel modules, vendor driver or any phone system partition. reboot does not undo earlier persistent writes.
- run only unprivileged userspace tests with reviewed, legally redistributable **synthetic fixtures**. the native mach-o loader is not a sandbox. never run untrusted mach-o through it.
- never bundle/decrypt/distribute proprietary apple frameworks, ios firmware, unauthorized decrypted binaries or copyrighted system-ui assets.
- if device-specific assumptions must be revisited, inspect https://github.com/mitschud/DirtyFrag and current upstream warnings first. no on-device tests have been completed.

## merged milestones

| phase | merged pr | proven scope |
| --- | --- | --- |
| 1 | #1 | bounded thin arm64 mach-o and freestanding native fixture runner |
| 2 | #2 | deny-by-default synthetic darwin svc probe |
| 3 | #3 | fat/fat64 inventory and import metadata |
| 4 | #4 | guest-virtual dyld path-token parsing |
| 5 | #5 | bounded mach-o export trie |
| 6 | #6 | classic bind planning; native synthetic arm64 cross-image call |
| 7 | #8 | subset of dyld chained fixups with 4k/16k page declarations |
| 8 | #10 | bounded native multi-image dependency graph, private w^x mapping, synthetic calls |
| 9 | #12, #13 | synthetic mach-like fifo and transferable send rights, quotas and in-flight reservation |
| 10 | #15, #16 | task-local port names, one-use reply rights, checked offset-based guest arena ipc |
| 11 | [#18](https://github.com/kallmek3nny/ios-27/pull/18) | atomic synthetic task teardown and narrow fixture-only register selector api; malformed input and lifecycle tests |

## phase 11 implementation and limits

- `include/ios27/mach_v2.h`, `src/mach_v2.c`: `ios27_mach_v2_task_teardown` preflights incoming queued grants for all names of a simulated task and all ports owned by that task. if outstanding in-flight rights exist, it fails **without mutation**. otherwise, it retires the task's receive queues, cancels credits queued on them, revokes all aliases held by other tasks for destroyed ports, and leaves the task-local name counter monotonic. this intentionally fails closed rather than implementing mach dead-name notifications.
- `include/ios27/darwin_mach_abi.h`, `src/darwin_mach_abi.c`: accepts an **explicit, synthetic, private selector** and eight arm64-style 64-bit registers for port allocation, rights grant/drop/inspection, destroy/teardown, and send/receive through phase 10's bounded guest-memory adapter. **these are not real darwin trap numbers** and are never forwarded to linux, android or the host kernel. overlapping register/arena buffers, unknown selectors and too-wide arguments are denied. outputs remain unchanged on errors other than an intentionally reported too-small receive size.
- `tests/test_darwin_mach_abi.c`, `docs/phase11-darwin-boundary.md`: exercise revoked aliases, pending descriptors, queued reply rights, one-time replies, task teardown, malformed offsets/selectors and a synthetic request/reply loop.
- exact frozen head `30528b9528bcba6b07b79d251ad11269cdcc1f58` passed 12 suites on both ubuntu x86_64 and native arm64 ci. phase 10/earlier suites remained green.
- the native tests do **not** establish actual xnu kernel, legal apple framework loading, ios api compatibility, or successful runtime behavior on samsung.

## phase 12: concrete new engineering work

1. **compiler-produced arm64-apple-darwin fixture:** compile a tiny freestanding c or assembly source with a real darwin target toolchain (possibly clang + ld64.lld if present). add deterministic inspection and tests for the actual emitted mach-o *without* proprietary sdk/frameworks or apple assets. explicit tooling checks and hard timeouts. do not equate a hand-built synthetic byte buffer with a real darwin binary.
2. **harden the real fixture loader path:** support the smallest legal mach-o segment/load-command patterns actually emitted by the open-source toolchain. reject unsupported chaining, arm64e/pac, dyld closures, entitlements and code-signing conditions, rather than skipping them. keep executable mapping private and w^x.
3. **task rights lifecycle fuzz/corpus:** bounded deterministic property tests for create/drop/transfer/teardown and saturation, ensuring no stale capability grants and failure-state atomicity.
4. **darwin interface reality check:** use public apple open-source xnu and dyld documentation for narrowly scoped abi comparisons, including true trap arguments, mach descriptors, bootstrap ports and libsystem assumptions. keep private fixture selectors separate from real mach syscall ids.
5. **official apple ui feasibility gate:** document the actual available arm64e framework/system-service dependencies and apple graphics-stack limitations *before* claiming genuine springboard or liquid glass can run.

## engineering workflow

- only modify `kallmek3nny/ios-27`; inspect main SHA and any open prs first, coordinate concurrent work.
- every shell/clone/configure/build/test/install invocation uses an **explicit hard timeout**, no indefinite polling loops.
- keep `-Wall -Wextra -Werror -Wpedantic`; never loosen tests to pass.
- push meaningful implementation/tests/docs; create a focused pr; verify **both x86_64 and arm64 ci** on its exact frozen head. recheck main has not moved before merging with expected-head sha.
- linux arm64 ci is not proof of galaxy s25+ compatibility.

trusted linux:

    timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
