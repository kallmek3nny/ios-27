# phase 16c: transactional send-once forwarding

This patch extends the **synthetic in-process** Darwin/Mach compatibility
fixture. It does not invoke real XNU traps, launch SpringBoard, or load Apple
frameworks. No bootloader, firmware, root, or kernel changes are involved.

## wire semantics

The local header disposition may now be `MOVE_SEND_ONCE (18)` as well as
`MAKE_SEND_ONCE (21)`. The former forwards one existing task-local send-once
capability, while the latter mints a send-once capability from a receive right.
Both arrive as a locally named send-once remote right on receive, using Mach
header disposition 18. The sender cannot reuse a forwarded capability.

Enqueue preflights queue capacity, destination right, reply right, receiver
alias/name capacity, pending-credit limit, descriptor disposition, and combined
debits. When the remote send-once destination is also the locally transferred
send-once name, **two** send-once urefs are required. Failed sends leave rights,
queues and guest buffers untouched.

## deterministic coverage

`tests/test_mach_v2.c` exercises a three-task request/reply relay with a
forwarded once right, queue-full retries, voucher rejection, double-debit
rejection, stale rights, and final message delivery. This continues to use
bounded task IDs, queues and message sizes. Timeouts remain deterministic
poll-only behavior; it does not implement Mach OOL descriptors, vouchers,
port sets, kernel IPC, or live iOS application compatibility.

## limits / next work

Expand descriptor disposition coverage and strict guest ABI validation
with independent memory-boundary tests before wiring any externally
supplied program bytes to this compatibility surface.
