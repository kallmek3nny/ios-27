# Phase 25c: isolated Objective-C ARC ownership runtime

## Scope and verified claims

This branch deliberately changes **only** `phase25c-*` paths. It starts from
`main` commit `a6cc52fbf54edc2e00fdfae8a31205a53049e9d6`.
The single-threaded `objc20c` implementation, phase22d, PR #94 / #100 and
phase25a/25b files are read-only. This runtime is an independent ownership
interface ready for later dispatcher integration, **not** a substitute for
Darwin's full libobjc.

### Engineering milestones

1. **Allocation and identity.** Validates class pointer, generation, pointer
   alignment, object bounds, ivar range and memory limits before zero-filled
   allocation. Stores isa at offset zero. Stable object tombstones and
   quarantined payloads prevent ABA/reuse within a runtime generation.
   Allocation failure injection leaves counts and class identity untouched.
2. **Strong ownership and dispatch.** Mutex-protected retain/release counts,
   retain-before-release strong stores, terminal-release weak zeroing,
   finalization exactly once, and dispatch pins which postpone destruction
   until active method execution returns. Class unload is denied as long as
   the matching image generation has live/finalizing objects.
3. **Weak ownership.** Synchronized weak slot registration, retained loads,
   remove/unregister, and atomic zeroing with terminal release under the
   **same registry lock**. Tests race 140k retained weak loads across four
   threads against terminal release. Registered weak storage must remain
   valid until unregistered; consumers must never race raw slot loads against
   runtime writes.
4. **ARC ABI and pools.** Scope-bound `objc_retain`, `objc_release`,
   `objc_storeStrong`, `objc_storeWeak`, `objc_loadWeakRetained`,
   `objc_destroyWeak`, `objc_autorelease`, and autorelease pool push/pop
   entrypoints. Per-runtime pthread TLS contexts support nested, strict LIFO
   pools, reverse-order drains, and automatic draining on worker-thread
   exit. Destructors are invoked without the registry mutex and may release
   owned children.

### Compiler-produced ARM64 Darwin evidence

`tests/phase25c-arc-darwin.m` uses real Clang Objective-C ARC, strong and
weak ivars, automatic weak cleanup, autorelease pools and a method send.
`tools/phase25c-run.py` compiles a real Mach-O arm64 Darwin object and
checks the generated assembly for `objc_retain`, `objc_release`,
`objc_storeStrong`, `objc_storeWeak`, `objc_loadWeakRetained`,
`objc_destroyWeak`, `objc_autoreleasePoolPush`,
`objc_autoreleasePoolPop`, `objc_autoreleaseReturnValue` and
`objc_msgSend`. On a macOS worker it also requests a real arm64 Mach-O
dynamic library from the system linker.

**Critical limitation:** the ARM64 fixture is compiler-produced and linked,
but its machine code is not executed by the standalone x86_64 ownership
tests. No cross-dylib Darwin ARC execution, cross-guest dispatcher binding,
or real ABI dispatch correctness can be claimed from symbol inspection.
The exported `objc_*` functions tested on the host are genuine runtime
ownership entrypoints, but are not evidence of compiler-generated Darwin
instructions running. Integration requires phase25a image-binding, phase25b
typed native dispatch, and verified guest load/unload behavior.

### Explicitly unsupported / fail-closed

- Tagged pointers, optimized autoreleased-return handshake, objc exception
  unwinding, arm64e ptrauth, nonpointer isa, side-table/associated-object
  representations, custom `retain`/`release` overrides, resurrection,
  weak external storage without stable lifetime.
- A missing ARC scope or invalid argument to a void-returning compiler ABI
  entrypoint aborts. Typed APIs return `-1` for invalid state.
- Unloading classes with live instances, reuse of dead object addresses, and
  destroying the runtime with live objects, active TLS contexts or weak
  registrations are rejected. Callers must join guest threads before
  destroying the runtime. Strong slots owned by a deallocated object must
  be cleared by its dealloc callback; no circular-reference collector.
- `objc_msgSend` and `objc_autoreleaseReturnValue` remain unbound in the
  phase25c standalone runtime; future native image integration must support
  them with verified ABI semantics.

### Reproduction

```sh
python3 tools/phase25c-run.py
```

The runner imposes a hard timeout on every build (300s), Darwin object
compile/link (120s), and test execution (180s). No configure or Git/GH
subprocesses are spawned. The dedicated PR workflow has a 15-minute job
timeout and separate Linux and macOS builds.

The deterministic test suite performs at least 240k reference transitions
and 140k synchronized weak loads, confirms weak-zeroing before dealloc,
deferred destruction while dispatch-pinned, pool LIFO, cross-thread pool
drains, class-unload rejection, failed-allocation rollback, 24 repeated
runtime teardown cycles, and ASan/UBSan when available.

### Known limitations and pending gates

- This is a **partial phase25c implementation**, not a verified completion of
  the user's entire 3+ active-hour assignment. Record actual engineering
  time from the developer's session rather than claiming an unmeasured
  duration.
- Integrate a native executable/dylib guest driver when 25a/25b loader APIs
  are merged. Add actual ARM64 execution tracing, independent dylib imports,
  side-table ABI rejection probes, ASan/TSan cross-thread owner tests,
  preemption-controlled pool-drain races and guest process lifecycle tests.
- Do not merge until the PR's frozen head has green required CI and an
  expected-SHA comparison confirms `main` has not changed since the
  originally captured base SHA. If it has, rebase in isolation and rerun.
