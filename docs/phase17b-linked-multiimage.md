# phase 17b: real linked ARM64 MH_DYLIB graph

## scope

This phase extends the original-source, freestanding Mach-O image graph to
accept Clang/LLVM-linked `MH_DYLIB` artifacts, not only hand-encoded return-value
fixtures. Existing bounded private anonymous mappings, source copies, W^X
sealing and graph-wide rollback remain mandatory.

- Accept `DYLD_CHAINED_PTR_64` (format 2), where the pointer carries a preferred
  VM target, and `DYLD_CHAINED_PTR_64_OFFSET` (format 6), where it carries an
  image-relative offset. Refuse nonzero high8/tag bits and all authenticated
  arm64e pointer formats.
- Permit a partial final `__LINKEDIT` page while rounding the reserved VM span
  to the host page size; require segment starts on host page boundaries.
- Resolve strongly ordered library ordinals only within explicit source
  graph entries. Permit `LC_REEXPORT_DYLIB` and chained reexport aliases,
  allowing weak-definition exports as executable/readable addresses.
- Permit `LC_LOAD_WEAK_DYLIB` with an absent explicitly named provider only
  when its chained import sets the weak bit. Null unresolved weak pointers
  receive no addends; strong unresolved references abort the full transaction.
- Reject invalid ordinals, dependency cycles, unsupported authenticated
  fixups, unsupported exports, VM overlap, unaligned host segment starts,
  W+X, and unresolved mandatory libraries.

No host filesystem resolution is performed by the runtime. Supplying image
bytes does not imply a safe sandbox: **execute reviewed fixtures only**.

## reproduce sdk-free genuine dynamic linking

Requirements: Python 3, Clang with `arm64-apple-darwin` target, LLVM
`ld64.lld`, host C compiler, and CMake. All commands include hard timeouts.

```sh
timeout 120s python3 tools/phase17b-build.py --output-dir build/phase17b
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON
timeout 300s cmake --build build -j 2
timeout 180s ctest --test-dir build --timeout 45 --output-on-failure
timeout 120s cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wpedantic \
  -Iinclude tests/fixtures/phase17b-verify.c build/libios27_core.a \
  -o build/phase17b-verify
timeout 90s build/phase17b-verify \
  build/phase17b/phase17b-provider.dylib \
  build/phase17b/phase17b-consumer.dylib
```

The build compiles both original C sources to real ARM64 Darwin `MH_OBJECT`
files; `ld64.lld` links a genuine provider `MH_DYLIB`, then a genuine
consumer `MH_DYLIB` with a TAPI export stub that records a real
`LC_LOAD_DYLIB` ordinal for the separately mapped provider. This works
without the Apple SDK. Some Swift-packaged LLVM linkers reject an object's
platform load command. On that exact linker diagnostic, the script strips
only the *object* platform command before retrying the same linker, preserving
all original machine code, symbols, and relocation bytes.

The verifier parses actual chained fixups and export tries, maps the consumer
before the provider (proving ordering does not rely on input array position),
checks that the real GOT bind points to the mapped provider function, and
checks graph rollback when the mandatory provider is absent. On ARM64
Linux/Android hardware, it calls the genuine provider and consumer functions
directly, expecting **42** and **43** respectively. On x86_64 it performs
parsing, binding, mapping and rollback validation but never executes ARM64 code.

Existing registered `test_images` / `test_chained` CTest targets further
exercise weak imports, weak definitions, reexports, malicious ordinals,
invalid pointer formats and rollback without a runtime linker dependency.
No change to `CMakeLists.txt` or another agent's files is required.

## limitations

This is not full dyld, the Darwin process ABI, Objective-C, UIKit,
SpringBoard, or full official iOS 27 execution. Unsupported authenticated
fixups, modern thread-local exports, non-weak unresolved symbols, upward/lazy
dependencies, untrusted iOS binaries and cross-host-page protection conflicts
are deliberately rejected. No CPU emulation, Apple proprietary code, kernel,
bootloader, firmware, partition, or persistent-root modification is involved.
