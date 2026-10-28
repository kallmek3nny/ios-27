# Phase 19e: standalone original-source Objective-C and libSystem foundations

## Implemented

- `objc_metadata.c` reads real Clang `arm64-apple-darwin` ObjC2 **MH_OBJECT** files. It walks bounded 64-bit Mach-O load commands, symbol and relocation tables; resolves 64-bit `ARM64_RELOC_UNSIGNED` local/external symbolic references from class lists to class records and `class_ro_t`; validates superclass references, class/selector/type names, non-relative method list entries and executable method addresses. It accounts for non-fragile ivar list sizes before computing safe host allocation sizes. Bad relocations, strings, cycles, counts and section bounds fail closed.
- `objc_runtime.c` supplies atomic batch class registration, selector interning, superclass method lookup, superclass-first once-only initialization (retry on failure), zeroed objects, explicit object ownership, failed-message handling and hard limits. Host C callbacks are trusted; no runtime guest execution or concurrency is implied.
- `objc_bridge.c` converts verified compiler-produced class/selector metadata to the host runtime, with a caller-supplied *trusted host implementation resolver*. This is deliberately not auto-binding the `MH_OBJECT` IMP addresses. Every implementation must be resolved or the registration batch fails.
- `libsystem_user.c` implements a bounded deterministic userspace BSD trap dispatch for read/write/close, guest pid and synthetic guest identity; isolated guest memory offsets, stdin, captured stdout/stderr and quota-tracked malloc/free. Error numbers are Darwin-compatible for this subset. No passthrough host filesystem or kernel access.
- `phase19e-standalone.py` builds actual Objective-C `MH_OBJECT` compiler output, exercises byte/relocation parsing, inheritance, host messaging, registration rollbacks, resource limits, and deterministic 4,000-cycle object stress at both `-O0`/`-O2`. With `PHASE19E_SANITIZE=1` the same tests run under ASan+UBSan (if supported). On native ARM64 Linux, tests additionally copy original compiler-emitted, relocation-free `-[Root answer]` instruction bytes into a W^X-sealed page and execute them directly, with no emulation.

## Test commands

All commands explicitly bounded. From the repository root:

```sh
timeout 180s python3 tools/phase19e-standalone.py
timeout 180s env PHASE19E_SANITIZE=1 python3 tools/phase19e-standalone.py
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 300s cmake --build build --parallel 2
timeout 180s ctest --test-dir build --timeout 30 --output-on-failure
```

Prerequisites: native C compiler and Clang supporting `--target=arm64-apple-darwin`; `ld64.lld`, Apple SDK and Apple binaries not required.

## Integration boundary and unsupported parts

This does not yet enable SpringBoard or arbitrary ObjC application execution. All components are independently built, and CMake, HANDOFF.md, and concurrently owned files remain untouched. Adding them to the core and wiring dyld/Mach identity requires a separate integration owner.

The parser currently supports ARM64-all ObjC2 24-byte absolute methods with UNSIGNED64 relocations, not relative methods, external superclasses, categories/protocols, metaclass dispatch, dynamic swizzling, arm64e/PAC, selector fixups, cross-image rebasing or ivar offset fixups. Host allocations account for ivar storage but do not patch the compiler-emitted non-fragile ivar offset variables. Therefore native ARM64 test execution is deliberately limited to the relocation-free original answer method.

Standard objc_msgSend and libSystem ABI entrypoints are not yet exported: the services are bounded host-side foundations. The standard GitHub Actions tests do not compile the new sources because CMake ownership is excluded. Passing standard CI alone cannot verify phase19e; run the standalone harness separately.
