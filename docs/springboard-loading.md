# SpringBoard loading checkpoint — not UI startup

**2026-11-15:** the real iOS 27.0 Simulator SpringBoard dependency graph can
now be staged and loaded into private **non-executable** memory on a 64-bit
Linux host. `_SBSystemAppMain`'s bound address is read back successfully.
**SpringBoard has not been entered and no iOS frame has been rendered.**

This is a prerequisite for a runtime, not an imitation launcher or a claim
that UIKit works. The existing Android APK still runs its original fixture;
this change does not add a SpringBoard launch button or install anything.

## Verified real-input result

Host: WSL x86_64, 4096-byte host pages; selected mapping granule 16384 bytes.
Inputs: existing external `C:/Users/Kentl/Downloads/ios27-runtime-inputs`,
Simulator 27.0 / build 24A434. No new download was needed.

| Check | Result |
| --- | ---: |
| Primary files checked against pinned sizes and SHA-256 | 4 |
| Reachable images in the dependency graph | 1,533 |
| Standalone images staged and mapped (app plus 67 dylibs) | 68 |
| Standalone pointer fixups staged | 505,706 |
| Unresolved standalone images under explicit load-order policy | 0 |
| Shared-cache mappings | 11 |
| Cache bytes mapped | 3,821,830,144 |
| Standalone file bytes copied and read back | 89,032,704 |
| Declared zero-fill bytes initialized | 331,082 |
| Executable pages created | **0** |
| Reservations released after verification | **All** |

The four pinned files are the app, SpringBoard.framework and the two cache
parts. The additional extracted dylibs are structurally validated and hashed
while staging; they do not each have a separately pinned upstream digest.
Apple files and keys remain outside the repository and APK.

**A later inspection found a concrete host-runtime blocker:** the cache's
kernel, pthread and platform host libraries contain zero-filled placeholder
code. Their export names can resolve while the implementations are absent.
The table's zero deferrals and staged fixup count therefore do not indicate
runtime readiness. The tools now expose these targets as
`host_placeholder` findings. See [Simulator host imports](simulator-host-imports.md).

The optional [platform-memory bridge](platform-memory-bridge.md) retargets 109
standalone bindings to original implementations and adds validated v4 cache
pointer override planning. It does not provide the remaining Apple runtime or
graphics services.

## Implementation and limits

`tools/springboard-load.py` reuses the existing cache reader, dependency
resolver and chained-fixup stager. It additionally validates segment/section
geometry, file/VM correspondence, page-rounded overlap, zero-fill bounds,
fixup/zero-fill overlap, mapping quotas and the exact input profile.

The native backend uses `MAP_PRIVATE | MAP_FIXED_NOREPLACE`. An occupied
address is an error, never permission to replace another mapping. The cache
is mapped read-only at its preferred addresses. Standalone reservations
start inaccessible, become RW only for copying and fixups, then become
read-only. Holes remain inaccessible. Final permissions are checked against
`/proc/self/maps`; partial failure and normal completion release reservations.
There is deliberately no executable protection or Apple-call interface.

The selected cache has no compressed slide records. This checkpoint refuses
cache slide metadata it cannot apply, rather than pretending another cache
can be relocated. It does not implement shared-cache override patch tables,
dynamic loading, authenticated pointers, TLS or runtime initialization.

Inputs must be trusted and quiescent. This is not an untrusted-file sandbox
or a defense against concurrent replacement/truncation of input files.
The parser validates metadata, but that does not establish safe execution.

### The two previously deferred images

The prior stager required a unique process-wide weak-symbol candidate. It
therefore deferred MapsSuggestions and SystemUISecureFlipBookUtilities,
which import the C++ `piecewise_construct` object with 25 possible definitions.

The default **remains unique-only**. The explicit `--weak-policy load-order`
option applies first-definition/strong-over-weak selection to the recorded
dependency traversal order. It follows the shallow weak-definition selection
in Apple's open-source
[dyld Loader.cpp, lines 3001–3031](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/Loader.cpp#L3001-L3031).
Reexport-only and resolver exports do not masquerade as direct definitions.

The resulting provider and candidate count are included in the report. This
is an **explicit research load order**, not proof that it reproduces Apple's
complete dyld startup order or cache coalescing. In particular, shared-cache
weak-override patching remains necessary before executing the graph.

## Run

On Windows, validate and plan only:

```powershell
python tools/springboard-load.py `
  --input-root C:/Users/Kentl/Downloads/ios27-runtime-inputs `
  --weak-policy load-order
```

On native 64-bit Linux/WSL, actually map, verify, then release the data:

```sh
timeout 600s python3 tools/springboard-load.py \
  --input-root /mnt/c/Users/Kentl/Downloads/ios27-runtime-inputs \
  --weak-policy load-order --map --verbose
```

`--runtime-root` can specify another extraction location for the standalone
dylibs. Otherwise the tool uses the existing simulator extraction beneath
`springboard-closure-inputs`. No network request is made. A successful exit
means the **non-executable checkpoint** passed; `ready_to_execute`,
`springboard_entered` and `rendered_ios_frame` remain false.

`--verbose` writes bounded stage progress to stderr, not into the JSON report.
Cold reads across a Windows/WSL mount can dominate verification of the large
cache files. Two repeat attempts reached their 240- and 180-second bounds
during input verification, before mapping. A 600-second mapped-buffer retry
reached the second cache file but also timed out before mapping. The final
Windows planning run completed with full size/SHA checks in 96.8 seconds.
The mapping counts above are from the earlier successful implementation;
final-source native materialization is **not reverified** after the additional
zero-fill and file checks. The verifier feeds read-only mapped buffers directly
to SHA-256; it does not skip or trust cached hashes.

## Tests and evidence

`tests/test_springboard_load.py` uses original synthetic bytes only. Its 22
cases cover unique/weak/strong selection, missing symbols, pinned input
checks, segment and section bounds, zero-fill, failure rollback, address
collisions, readback, refusal of executable protections and cleanup.
All 22 passed on Linux; Windows passed 17 and skipped the 5 Linux-native cases.
The existing 14 cache-audit tests also passed. The full Clang Debug CTest run
has 51 entries: **45 passed, 6 existing host/opt-in skips, 0 failed**.

Ignored `out/springboard-load-2026-11-15/` holds real planning/mapping reports,
test results and source hashes. No Apple bytes are included in those reports.
No Apple instructions, device installs or boot/system changes were made for
this checkpoint. Prior isolated leaf-function probe results are not UI proof.

## What still blocks the requested UI

1. Replace the observed Simulator host-library placeholders with compatible
   runtime implementations; complete dyld/cache fixups and startup integration.
2. libSystem/Simulator host ABI, thread-local storage and Darwin/Mach services.
3. Objective-C/Swift registration and framework initializers.
4. BackBoard/FrontBoard services and a CoreAnimation/IOSurface/Metal-to-Android
   presentation and input bridge.

These are runtime implementations, not flags to bypass. Running the current
entry point without them would not establish a working SpringBoard. Actual
Apple execution also remains subject to the unresolved project input-use
conditions in [the licensing inventory](apple-input-licensing-27.0.md).

The next C startup building block is implemented in the
[native initializer runner](native-initializers.md). Its original compiled
constructor/`ProgramVars` workload now passes on the physical S25+. It does
not remove the Apple runtime or graphics blockers, and is not SpringBoard.
