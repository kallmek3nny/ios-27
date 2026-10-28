# phase 21e: native host TLS, cooperative cancellation and exit cleanup

## scope

Original-source **native host** thread-local storage and cooperative cancellation
layer. Not a substitute for Darwin `pthread_key_t`, `pthread_t`, a Mach thread
port, direct guest-memory access, Objective-C runtime TLS, or Apple's cancellation
ABI. The ARM64 Mach-O fixture is a **Clang-generated callsite compilation**,
not a running iOS/Darwin application. Integration with Darwin syscall and symbol
thunks will require a separately verified ABI translator; the code must not be
exported as Apple ABI-compatible pthread functions without that integration.

The implementation uses an isolated runtime with a native POSIX `pthread_key_t`
and mutex, and a 64-key / 128-cooperative-cancellation-token bounded registry.
No proprietary Apple code, emulator, root modification, bootloader modification
or changes to shared CMake or existing agents' files.

## implemented milestones

1. **Generation-safe, runtime-isolated TLS**: creation, deletion, validation, stale-handle
   rejection, slot reuse, independent per-thread values, automatic native
   thread-exit cleanup, explicit detach, four deterministic destructor passes,
   reentrant `setspecific` from destructors, and bounded quotas. Destructors
   are called outside locks, with the slot pinned; deletion returns `EBUSY`
   while a callback is in flight. Deletion/recreation invalidates all previous
   thread-local values by generation, including values on other threads.
   Handles carry an explicit process-unique runtime identity in addition to
   slot and generation, so an old handle cannot alias an unrelated runtime,
   even after the former runtime was destroyed.
2. **Cooperative cancellation**: owner-bound, generation-protected tokens,
   explicit request, per-thread enabled/disabled state, pending cancellation
   preserved during disabled intervals, and deterministic checkpoints returning
   `ECANCELED`. Cancellation requests never terminate a thread asynchronously.
   Exit invalidates outstanding tokens, protecting against reused slots.
3. **Scoped cleanup and teardown**: strictly nested, capped 64-entry cleanup
   stacks; unique non-repeating scope IDs; LIFO pop/unwind; callback ownership
   transferred off-stack before invocation. Cleanup execution pins its owning
   thread context until callback return; attempts to detach or recursively
   drain the same stack are rejected with `EBUSY`. Callbacks cannot extend
   an in-progress drain by indefinitely pushing more cleanup handlers.
   Automatic thread exit runs
   remaining cleanup handlers before four TLS destructor iterations, and safely
   removes the thread context and its token. Runtime destruction refuses live
   threads, TLS keys and tokens. Reentrant detach during teardown is rejected.
4. **Compiler-produced integration and stress**: 2,048 pthread-created workers
   in 128 batches, 8,193 destructor invocations, 128 simultaneous cancellation
   registrations, reentrant/recursive destructor exercises, nested cleanup,
   invalid ownership/handles, repeated teardown, exhaustion, stale handles and
   deletion during in-flight destructor. Additional compiled tests cover 1,000
   separate runtime creations, 768 cross-key destructor reentries, 768 threads
   each managing three independent runtimes, and 4,000 key generations with
   at least one synchronized reader per generation amid concurrent deletion.
   Run both `-O0` and `-O2` with `-Werror`,
   optional ASan+UBSan, plus a genuine SDK-free Clang ARM64 Mach-O object.

## standalone verification

```sh
timeout 300s python3 tools/phase21e-standalone.py
PHASE21E_SANITIZE=1 timeout 300s python3 tools/phase21e-standalone.py
```

The independent workflow runs on `ubuntu-24.04` (x86_64) and
`ubuntu-24.04-arm` (arm64) when a PR changes phase21e-owned paths.
CI matrix results must be checked on the PR's **unchanged head SHA**.

## ownership contracts and safety limitations

- Callers must synchronize shutdown with other users of the runtime. No API
  call may race `ios27_21e_destroy` and no callback may outlive the runtime.
- TLS values are caller-owned. Replacing a value or deleting a key does **not**
  invoke its destructor. Callers must reclaim such values themselves, including
  values orphaned by concurrent key deletion. On thread exit, values with
  recycled key generations are discarded without destructor invocation.
- Destructor callbacks must return normally. A destructor that keeps rearming
  a value beyond four passes leaves that value under caller ownership. Do not
  call `pthread_exit`, throw or unwind across these C callback frames.
- A destructor pins its key only during its callback. Concurrent key deletion
  returns `EBUSY`, never waits while the callback could reenter the runtime.
  To delete a key safely, coordinate with thread writers and owners.
- Cancellation is a **check**, not pthread cancellation. Workers must observe
  `ECANCELED` and return through their normal control flow, allowing any
  external locks to be released safely. A pending request is not consumed by a
  checkpoint. No unsafe asynchronous cancellation or cancellation signal.
- Cleanup callbacks are invoked without holding the runtime mutex. External
  application resources must have their own synchronization; ownership is
  transferred before the handler runs. Reentrant unwind, callback-driven
  cleanup registration and same-thread detach are rejected while a callback
  is executing, avoiding reentrant teardown and freed-context reuse.
  Nonlocal control transfers are unsupported.
- Every key/cancellation handle contains a runtime identity that does not repeat
  until the process-wide 64-bit identity sequence is exhausted. These handles
  are *not* binary compatible with Darwin opaque pthread handles.
- The runtime's native pthread key is per-runtime; explicit thread detach
  before exit is recommended when using long-lived external thread pools.
- This module deliberately does not modify the shared build system, so users
  must run its standalone test or enable its dedicated CI workflow. Passing
  these tests does not establish actual iOS application execution.

## work timing

Initial engineering began at 2026-10-28 19:45 UTC. The first isolated PR
(#72) merged at 19:56:54 UTC after roughly twelve minutes of engineering
and CI. This follow-up fixes independently discovered lifetime and cross-
runtime handle hazards. Report its completion timestamp separately; initial
work plus follow-up work does not by itself establish a sixty-minute session.
