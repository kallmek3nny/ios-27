# phase 21i: arm64 darwin synchronization abi boundary

## guarantees and limits

- starts from verified main `cf218a5f31a7503c0ae85939981bc98ffd98d799`; no shared `CMakeLists.txt`, `HANDOFF.md`, or another phase's files changed.
- an SDK-free clang cross-compiled **real arm64 darwin MH_OBJECT** supplies `_pthread_*` external callsites. `tools/phase21i-macho.py` validates the Mach-O header, CPU, filetype, bounded commands, symbol table, and external `ARM64_RELOC_BRANCH26` entries, including corrupt-file rejection. It is **not** a Darwin executable and is **not** proof of running pthreads on macOS.
- the guest-object bridge enforces checked range arithmetic, supplied object alignment and extents, nonoverlapping live object regions, in-flight references and safe destroy refusal, capacity limits, generation-checked **host-side** tokens, and deterministic errno values.
- opaque host synchronization is delegated to phase 21c; no Linux pthread object layout is written into darwin guest memory. Published Apple LP64 *public type shape* is compile-checked, but unknown internal pthread fields are not guessed or written.
- caller **must** supply confirmed target-platform object sizes, alignments, and valid guest memory mapping. In [Apple's published libpthread LP64 type definitions](https://github.com/apple-oss-distributions/libpthread/blob/42d026df5b07825070f60134b980a1ec2552dfee/include/sys/_pthread/_pthread_types.h), the mutex includes an 8-byte `long` signature and 56 opaque bytes (64 total), while a condition includes an 8-byte signature and 40 opaque bytes (48 total), each aligned to an 8-byte `long`. The Clang-built Darwin fixture now uses those actual published LP64 type extents with compile-time assertions. This does **not** establish that an unreleased iOS 27 SDK retains the same layout, object signatures, or runtime semantics. Attributes other than null are rejected as `ENOTSUP`. Static pthread initializer patterns are intentionally not implemented.
- `pthread_cond_timedwait` reads a bounded two-field little-endian signed 64-bit absolute timespec profile. an actual Apple SDK/profile confirmation is required before advertising full ABI compatibility. guest memory backing must remain valid for runtime lifetime; callers cannot concurrently destroy runtime while entering its public methods.
- a raw pthread pointer alone does **not** encode a generation. once an address has been destroyed and reinitialized, a stale raw pointer is indistinguishable from a current one. `ios27_21i_token` is deliberately host-only and guards bridge-managed references, not native Darwin pthread callsites.

## milestones

1. verified sidecar object registry, overlap protection, capacity, pointer translation, alignment, rollback, lifecycle, stale host token rejection.
2. exact Darwin symbol dispatch for mutex/condition operations, deadline decoding, in-flight refs and cleanup on pthread cancellation; independent multithreaded native test.
3. compiler-produced ARM64 Darwin Mach-O with Apple-published LP64 opaque object shape and compile-time assertions, plus bounded structural validator verifying external relocation symbol indexes, branch type/width/alignment, malformed input coverage, and independent architecture CI.
4. AArch64 guest register adapter reading x0-x2 and writing only the guest w0 result; a bounded symbol-slice API rejects malformed Mach-O names. The adapter translates Linux host errno values into [Apple XNU's published Darwin errno numbers](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/errno.h). EDEADLK is 11 on Darwin, EAGAIN is 35, ENOTSUP is 45, ETIMEDOUT is 60, ENOSYS is 78. Direct host `ios27_21i_dispatch` remains a native-errno implementation API, **not** a guest ABI return channel.
5. Guarded host-side usage statistics, repeated concurrent one-slot generation turnover, high-address overflow tests, 60,000 deterministic malformed+valid guest calls, and mutation testing of 1,500 genuine Clang-produced Mach-O variants. CI also runs an extra Clang ASan/UBSan mode on x86_64.


## verification

`timeout 300s python3 tools/phase21i-standalone.py`

Runs independent `-Wall -Wextra -Wpedantic -Werror` native tests at `-O0` and `-O2`: 12 threads, 48k contended increments, 12 condition waiters, mutex/condition cancellation, deadlines, 300 sequential recycle cycles, 2,800 concurrent one-slot recycle cycles, 60k deterministic dispatch mutations, and AArch64 register/errno/bounded-symbol cases. A Clang-produced `--target=arm64-apple-darwin` object is checked for actual external relocations. Dedicated CI runs on x86_64 and arm64 Linux; x86_64 additionally runs `CC=clang PHASE21I_SANITIZE=1 timeout 300s python3 tools/phase21i-standalone.py` with AddressSanitizer and UndefinedBehaviorSanitizer. All subprocesses and jobs have hard timeouts.

**Limitations:** This layer does not execute Darwin guest instructions, run Apple pthreads, support static PTHREAD initializers, decode non-null pthread attributes, or establish arm64e pointer-authentication semantics. The guest memory backing is caller-owned and must remain valid, with writes to the deadline bytes synchronized by the caller. Runtime destruction is serialized only against currently registered objects; callers must not concurrently enter a runtime being freed. The actual target iOS SDK/profile and full semantics still require verification before claiming binary compatibility.
