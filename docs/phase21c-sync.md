# phase 21c: bounded pthread synchronization

This standalone, original-source host-native layer is not a drop-in Darwin
pthread ABI and does not execute Mach-O Darwin programs. Its two-word handles
are generation-checked bridge tokens, not Apple's opaque pthread structures.

## milestones
1. Normal, recursive, and error-checking mutex policies with ownership
   enforcement, self-deadlock detection, try-lock, recursion depth, and
   contention with guarded transitions. Normal self-lock intentionally
   fails with EDEADLK instead of hanging the bridge.
2. Condition wait releases mutex ownership atomically with waiter
   registration under a single internal guard. It always reacquires
   ownership before returning, including timed-out waits. Signal and
   broadcast have predicate-based POSIX-style semantics, not stored events.
3. Lifetime management refuses destruction of busy, owned, or referenced
   synchronization objects. Handles are generation checked, and slots
   permanently retire at generation overflow. Bounded runtime capacity
   and deterministic errors are tested.
4. Real native compiler-built multithreaded stress on x86_64 and ARM64
   Linux runners: queues, contention, broadcast, timeout/ownership races,
   invalid handles, and quotas. Clang also compiles a genuine SDK-free
   ARM64 Darwin Mach-O object with external symbol callsites.

## independently test

Run with hard wall-clock bound:

    timeout 180s python3 tools/phase21c-standalone.py

The independent CI workflow runs this at -O0 and -O2 with -Werror on
ubuntu-24.04 and ubuntu-24.04-arm. Shared CMakeLists.txt and HANDOFF.md
are untouched. The CI test does not imply integration into existing
application runtime.

## behavioral boundaries

Absolute condition deadlines accept realtime or monotonic timestamps.
Realtime deadlines are sampled and converted to CLOCK_MONOTONIC once
before the mutex is released; later wall-clock corrections do not
change the timeout. Timespec nanoseconds must be in [0, 1000000000).
Condition waiting with recursion depth greater than one returns EINVAL;
waiting without ownership returns EPERM. Every condition predicate must
be checked in a loop because spurious wakeups are allowed. Wakeup order
is not guaranteed FIFO. No guest memory mapping, actual Darwin symbol
exports or native iOS application compatibility is implied. Callers must
quiesce all threads before runtime destruction and never access freed
runtime pointers.

## additional safety coverage

Independent race tests run 700 signal-versus-timeout transitions, ensure
ownership after timeouts, verify one condition is not concurrently bound to
two different mutexes, and cycle 16,000 generation-checked handle reuses.

## cancellation hardening

A native pthread cancellation point in mutex contention or condition waits
must not leak the internal runtime guard, queued waiters or condition/mutex
references. On condition wait cancellation, the bridge reacquires the logical
mutex before user cleanup handlers run, preserving POSIX-style cancellation
cleanup ordering. A caller that cancels a thread while it owns a mutex must
install its own cleanup handler to unlock that mutex. Async cancellation while
inside arbitrary user callbacks remains unsupported.

## compiler-generated Darwin pthread callsites

The SDK-free ARM64 Darwin fixture phase21c-darwin-threaded.c compiles
real pthread_create, pthread_join, pthread_mutex_lock/unlock and
pthread_cond_wait/signal/broadcast external callsites from a four-worker
program. The runner verifies Mach-O magic and symbol names, but never
claims to execute or bind those symbols to the bridge. The opaque mutex
and condition storage must be supplied and validated by future ABI work.
