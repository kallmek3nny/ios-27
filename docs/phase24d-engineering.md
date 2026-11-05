# Phase 24d - isolated process, BSD descriptor, Mach-style message service research

This is original-source, fail-closed *modeled* userspace compatibility, not Apple XNU,
not macOS kernel traps, and not a real arm64 Darwin process execution bridge.
It is standalone because CMakeLists.txt, shared runtime files and other phase-owned
paths are excluded from the concurrent ownership lease.

## Milestones

1. Typed `0x240d0001` arm64-style six-register frame with explicit argument
   count, strict supported selector allowlist, and generation-safe process and
   thread handles. No host addresses cross the guest boundary.
2. Per-process 64KiB page-protected guest memory and fixed fixture files.
   Guest copyin/copyout validates complete spans *before* memmove. Multiple
   descriptor aliases share a single open-file-description offset while CLOEXEC
   is per-FD. Unsupported files and syscalls fail closed.
3. Bounded synthetic Mach-style receive ports, send grants, send-once replies,
   enqueue-time explicit right passing, reply-port correlation and dead-name
   notification. Per-port queues have 8 messages. Locked admission is atomic
   with respect to queue and right state. External callbacks are not supported.
4. Cross-boundary pthread wait/wakeup and timed cancellation, recursive
   guest-level request/reply through three modeled processes, deterministic
   malformed-wire rejections, repeated task cleanup and accounting.
5. Actual compiler-produced arm64 Darwin Mach-O object and linker output:
   four MH_DYLIBs with imports (`echo`, `sum`, `relay`, `worker`), plus four
   MH_EXECUTE clients linked against the correct dylib. Link-time-only TAPI
   contracts identify an **unimplemented native service-bridge symbol**.

## Local evidence and reproducibility

The engineer ran these bounded commands on an x86_64 Linux host:

```
timeout 300s python3 tools/phase24d-verify.py
timeout 300s env PHASE24D_SANITIZE=1 python3 tools/phase24d-verify.py
```

Both passed independently, with `-Wall -Wextra -Wpedantic -Werror` retained.
Host test runs (each optimization/sanitizer mode independently):

- 221,060 modeled syscall and IPC trap calls
- 97,019 successful sends and 97,019 successful receives
- 107,027 memory copy operations and 10,009 rejected traps
- 6 concurrent producer/consumer host workers plus cancellation/port teardown
- finite-time queue overflow, wait/notify, and whole-buffer memory rollback
- no active tasks, threads or queued messages after shutdown

The verifier compiles Darwin source as `arm64-apple-macos13` and checks bounded
Mach-O headers, LC_LOAD_DYLIB dependencies and Mach-O file types. `ld64.lld`
actually links the four dylibs and four executables. Where Swift LLVM linkers
reject Clang's LC_BUILD_VERSION, only a *copy of an MH_OBJECT* has that
platform command removed before linking; no linked Mach-O is fabricated.

## Boundaries and incomplete requirements

- **No arm64 Darwin instructions executed.** The 8 linked arm64 images have
  real cross-dylib references but lack dyld/libSystem/native trap adapter.
  The special `ios27_24d_native_service_bridge` symbol is only a TAPI
  import declaration, with no executable implementation. Its absence is the
  precise execution boundary; it does not imply an emulated Darwin kernel.
- No `fork`, `execve` or actual close-on-exec transition; only the
  per-descriptor modeled CLOEXEC flag is stored/queried. No host-backed FDs.
- No MIG wire translator, task-wide real capability namespace, OS mach_msg,
  priority inheritance, Apple pthread ABI, exceptions or signals. Timeouts
  use wall-clock condition waits with 2-second maximum, not Darwin clocks.
- Host pthreads execute the test harness, not guest arm64 pthread code.
  User-visible diagnostics are status enums and bounded resource counters.
- Calling `ios27_24d_free` while host worker threads are still executing is
  unsupported: clients must join host workers before freeing the runtime.
- This change does not consume or edit PR #95 or unmerged phase22e changes.
- The requested 3 hours of *active* engineering are not completed in this
  shorter session. PR must remain **draft and unmerged** regardless of tests.

## Audit notes

Initial verified `main`: `527adf78a63890e219812bed8ff773af2f5cd70f`.
Refresh-only commits advanced main during development; the branch was based on
`3cc930d3cf6a3f3a0ab396b3637562798d3da9a7` after a new verification. Work performed on 2026-10-29 approximately
21:44-21:53 UTC, not three hours. Build/test subprocesses use hard
30/120/180/300-second limits; GitHub connector calls are request-bounded.
The PR-triggered CI matrix checks x86_64 and arm64 Linux hosts in normal
and ASan+UBSan modes, **not native Darwin execution**. Merge requires frozen
PR head CI passing, a main-unchanged check, and expected_head_sha.
