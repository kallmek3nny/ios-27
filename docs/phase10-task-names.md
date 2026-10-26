# phase 10: task-local port names and single-use replies

the new `ios27_mach_v2` api adds a separate fixture-only mach-style
userspace message space. it does **not** replace the public xnu mach kernel,
run ios services, authenticate real processes, or establish guest isolation.

## implemented

- every simulated task has its own bounded, monotonically increasing local
  port-name table (64 slots each). different tasks can use the same numeric
  name for different underlying port objects. no names recycle within a
  namespace, even if the object slot is reused.
- receive-right creation and owner-checked deletion; explicitly granted
  per-task send aliases, checked send-right drop and count inspection.
- `MACH_MSG_TYPE_MOVE_SEND_ONCE` (18) for a one-use remote destination.
  `MACH_MSG_TYPE_MAKE_SEND_ONCE` (21) in the outgoing header's local
  disposition grants the server a **new task-local** reply name when the
  request is received. one-use rights are consumed on successful enqueue,
  not on rejected sends.
- 24-byte mach-like headers and one optional 12-byte copy/move send-port
  descriptor. the descriptor is translated from the sender's local name
  into the receiver's local name and reserves in-flight capacity; no implicit
  access to another task's numeric handles.
- preflight checks for queue capacity, per-task slot availability and send
  credits before transfer. pending rights are not usable before dequeue.
  short-buffer receives do not consume messages or rights.
- destroying a receive port cancels queued credits and invalidates its
  task-local references; active transfers *to that port* prevent teardown.
- strict tests for name collisions, stale handles, native request/reply
  flow, one-use consumption, copy/move descriptors, malformed wire input,
  quotas and transactional full-queue failure on x86_64 and arm64 linux.

## bounded guest memory adapter

`include/ios27/mach_guest.h` and `src/mach_guest.c` add a separate
experimental fixture-oriented C ABI boundary: `ios27_mach_guest_call`.
The caller supplies an explicit guest byte arena and a 64-bit offset/length,
not an arbitrary host pointer. Before any Mach-style send or receive the
adapter checks addition-free bounds, maximum message sizes, operation type
and aliasing between the result pointer and the guest arena. A private
256-byte staging buffer isolates the queue from guest bytes. Short receives
return the required length while leaving the queue and guest arena intact.

This is **not** an Apple Mach trap, Darwin syscall number, process
sandbox, Linux syscall translation, or proof that native iOS services run.

## non-support

this is a deliberately narrow independent `mach_v2` API alongside the
phase 9 `mach_ipc` compatibility API, which remains unchanged.
in-process numeric task identifiers do not authenticate real os callers.
not implemented: receive-right migration, port sets, real mach traps,
send-once descriptors, service discovery, ool buffers, kernel trailers,
vouchers, notification APIs, work queues, xnu address spaces or security.
timeouts are immediate poll-only; no blocking or scheduling guarantees.
host code execution remains unsandboxed and requires trusted test fixtures.

## safety

userspace-only in-memory tests. never edit the bootloader, firmware, boot
partitions, odin/d2 restrictions, persistent root/kernel/selinux, or trusted
device recovery. no apple binaries are bundled. this does not run springboard,
liquid glass or an ios app on a galaxy s25+.

## trusted linux tests

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
```

## further progress

next: a narrow, deny-by-default darwin c abi/trap bridge into the v2 message
space, send-once descriptor support, deeper per-task teardown tests, and real
redistributable darwin test inputs. genuine apple system ui feasibility remains
unknown, especially with arm64e/pac and closed graphics/runtime services.
