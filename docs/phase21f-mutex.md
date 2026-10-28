# phase 21f: fair and bounded pthread mutex bridge

This independent original-source host-native synchronization component provides
three mutex policies with generation-checked handles and non-reused runtime
identity tags. Cross-runtime handles are explicitly rejected even when numeric
slots and generations coincide. Its handle values are NOT
Darwin's opaque pthread mutex ABI, and compiled Darwin callsites are not run or
bound by the bridge. It does not claim native iOS app compatibility.

## milestones

1. Own/recursion tracking: normal and error-checking self lock return
   `EDEADLK`, recursive self lock increases a checked depth, trylock returns
   `EBUSY` rather than blocking, and non-owner unlock returns `EPERM`.
2. FIFO handoff: contending threads are enqueued in arrival order under one
   internal POSIX guard. Releasing the final recursion level transfers logical
   ownership to the oldest queued waiter BEFORE waking it. Arriving threads
   cannot barge ahead of queued threads; an ordinary mutex holder can still
   starve waiters by never releasing, and OS scheduling latency is unbounded.
3. Bounded wait-for graph: when a thread requests an owned mutex, follow
   existing same-runtime blocking edges to reject cycles with `EDEADLK`.
   Cancellation removes queued nodes or retransfers a granted mutex atomically.
   Slots, waiters, and generation rollover are bounded, with deterministic
   `EAGAIN`, `EINVAL`, and `EBUSY` failures for exhaustion/invalid state.
4. Runtime isolation uses an atomic, saturating allocator for runtime IDs;
   retired runtime tokens cannot alias a freshly created runtime. Six
   concurrent worker threads create/destroy 6,000 separate runtimes and test
   malformed, cross-runtime, and stale identity handles. A separate
   1,200-handoff fairness program verifies every queued worker gets the mutex
   in arrival order while four concurrent try-lock competitors attempt to barge.
   A 16-thread, two-slot lifecycle stress performs 38,400 concurrent
   create/lock/unlock/destroy cycles, quota failures, and stale-handle checks.
   Fifteen separate 64-thread wait-for graphs verify that only the closing
   edge is rejected while acyclic chains unwind without ownership leaks.
   A 24-round, 48-waiter cancellation storm removes head, middle and tail
   queued nodes, then verifies that all remaining workers receive handoffs;
   all 1,152 threads either acquire or cancel without leaving an owner or
   queue node. Separate adversarial tests cover cancellation at grant time.
5. Compiler-produced native executable suites exercise FIFO ordering,
   ownership errors, recursive calls, invalid handles, two-lock cycles,
   100,000 contended increments, 250 cancellation/handoff races, and capacity
   exhaustion at `-O0` and `-O2` with `-Werror`. An SDK-free fixture builds real
   Darwin Mach-O pthread callsites for ARM64 and x86_64, without executing them.
   An independently runnable Mach-O parser validates architecture, load
   commands, bounds, external undefined symbol records, and actual callsite
   relocation references, not merely the presence of symbol-name strings.
   Eleven malformed variants of each real compiled Mach-O object must fail
   closed under independent bounds, architecture, header, and symbol checks.

## independent execution

`timeout 300s python3 tools/phase21f-standalone.py`

The phase-specific workflow covers Linux x86_64 and ARM64 runners and builds
Darwin-target objects on each. Shared CMake/entrypoint files are unchanged.

## contract and known limitations

- API is an explicit bridge interface rather than Apple pthread symbol/ABI
  compatibility. No Apple implementation was copied or embedded.
- Only deferred cancellation is supported during blocked lock operations.
  Owners are responsible for cleanup handlers when canceled after acquisition.
  Asynchronous cancellation and thread termination while owning a mutex are
  unsupported; a non-releasing owner can block indefinitely.
- FIFO fairness is defined by arrival at the internal mutex queue, not by
  OS thread start ordering. FIFO does not establish strict real-time progress.
- Deadlock detection covers lock-order cycles for waiters of this same runtime;
  it does not discover deadlocks through other APIs or across runtimes.
- Runtime destruction requires all users to be quiescent. Clients may not use
  handles or runtime addresses after destruction. The bridge does not protect
  calls racing with destruction of the runtime object itself.

## extended active stress campaign

`timeout 300s python3 tools/phase21f-campaign.py --duration 240`

This optional bounded test repeatedly runs compiler-produced native programs
with varied deterministic PRNG seeds, records graph-cycle rejections, and
interleaves contention/cancellation/queue-lifetime suites. Each child program
has a hard 60s limit; compilation has a hard 120s limit. No artificial waits
or idle padding are used. This is independent of the normal quick CI gate.

The capacity test compiles a 16-waiter bridge to verify the 17th waiter
receives `EAGAIN`, then cancels one queued waiter and verifies quota reuse.
The production default remains 1,024 concurrent waiter nodes.

For longer work sessions, the bounded no-idle orchestrator is available:

`timeout 2700s python3 tools/phase21f-endurance.py --duration 2400`

It alternates gcc/clang campaigns, verifies each child's success, and records
actual UTC start/end timestamps plus elapsed test duration. Each wave and the
entire session are hard-bounded. No reported duration is estimated or padded.
