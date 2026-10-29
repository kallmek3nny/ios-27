# phase 22e: isolated mach-style ipc and mig fixture

## Scope and proof boundary

This is a **bounded, in-process, pthread-backed model** of selected Mach
message and service semantics. It is **not** a guest XNU kernel, native
`mach_msg` system call, Apple MIG runtime, authenticated process namespace,
real arm64 Mach-O execution, `libSystem` ABI implementation, or full iOS app
support. The 24-byte little-endian header, 12-byte port descriptor, and
16-byte OOL layout deliberately resemble public LP64 Mach wire structures,
but tests compile ordinary native C clients/servers against this isolated API.
The interface generator creates tiny **MIG-style fixtures**, not actual
Apple `.defs` stubs. Nothing requires proprietary Apple frameworks.

The existing shared `mach_ipc`, `mach_v2`, and guest modules are untouched;
this prototype is not wired into their dispatch paths or root CMake graph.
Only files owned by phase 22e and its independent workflow are changed.

## Milestones

1. **Bounded wire validation and namespace.** Strong size, alignment,
   disposition, descriptor-count and overflow checks. Up to 16 task IDs,
   128 never-recycled port IDs, and 32 queued messages per endpoint. Guest
   offsets are never cast to host pointers; OOL copies require a bounded,
   explicitly passed source arena. Unsupported voucher/descriptor forms fail.
2. **Transactional rights and lifecycle.** COPY_SEND, MOVE_SEND, one-use
   MOVE_SEND_ONCE replies, MAKE_SEND_ONCE reply creation, descriptor grants,
   port retirement, task death, dead-name state, and queue-drop rollback.
   Rights changes and in-flight accounting are made under one mutex. Pending
   grants protect referenced ports against destruction. The caller must join
   threads before `phase22e_free`.
3. **Bounded queues and timeouts.** FIFO send/receive waiter lists; timed
   condition waits, cancellation generation, queue backpressure, and strict
   no-dequeue-on-short-buffer behavior. Cancellation is a cooperative wake,
   not asynchronous POSIX thread cancellation. Timeout clocks currently use
   CLOCK_REALTIME for portability; clock jumps are a known limitation.
4. **MIG-style dispatch and concurrent services.** A deterministic, typed
   interface generator, request-ID and exact-size checks, response ID
   mapping, concurrent registration under lock, safe handler context
   lifetime via in-flight unregister preflight, and two concurrent native
   service threads with nested client/server RPC. The service handler
   executes outside the namespace mutex so nested RPC can progress.
5. **Adversarial and lifecycle tests.** Compiled native C clients/servers
   perform 100,000 main RPCs and 781 nested calls, corresponding to 201,562
   successful send operations and 201,562 successful receives. Additional
   tests cover deterministic 100,000-input malformed-wire fuzzing, queue
   saturation, OOL snapshots, atomic transfer rollback, dead names,
   cancellation, FIFO waiters, concurrent registration, exhaustion, and
   repeated namespace startup/teardown.

## Building and reproducing

Without changing shared CMake files:

```sh
# Bound orchestration; Python enforces 30-second generator, 300-second
# compile, and 180-second per-test limits.
timeout 300s python3 tools/phase22e-test.py
PHASE22E_SANITIZE=address,undefined timeout 300s python3 tools/phase22e-test.py
PHASE22E_SEED=0x1830c0de timeout 300s python3 tools/phase22e-test.py
```

On macOS runners without GNU timeout, the Python driver enforces the per
process limits and CI enforces a 15-minute total job limit. All tests compile
with `-Wall -Wextra -Wpedantic -Werror`; the independent CI matrix targets
native Linux x86_64 (Clang + sanitizers, GCC), macOS x86_64 and macOS arm64.
The latter two targets only count as **verified** after their CI runs pass.

## Known limitations

* A fixed-capacity single namespace, not genuine multi-process transport,
  XNU task ports, `mach_msg` traps, kernel-backed port notifications, or
  launchd/Bootstrap registration.
* Only one OOL memory descriptor per message; no VM mapping, OOL port arrays,
  deallocate transfer, vouchers, kernel audit trailers, or arm64e signing.
* OOL descriptors' sender offsets are cleared when received; copied payload
  bytes are returned in a separate, explicitly bounded output. This is not
  native Darwin OOL allocation semantics.
* A send-once reply right is installed at receive time. The model does not
  handle all XNU in-flight right and notification corner cases.
* Method IDs use a toy generated table and `request_id + 100` convention.
  No claim of wire interoperability with Apple's MIG-generated interfaces.
* No automatic shutdown of threads from free; callers must cancel and join.
* Host-pthread synchronization is not proof of Darwin scheduling fairness.
* A native GCC/Clang C executable is **not** evidence of verified ARM64
  Darwin Mach-O guest execution. Existing Mach IPC modules remain separate.

## Observed local results

Native x86_64 Clang release and AddressSanitizer/UndefinedBehaviorSanitizer
passes recorded during implementation on 2026-10-29 UTC. Stress: 100,000
client RPCs, 781 nested RPCs, 201,562 sends, 201,562 receives, and zero
pending messages/in-flight transfers. Negative fuzz: 100,000 deterministically
rejected malformed messages; FIFO, registration, quota, and teardown tests pass.
No ARM64 run or PR CI status is claimed here until independently observed.
Actual start/end and active development duration should be recorded in the
handoff or PR after the current session finishes, rather than inferred from
operation counts or padded with artificial delays.
