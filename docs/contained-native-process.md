# Contained native process execution

`ios27_run_executable_contained` runs a reviewed, freestanding ARM64 Mach-O
fixture in a Linux child process. A fault, forbidden syscall, or infinite loop
can be reported to the launcher without terminating it. This is groundwork
for runtime development with trusted fixtures; it does not provide Darwin/XNU
services or run Apple frameworks, SpringBoard, UIKit, or iOS applications.

## Command and API

On ARM64 Linux, build the project and run its original SDK-free linked fixture:

```sh
./build/ios27 run-contained ./build/phase15a-executable --unsafe
# contained ARM64 entry returned 43
```

The explicit `--unsafe` flag is required. The command uses an empty startup
context and a fixed 1000 ms deadline. Its host exit status is 0 after a normal
guest return (the full guest C `int` is printed), 124 after a deadline,
`128 + signal` after a terminating signal, 1 for setup/preflight failure, and
2 for incorrect command usage.

The public API is declared in `include/ios27/process.h`:

```c
ios27_process_options options = {.timeout_ms = 1000};
ios27_process_result result;
int status = ios27_run_executable_contained(
    bytes, size, &info, &startup, &options, &result, error, sizeof(error));
```

The caller supplies `argc`, `argv`, `envp`, and `apple` through the existing
`ios27_startup_context`. No host environment is implicitly forwarded. Strings
and vectors are copied into a guarded private guest stack before execution.
`timeout_ms` accepts 1 through 60000 and includes child setup time.

API status 0 means `result.outcome` is `IOS27_PROCESS_RETURNED`,
`IOS27_PROCESS_SIGNALED`, or `IOS27_PROCESS_TIMED_OUT`. A returned guest C `int`
is preserved in `exit_code`; it is not truncated to an eight-bit wait status.
Status -1 means preflight, host setup, unsupported host, cleanup, or report
failure, with a cleared result and diagnostic text when a buffer is supplied.

## Process boundary

The launcher validates the Mach-O and startup arguments, prepares the guarded
stack, and seals the private image mappings with their final W^X protections
before `fork`. The original in-process APIs remain available.

Before guest entry, the child resets catchable signal dispositions and clears
its signal mask, disables core dumps, requests termination if its parent dies,
closes every inherited descriptor except a private report pipe, sets
`PR_SET_NO_NEW_PRIVS`, and installs a mandatory seccomp filter. The launcher
retains its own signal dispositions, signal mask, descriptors, and filter state.

The filter checks the native syscall architecture. It permits only native
`exit`, `exit_group`, `rt_sigreturn`, and a 12-byte `write` to the report pipe
(checking both halves of the descriptor and count arguments). Other syscalls,
including general file/network I/O, allocation/protection changes, and process
creation, terminate the child with `SIGSYS`. Darwin syscall ABI translation
is not provided; the filter checks native Linux syscall numbers, not Darwin's
selector or the `SVC` immediate. The guest cannot use ordinary host libc
services that require syscalls outside the allowlist.

A readiness packet confirms that containment setup completed before the guest
callback is entered. Setup errors, including a signal before readiness, fail
the API. A signal after readiness is a signaled outcome, including the small
interval before the first guest instruction. The launcher checks the bounded
report protocol after reaping the child. A raw exit without a return report
is a report failure. A monotonic deadline covers setup and execution; the
launcher kills and reaps the child when it expires.

## Host requirements and limits

Native Mach-O entry requires ARM64 Linux. Linux x86_64 can run the supervisor
tests and inspect the fixtures, but the native entry API rejects execution.
Mandatory kernel features include `close_range`, seccomp filters, and
`PR_SET_NO_NEW_PRIVS`; setup fails closed if unavailable. Android kernel and
libc support are not verified by Linux CI. No Galaxy S25+ device test is
claimed.

Use a single-threaded launcher with default `SIGCHLD` disposition, without
`SA_NOCLDWAIT` or a competing child reaper. Guest threads and host callbacks
are outside this API's contract.

This is trusted-fixture failure containment, not a sandbox for downloaded or
untrusted Mach-O code. `fork` inherits host memory, shared mappings, and the
host address-space ABI. The report channel is also visible in that address
space; its packets are not authenticated evidence about an untrusted guest.
Only execute original, deliberately constructed, reviewed fixtures.

`ios27_run_executable_exec` avoids inheriting launcher memory by executing
the trusted `ios27-loader` helper in a fresh address space. It keeps the same
outcomes and the same trusted-fixture limit; see
[exec guest loader](exec-guest-loader.md).

## Verification scope

CTest registers `contained-process-supervisor` and `contained-native-macho`.
The host supervisor tests cover full integer returns, private child memory,
forbidden syscalls, faults, deadlines under interrupted waits, signal-state
reset, setup refusal, descriptor closure, result clearing, and child reaping.

The Mach-O test uses the unmodified compiler-linked phase15a and phase19a
fixtures. Native ARM64 checks exercise the real return values, recursive stack
use, `argv`/`envp`/`apple` copies, and fresh globals across repeated launches.
Clearly labeled synthetic instruction injections exercise a trap, a loop,
a forbidden Linux syscall, and recovery afterward. Those injected cases are
fault tests, not additional compiler-produced program capabilities.

Both new tests run in the existing x86_64 and native ARM64 CI matrix. Passing
fixture tests do not satisfy the audited general Darwin process-startup or
untrusted-guest-isolation capability gates.
