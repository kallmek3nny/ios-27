# Phase 21j coverage, exact replay and test contracts

This document describes **native diagnostic integration only**. It does not
assert a verified ARM64 Darwin pthread layout, binary translation, guest
pointer safety, or OS-native kernel behavior. Phase 21j consumes existing
phase 21b, 21c and 21e APIs without editing their sources.

## Test surfaces and failure signals

| Compiled test | Verified invariants and coverage per invocation |
| --- | --- |
| `phase21j-diagnostics` | 20,000 paired keyed probes, 10,000 faultable allocations, 24,000 contended resource leases, 256 double-release races, stale generations, quota-4 exhaustion, trace wraparound, five contention latency buckets |
| `phase21j-rules` | Immutable 16-entry exact fault script, forced pass/fail overrides, duplicate key/capacity errors and 90,000 keyed concurrent probes |
| `phase21j-export` | 40,000 concurrent ring writes while taking atomic snapshots, strict contiguous sequence records and bounded ENOSPC semantics |
| `phase21j-integration` | Eight worker threads, phase21b lifecycle, phase21c native-backed mutex/conditions, phase21e cooperative cancellation and heap-backed TLS destructors; 60,000 queue elements in six full 17-slot-queue recreate cycles; seeded fault replay, owned resource teardown |
| `phase21j-schedule` | 16,384 preplanned turns across eight threads, stale/foreign/reentrant turn errors, cancellation during timed scheduler wait |
| `phase21j-schedule-races` | 256 cancellation cleanup races, 256 duplicate turn claims and 512 competing timeout/release races |
| `phase21j-replay` | Three 4,096-step reproducible ordered mutex + condition FIFO programs; journal records match computed actor/order pairs and independent golden hashes |
| `phase21j-handoff` | 1,024 controlled `cond_wait`/signal handoffs at the mutex-release boundary, with ordered trace checks |
| `phase21j-broadcast` | 32 waiter threads, 128 synchronized broadcast generations, 4,096 successful wakes, zero outstanding leases |
| `phase21j-lockorder` | 96,000 contended graph ownership transitions, historical order-cycle rejection, 64th-bit correctness, reset and ownership validation |
| `phase21j-deadlock-integration` | 512 native two-worker and 256 three-worker actual mutex wait-for cycles, only one rejected dependency per cycle, no surviving owners |
| `phase21j-large-cycle` | Sixteen 64-worker native POSIX mutex rings, bounded wait-for traversal over maximum supported actor count, complete safe unwind |

Tests abort on violated invariants and return nonzero. Nothing weakens
`-Werror`, skips a known regression or silently treats a timeout as success.

## Reproducible invocations

From the checkout root, bounded (the runner independently bounds each compile
at 120 seconds and executable at 150 seconds):

```sh
timeout 360s python3 tools/phase21j-standalone.py
timeout 360s env CC=clang python3 tools/phase21j-standalone.py
timeout 360s env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 python3 tools/phase21j-standalone.py --sanitize
timeout 360s python3 tools/phase21j-standalone.py --stress
timeout 360s env PHASE21J_SEED_BASE=0x21a000009f python3 tools/phase21j-standalone.py
```

Note that `--stress` recompiles at `-O2` with `-Werror` and executes all
programs eight times. Its integrated queue tests select a different,
explicitly printed deterministic seed each pass. The default runner compiles
at both `-O0` and `-O2`; `--sanitize` selects `-O1` and
AddressSanitizer/UndefinedBehaviorSanitizer plus explicit leak checks.

Native architecture is checked in the runner. The dedicated pull-request
workflow exercises **x86_64** and **aarch64** separately in native GCC, native
Clang, eight-pass high-contention stress, and ASan/UBSan/leak jobs. It does not
treat cross-compilation as physical ARM64 execution.

## Exact replay semantics

The seeded fault selector maps
`(seed, stage, actor, ordinal)` to an outcome with a keyed 64-bit SplitMix
mixer; the same tuple always selects the same result, regardless of pthread
scheduling. An optional immutable 16-entry exact-match rule script can override
individual decisions before the first probe. A `PHASE21J_SEED_BASE` environment
variable selects the integrated stress seeds without source changes.

Native pthread scheduler ordering is **not** deterministic merely because a
fault seed is fixed. The distinct bounded `ios27_21j_schedule` turn scheduler
explicitly orders test program operations to replay known actor sequences.
Independent golden replay checks are:

- Seed `0x21feed`: digest `0xf04b4f8b18cea5d7`
- Seed `0x21fede`: digest `0xf9b2b343cc836760`

The diagnostic journal is a 1024-entry synchronized ring. Individual
`ios27_21j_read` requests reject overwritten sequences with `ERANGE`.
`ios27_21j_export` atomically copies an ordered snapshot under one lock; if the
provided buffer is insufficient it returns `ENOSPC` and the required count
without a partial copy. Monotonic timestamps measure observed latency; they
are not stable across runs, and are excluded from the deterministic digest.

## Ownership and cancellation boundaries

Resource leases have a slot, generation and runtime identity. A stale or
cross-runtime lease, invalid release or duplicate release is rejected.
An injected allocation failure never consumes a lease. Explicit worker TLS
allocation is performed only after its fault checkpoint permits it; detach
calls the destructor and frees the heap payload. Leak counters and ASan/LSan
are independent checks.

The optional lock-order monitor tracks a bounded directed graph of
previously observed lock nesting plus active actor-to-owner wait-for edges.
It rejects historical cycles or currently closing wait-for cycles before a
caller blocks. The observer **must be wired to actual acquire, wait, cancel,
and release transitions** by its caller. It does not magically inspect all
pthread mutexes or detect an uninstrumented runtime's graph.

Cancellation while waiting in the deterministic scheduler is handled by a
POSIX cleanup handler that releases the scheduler guard and waiter reference.
A caller holding an acquired turn must still release that turn explicitly;
the scheduler does not support unsafe asynchronous thread termination while
holding application locks. Logical wait-for registrations likewise need
explicit cancellation when the caller abandons a wait.

All runtimes require external synchronization and joining of participating
threads before destruction. Tests deliberately cover `EBUSY` at lifecycle
boundaries, but they never manufacture use-after-free behavior.

## Source ownership

New `src/libsystem21j_*`, matching `include/ios27/libsystem21j_*`,
`tests/phase21j-*`, `tools/phase21j-*`, `docs/phase21j-*` and
`.github/workflows/phase21j-sync-diagnostics.yml` only. No shared
`CMakeLists.txt`, common workflow, phase21b, phase21c or phase21e file
has been modified. Components are compiled as independent dependencies by
the owned Python runner.
