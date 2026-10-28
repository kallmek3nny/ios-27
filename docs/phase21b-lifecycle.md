# phase 21b: generation-safe Darwin-oriented pthread lifecycle foundation

## ABI truth

Original-source **host pthread lifecycle model**, not Apple `pthread_t`, `_opaque_pthread_t`, `pthread_attr_t`, pthread priorities, an exported `libsystem_pthread.dylib`, or a loader/guest-memory ABI translation. The real Clang-generated ARM64 Darwin Mach-O fixture proves compiler-produced external callsites and relocations, **not runnable iOS apps**. No Linux layout is treated as Darwin's. Guest pointers must be validated and translated by a future isolated bridge before a callback can run. No proprietary binaries, root changes, or emulation.

## milestones

1. **Exclusive lifecycle ownership**: create, generation-checked identity, self, join, detach, join vs detach and join vs join rejection; self-join returns `EDEADLK`. Reclamation increments generation. After `UINT32_MAX`, slot is retired forever instead of reusing an old handle.
2. **Detached cleanup ordering**: a dedicated host reaper `pthread_join`s detached guest workers rather than calling host `pthread_detach`. Guest detachment is immediate but the runtime slot remains occupied until the entire native worker exit and host TLS-destructor sequence completes. No runtime teardown while any tracked worker is live; `wait_idle` provides a bounded completion barrier.
3. **Attributes, quotas, and deterministic errors**: own versioned attrs with bounded page-aligned stack and guard sizes. Host scheduling/priority and guest-provided stack addresses are denied with `ENOTSUP`; invalid stack sizes and attrs return `EINVAL`. Quota exhaustion returns `EAGAIN`. Public IDs are not host `pthread_t` values. Host pthread failure codes are passed through where appropriate.
4. **Stress and compiler evidence**: 2048 real worker transitions with TLS destructors, 128 concurrent join/detach races, forced quota exhaustion, stale handle reuse checks, explicit stack/guard fixture, and Clang `arm64-apple-darwin` Mach-O object external relocations. Run in native x86_64 and ARM64 GitHub Actions jobs independently. Preserve `-Werror`.

## standalone commands (all bounded)

`timeout 180s python3 tools/phase21b-standalone.py`

`PHASE21B_SANITIZE=1 timeout 240s python3 tools/phase21b-standalone.py`

## limits and ownership

- Source and tests are standalone; no shared `CMakeLists.txt` or `HANDOFF.md` edits. Native host tests never execute the Darwin Mach-O object.
- Callback must be a trusted native host function. The bridge does not permit arbitrary Mach-O guest code to execute directly, and does not model Darwin cancellation, Mach ports or workqueues.
- The runtime owner must externally serialize `runtime_destroy` against other public calls; operations racing destruction of a freed runtime are unsupported. Thread callbacks should not call runtime destruction.
- `runtime_wait_idle` uses CLOCK_REALTIME milliseconds and returns `ETIMEDOUT` on expiry. A finished joinable worker remains tracked until joined or detached. No detached slot is reclaimed while host TLS destructors remain pending.
- The reaper owns detached joins, callers own explicit joins. Callbacks using host asynchronous cancellation or nonlocal exits may have platform-specific behavior; only normal returns and the `pthread_cleanup` completion handler are covered.
