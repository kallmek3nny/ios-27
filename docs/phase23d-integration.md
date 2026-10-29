# Phase 23d: bounded Darwin-style syscall and Mach IPC/MIG integration

This is an **original-source deterministic userspace compatibility model**, not XNU,
not a real iOS syscall trap, and not a way to run Apple binaries. It has no direct
host filesystem, network, or arbitrary guest-to-host pointer interface.

## Milestones implemented

1. A strict `0x230d0001` typed six-argument syscall frame and fail-closed
   unsupported-call paths. Only modeled BSD read/write/open/close/lseek/getpid
   and a phase-specific RPC extension are handled.
2. Per-process 16 KiB offset-addressed memory, permissions on four pages,
   bounded reads and writes, synthetic input/output fixture files, private
   descriptor tables, and generational process/thread identity.
3. Per-service task-local names and generation-safe ports, explicit grants,
   send-once rights, port-death revocation, a fixed-structure 160-byte RPC
   wire format, nonzero strictly monotonic correlation identifiers per thread,
   fixed bounded echo/sum/relay services and deterministic logical timeout budgets.
4. Thread-safe serialized dispatcher with synchronous nested modeled RPC,
   guest output / send-capability commit only after complete validation,
   cancellation at syscall entry, repeatable 120k+ multithreaded RPC workload,
   malformed input fuzzing, teardown/reuse, and ASan/UBSan verification.

## Trust boundary

All guest addresses are offsets within an owned arena. No guest pointer is
cast into a host address. No real host `open`, `read`, `write`, socket, or Mach
kernel function is called. Fixture paths are limited to `/fixture/input` and
`/fixture/output` and contain no host filesystem mapping.

The user-facing RPC ABI is a fixed 32-byte header followed by at most 128 bytes.
`flags` only permits a send-right copy on a valid echo/sum message; a relay
message uses `transfer` as the next bounded service port. There are no arbitrary
callbacks, kernel privileges, out-of-line pointers or unrestricted descriptors.
The implementation assumes a little-endian 64-bit host and compiler-compatible
C structure layout; the wire size is enforced by a compile-time assertion.

## Validation and explicit limitations

Run `timeout 300s python3 tools/phase23d-verify.py`, and for sanitizers use
`PHASE23D_SANITIZE=1 timeout 300s python3 tools/phase23d-verify.py`.
The workflow installs the required LLVM linker if absent, then runs the
same checks on x86_64 and arm64 Ubuntu in normal and ASan/UBSan modes. Commands run with individual finite timeouts.

SDK-free Clang compiles four real ARM64 Darwin Mach-O client/service *objects*.
The verifier **requires** linking three independent ARM64 Darwin MH_DYLIB
images and a native MH_EXECUTE with direct and transitive dependency commands.
It verifies bounded Mach-O headers, library load commands, exports, and
symbol-resolution via LLVM `ld64.lld`. The linker script includes a narrowly
validated **MH_OBJECT copy-only** workaround for Swift-built linker distributions
that reject Clang's platform metadata. Neither the source objects nor linked
artifacts are rewritten afterward. The linked binaries are not executed on
Linux; the host-compiled compatibility model is exercised separately.
The C integration test instead drives the host-compiled model.

Timeouts represent a logical per-call service-hop budget, not wall-clock waits.
Cancellation is checked at syscall entry; there is no mid-call preemption.
RPC execution is globally mutex-serialized, so workloads demonstrate concurrent
callers and reproducible contention, not parallel service workers. Nested
RPC supports only a single relay dispatch type and a depth cap. The code does
not yet integrate any unmerged phase22a/22b/22e functions. Future integration
must use an independently tested adapter once these APIs land on `main`.

The runtime must not be freed while external threads are still calling into it;
the caller joins workers before free. This is a standalone prototype, not yet
wired into `ios27_core` because shared-file edits are excluded by the lease.
