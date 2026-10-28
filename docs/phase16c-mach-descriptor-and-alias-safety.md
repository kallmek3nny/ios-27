# phase 16c: mach descriptor and host buffer safety

## scope

This increment hardens the bounded, **synthetic** Mach fixture used to test Darwin-facing userspace code. It does **not** implement real XNU traps, SpringBoard IPC, Apple entitlements, or iOS framework compatibility.

- After receiving a complex message with a port descriptor, the receiver sees `MOVE_SEND` in the descriptor disposition for its newly granted send uref, independent of whether the sender requested `COPY_SEND` or `MOVE_SEND`. Sender-side copy and move semantics remain unchanged.
- Reject raw host wire pointers that overlap the fixture's internal state before any message rights or queue are mutated.
- Reject raw receive destinations overlapping the fixture state or `inout_size` so an untrusted host caller cannot corrupt the queued message or right bookkeeping with aliases.
- Guest-facing ABI remains offset-based and bounded, preserving the original no-alias contract.

## deterministic regression coverage

`test_mach_v2` now covers malformed complex descriptor padding, a too-small receive retaining queued grants, normalized descriptors, send-once reply consumption, and deliberately overlapping raw host buffers without dequeuing.

`test_darwin_mach_abi` covers a complex descriptor copy through the synthetic AArch64 register ABI and guest arena. It checks sender rights, TOO_LARGE rollback, and the receiver's final port disposition.

Existing CMake targets and strict compiler flags are unchanged. Both platform CI jobs are required on the exact PR head before merging.

## limitations

This is a bounded single-process fixture with poll-only timeouts, no actual Darwin syscall dispatcher, no cross-process kernel namespaces, and no genuine Apple app execution. Advancing to official framework or SpringBoard compatibility still requires substantially more Darwin kernel APIs and userland integration.
