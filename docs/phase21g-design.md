# phase 21g: darwin-style condition variable semantics

scope: an independent **host-backed semantic reference model**, not a drop-in
implementation of Apple pthread opaque object layouts or a claim of Darwin
ABI support in the ios27 core. The public API uses `ios27_21g_*` names and
explicit versioned handles; linking this code into the Mach-O loader requires
an ABI translation layer and is intentionally outside this file set.

## synchronization invariants

1. Every logical mutex owner, condition association, waiter queue, signal
   selection, and reference count change occurs under one host guard.
   Registration and logical mutex release happen in the same critical section;
   `pthread_cond_wait` releases the guard atomically with becoming blocked.
   A signaler cannot overtake a newly registered waiter.
2. Each waiter has a private host condition and intrusive FIFO list links.
   `signal` dequeues exactly the oldest queued waiter and marks it notified
   under the guard. `broadcast` visits all registered waiters. Spurious host
   wakeups loop on `notified`. A timeout removes only its own unnotified waiter
   and cannot turn an already selected signal into `ETIMEDOUT`.
3. The logical mutex is always reacquired before a wait returns, even on
   timeout and deferred cancellation. Cleanup restores ownership before user
   cleanup handlers run. Canceled designated receivers transfer the signal
   to the next queued waiter. While outstanding waiters (including signaled
   ones reacquiring their mutex) exist, either synchronization object cannot
   be destroyed or rebound to a different mutex.
4. Realtime absolute deadlines are converted exactly once to the monotonic
   host clock. Invalid clocks, nanoseconds, and unrepresentable deadlines are
   rejected deterministically. Realtime clock adjustments *after* conversion
   are not tracked. Monotonic waits use `CLOCK_MONOTONIC` cond attributes.
5. Logical mutexes are nonrecursive, reject self-deadlock and wrong-owner
   unlocks, and return `EBUSY` for failed trylock. Handles have generation
   counters to reject stale copies after slot reuse.

## validation

`python3 tools/phase21g-standalone.py` builds with `-Wall -Wextra -Wpedantic
-Werror` in both `-O0` and `-O2`, then runs each compiled suite three times.
Host tests include a 19,800-item bounded producer/consumer queue, 48 FIFO
registrations, 5,376 broadcast-barrier wait/reacquire transitions, 2,160
competing absolute timed waits across both clocks, invalid ownership, quota
exhaustion, stale handles, and deferred cancellation. The suite also compiles
an SDK-free, real ARM64 Darwin Mach-O object containing native compiler-emitted
`_pthread_cond_*` and `_pthread_mutex_*` symbol references. That is a symbol
fixture, **not** a runnable Darwin pthread implementation.

Independent GitHub Actions jobs run the host suite on native x86_64 and arm64
Ubuntu runners. All commands have process-level bounds and no artificial
sleep delays. Callers must always test their own application predicate in a
loop, as POSIX requires. Waiter FIFO refers to notification selection, not
operating-system mutex scheduling or order of post-wake execution.
