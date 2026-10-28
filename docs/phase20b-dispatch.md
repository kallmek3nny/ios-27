# phase 20b: independent native objc dispatch layer

## implemented

- bounded, transactional registration of genuine class/metaclass pointer identities, including forward-listed parents, duplicate rejection, cycle checks and rollback
- stable selector pointer interning; method tables for independent class and instance dispatch; inherited lookup, overrides and lexical super lookup
- per-class parent-first initialization with single-thread same-class reentrancy, retry after initializer failures and zero partial registration on batch failure
- direct-mapped method cache with class/selector/kind key, epoch invalidation on class registration and method replacement, and observability counters
- registered heap objects with native `isa` pointer and external object bindings, bounded allocations and identity checks without reading arbitrary receiver addresses
- 0..4 word-argument typed host calls and generic resolved IMPs for native arm64 entrypoint tailcalls
- original arm64 Darwin assembly entrypoints `_objc_msgSend` / `_objc_msgSendSuper2`, preserving x0-x8, q0-q7, lr and 16-byte stack alignment on the resolver hop
- genuine Clang-compiled SDK-free ObjC2 Mach-O ARM64 fixture with root/child classes, class methods, recursion, polymorphic calls, nested dispatch and `super` sends

## run standalone

`timeout 300s bash tools/phase20b-verify.sh`

No modifications to shared CMake files. All commands in the script have hard bounds; the caller should use a 300s external bound.

## integration boundary, not yet a complete app runtime

The class metadata/image loader (including relocation and selector-reference binding) must pass **validated** class and metaclass VM addresses, executable IMPs, object sizes and initialization callbacks to this API. The dispatcher does **not** parse compiler metadata, map Mach-O pages, resolve dyld dependencies or rewrite `__objc_selrefs`. Class pointers and method IMPs must already be mapped with correct protection and lifetime. It deliberately does not execute foreign images on the host.

The assembly entrypoints compile to real ARM64 Mach-O machine code but cannot run on the x86_64 test host; the portable C tests execute on x86_64. Native ARM64 Darwin integration execution is pending the linker/loader work and a suitable native environment. This is **not** proof of end-to-end execution of an iOS app. No Apple binaries, root changes, bootloader changes or CPU emulation are used.

The single-process runtime and global entrypoint are **single-threaded**. Do not expose them to concurrent message sends until synchronization and class-initialization ownership are implemented. Class ownership and external object lifetimes remain the embedding loader's responsibility. The 0..4 word helper is only for integer/pointer argument/return signatures; native assembly preserves floating and indirect-result argument registers for the callee but has not been runtime-validated for large struct returns, variadic methods, swift bridging, arm64e pointer authentication, exceptions, ARC side tables, categories, message forwarding, or fully dynamic method resolution. The unknown-selector assembly path returns zero (including `v0`) rather than implementing Objective-C exception behavior. Call the checked resolve API to diagnose invalid sends when possible.

## current tests

The standalone host test covers inherited dispatch, overrides, nested sends, recursion depth 30, class methods, lexical super, forward parent registration, parent-first + retry initialization, repeated dispatch (12k sends), duplicate classes/methods and cyclic/missing parents with no partial commit, invalid selector identity, invalid receiver, cache invalidation, object rebind/release and 1024-object exhaustion. The script compiles the real ObjC fixture at `-O0` and `-O2` for ARM64 Darwin, validates emitted ObjC2 symbols and `_objc_msgSendSuper2`, and assembles genuine ARM64 Mach-O entrypoints.
