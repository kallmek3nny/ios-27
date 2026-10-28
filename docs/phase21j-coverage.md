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
| `phase21j-rule-races` | 1,024 first-probe versus rule-configuration pthread races; exact freeze linearization, immutable post-freeze outcomes and failure accounting |
| `phase21j-modelcheck` | 200,000 randomized nested logical lock acquisitions checked against an independently implemented FIFO BFS graph-reachability oracle, including edge accounting |
| `phase21j-wait-model` | 120,000 seeded live lock-owner/wait-for transitions compared to an independent state machine, including cancellation, recursive lock checks, and reset invariants |
| `phase21j-identities` | 4,096 concurrent runtime create/destroy lifecycles with unique nonwrapping identities and rejection of previous runtime handles |
| `phase21j-boundaries` | 180,000 malformed scalar arguments, wrong slots, invalid event kinds, out-of-range schedule indices, and nonmutating error returns |
| `phase21j-timed-handoff` | 64 guaranteed monotonic expirations and 512 ordered successful timed condition waits; exact trace suffix checks |
| `phase21j-timeout-races` | 768 real native condition signal/deadline interactions, separately synchronized guaranteed-expiry controls, guaranteed signaled controls and truly competing timeout cases |
| `phase21j-trace` and independent verifier | Compiled explicit actor-plan or seeded replay with stable JSON/JSONL digest, independent Python checker reconstructs all retained events and rejects tampered actor plans, corrupted events and malformed IDs |

| `phase21j-audit` | 20,000 threaded lease lifecycles and 30,000+ concurrent atomic audits validating generation slots, resource balance, journal retention and contention histograms |
| `phase21j-graph-audit` | 32,000 concurrent logical owner transitions, 10,000+ historical DAG / live wait-for audits, and owner-change while a wait is pending |
| `phase21j-schedule-limits` | Actual maximum-capacity 128-thread turn schedule with 4,096 exact ordered operations and a fully checked wrapped event-ring suffix |

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


## Machine-readable reproducible failure captures

Compile the owned CLI against the diagnostics and turn-scheduler sources; the
test runner already does this for each native compiler/optimization combination.
Two examples of the binary invocation after compiling it:

```sh
./phase21j-trace 0x21feed 4 512 --jsonl > phase21j-seeded.jsonl
./phase21j-trace 0x21feed 4 8 --plan 03120121 --jsonl > phase21j-planned.jsonl
timeout 30s python3 tools/phase21j-verify-trace.py < phase21j-planned.jsonl
```

The explicit actor-plan string only accepts 0-7 and must contain exactly
`STEPS` digits. All actors must be less than `ACTORS`. Unplanned mode
derives actors from SplitMix(seed XOR step), not physical scheduler order.
The seeded 512-step golden trace is
`0x084ccace638e3858` (86 injected events and 598 total records); the
8-step explicit actor plan `03120121` has golden digest
`0x79cdeb98a766411b` (4 injected events and 12 total records).
The independently implemented JSONL verifier rejects extra/missing event
fields, actor substitution, mismatched sequence, malformed plan, altered
fault return codes and incorrect digests. Its input is bounded to 2 MiB.

The script preserves a clear distinction between two reproducibility modes:
*seeded fault outcomes* are deterministic even under nondeterministic
thread scheduling, whereas *exact interleavings* require the separate
logical turn scheduler or a supplied actor plan. Native pthread scheduling
is neither controlled nor described as deterministic by the seed alone.

### Native condition-variable deadline races

A genuine condition variable may signal near its timeout boundary. The
winner of a competing signal versus timeout is not fixed by the deadline
alone. The `phase21j-timeout-races` suite allows both legal outcomes in
the competing phase, and uses a distinct synchronization barrier before
signaling to prove the no-signal expired case. This avoids the invalid
assumption that every already-expired deadline necessarily returns
`ETIMEDOUT` when a concurrent signal races it.

## Native CI evidence attestation

Every successful phase21j matrix job stores an individually named artifact
`phase21j-ARCH-MODE`. Its compact JSON report records the physical runner
architecture, actual compiler and warning flags, test optimization, real
per-case elapsed seconds, number of test repetitions and the immutable
pull-request head SHA. Recorded times describe **machine test execution**;
they are never presented as the engineer's active work duration.

The independent `tools/phase21j-verify-evidence.py` rejects incomplete
test inventories, duplicate or skipped cells, missing `-Werror`, missing
sanitizers in memory-safety mode, mismatched native host architecture, wrong
SHA, forged elapsed durations and unknown report fields. The
`tools/phase21j-test-evidence.py` negative fixture runner exercises 30
malformed variants before evidence upload. Uploads are made only when all
tests and both attestation steps succeed.

The eight workflow jobs are **GCC** x86_64/arm64,
**Clang** x86_64/arm64, **ASan/UBSan/LSan** x86_64/arm64 and
**eight-pass stress** x86_64/arm64. Each runner checks the exact on-disk
`tests/phase21j-*.c` inventory against its 23-entry executable manifest
(including the standalone trace CLI) before compiling anything.

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
