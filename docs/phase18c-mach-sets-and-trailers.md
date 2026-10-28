# phase 18c: bounded mach port sets and receive trailers

This slice advances the in-process, **synthetic** Darwin compatibility boundary. It does not issue XNU traps, authenticate real processes, run SpringBoard, or claim support for proprietary iOS frameworks.

## Implemented
- Task-local, never-recycled port-set names. A set can contain multiple receive rights held by its owner, but membership does not create or transfer any right.
- Deterministic round-robin selection across active member queues. Invalid membership, foreign-task access, exhausted set slots, and stale names fail closed.
- Port destruction detaches membership. Successful task teardown retires all owned port sets after existing pending-right and dead-name capacity preflights; a failed teardown leaves them intact.
- Optional `mach_msg_seqno_trailer_t` layout (12 bytes: format 0, trailer size, port-local sequence number) after the message's ordinary wire bytes. `msgh_size` excludes the trailer. A successful receive increments the port's sequence; a poll/too-small receive does not.
- Explicit guest arena and Darwin *fixture* selectors for set receive, trailer receive, and bounded OOL receive. Capacity includes trailer bytes. The result slot must be disjoint from the receive span; failure never performs a partial guest write.

## Bounded OOL memory-descriptor subset
LP64 OOL descriptors use the XNU-compatible 16-byte wire layout. A single memory OOL descriptor, possibly mixed with up to three port descriptors, may snapshot at most 128 bytes from the sender's explicit guest arena. A separate OOL-aware receive path appends the payload to an explicitly allocated receive span and rewrites its 64-bit descriptor address to the receiver's guest-arena offset. Failed or short receives preserve the queue and rights. The receiver cannot silently interpret a queued OOL message via the ordinary receive API.

## Intentional limits
This does not implement kernel VM allocations, deallocate=true, OOL ports, multiple OOL memory regions, authenticated audit trailers, or full XNU Mach IPC semantics. Unknown descriptor formats and vouchers still fail closed; guest offsets never become raw host pointers. The optional trailer does not manufacture kernel security/audit token data or real task credentials. The optional trailer does not manufacture kernel security/audit token data or real task credentials. All scheduling is poll-only and synchronous, not kernel-backed.

## Coverage
`tests/test_mach_v2.c` exercises round-robin ordering, too-small buffer retry, membership changes, stale names, resource exhaustion, dead-name delivery and task teardown. `tests/test_mach_guest.c` checks result-slot isolation and trailer memory bounds; `tests/test_darwin_mach_abi.c` exercises set and trailer dispatch with task-local authorization.

Use `timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON`, `timeout 300s cmake --build build -j2`, and `timeout 180s ctest --test-dir build --output-on-failure`. Existing `-Werror` remains enabled.
