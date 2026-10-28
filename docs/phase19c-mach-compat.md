# Phase 19c: bounded Darwin/Mach userspace compatibility

This milestone extends the in-process, guest-facing **Mach fixture**. It does **not**
claim to implement the XNU kernel, native Darwin traps, task security, host exception
delivery, or a complete SpringBoard/iOS runtime. No Apple binaries, bootloader
changes, root operations, or host Mach services are used.

## Task and thread identities

- Tasks remain explicit fixture IDs in the range 1..8.
- Thread IDs are globally unique within a space, monotonic, and never reused;
  each task can own at most 16 live thread records.
- Creating, releasing, and querying a thread handle require the same task ID.
  Teardown invalidates its live handles without resetting the ID generation.
- No host pthread identity is returned as a guest identity.

## Exception port registration and delivery

- Each task has eight independent exception slots (bits 0..7).
- Registering a nonzero port requires an existing local **send** right to a
  live receive port; a zero port clears the selected slots.
- Registrations are references, not hidden send-right grants. After the last
  send right is released or the receive port is destroyed, lookup returns zero,
  and delivery fails closed. No reused local name can inherit a stale handler.
- Each successful delivery enqueues a 40-byte, copy-send, noncomplex message.
  The header carries message ID `0x495319c0`; the four little-endian words at
  offsets 24, 28, 32 and 36 carry thread ID, source task, exception slot and
  exception code. This is a **private fixture envelope**, not XNU's
  `exception_raise` MIG ABI.
- Queue exhaustion returns `IOS27_MACH_TIMEOUT` without changing rights or
  stored message state; too-small receive buffers preserve the original packet.

## Bounded guest-memory operations

- The private Darwin register facade additionally exposes `VM_COPY` (source
  offset, destination offset, length) and `VM_ZERO` (destination, length).
- Both enforce the caller-supplied disjoint arena, overflow-safe range checks
  and at most 4KiB per call. `VM_COPY` permits overlapping spans via
  `memmove`; invalid requests leave both the arena and output registers intact.
- These are bounded memory operations, not host `mmap`, `mprotect`, or XNU VM
  mappings. No guest memory can alias the private Mach state through this API.

## Concurrent caller contract

- Every public mutable Mach v2 operation is serialized on a process-local C11
  atomic flag, including rights, port sets, send/receive, teardown, threads,
  and exception registration.
- Compound operations use internal unlocked implementations under one outer
  lock to avoid nested locking.
- Callers must still keep `ios27_mach_v2_free` outside concurrent operations
  and synchronize their **own** external guest arenas, register structs, and
  buffers if they share them across threads.
- Guest queue operations are poll-only; the lock yields under contention but
  never turns a guest queue timeout into an operating-system wait.

## Verification and remaining ceiling

The registered CTest targets `task-local-mach-names` and
`darwin-fixture-abi-lifecycle` include phase 19c tests covering:

- independent task/thread identities, stale handles and per-task exhaustion;
- registration validation, missing rights, FIFO exception delivery, full
  queues and failed short receives without dequeue;
- ownership isolation, expired handler names, teardown and reallocation;
- concurrent host workers allocating, sending, receiving, dropping rights,
  destroying ports and racing task teardown;
- bounded guest-memory copy/zero, overlapping copy, invalid pointers, size
  ceilings and rollback.

The next major ceiling is native Darwin Mach trap table compatibility,
credential-bearing authenticated task identity, real exception MIG message
semantics, VM mapping/protection rights, and full dead-name notification
delivery. Neither the tests nor these fixture APIs establish those features.
