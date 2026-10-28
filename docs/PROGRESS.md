# ios-27 | capability progress

snapshot: 2026-10-28 · baseline: `75d631ff507d`

![capability gate dashboard](progress.svg)

**counting rule:** these are individually declared, curated research milestones, **not percentages of full iOS compatibility**. A green gate means there is documented source/test-fixture evidence in this repository, not that Apple code works or that the Galaxy S25+ has been tested. The numerator can change only after evidence-backed review; adding code files alone never completes a gate.

| capability | documented gates | gate coverage |
| --- | ---: | ---: |
| mach-o and native arm64 | 5/7 | 71.4% |
| dyld and image loading | 6/9 | 66.7% |
| mach ipc and darwin calls | 5/8 | 62.5% |
| objective-c swift and system libraries | 3/7 | 42.9% |
| apple graphics and liquid glass | 0/4 | 0.0% |
| official system ui and apps | 0/4 | 0.0% |
| android deployment and isolation | 0/4 | 0.0% |

## realistic technical ceiling

- **demonstrated:** SDK-free, reviewed, linked ARM64 Darwin executable and dylib fixtures can be parsed, mapped and executed under narrow native ABI conditions; synthetic Mach IPC works in bounded in-process tests.
- **plausible research direction, not delivered:** a progressively wider compatibility environment for selected native Darwin software, requiring a real process boundary, more dyld and ABI support, trustworthy isolation and on-device validation.
- **unproven full target:** Apple's genuine iOS 27 SpringBoard, UIKit/SwiftUI, Liquid Glass, frameworks and apps over Android. No complete Apple system UI, Apple services, arm64e environment or graphics pipeline has been shown to run. No honest overall percentage or guaranteed upper limit can be inferred.

## milestone ledger

### mach-o and native arm64

- [x] bounded arm64 macho parsing ([evidence](../docs/architecture.md))
- [x] fat binaries and dylib inventory ([evidence](../docs/architecture.md))
- [x] compiler-linked mh_execute fixture ([evidence](../docs/phase16a-linked-native-execution.md))
- [x] multisegment pc-relative native execution ([evidence](../docs/phase17a-native-multisegment-execution.md))
- [ ] general darwin process startup and stack abi
- [ ] native device validation on 4k/16k hardware
- [x] transactional ARM64 multi-object relocations and native cross-object calls ([evidence](../docs/phase19d-relocation-linker.md))

### dyld and image loading

- [x] guest-virtual dyld install paths ([evidence](../docs/phase8-native-images.md))
- [x] bounded export trie parsing ([evidence](../docs/phase14c-dyld-symbols.md))
- [x] classic bind planning ([evidence](../docs/architecture.md))
- [x] limited unauthenticated chained fixups ([evidence](../docs/phase16b-chained-import-addend-validation.md))
- [x] linked cross-image dylib fixture ([evidence](../docs/phase17b-linked-multiimage.md))
- [ ] dyld shared cache and closures
- [ ] arm64e authenticated fixups and abi
- [ ] general real ios framework loading
- [x] special ordinal bindings and dependency finalization plan ([evidence](../src/images.c))

### mach ipc and darwin calls

- [x] task-local mach port rights and send-once ([evidence](../docs/phase16c-send-once-forwarding.md))
- [x] complex messages with bounded descriptors ([evidence](../docs/phase17c-mach-lifecycle.md))
- [x] synthetic dead-name observation and teardown ([evidence](../docs/phase17c-mach-lifecycle.md))
- [x] bounded OOL descriptor and receive sequence trailers ([evidence](../docs/phase18c-mach-sets-and-trailers.md))
- [ ] mach interface generator service parity
- [ ] real darwin syscall and libsystem compatibility
- [ ] concurrent service lifecycle under real processes
- [x] task-local threads, exception messages and bounded guest VM copy ([evidence](../docs/phase19c-mach-compat.md))

### objective-c swift and system libraries

- [ ] libobjc dispatch and runtime lifecycle
- [ ] swift runtime and metadata interoperability
- [ ] libsystem pthread workqueue and signals
- [ ] foundation and app bootstrap
- [x] standalone compiler-produced ObjC2 metadata parsing ([evidence](../docs/phase19e-architecture.md))
- [x] standalone host class registration, lookup, initialization and dispatch ([evidence](../docs/phase19e-architecture.md))
- [x] bounded host-only Darwin libSystem guest trap model ([evidence](../docs/phase19e-architecture.md))

### apple graphics and liquid glass

- [ ] metal to android gpu api integration
- [ ] iosurface sharing and display integration
- [ ] coreanimation compositor bridge
- [ ] real liquid glass framework rendering

### official system ui and apps

- [ ] launchd backboardd and frontboard services
- [ ] genuine springboard launch
- [ ] apple lock screen and control center
- [ ] genuine ios application lifecycle

### android deployment and isolation

- [ ] galaxy s25+ physical on-device test
- [ ] untrusted guest process isolation
- [ ] android input audio and permission integration
- [ ] stable system ui overlay without boot modification

## maintenance and verification

Edit `docs/progress.json` only when adding/removing a specifically defined gate or when new evidence establishes completion. Completed gates need a real repository evidence path; registered CTest names are checked where specified. Run `timeout 15s python3 tools/progress.py` to regenerate this page and the SVG. Run `timeout 15s python3 tools/progress.py --check` to reject stale results. A passing generator check does not execute native code; the existing x86_64 and ARM64 CTest workflows remain necessary.

The baseline starts on 2026-10-28; no earlier percentages have been invented. The status table is a declared-gate ledger, not an extrapolation of how long the final project will take.
