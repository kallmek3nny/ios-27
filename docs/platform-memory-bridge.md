# Original platform memory/string bridge

This implements actual replacement code for twelve Simulator host imports:
`bzero`, `memcmp`, `memcpy`, `memmove`, `memset`, `strcmp`, `strcpy`, `strlen`,
`strncmp`, `strncpy`, `strnlen`, and `strstr`.

The original compiled ARM64 client has called all twelve routines from a
separate Mach-O dylib on the **physical S25+** and returned 42. **SpringBoard
and Apple's UI still have not been executed or rendered by this bridge.**

## Implementation

`src/platform_memory.c` supplies allocation-free standard C semantics,
including overlap-safe `memmove`, unsigned-byte comparisons and zero padding
for `strncpy`. Inputs must be valid C objects. The functions use no host
filesystem, allocator, syscall or callback table.

Host builds use `ios27_platform_*` names to avoid interposing on the host's
libc. `tools/platform-memory-build.py` compiles the same source for ARM64
iOS Simulator, then links a real `PlatformMemory.dylib` with install name
`/ios27/libplatform-memory.dylib`. Its manifest records the source/binary
hashes, twelve exports and absence of unresolved runtime imports. A separate
original `PlatformMemoryClient` imports all twelve names.

The loader validates the replacement as a stateless platform-7 dylib with
regular code exports and no dependencies, initializers or pointer fixups.
Only the exact twelve symbols resolving to an observed zero-code
`libsystem_platform.dylib` placeholder are eligible. Imports from another
provider, unrelated code and unsupported APIs are not silently replaced.
Interior function pointers with nonzero addends are refused.

## Real SpringBoard graph

The full size/SHA-256-verified 24A434 planning run with this dylib retargeted
**109 standalone bind sites** to its real code addresses. This adds one
explicit image, for **69 staged standalone images**. **115 standalone
placeholder imports remain**, plus the remaining runtime/service work.

`--cache-platform-patches` additionally handles selected v4 shared-cache
pointer overrides. The real table contains **44** applicable pointers:
**3 client slots and 41 coalesced GOT slots**. `memcpy` aliases are routed to
the overlap-safe `memmove` implementation when they share a host export.

The parser checks the table version, array extents, image/export indices,
metadata overlap, exact old pointer values, non-executable data ownership and
pointer alignment. Authenticated, tagged and nonzero-addend locations are
refused. Layout reference:
[dyld CachePatching.h](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/common/CachePatching.h).

The mapping checkpoint applies a validated plan only to its private data
pages, reads back each new pointer, and restores read-only protection.
Executable permission is never granted there. Planning a cache override is
reported separately from actually applying it; these are not runtime-startup
or UI-rendering results.

## Reproduction

Build original binaries with an open-source compiler and linker:

```sh
timeout 120s python3 tools/platform-memory-build.py --clang clang --linker ld64.lld \
  --output-dir out/platform-memory
```

Then plan against existing external Apple inputs:

```sh
timeout 600s python3 tools/springboard-load.py \
  --input-root /path/to/ios27-runtime-inputs --weak-policy load-order \
  --platform-memory out/platform-memory/PlatformMemory.dylib \
  --cache-platform-patches --verbose
```

Adding `--map` on supported 64-bit Linux materializes **non-executable** pages,
applies the validated data-pointer plan, verifies it, then releases everything.
It does not enter SpringBoard. No Apple bytes are built into the replacement
or the Android APK.

## Verification and limits

The S25+ (SM-S936U1, API 37, 4 KiB pages) passed **16,958** differential cases
on the ELF-compiled source, linked metadata/startup checks, and three fresh
supervised Darwin client executions calling all twelve dylib exports. Uploads
were hash-verified and the temporary files removed. No APK or device setting
was changed. [Machine-readable evidence](platform-memory-bridge.json).

The complete real-input planning run with v4 cache overrides passed in 143.0
seconds. It validated the 44 existing pointer values against their declared
exports. Actual cache writes/readback were tested with synthetic private
file mappings on Linux; this phase did not materialize the real cache plan.

The first native attempt exposed a real loader issue: ordinary ARM64 import
stubs (`S_SYMBOL_STUBS`) were rejected as initializer code. The inspector now
accepts bounded, file-backed RX stubs with the verified 12-byte stride, while
excluding them from valid main/constructor targets. The regression includes
bad-stride and LC_MAIN-in-stub rejection cases. The corrected device run passed.

Direct Linux ASan+UBSan checks, with leak detection enabled, passed the
memory/string contracts, compiled platform graph inspection, and existing
initializer regressions. Synthetic tests also exercised actual private cache
pointer writes/readback/unmapping on Linux and rejected malformed v4 tables,
authenticated/tagged pointers, stale values and executable patch locations.
The public evidence distinguishes these synthetic write tests from planning
against the real cache.

The final full Clang Debug build and CTest run completed: **50 passed, 8
skipped, 0 failed** across 58 entries on x86_64. Skips are the existing
native-ARM64 and opt-in CPU cases; the new native platform-memory case was
verified separately on the S25+. Earlier bounded rebuilds were interrupted
during linking, before CTest. The completed run is recorded in
`out/platform-memory-2026-11-15/host-final.xml`.

The library does not implement the remaining unfair locks, pthread/TLS,
Darwin kernel/Mach interfaces, dyld process objects, Objective-C/Swift
initialization or graphics. Those still block the requested SpringBoard UI.
The measurements above are local/device evidence. [PR #126](https://github.com/kallmek3nny/ios-27/pull/126)
tracks publication and exact-head CI separately. Real 16 KiB hardware remains
unverified.
