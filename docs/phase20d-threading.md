# phase 20d: native Darwin libSystem threading bridge

## scope and ABI boundary

Original-source **native host threading foundation**, not a drop-in Darwin pthread runtime. Public two-word `ios27_20d_handle` is generation checked, **not** Darwin `pthread_t` or `_opaque_pthread_*`. Darwin ARM64 calling conventions, Mach thread ports, Apple TLS, pthread symbol exports and guest pointer validation remain unimplemented. Separate independently verified Mach-O ABI thunks and loader integration are necessary before real iOS app compatibility can be claimed. No emulation, proprietary Apple binaries or root changes.

## implemented milestones

1. **Thread lifecycle**: bounded 1–64 worker threads, creation, joining, detach, thread self identity, stale handle invalidation, rejection of double joining and join-on-self, cooperative cancellation requests/checkpoints, and quiescent teardown refusing live resources.
2. **Synchronization**: error-checking/recursive mutexes, condition variable signaling and absolute realtime deadlines, reader/writer locking, once-initialization ordering, in-flight pinning and 128 slot quota.
3. **TLS and resource control**: 64 host native thread-specific keys, destructor cleanup on thread completion, generation-checked deletion, quotas, usage reporting, destroy refusing live handles.
4. **Compiler verification**: multithreaded native C integration executable with contended operations, initialization races, timeout, quotas, TLS destruction, join and detach; SDK-free Clang emits actual ARM64 Darwin Mach-O callsites. x86_64 and ARM64 Linux native hosts can run the C integration; Mach-O object emission does not imply ARM64 Darwin execution.

## independent execution

Run `timeout 120s python3 tools/phase20d-standalone.py`.
Optional sanitizer: `PHASE20D_SANITIZE=1 timeout 180s python3 tools/phase20d-standalone.py`.

Shared CMakeLists.txt, workflow and HANDOFF.md are untouched. Existing repository CI does not automatically run phase20d standalone tests. CI success alone cannot establish independent integration coverage.

## contracts and limitations

- Threads execute via host pthreads. Raw guest pointers are unsafe until guest memory translation is implemented.
- Callers must coordinate teardown. Do not concurrently use a freed runtime. Destroy requires no in-flight workers, sync objects or keys.
- Deadlines are absolute **CLOCK_REALTIME nanoseconds**, not Darwin mach-absolute-time. Condition waits require a locked mutex and predicate loops. Recursive mutexes should be locked once for waits.
- Cancellation is **cooperative status checking**, not asynchronous thread termination. Callbacks must return normally. Once initializer callbacks must not exit through nonlocal control transfer.
- A TLS key deletion does not invoke destructors for existing values. Avoid key deletion during detached worker cleanup. `pthread_rwlock_unlock` must be called by a lawful owner; invalid ownership is not synthesized.
