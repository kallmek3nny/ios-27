# Phase 21j: independent synchronization diagnostics and integration

## Scope and compatibility claim

This is a **native host-side diagnostic harness**. It consumes the existing
phase 21b lifecycle, phase 21c synchronization, and phase 21e cancellation/TLS
interfaces. It does not change shared source files, dispatch native Darwin
pthread symbols, or claim Apple pthread ABI compatibility. The new source lives
under `src/libsystem21j_*`, the matching public header under
`include/ios27/libsystem21j_*`, plus namespaced tests/tools and dedicated CI.

## Completed implementation milestones

1. Reusable seeded fault selection and bounded journal. A deterministic
   64-bit keyed mixer maps (seed, stage, actor, ordinal) to a failure decision.
   Scheduling does not influence *which* tuple fails. The event journal is
   synchronized, allocated once, bounded to 1024 entries and tracks overwritten
   records; observed race ordering is **not** promised to be reproducible from
   the seed alone. Snapshot counters track resource ownership, peak use,
   contention, waits, timeouts, probes and injected failures.
2. Generation-safe resource accounting and failure rollback. Each diagnostic
   lease carries a slot, generation and runtime identity; stale, double and
   cross-runtime releases return EINVAL. Quota exhaustion returns EAGAIN;
   an injected ENOMEM leaves a zero handle and allocates nothing. Destruction
   reports EBUSY for outstanding leases. Caller must externally coordinate
   runtime destruction with all participating threads, even with zero leases.
3. Compiled race stress. Tests cover 20k deterministic paired probes plus
   10k faultable acquisitions, 24k concurrent quota-4 lease churn operations,
   256 two-thread release races via a three-party barrier, trace wraparound
   and stale-resource rejection.
4. Compiled cross-component synchronization stress. Each integration run
   creates a phase 21b lifecycle runtime, a phase 21c mutex and conditions,
   a phase 21e TLS/cancellation runtime, and phase 21j diagnostics. Four
   producers each enqueue 2500 unique values; four consumers drain a bounded
   17-slot queue. Six full rounds exercise **60k queued elements** per binary;
   the standalone runner compiles and runs both O0 and O2 versions. It checks
   exact arithmetic sums, timeout/reacquisition, lock contention, worker
   completion, checkpoint-triggered cooperative cancellation, cleanup/TLS
   destructor counts, invalid stale handles, quotas, zero surviving handles
   and teardown/reinitialization.

## Verification

Run `timeout 360s python3 tools/phase21j-standalone.py` on native
x86_64 and aarch64 machines. The runner has per-compile 120s and per-program
150s timeouts and keeps `-std=c11 -Wall -Wextra -Wpedantic -Werror -pthread`.
Dedicated pull-request jobs target `ubuntu-24.04` and
`ubuntu-24.04-arm`. They do **not** substitute cross-compiled objects for
native execution. CI must pass on one frozen PR head before any expected-head
SHA merge.

The recorded event journal can be examined by sequence with
`ios27_21j_read`; ERANGE indicates overwritten/unknown entries. Probe
determinism and exact outcome counts are asserted. Native scheduling remains
nondeterministic, so races are reproduced as invariant checks rather than by
pretending a seed controls pthread scheduling.

## Engineering session log (UTC)

- Verified base `main`: `cf218a5f31a7503c0ae85939981bc98ffd98d799`.
- Active implementation began at **2026-10-28 21:08:39 UTC**.
- First recorded engineering checkpoint at **2026-10-28 21:14:53 UTC**
  (6 minutes, 14 seconds of elapsed engineering-session time).
- The 60-minute target is **not** yet satisfied by this checkpoint.
  Do not present a later CI completion time as human engineering activity.
- Final completion, CI evidence and any actual additional active duration
  must be recorded only after verification, not projected.

## Constraints and limitations

- No source modification to phases 21b/21c/21e or any shared build file.
- Only checked guest-independent native host synchronization; no Apple ABI
  guarantees or emulation.
- Diagnostic counters identify leaks at suite boundaries; not a global heap
  allocator interceptor.
- Fault injection is opted into at selected harness allocation checkpoints;
  it does not inject into opaque dependencies without their cooperation.
- Program timeout and CI timeout bound hangs, but a failed assertion must be
  debugged rather than ignored.
