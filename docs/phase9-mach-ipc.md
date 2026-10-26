# phase 9: synthetic mach-style ipc and port-rights boundary

this phase is an independently testable **in-process userspace simulation**.
it does not make android's linux kernel speak xnu/mach, does not intercept
darwin syscalls, and does not run apple frameworks or springboard.

## implemented and tested

- wire-level 24-byte `mach_msg_header_t`-shaped header and optional
  4-byte descriptor count followed by one 12-byte port descriptor.
  layouts and the constant values for `MACH_MSGH_BITS_COMPLEX`,
  `MACH_MSG_TYPE_COPY_SEND` (19), `MOVE_SEND` (17) and port-descriptor
  type 0 are cross-checked against apple's public open-source
  [xnu message.h](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/message.h).
  the implementation is original c code, not copied xnu kernel code.
- up to 8 simulated task owners, 32 ports, 4 queued messages per port,
  256-byte message maximum and 1024 send rights per simulated task and port.
- owner-checked receive right creation/destruction and explicit minting
  and releasing send rights. port names never recycle within a namespace.
- deterministic fifo send and receive, with immediate `TIMEOUT` on full
  or empty queues. the timeout argument is accepted for test-api stability
  but no kernel wait/timing is promised.
- optional complex message with **exactly one** send-right descriptor:
  copy-send keeps the sender right, move-send atomically consumes it only
  after the queue can accept the whole validated message. rights remain
  in-flight until successful receiver dequeue.
- undersized receive buffers return required length without dequeueing
  or modifying rights. destroying a queue drops its in-flight transfers,
  and destroying an in-transit port is blocked until rights are delivered
  or discarded.
- regression tests include unauthorized sends/receives, invalid port
  names/descriptors, right-count bounds, fifo order, queue limits,
  short receive buffers, full-queue rollback, and stale names.

## limits and security semantics

- in-process task ids are synthetic, not process ids or host identity.
  the owner namespace is *not* a host security boundary or sandbox.
- one synthetic 32-bit port name is shared across task ids; this is not
  xnu's per-task name translation. only simple remote copy-send and
  single port descriptors are accepted. reply ports, vouchers,
  receive-right transfers, send-once, ool memory, priority, trailers,
  cancellation, blocking/timeouts and mach_msg2 are **not supported**.
- message receive rewrites remote port to zero and local port to the
  receiver's synthetic name. this is an explicit fixture behavior,
  **not** complete mach kernel semantics.
- no privilege, root, kernel hooking, filesystem changes, bootloader,
  firmware, odin/d2 interaction, persistent state or apple binaries.
- native arm64 linux ci compiles and directly executes the c abi
  tests. this does **not** establish actual iOS ABI compatibility or
  on-device galaxy s25+ behavior.

## verification (trusted linux, each operation hard-bounded)

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
```

## next steps

1. model per-task port name tables (currently port names are global),
   enforce per-task receive-right visibility, and add send-once semantics.
2. link a narrow darwin syscall trap dispatcher to a separately verified
   userspace mach facade, without forwarding arbitrary syscalls to android.
3. test an independently compiled, lawful redistributable darwin fixture,
   then expand message descriptors and real mach-like error contracts.
4. investigate the blockers for native, *official* apple ui execution:
   arm64e/pac, dyld caches, signing/entitlements, system services,
   objc/swift frameworks and graphics compositing.
