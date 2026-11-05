# Phase 23b: Mach-O dylib lifecycle (independent native harness)

## Verified scope and safety boundary

- Based on `main` SHA `638b180aeed62ca452848252fedfcba8ccb3b9ed` (2026-10-29).
- `src/phase23b_lifecycle.c` and the matching isolated header implement a bounded thin ARM64 `MH_DYLIB` load-command and `section_64` parser. Accepted callback tables are `__mod_init_func` and `__mod_term_func` only, max 64 entries each, with bounded file offsets, VM spans, alignment, and executable-only callback ownership. The `bind` reader **must** be supplied by an external, independently validated loader after its relocations are applied; raw on-disk pointers are not trusted.
- Each process-local lifecycle graph has at most 16 images, 8 dependencies per image, and 65535 references. A cycle or missing dependency aborts the whole open before mutating counts. Each active graph acquires a dependency ref once, even if reached via two paths. Constructors execute dependency-first and terminators run in reverse topological order and reverse within each image. Duplicate opens do not repeat constructors.
- `pthread_mutex` + condition variable coordinate simultaneous initialization; a same-thread recursive reentry returns `EDEADLK`, and a waiter is bounded to three seconds. A failed constructor sets sticky failure and invokes **caller-provided compensators** for completed constructors in reverse order. There is no universal C/Darwin ABI way to rollback an arbitrary partially completed initializer; `__mod_term_func` is deliberately **not** called on failed initialization. The failure clears only when the final reference drops.
- A close during a callback or initializer returns `EBUSY` without mutating references. Native callback addresses are not directly invoked by this module: all callback work goes through the supplied dispatcher, which is an explicit trust boundary. On a host other than a supported trusted native ARM64 fixture environment, dispatch is simulation only, **not** proof of the Darwin C ABI. No Apple frameworks, arm64e authenticated pointers, generic Android-to-Darwin execution, arbitrary host filesystem access, or untrusted callbacks are supported.

## Original-source compiler fixtures

`tests/phase23b-lib0.c` through `tests/phase23b-lib5.c` are six actual Clang-produced and LLVM `ld64.lld`-linked ARM64 Darwin dylibs. Each has a constructor, a terminator-section callback, mutable state, and an exported function. Libraries 1..5 link against the preceding dylib's TAPI export stub; every linked image is checked for the correct Mach-O filetype, the exact `__mod_init_func` and `__mod_term_func` sections, and an `LC_LOAD_DYLIB` dependency for libraries 1..5.

The SDK-free fixtures deliberately target macOS 11.0 with `-no_fixup_chains`, because modern `-fixup_chains` transforms conventional initializer pointer sections into `__TEXT,__init_offsets`. A minimal unresolved `dyld_stub_binder` is permitted in the fixture's classic lazy-binding path (using `-U dyld_stub_binder`); **there is no runtime implementation or native execution proof of that binder**. This separate verification target exercises the requested section representation without changing phase 22c or any shared build file. Production integration must add support for modern `__init_offsets` separately rather than interpreting them as 64-bit pointer tables.

## Reproduce with hard timeouts

```sh
timeout 300s python3 tools/phase23b-check.py
PHASE23B_SANITIZE=1 timeout 300s python3 tools/phase23b-check.py
```

The independent C harness reads all six genuine dylibs, parses section tables, binds **test-mapped callback values** via an injectable reader, verifies transactional rejection of invalid target addresses, runs concurrent exactly-once initialization, 10k repeated graph load/unload rounds (60k constructions and 60k terminations), 120k reference acquisitions/releases, sticky failure and compensating rollback, recursion, active callback refusal, stale handles, dependency cycles, teardown, and leak counters. It never jumps to the linked ARM64 instructions; simulated callback tests do not establish actual native dyld compatibility.

CI has independent x86_64 and ARM64 Linux runners and ASan/UBSan. Each job checks out the frozen PR head SHA, rejects mismatch, and runs bounded commands with `-Werror`. Existing `CMakeLists.txt` and preexisting dyld, Mach-O, phase 22c files remain untouched. A green frozen-head CI run, SHA-protected merge, and actual 3+ hours of active implementation require separate verification. Do not infer them from this document.

## 2026-10-29 delivery audit

- The observed session started around **17:08 EDT** and the final audit was performed around **17:28 EDT**. This is approximately **20 minutes of elapsed session time**, *not* three hours and not a precise measurement of uninterrupted active coding. The requested 3+ hour active-duration target is **not met** and must not be claimed.
- The x86_64 local standalone tests and ASan/UBSan checks passed using strict warning flags. The six genuine compiler/LLVM linked ARM64 Darwin dylibs were inspected without invoking guest code.
- Frozen-head PR check suite for commit `e1f1da6670fd3592c8fa92277fba33216eeb5a34` completed successfully across **10 checks**, including the phase23b lifecycle workflow on both x86_64 and arm64 and its x86_64 sanitizer step. Its first CI attempt had failed on both runners because the LLVM Mach-O linker was not installed; `phase23b-lifecycle.yml` now installs bounded compiler/linker tooling and `tools/phase23b-build.py` discovers versioned LLVM linkers.
- The final documentation-only follow-up commit requires its **own** frozen-head checks; do not conflate previous successful checks with a newer SHA. Draft PR #96 remains unmerged. Use GitHub's expected PR head SHA protection before merging and require a genuinely green **current head**.
