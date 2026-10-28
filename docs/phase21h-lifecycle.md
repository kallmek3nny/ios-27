# phase 21h: synchronization-object lifecycle safety

This is an **independent host-backed lifecycle research component**, not a Darwin
pthread ABI implementation or evidence that official Apple frameworks run.
It does not alter the Android boot chain and requires no proprietary code.
No shared build metadata or other phase's source files are edited.

## milestones and model

1. **Stable slot identity and transactional creation:** handles have 1-based
   slot indices and monotonically increasing nonzero generation counters.
   Failed `pthread_cond_init` never publishes an object or mutates its
   generation. Exhausted generations permanently retire their slot rather
   than wrapping to a previously valid handle.
2. **Lifecycle and ownership transitions:** under a single registry mutex,
   OPEN permits acquisition, CLOSING rejects acquisitions with `ECANCELED`,
   and destruction succeeds only with no owner and no pending waiters.
   The current owner may still unlock a closing object; all waiting calls
   wake and drain before reclamation. Wrong-owner unlock is `EPERM`, recursive
   acquisition is `EDEADLK`, outstanding owner/waiters cause `EBUSY`.
3. **Cancellation and reuse safety:** waiters are counted before entering
   `pthread_cond_wait`; POSIX cleanup callbacks release that reference and
   the guard on thread cancellation. All close/wake/reclaim decisions use the
   same lock, avoiding a missed close notification and reuse under a waiter.
   Invalid/stale handles are `EINVAL`; double close is `EALREADY`.

`runtime_destroy` is only valid after callers have *externally stopped all new
runtime API entries*. It rejects live objects; no implementation can safely
free the runtime while a rogue thread may call into its already-freed address.
This explicit boundary prevents suggesting otherwise.

## evidence and limitations

`timeout 300s python3 tools/phase21h-standalone.py` builds real pthread
programs under `-Werror` at `-O0` and `-O2`. It tests 25,000 object-recycling
cycles, concurrent owner/close transitions, 48-waiter broadcasts, 350
simultaneous close/destroy contests with 2,800 blocked waiter lifetimes,
192 canceled waits, repeated resource teardown, deterministic exhaustion,
and injected initialization failure rollback. Build also produces and checks
Clang-emitted ARM64 Darwin `MH_OBJECT` with unresolved phase21h callsites.
On GitHub Actions, independent x86_64 and arm64 runners compile and execute
native tests plus ASan/UBSan. Cross-compiling a Darwin object is **not**
running Darwin code on Android, and no proprietary ABI layouts are assumed.

The gate deliberately models exclusive ownership and lifecycle interactions;
it is not a replacement for phase21c/21f pthread mutex and condition APIs.
Its global registry mutex prioritizes race correctness over fairness and scale.

## timing and ownership

Phase 21h owns only `src/libsystem21h_*`, `include/ios27/libsystem21h_*`,
`tests/phase21h-*`, `tools/phase21h-*`, `docs/phase21h-*`, and its dedicated
`.github/workflows/phase21h-lifecycle.yml`. Actual active engineering duration
must be recorded from observed timestamps; an hour must not be asserted unless
an hour of active work actually happened.
