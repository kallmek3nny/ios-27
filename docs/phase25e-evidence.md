# phase 25e: linked Objective-C application integration evidence

## exact source boundary

Baseline main at branch creation: `a6cc52fbf54edc2e00fdfae8a31205a53049e9d6`.
Phase24a-e PRs #97–#101 are **unmerged drafts** at the baseline.
Phase25a-d are owned by separate agents, not imported or cherry-picked.
Only `tests/phase25e-*`, `tools/phase25e-*`, `docs/phase25e-*`
and the dedicated phase25e GitHub workflow were modified.
There is **no** accidental dependency on any unmerged runtime ABI.

## four engineering milestones

1. **Actual compiler-linked fixtures:** a freestanding Objective-C 2.0
   source is compiled independently into six real ARM64 Darwin
   `MH_DYLIB` images (root, derived, worker, category, ledger, pipeline)
   and three independently linked `MH_EXECUTE` images (app_basic,
   app_protocol, app_lifecycle). Clang produces the method instruction
   bodies and metadata; `ld64.lld` links the images. Minimal .tbd files
   are link-time symbol declarations, **not runtime shims**.
   On older lld, the build tool removes just LC_BUILD_VERSION from copies
   of the compiler-produced objects; original objects are retained.
2. **Cross-image integration contracts:** the verifier checks ARM64
   Mach-O headers, segment and section bounds, W^X protections,
   LC_MAIN executable entry range, bounded symbols, import names,
   dependency-first graph order, and compiler-emitted ObjC metadata.
   Root +load, +initialize, inherited method lookup, super dispatch,
   protocol conformance, category replacement, shared ledger state,
   and explicit retain/release are expressed in genuine guest source.
3. **Negative testing and isolation contracts:** bounded corruption
   cases (wrong CPU, truncation, malformed load commands, bad LC_MAIN,
   ObjC section overflow), missing library, invalid startup vectors,
   and explicit unverified runtime ABI gates. Multiple independent
   host-side metadata catalogs are parsed concurrently and compared.
   This does **not** amount to isolated Darwin application instances.
4. **Structured evidence and performance:** produce SHA-256 for every
   image, actual dependency ordering and unresolved symbols, a
   provenance-labeled execution trace, x86_64/ARM64 host inspection
   timing, peak **host** RSS, null guest startup/dispatch/teardown
   metrics, zero guest resource accounting, and a full failure matrix.
   A 4-job PR CI matrix runs unchanged merged-core tests plus the
   independent fixture verifier on x86_64 and Linux ARM64, normal and
   ASan/UBSan baseline C builds, maintaining -Werror.

## exact instruction execution policy

The nine linked Mach-O files are real, but **none are started by this
phase25e adapter**. Building, linking, disassembling, inspecting,
parsing, host callbacks, and counting guest-source loop iterations
are NOT Darwin instruction execution.

The guest source contains 50,000 loop iterations per application,
with four intended ObjC message sends and three explicit ownership
operations each. That is **at least 200,000 planned sends and 150,000
planned transitions per app**, subject to a genuine runtime bridge
actually executing the native application and independently verifying
all results, including a zero exit status. These are *fixture goals*,
not achieved runtime measurements.

Current independently verified totals:

| signal | native verified |
| --- | ---: |
| application starts | 0 |
| compiler-produced Darwin Objective-C sends | 0 |
| compiler-produced Darwin Objective-C method entries | 0 |
| objective-c object ownership transitions | 0 |
| +load/+initialize callbacks executed | 0 |
| traced guest ARM64 instructions | 0 |
| mapped guest memory still live | 0 |

**No host-only counters are substituted.**

## exact integration blockers

- `dyld`: no verified app-level process mapper/relocator linking
  dependent objective-c dylibs on main, including lazy and metadata
  fixups; an LC_LOAD_DYLIB is only metadata here.
- `objc metadata`: no verified main-branch global ObjC2 registration
  API across independently mapped compiler-linked images.
- `symbol binding`: unresolved Objective-C runtime imports such as
  objc_msgSend, objc_msgSendSuper2, objc_alloc, objc_retain,
  objc_release must resolve to a validated guest-compatible ABI.
- `message dispatch`: no verified main-branch class/metaclass and
  lexical super dispatch boundary supporting these signatures.
- `ownership`: no verified main-branch native retain/release/weak
  integration with this application's compiler-generated code.
- `lifecycle`: no verified main-branch +load/+initialize ordering
  and method/image lifetime pinning with actual constructors.
- `startup`: no verified combined Darwin process LC_MAIN, argc/argv,
  env, apple strings and stack ABI with Objective-C runtime services.
- `process isolation`: host-side independent parsed catalogs are not
  independent guest address spaces or process instances.
- `failure injection`: malformed metadata and missing dependencies
  are tested. Unresolved selectors, unsupported method signatures,
  premature teardown, partial guest startup and concurrent runtime
  cancellation remain **blocked, not simulated successes**.
- `performance`: inspection metrics are measurable; native startup,
  dispatch throughput, guest heap and teardown measurements remain null.

Never execute untrusted Mach-O through a native bridge; this work is
not a sandbox, an Apple framework replacement, iOS itself, or a device
boot-chain modification.

## bounded reproduction

```sh
timeout 300s python3 tools/phase25e-build.py --output build/phase25e --rounds 100
PHASE25E_RESULTS=build/phase25e timeout 180s python3 tests/phase25e-integration.py -v
timeout 120s cmake -S . -B build/phase25e-core -DIOS27_BUILD_TESTS=ON -DCMAKE_C_FLAGS="-Wall -Wextra -Wpedantic -Werror"
timeout 300s cmake --build build/phase25e-core --parallel 2
timeout 180s ctest --test-dir build/phase25e-core --output-on-failure --timeout 60
```

All compiler/linker subprocesses use explicit 30-second limits.
Git is capped at 30s, configure at 120s, build at 300s, tests at
180s and GitHub commands at 60s where a shell is used. CI jobs have
a 15-minute absolute cap. The standalone Python parser uses bounded
data structures and deterministic bounded repetitions.

## engineering-time and PR release policy

This report must NOT claim three genuine hours unless independently
recorded as active development; timestamps, waits, and CI time are not
engineering effort. The actual shorter active session is not padded.

Keep the PR in **draft**. Merge only if all native integration
requirements are satisfied, every frozen-head required CI check is
green, `main` has been re-read and compared against its expected SHA
lease, and the actual capability/measurement claims have been updated
from verified native evidence. No PR may be merged to satisfy a
percentage or manufactured operation count.
