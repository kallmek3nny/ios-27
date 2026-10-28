# Phase 17c: bounded multi-task Mach IPC lifecycle

## Implemented (synthetic userspace fixture)

The task-local `ios27_mach_v2` Mach-style wire path now accepts one through
**four** `mach_msg_port_descriptor_t` records in a complex message. Each
descriptor may COPY_SEND or MOVE_SEND. The destination may also MOVE_SEND,
MOVE_SEND_ONCE, or COPY_SEND, and replies may MAKE_SEND_ONCE or MOVE_SEND_ONCE.
Every right is keyed to an underlying port object but named independently in
each task's namespace.

A send validates the entire message before a commit: wire layout and reserved
fields, each descriptor's object and disposition, aggregate sender debits
(including collisions with the destination), aggregate receiver credits for
repeated objects, new task-local alias slots, and destination queue space. If
any check fails, no right, queue, or name reservation changes. Only after
the full preflight does it reserve pending credits, consume MOVE rights, and
enqueue. A receive normalizes *every* delivered descriptor to MOVE_SEND,
resolves names in the recipient's namespace, and consumes all pending credits
in a single synchronous transaction. TOO_LARGE never dequeues.

Port destruction and task teardown cancel pending credits for **all**
descriptors of every discarded message. Credits pending in another live
queue cannot be revoked mid-transfer, so attempted retirement fails
transactionally. Task-local names never recycle.

## Dead-name observation (fixture API)

`ios27_mach_v2_watch_dead_name(space, task, name)` enables an opt-in
synthetic watch on an existing send or send-once alias.
`ios27_mach_v2_poll_dead_name(space, task, &name)` returns the oldest
retired local name, or TIMEOUT without altering the output if empty. The
Darwin register fixture exposes WATCH_DEAD and POLL_DEAD selectors. A
destroyed port queues an event for each subscribed alias, and a multi-port
task teardown queues events for watchers in surviving tasks. An exiting task
loses its own pending events.

Each task has exactly `IOS27_MACH_V2_NAMES` pending event slots. All event
requirements are checked **before** destroying any port or tearing down any
namespace. If there is insufficient space, the operation returns LIMIT
without partially retiring ports. Events carry a historic local name, not
an acquired send right; old names cannot be reused.

These are observable fixture events, **not real XNU dead-name Mach messages**
or send-once notification rights. No notifications are emitted through host
syscalls, worker threads, or operating-system ports.

## Deterministic test surfaces

Existing test executables now cover:

- Four descriptors with repeated rights, mixed COPY/MOVE, and send-once
  replies across three independent task namespaces.
- Aggregate debits when one right is simultaneously destination and several
  descriptors, malformed descriptor rejection, and unchanged rights on error.
- TOO_LARGE reception, normalized received dispositions, and cleanup when a
  destination port is destroyed with unclaimed credits.
- Foreign grant pinning, failed owner teardown, FIFO dead-name events,
  saturation/backpressure, and full multi-port teardown.
- AArch64 register fixture dispatch for notification selectors, input range
  rejection, and preserved output registers on failed/empty polls.

Build and run with the existing CMake targets and CI, without modifying
`CMakeLists.txt`:

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 300s cmake --build build --parallel 2
timeout 180s ctest --test-dir build --timeout 30 --output-on-failure
```

The PR must pass both `ubuntu-24.04` and `ubuntu-24.04-arm` jobs on one
frozen head SHA. Avoid merging on a stale head.

## Important boundaries

This is a synchronous, bounded memory-safe **simulation of a Mach IPC subset**
and does not provide SpringBoard integration, entitlement enforcement, Mach
kernel traps, OOL memory descriptors, port sets, actual dead-name messages,
vouchers, or userspace dyld frameworks. It is not a real iOS kernel, an
Android-to-XNU syscall adapter, or a runnable official Apple framework.

Remaining work needs genuine guest-virtual memory mapping validation,
message trailers, OOL descriptors, real MIG message/notification semantics,
authenticated caller identity, and supported Darwin service routing.
Unknown selectors and unsupported descriptors must continue failing closed.
No bootloader, firmware, root, or proprietary Apple binary interaction.
