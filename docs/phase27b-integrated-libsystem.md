# phase 27b: compiler-linked darwin libc and bounded BSD services

## source integration and ABI

this PR is stacked on #126, which supplies the supervised native arm64 LC_MAIN
execution, a 16-byte versioned service-binding slot, and a tested named BSD
shim. it reuses the original #109 freestanding libc source and 840,000-op host
test without replacing them. #110's generation-tagged descriptor model is a
valuable separate implementation but is NOT yet attached to the #126 service
bridge. no claim of generation-safe descriptors is made here.

the service API v1, slot layout, regular four entry arguments and #126
linker/build contract remain unchanged. a separate audited multi-object
linker driver compiles ordinary libc and BSD symbol names into an SDK-free
arm64 Darwin executable with no unresolved imports.

## supported fixture subset

- phase26a allocator: 16-byte aligned, 128 KiB live quota, max 256
  allocations and 256 KiB static arena. strict checked heap-pointer
  memory/string routines; format subset %s, %d, %u, %x and %%, max 4 KiB.
- original named open, read, write, close, lseek, getpid, mmap, mprotect,
  munmap, __error from #126, plus new openat (AT_FDCWD only), pread,
  pwrite, fstat (144-byte Darwin inode64), fcntl (F_GETFL only),
  gettimeofday (16-byte timeval only).
- bounded qsort/bsearch with trusted comparator pointers, max 256
  elements and 4096 total bytes. 16 atexit registrations run LIFO
  through a fixture-explicit finalizer. normal LC_MAIN return does not
  automatically trigger atexit yet.
- virtual path namespace restricted to /tmp and /app; 8 files,
  16 descriptors, 4096 bytes per virtual file. backend uses private
  per-helper state and never delegates to host filesystem calls.
- 64-KiB service arena and 32-KiB bounded virtual mappings. executable
  mappings unsupported. invalid page access during named shim calls,
  negative/overflow offsets, and unsupported selectors fail closed.
- BSD failures write Darwin error numbers through guest __error; success
  preserves it. phase26a libc internal last_error is separate and is not
  synchronized with __error, pending a later ABI integration.

## verification and caveats

CMake adds phase27b-bounded-libc, phase27b-bounded-bsd, and
phase27b-compiler-linked-file-io. x86_64 parses the compiler-linked
Mach-O and manifest without executing ARM64 code. ARM64 Linux should
run the trusted fixture via ios27-services-loader and return 42 after
allocation, formatting, sorting, environment calls, virtual file creation,
write/read/pread/pwrite verification, fstat, and cleanup.

run:
    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON
    timeout 300s cmake --build build --parallel 2
    timeout 180s ctest --test-dir build --output-on-failure

real native arm64:
    ./build/ios27 run-exec ./build/ios27-services-loader ./build/phase27b-executable --unsafe

IMPORTANT: no native arbitrary Apple dylib loading, syscall interception,
unrestricted POSIX libc, generation-safe descriptors, concurrent errno,
TLS, or raw guest pointer safety is proved. trusted C literals and stack
buffers are accepted by the named shim and may fault the supervised
process if invalid. mapped-buffer permissions are checked at call
boundaries, not at every raw memory operation. atexit remains explicitly
finalized. this is not a complete Apple libSystem implementation.

shared progress dashboard is intentionally unchanged pending frozen-head
native CI and re-review of individual ABI targets.
