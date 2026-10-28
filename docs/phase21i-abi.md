# phase 21i: arm64 darwin synchronization abi boundary

## guarantees and limits

- starts from verified main `cf218a5f31a7503c0ae85939981bc98ffd98d799`; no shared `CMakeLists.txt`, `HANDOFF.md`, or another phase's files changed.
- an SDK-free clang cross-compiled **real arm64 darwin MH_OBJECT** supplies `_pthread_*` external callsites. `tools/phase21i-macho.py` validates the Mach-O header, CPU, filetype, bounded commands, symbol table, and external `ARM64_RELOC_BRANCH26` entries, including corrupt-file rejection. It is **not** a Darwin executable and is **not** proof of running pthreads on macOS.
- the guest-object bridge enforces checked range arithmetic, supplied object alignment and extents, nonoverlapping live object regions, in-flight references and safe destroy refusal, capacity limits, generation-checked **host-side** tokens, and deterministic errno values.
- opaque host synchronization is delegated to phase 21c; no Linux pthread object layout is written into darwin guest memory. no Darwin opaque layout/attribute constant is guessed.
- caller **must** supply confirmed target-platform object sizes, alignments, and valid guest memory mapping. In [Apple's published libpthread LP64 type definitions](https://github.com/apple-oss-distributions/libpthread/blob/main/include/sys/_pthread/_pthread_types.h), the mutex includes an 8-byte `long` signature and 56 opaque bytes (64 total), while a condition includes an 8-byte signature and 40 opaque bytes (48 total), each aligned to an 8-byte `long`. The Clang-built Darwin fixture now uses those actual published LP64 type extents with compile-time assertions. This does **not** establish that an unreleased iOS 27 SDK retains the same layout, object signatures, or runtime semantics. Attributes other than null are rejected as `ENOTSUP`. Static pthread initializer patterns are intentionally not implemented.
- `pthread_cond_timedwait` reads a bounded two-field little-endian signed 64-bit absolute timespec profile. an actual Apple SDK/profile confirmation is required before advertising full ABI compatibility. guest memory backing must remain valid for runtime lifetime; callers cannot concurrently destroy runtime while entering its public methods.
- a raw pthread pointer alone does **not** encode a generation. once an address has been destroyed and reinitialized, a stale raw pointer is indistinguishable from a current one. `ios27_21i_token` is deliberately host-only and guards bridge-managed references, not native Darwin pthread callsites.

## milestones

1. verified sidecar object registry, overlap protection, capacity, pointer translation, alignment, rollback, lifecycle, stale host token rejection.
2. exact Darwin symbol dispatch for mutex/condition operations, deadline decoding, in-flight refs and cleanup on pthread cancellation; independent multithreaded native test.
3. compiler-produced ARM64 Darwin Mach-O with Apple-published LP64 opaque object shape and compile-time assertions, plus bounded structural validator verifying external relocation symbol indexes, branch type/width/alignment, malformed input coverage, and independent architecture CI.

## verification

`timeout 300s python3 tools/phase21i-standalone.py`

Runs independent `-Wall -Wextra -Wpedantic -Werror` native tests at `-O0` and `-O2` (12 threads, 48k contended increments, 12 condition waiters, deadline races, 300 recycle cycles), and a `clang --target=arm64-apple-darwin` compile with Mach-O relocation verification. Workflow runs separately on x86_64 and arm64 Linux. The macOS / Apple SDK integration and verified object ABI remain prerequisites before claiming full Darwin pthread binary compatibility.
