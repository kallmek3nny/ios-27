# phase 22b: isolated capability-oriented darwin bsd adapter

## scope and actual compatibility

This is original-source **in-process synthetic userspace work**. It is not Apple's libSystem or a live Darwin ABI bridge. It calls the existing `ios27_20e_guest_ptr` to validate every guest range, including per-page protections. The backing arena belongs to the caller. No host filesystem/network operations, host-process IDs, real clock reads, raw guest-to-libc pointers, Mach IPC or arbitrary guest binaries are permitted.

Main evidence reviewed at start: `e8124a374e04114c218028d82ce300ab70d9631e`; shared files are not edited. This independent target compiles with `src/libsystem20e_services.c` and does not change CMake. Native tests compile host-source programs; the Darwin fixture is a compiler-produced *Mach-O object* but is **not executable on Linux** or proof that Apple libSystem imports resolve.

## four implementation increments

1. `IOS27_22B_BSD | number` dispatch with fail-closed class checking, carry/error values, context-local Darwin errno, identity numbers, and a bounded per-context synthetic thread-id table. Successful calls preserve errno.
2. Preopened in-memory `/cap/<name>` resources, exact-name resolution, 16 guest descriptors, read/write rights, append, seek, stdio, metadata host API, and transactional `read`, `write`, `readv`, `writev`. The iovec array is an ARM64 little-endian 16-byte pair `(guest offset, length)`; it is never interpreted as a host pointer. Every slice is checked before modifying file contents or descriptor offset. No create, directory lookup, symlinks, socket, or host path operations.
3. Deterministic injectable nanosecond realtime and monotonic clocks. `gettimeofday(116)` is supported only when timezone and mach-absolute-time outputs are absent. `clock_gettime` and `nanosleep` are **PRIVATE adapter selectors** (`IOS27_22B_PRIVATE | 1/2`), not XNU kernel syscalls; raw BSD 232/240 return `ENOSYS`. A virtual sleep advances clocks without blocking, unless interrupted. Invalid remaining/timeval pointers and clock overflow fail before advancing time.
4. Pthread-serialized context state with stress clients, repeat teardown after all clients join, 100k+ deterministic syscall/descriptor operations, sanitizer build, and genuine SDK-free ARM64 Darwin Clang MH_OBJECT compilation of a dispatcher caller.

## supported guest operations (bounded synthetic simulation)

- BSD `read(3)`, `write(4)`, `open(5)` (preopened `/cap/*` only), `close(6)`, `getpid(20)`, `getuid(24)`, `geteuid(25)`, `getegid(43)`, `getgid(47)`, `gettimeofday(116)` (restricted), `readv(120)`, `writev(121)`, `lseek(199)`, `thread_selfid(372)` (synthetic context-local ID).
- Private adapter selectors `clock_gettime(1)` and `virtual_nanosleep(2)` in private class `0x7f000000`. Explicitly *not Darwin BSD syscall numbers*. `IOS27_22B_CLOCK_MONOTONIC=6`, `REALTIME=0` scoped to private clock service.
- Metadata is available via `ios27_22b_metadata_fd`, not through Darwin `fstat` or `stat64` ABI, which is currently unimplemented.
- Max 8 preopened resources, 16 FDs including stdio, max 4096 bytes per resource/IO transaction, 16 iovec entries, 64 recorded unique thread IDs.

## tests and limits

```sh
timeout 300s python3 tools/phase22b-standalone.py
PHASE22B_SANITIZE=1 timeout 300s python3 tools/phase22b-standalone.py
```

The standalone script has hard 120-second subprocess ceilings; the dedicated CI workflow runs on native x86_64 and ARM64 runners with sanitizer and optimized variants. The parent repository's default CMake suite does not include this module due to parallel ownership boundaries. Mainline checks therefore do not prove phase22b until the independent workflow runs on the PR.

Not supported: actual Apple libc and pthread symbol interposition, Darwin `stat`/`fstat64` wire layouts, SDK-linked executables, unbounded filesystem traversal, real kernel timeouts or signals, network, Mach traps, memory ownership transfers, or concurrently destroying the context while a call is in progress. Thread-id allocation is bounded and does not detect pthread handle reuse after a thread exits. Caller must join all worker threads before teardown, and all outside guest-arena writes must be serialized with adapter calls; `ios27_20e_ctx` itself is not internally synchronized. Public APIs are a research model rather than a security sandbox.

## work record

Engineering started on 2026-10-28 at approximately 20:09 America/New_York. Actual elapsed work is the git/CI record and test timestamps, **not** a claimed 3-hour session. Do not mark three hours complete without three genuinely elapsed and documented hours of active work.