# phase 8: explicit native multi-image runtime

Phase 8 adds a **synthetic, fixture-only** ARM64 Mach-O image graph loader,
not an iOS kernel, dyld replacement, official UI runtime or phone installer.

## Implemented

- The caller provides a bounded list of up to 8 named Mach-O buffers and
  exact, ordered dependency names. Names must match the image's LC_LOAD_DYLIB
  commands. No filesystem scanning, implicit host framework lookup, weak or
  special dependency resolution or library fetches.
- Rejects duplicate/missing/self/cyclic dependencies, unsupported reexports,
  non-executable or overlapping VM segments, misaligned pages and W+X segments.
- Parses valid export tries and classic or supported phase-7 chained fixup
  plans before allocating runtime mappings.
- Reserves all image VM spans with private anonymous PROT_NONE memory,
  stages every bind/rebase into independent **caller-owned byte copies**,
  and resolves imported addresses against the reserved mapping of an
  explicitly named provider. All images must relocate before mapping writes.
- Copies file-backed segments and zero-fills BSS in temporary RW-only pages;
  transitions text to RX, writable data to RW, and leaves unmapped gaps
  inaccessible. No RWX window is used; any failure frees the entire graph.
- Provides explicit image states and unload cleanup. Runtime export lookup
  returns actual native mapped addresses, but never executes them itself.
- Regression tests cover modern chained and classic binds, 4K/16K dyld
  declared page layouts, malformed dependency graphs and fixups, invalid
  W^X/overlap/alignment, caller byte immutability and rollback. The ARM64
  Linux CI runner invokes a fixture consumer that branches to a fixture
  provider function returning 42.

## Boundaries

- Only Linux/Android ordinary userspace private anonymous mapping is in scope.
- This does not understand dyld shared caches, multi-start/authenticated
  arm64e/PAC fixups, real iOS code-signing, Objective-C/Swift, Mach/XNU,
  UIKit, CoreAnimation, Metal, SpringBoard or Liquid Glass.
- Segment VM alignment follows the host OS page size. A 16K-host device
  may reject layouts using separate text/data protections within one 16K
  page; that is an intentional safety restriction, not a proof of support.
- Runtime symbol resolution assumes a synthetic fixture export trie using
  a base-relative regular executable address. Reexports, symbol kinds,
  data exports, undefined/dynamic symbols and lazy binding are rejected.
- **Never execute unknown/untrusted code.** This primitive does not sandbox
  native code. No root requirement or device-level experimentation.
- No bootloader, firmware, Odin/D2, partitions, SELinux, privileged state,
  secure boot, Apple assets or persistent Android changes.

## Trusted developer verification

\`\`\`sh
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
\`\`\`

## Next work

1. Harden loader with image-size and graph mutation/property fuzz tests;
   add separate tests on actual Android userspace and 16KiB-host environments.
2. Expand native multi-image lifecycle semantics: independent teardown and
   dependency reference counting, dyld interposing and weak/reexport support
   only after explicit ABI tests.
3. Implement a scoped userspace Darwin/Mach port-message simulation with
   rights transfer and bounded queueing, then test it against legally compiled
   redistributable Darwin sample code. Do not confuse that with XNU.
4. Audit arm64e/PAC, Apple framework binary availability and Apple Metal /
   compositor boundaries before speculating about genuine SpringBoard.
