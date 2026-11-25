# Guarded guest stacks: host-side validation

Updated: 2026-11-15. This supplements the earlier
[local verification](crew-verification-2026-11-15.md); it is not an ARM64
execution, real 16 KiB hardware, or Apple framework compatibility result.

## Change

`src/runner.c` now exposes its existing stack preparation and release code
through `src/guest_stack_internal.h`, an internal Linux/Android interface.
The native runner uses the same implementation that the host tests exercise.
No public execution API, entry trampoline, syscall filter, or device policy
was changed. Native execution remains restricted to ARM64 Linux/Android.

The internal page-size parameter accepts zero for the real host page, or a
power of two from 4 to 64 KiB that is a whole multiple of the host page. A
smaller or malformed granule is rejected before mapping. The stack retains
its existing 1 MiB default, 64 KiB minimum, 8 MiB maximum request, startup
vector/string limits, and 16-byte-aligned initial stack pointer.

The earlier implementation rounded lengths to the selected page size but
relied on `mmap` to align the reservation's address. That only guarantees
**host** page alignment. A test-only wrapper forced a 4 KiB-aligned reservation
that was not aligned to the requested 8 KiB granule; the uncorrected helper
failed the stack-base assertion deterministically.

The corrected reservation includes two selected-page guards plus at most
`selected_page - host_page` bytes of alignment slack. The usable region and
both guard boundaries are aligned to the selected granule. Only the usable
region becomes RW, never executable; guards and slack remain inaccessible.
Release and setup rollback unmap the entire original reservation.

The production runner already passed its **measured host page** to this
helper. The reproduced defect concerns coarser injected granules, not a
demonstrated failure on a phone or a real 16 KiB kernel.

## Deterministic regression coverage

`tests/test_guest_stack.c` / CTest `guest-stack-page-size` checks:

- Host-page selection and all host-compatible 4/8/16/32/64 KiB granules,
  including deliberately misaligned reservation addresses.
- Every real page's permissions through `/proc/self/maps`, including guards
  and alignment slack; no deliberate faults or guest instructions.
- Default, minimum, rounded and maximum stack requests; 16-byte SP alignment
  and at least 16 KiB of remaining space below the copied vectors.
- Empty and maximum-size vectors, private string copies, repeated source
  pointers, combined payload boundaries and independent simultaneous stacks.
- Invalid/null inputs, oversized requests, unterminated strings, malformed
  page sizes, cleared failure outputs and bounded diagnostics. Invalid input
  must not allocate or alter mapping state.
- Injected allocation/protection failures, successful recovery, idempotent
  release, and `mincore` checks that every reserved real page was unmapped.

The linker wrappers exist **only in this test executable**. Production builds
still call ordinary `mmap`, `mprotect` and `munmap`.

## Local results

Verified from the uncommitted checkout at HEAD `a387567`, on WSL x86_64 with
4096-byte pages, Clang 21.1.8 and GCC 15.2.0. Existing strict warnings stayed
enabled. Every build/test invocation had a hard timeout.

| Configuration | Passed | Skipped | Failed |
| --- | ---: | ---: | ---: |
| Clang Debug, full CTest | 44 | 6 | 0 |
| GCC Debug, full CTest | 44 | 6 | 0 |
| Clang ASan + UBSan, full CTest | 42 | 8 | 0 |
| New stack suite, ASan + UBSan with leak detection | 1 | 0 | 0 |

Each full run contained 50 CTest entries. Common skips are the opt-in default
CPU-clamp test and native ARM64-only suites. ASan additionally skips the
existing fork/filter-incompatible supervisor and service-callback coverage;
the supervisor still runs its exec-loader checks before returning skip.
Full ASan used `detect_leaks=0`; only the separate new stack-suite run used
`detect_leaks=1`. Mixed inspection/native suites can pass their host checks
without executing any ARM64 code.

The Android CMake runner and loader targets also compiled and linked for
**arm64-v8a and x86_64**, using NDK 29.0.13846066, CMake 3.22.1 and the app's
declared minimum API 30. This was a native-target build only, not a fresh APK
build, installation or device run. An initial manual API-28 attempt was below
the app's supported minimum and failed on the existing API-30 `memfd_create`
dependency; no source or platform requirement was weakened to accommodate it.
Two initial GCC configuration attempts exceeded a 120-second command bound;
a bounded retry completed, followed by the successful full build and tests.

Reports, the pre-fix assertion, source hashes and local Android build outputs
are under ignored `out/guest-stack-2026-11-15/`. No commits, pushes, device
operations or Apple-binary execution were performed for this change.

## Reproduce on Linux

```sh
timeout 180s cmake -S . -B out/guest-stack-clang -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 180s cmake --build out/guest-stack-clang --target test_guest_stack --parallel 2
timeout 40s ctest --test-dir out/guest-stack-clang \
  -R '^guest-stack-page-size$' --timeout 30 --output-on-failure
```

For the sanitizer run, use a separate build directory with
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`, then run the same
test with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`.

Still unverified: actual 16 KiB hardware, native ARM64 execution of this
revision, guest-arena page-size readiness, frozen-head CI and merged delivery.
SpringBoard, UIKit and Liquid Glass are not made functional by this change.
