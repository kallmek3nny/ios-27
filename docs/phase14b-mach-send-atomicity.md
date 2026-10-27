# phase 14b: mach send-right atomicity and lifecycle regression

Scope: `src/mach_v2.c`, `include/ios27/mach_v2.h`, and their existing
Mach test target. No loader, dyld, build configuration, kernel, bootloader,
firmware, or persistent root changes.

## Behaviors

- Admit Darwin-style `MACH_MSG_TYPE_MOVE_SEND` (17) for the remote
  destination, consuming one sender send user reference **only when enqueued**.
- Preflight two send-reference debits when the destination and sole port
  descriptor both `MOVE_SEND` the same task-local name. Insufficient rights
  return `BAD_RIGHT` without queue changes or partial consumption.
- Receive-side header remote disposition reflects a received reply
  send-once right instead of reporting zero rights while exposing a name.
- Preserve the existing deliberately conservative port-destruction and
  task-teardown behavior: pending grants prevent destruction rather than
  creating broken queued transfers. Names are monotonic and stale handles
  are never rebound to newly allocated port objects.

## Deterministic regression coverage

- Combined destination+descriptor double-move succeeds only with two
  user references, and transfers the descriptor to the receive owner.
- A full queue does not debit a remote MOVE_SEND right; FIFO remains
  correct through wraparound.
- Pending reply delivery blocks teardown atomically; after receipt, teardown
  revokes local and cross-task rights without recycling names.
- Exhaust all bounded receive-port slots, destroy/reallocate a port, and
  ensure a stale cross-task send name cannot access its replacement.
- 512 seeded malformed-wire submissions leave send refs and queue empty.

## Boundaries

All ports, tasks, queues, rights, and names are in a synthetic in-process
fixture. This is not an XNU implementation, does not launch SpringBoard,
and cannot communicate with iOS system services. Unsupported Mach
operations remain rejected; none is forwarded to Linux.
