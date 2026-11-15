# Local crew verification — 2026-11-15

This is the historical verification snapshot before publication. Subsequent
delivery and exact-head CI are tracked in [PR #126](https://github.com/kallmek3nny/ios-27/pull/126).

## Result and provenance

Three scoped milestones are implemented and locally verified: image-page-size
mapping and loader-test stabilization, synthetic cache-audit hardening, and
Android smoke-mode validation. Shared CMake integration is fixed. No commits,
pushes, PR updates, device installs, or Apple-binary execution were performed.
Unrelated preexisting working-tree changes were preserved.

Starting/final local HEAD is `a387567` on `codex/cpu-limit-expiry`; the changes
remain uncommitted. A read-only GitHub query found main at
`1ac496566436dbdb0ef0d1b2c5d00dadf7af791e` and draft PR #126 at
`4c0f4cb716dc7aea546ffe845740c38383560840`. Older CI on that PR does not verify
this checkout. No result here counts as merged capability.

## Final verification

Host: WSL Ubuntu x86_64, **4096-byte pages**, kernel
`6.18.33.2-microsoft-standard-WSL2`; Clang 21.1.8, GCC 15.2.0, Python 3.14.4.
Existing `-Wall -Wextra -Werror -Wpedantic` warnings remain enabled. Builds and
tests were timeout-bounded and used private directories.

| Configuration | Passed | Skipped | Failed |
| --- | ---: | ---: | ---: |
| Clang Debug, full CTest | 43 | 6 | 0 |
| GCC Debug, full CTest | 43 | 6 | 0 |
| Clang UBSan, full CTest | 43 | 6 | 0 |
| Clang ASan + UBSan, full CTest | 41 | 8 | 0 |
| Opt-in default CPU-clamp expiry, separate run | 1 | 0 | 0 |

Each full configuration has **49 CTest entries**. The final CPU-clamp run
passed in **63.32 seconds**; an earlier run passed in 61.02 seconds. It tests
actual kernel expiry for the existing host callback, not native ARM64 code.
The tests-disabled Clang build also passed, as did census CLI checks using
only the project's compiler-produced fixture and invalid inputs.

The six common skips are the opt-in `contained-process-cpu-default-clamp`,
`contained-native-cpu-expiry`, `contained-native-cpu-default-clamp`, and three
`contained-native-services*` tests. The native tests require ARM64. Some mixed
inspection/native suites return success after host checks while omitting
native execution; their pass does not prove ARM64 execution.

ASan adds two partial/incompatible suites: `contained-service-callbacks` and
`contained-process-supervisor`. The latter **runs and passes its exec-loader
assertions before returning 77**, because the fork-runner portion conflicts
with sanitizer runtime syscalls under the mandatory filter. Neither skip is
counted as a full pass. Full ASan runs used `detect_leaks=0`; no whole-suite
leak-clean claim is made. The page-size regression separately passed with
`detect_leaks=1:halt_on_error=1`.

## Completed work

### M0 — build integration

The preexisting `test_libsystem21_adversarial` target failed to link because
it omitted its runtime. CMake now includes `src/libsystem21g_cond.c` and
`Threads::Threads`. Its existing cases test condition handles, not all
phase21c/phase21e functionality. The three new host suites are registered,
and Python validation tests have explicit CTest timeouts.

### M1 — page-size mapping and loader tests

The existing injected-page-size path validated a larger granule but still
mapped/protected memory at the real host granularity. The new regression
failed before the fix at the image-base alignment assertion. The loader now
aligns the image mapping inside a bounded reservation, rounds protection
lengths to the selected granule, and releases the entire reservation on
unload/rollback.

Synthetic nonexecuting graph tests cover host-compatible 4/8/16/32/64 KiB
selections, actual pointer binding/rebasing, per-real-page RX/RW/R/inaccessible
permissions, zero fill, gaps, input preservation, rejection/recovery and
unmapping. This does not complete all historical 16 KiB work or validate
real 16 KiB hardware, guest stacks, or guest arenas.

Test-only resource-limit injection now distinguishes CPU and stack limits,
so one refusal cannot mask the other. ASan's faked unlimited-stack rejection
uses a finite 32 MiB oversized stack; the real unlimited clamp control stays,
and plain/UBSan retain the actual unlimited rejection. This avoided the
reproduced intermittent pre-main ASan failure; the hypothesized shadow-layout
re-exec mechanism was source-reviewed, not traced. No production seccomp,
descriptor-closure or supervision policy was weakened.

An explicit bounded harness completed **20/20 ASan exec-loader matrices**,
each returning the documented partial-coverage skip 77 after assertions.
Detailed evidence is in ignored `out/crew-m1-2026-11-15/`.

### M2 — synthetic cache audit and fixup staging

The tools reject duplicate dyld metadata, malformed export-terminal extents
and child tables, overlapping staged VM segments, and missing/spoofed entry
binds. A literal pointer no longer counts as evidence that the expected
strong `_SBSystemAppMain` import was parsed and staged. Short inputs fail
with a bounded diagnostic instead of an uncaught `struct.error`. Valid
export-command precedence and immutable source bytes are retained.

**14/14 tests** passed under Windows and Linux Python. The lead independently
reproduced the pre-fix staging failures, reviewed the corrections and reran
the suite. Only original synthetic inputs were used.

### M3 — Android host modes

The activity checks the selected mode's expected result instead of the
normal-return message for every mode. The smoke tool accepts `success`
(default), `trap`, `deadline`, and `raw-syscall`, requires the matching result
and launcher exit, and cannot publish a passing report before a requested
screenshot is validated. Native mode predicates are shared with host tests.

- **15/15** mocked Python tests passed on Windows and Linux.
- **700** C outcome combinations and **7** invalid selectors passed, including
  strict/sanitized host builds; these tests execute no guest instructions.
- The initial isolated Windows snapshot passed Gradle 8.13 offline
  `:app:assembleDebug :app:lintDebug` in 40 seconds.
- A **second snapshot after the final native source freeze** passed the same
  tasks in **44 seconds**, with all **52 tasks executed**. Both ARM64 and
  x86_64 native libraries and Java compiled; the lead verified the APK exists,
  contains both architecture pairs, and the snapshot source hashes match.
- Lint: **0 errors, 5 warnings** (old target API, backup rules, missing icon,
  two hardcoded UI strings). SDK XML-version and Java/Gradle deprecation
  notices were also reported. Nothing was installed or launched on a device.

Independent peer reviews of M1, M2, M3 and the CMake correction are complete
with no actionable correctness findings. The final M1 review used the frozen
five-file source state; all review completions are recorded on the crew board.

## Reproduction artifacts and unsuccessful intermediate checks

Ignored `out/crew-2026-11-15/` contains `clang-final.xml`, `gcc-final.xml`,
`ubsan-final.xml`, `asan-final.xml`, `long-cpu.xml`, `page-size-leaks.xml`,
`cache-audit.xml`, and a 233-file `source-sha256.json` manifest. The lead and
an independent peer recomputed the source hashes with zero mismatches.
Final Android logs, lint reports, snapshot manifest and APK are under
`android-final-lucky-toad-ee-fmx24kyz/`; the earlier M3-only snapshot has its
own `android-lucky-toad-ee-2fh17lzl/` records.

Initial failures were not hidden: the missing condition-runtime link, the
page alignment regression and ASan loader-test failure were reproduced and
corrected. Two integration attempts caught partially applied concurrent edits
(cache parser and test-injector signature); final frozen-source reruns passed.
A redundant peer CMake compiler-identification attempt timed out after 120
seconds without building/running its extra target; completed integrated host
and Android verification supersede that attempt.

Initial WSL `/tmp` builds/reports disappeared between invocations. Final
builds use `/var/tmp/ios27-calm-wolf-b5*`, with reports on the persistent
checkout. Tool logs retain earlier checkpoint outcomes; no source was lost.

## Still unverified

- Actual ARM64 execution and phone smoke modes for these changes.
- A real 16 KiB kernel/device and full stack/arena page-size support.
- Frozen-head CI and merged-main delivery.
- Genuine Apple framework, SpringBoard, UIKit, Liquid Glass or iOS application
  execution. Synthetic parsing/staging and original fixtures do not establish
  any of these capabilities.
