# Phase 20e: bounded Darwin libSystem userspace services

## Scope and architecture

This is an **original-source research service boundary**, not a working
Apple libSystem or iOS app loader. It adds independent `ios27_20e_*` C
entrypoints without touching the shared runtime, CMake, HANDOFF, bootloader,
root, or files owned by parallel agents.

- **ABI / errors:** explicit 64-bit *guest offsets* (never trusted host
  pointers), Darwin BSD syscall numbers for the narrow allowlist of
  read(3), write(4), open(5), close(6), getpid(20), getuid(24),
  geteuid(25), getegid(43), getgid(47), munmap(73), mprotect(74),
  gettimeofday(116), mmap(197), lseek(199). Unsupported numbers return
  `-1` and Darwin `ENOSYS=78`; guest errno lives in each context rather
  than the host TLS. Successful calls leave the previous errno alone.
  Invalid narrow integer conversions are rejected before dispatch.
- **Filesystem / FDs:** 16 guest-local slots, with 0/1/2 bound only to
  in-memory stdin/stdout/stderr; 8 synthetic files, 4096 bytes each;
  explicit seeding of `/app/*` and `/tmp/*` files; `/tmp/*` creation only;
  bounded reads/writes, seek and append, deterministic end-of-file,
  descriptor exhaustion and rollback. No host `open`, `read`, or `write`
  paths, no symlinks, network, directory traversal or mounting.
- **Memory:** 65536 guest bytes with offset zero reserved, 32 tracked
  zero-initialized malloc/calloc/realloc allocations (16-byte aligned),
  checked free, reusable allocations, and eight distinct 4-KiB virtual
  mapping pages. Anonymous/private mappings alone are supported; map
  operations and protection transitions verify all pages before commit;
  executable pages and file-backed mappings are denied. There is **no**
  host executable mmap, no host page protection changes, and no CPU
  emulation. Direct access to the public context struct is trusted host
  test code; guest accesses must use the checked accessor.
- **Environment / clock:** eight explicit in-context environment pairs,
  bounded guest-key lookup, deterministic guest IDs and synthetic timeval
  with deliberate virtual-time advancement. No host environment, realtime
  clock, or process identity is exposed. No real waiting or blocking.

## Independent tests

```sh
timeout 180s python3 tools/phase20e-standalone.py
timeout 180s env PHASE20E_SANITIZE=1 python3 tools/phase20e-standalone.py
timeout 180s env CC=gcc python3 tools/phase20e-standalone.py
```

The test script runs a **single independently authored, compiler-produced
client integration program** against the C service implementation on the
available native Linux architecture at `-O0` and `-O2`, with optional ASan
and UBSan. The same source is separately compiled with
`clang --target=arm64-apple-darwin` into a genuine SDK-free 64-bit ARM64
Darwin Mach-O object, whose header, CPU type, file type, and external call
symbols are validated. It contains actual calls to the phase20e services,
not hand-written object bytes. This does **not** execute the Darwin object on
Linux or implement a runtime relocation/binding bridge.

Tests cover compiled call chains, real FD lifecycle, virtual file content,
failed opens and closed FDs, `EMFILE`, errno preservation and failure,
readonly file isolation, blocked path traversal, guest pointer overflow,
allocation exhaustion/reuse (5000 deterministic iterations), zeroing,
realloc preservation, mmap allocation and exhaustion, page protection,
atomic failed unmaps, virtual time, and environment buffers.

The repository's standard PR CI runs its existing x86_64/ARM64 CMake tests,
**but it does not include phase20e**, because integrating new sources into
CMake or workflows would violate concurrent file ownership. The standalone
suite is the evidence for phase20e until a separate integration owner adds it.

## Known compatibility gaps

No exported Darwin standard `malloc`, `open`, `__error`, or `syscall` symbols;
no dyld fixups, POSIX threads/TLS errno, host OS syscall passthrough, real
Darwin VM, signals, sockets, filesystem metadata, symlinks, `stat`, `fcntl`,
`fork`, locale, stdio FILE streams, dynamic linker, Objective-C bridging,
UIKit, or actual iOS applications. The struct model and errno slot are not
thread-safe. Darwin structs, pointer authentication, and host endianness
must be audited for a real ARM64 ABI bridge. No claims of iOS framework or
SpringBoard execution follow from these tests.