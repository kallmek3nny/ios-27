# Phase 20c: bounded Objective-C object ownership

## Implemented foundation

`src/objc20c_lifetime.c` and `include/ios27/objc20c_lifetime.h` provide an independent single-threaded, explicitly reference-counted object substrate.

1. Zero-initialized, pointer-aligned object allocation with immutable identity, fixed quotas for object size, live objects, lifetime objects, lifetime payload bytes, weak slots, pool depth, and pending autoreleases. Allocation failures do not alter counters.
2. Strong-retain/release and strong-store (retain new before release old), retain-count inspection, overflow and overrelease detection. Final release zeroes weak slots, enters a non-resurrectable deallocating state, then runs the destructor exactly once. Destructors may release owned children in deliberate order.
3. Bounded weak-reference side table, weak-store, retain-on-weak-load, unregister, and automatic nil zeroing. Caller keeps weak slot addresses valid until unregister or runtime destruction.
4. Nested autorelease pools with strict LIFO tokens and reverse-order draining. Pop first snapshots its release list so destructors may enqueue into an active parent pool. Misowned autoreleases fail closed but remaining entries are drained.
5. Freed object payloads are quarantined until runtime shutdown. This bounded tombstone policy prevents stale pointers aliasing new allocations in the same runtime. Quotas eventually reject further allocations. Shutdown frees leaked/cyclic payloads *without* running their destructors; a normal final release invokes destruction.

## Tests and evidence

Run `timeout 180s python3 tools/phase20c-build.py`. The independent harness builds and executes lifecycle tests at `-O0` and `-O2` with `-Wall -Wextra -Werror -Wpedantic`. Tests cover weak zeroing, repeated allocation, nested pools, out-of-order tokens, double releases, stale pointers, weak/pool/object/payload quotas, transactional ownership, inherited object-shaped graphs, deterministic stress, cycle cleanup, and callbacks autoreleasing into a parent pool.

The script also compiles `tests/phase20c-compiled.m` with Clang as a real ARM64 Darwin ObjC2 Mach-O object, using `--target=arm64-apple-darwin -fobjc-runtime=macosx-13.0 -fno-objc-arc`. The object contains compiler-generated methods, class metadata, selectors, inheritance, overrides, recursion, and cross-class calls. **This ARM64 Darwin object is compile-validated, not run on Linux.**

The same original Objective-C source is compiled for the native host using the Clang GNU legacy runtime and linked with the **test-only** selector lookup in `tests/phase20c-compiled-host.c`. This executes actual Clang-produced Objective-C method bodies and nested message sends against the phase 20c ownership implementation, including repeated creation, a strong setter, compiled autorelease, overridden methods, and inherited recursion. This test shim does not implement production Darwin message dispatch.

Optionally run `PHASE20C_SANITIZE=1 timeout 180s python3 tools/phase20c-build.py` for AddressSanitizer and UBSan coverage of lifecycle tests. Each subprocess has its own timeout.

## Integration boundary

No shared runtime, build configuration, `CMakeLists.txt`, `HANDOFF.md`, or other agents' files were modified. An integration owner must connect native image allocation, class-specific isa/layout, true ARM64 Darwin ABI dispatch, destructor callbacks, autorelease ABI symbols, and guest address handling. This is **not** ARC, objc_retain/objc_release symbol compatibility, thread-safe weak storage, nonpointer isa, tagged pointers, associated objects, exception handling, or cyclic GC. The new code is not automatically included in the repository CMake target; existing x86_64/ARM64 PR CI therefore tests the unchanged core, while standalone phase 20c tests must be run separately.
