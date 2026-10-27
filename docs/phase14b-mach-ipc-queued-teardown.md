# phase 14b: transactional mach ipc port retirement

## scope and contract

this phase remains a **single-process synthetic mach fixture**. it does not implement xnu, kernel mach ports, bootstrap services, dead-name notifications, real springboard ipc, or linux syscall forwarding. it changes only the isolated `mach_v2` implementation and tests.

## bug and invariant

originally, `ios27_mach_v2_destroy` and `ios27_mach_v2_task_teardown` rejected *all* outstanding pending credits, including send and send-once credits reserved by messages sitting **inside the receive queue being destroyed**. this could make an otherwise valid task or port impossible to retire. a queued message may also carry both a port descriptor and a reply send-once transfer, potentially to the same object.

retirement now precomputes the exact per-object credits represented in queues that will be discarded. the implementation then verifies **before mutating anything** that every pending local credit affected by the retirement has an owned queued message to cancel it. a pending grant in a *different live queue*, including another task's queue, still prevents retirement. once the preflight passes, the existing queue cancellation and rights revocation commits without further failure paths.

## regression coverage

- discard self-targeted descriptor and make-send-once reply credits while destroying the receiving port
- tear down several same-task ports when one owns pending descriptor and reply transfers into another
- fail task teardown and port destruction atomically while an inbound move-send transfer remains queued in a foreign live port
- receive the foreign transfer and then safely tear down the owning task, revoking cross-task aliases
- exhaust 32 synthetic port slots, retire and reallocate, reject stale names and clean up all remaining ports
- submit 512 deterministic malformed messages and assert that neither the sender's rights nor the receiver's queue changes

## limitations

- does not add full mach dead-name semantics, notifications, port sets, vouchers, trailers, real task authentication, blocking timeouts, or ipc to apple services
- queue cancellation here destroys, rather than forwards, in-flight transferred rights as part of the synthetic port retirement
- all tests are fixture-level and must be run as existing `test_mach_v2` target under strict warnings, then confirmed on x86_64 and arm64 ci

## file ownership

`src/mach_v2.c`, `tests/test_mach_v2.c`, and this uniquely named document only. no other agent-owned file or build workflow changed.
