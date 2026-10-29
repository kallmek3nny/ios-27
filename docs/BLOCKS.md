# ios-27 | API/function block map and feasibility ceilings

![per-function compatibility block map](blocks.svg)

**how to read:** each block is one explicitly enumerated **sample target**, not every function exported by iOS. green = native ARM64 test fixture only; amber = parser, bounded synthetic Mach model or syscall interpreter probe; grey = not implemented; red = expressly excluded by userspace-only policy. No color means that a real Apple framework or application is runnable.

[Browsable per-target details](blocks.html) · [Machine-readable sample catalog](compat-targets.json) · [Broader milestones](PROGRESS.md) · [Audited ceilings](CEILINGS.md) · [Incoming agent work](BRANCHES.md)

The displayed percentages are **sample evidence coverage**: native fixtures plus synthetic probes divided by explicitly tracked targets, not real iOS framework compatibility.

## counted target samples

| layer | native fixtures | synthetic/probe | unimplemented | excluded | sampled targets |
| --- | ---: | ---: | ---: | ---: | ---: |
| **system libraries / api targets** | **1** | **22** | **226** | **0** | **249** |
| ↳ libsystem_kernel / bsd | 0 | 7 | 35 | 0 | 42 |
| ↳ libsystem_c / libc | 0 | 0 | 35 | 0 | 35 |
| ↳ libsystem_pthread | 0 | 0 | 21 | 0 | 21 |
| ↳ libdispatch / gdc | 0 | 0 | 20 | 0 | 20 |
| ↳ libdyld / dynamic loading | 0 | 0 | 12 | 0 | 12 |
| ↳ libobjc / objc runtime | 0 | 0 | 27 | 0 | 27 |
| ↳ Swift runtime / ABI | 0 | 0 | 16 | 0 | 16 |
| ↳ CoreFoundation | 0 | 0 | 20 | 0 | 20 |
| ↳ Foundation / classes | 0 | 0 | 16 | 0 | 16 |
| ↳ Security / keychain | 0 | 0 | 12 | 0 | 12 |
| ↳ CFNetwork / Network.framework | 0 | 0 | 12 | 0 | 12 |
| ↳ ObjC host prototype (not Apple libobjc) | 1 | 7 | 0 | 0 | 8 |
| ↳ libSystem guest model (not dylib) | 0 | 8 | 0 | 0 | 8 |
| **darwin runtime / native compatibility** | **5** | **41** | **43** | **2** | **91** |
| ↳ mach-o parser and native loader | 4 | 5 | 4 | 0 | 13 |
| ↳ dyld / image graph | 1 | 10 | 6 | 0 | 17 |
| ↳ mach ipc model | 0 | 16 | 4 | 0 | 20 |
| ↳ bsd syscall boundary | 0 | 7 | 10 | 0 | 17 |
| ↳ arm64e and abi compatibility | 0 | 0 | 10 | 0 | 10 |
| ↳ guest security / isolation | 0 | 3 | 9 | 2 | 14 |
| **apple ui / android integration** | **0** | **0** | **95** | **4** | **99** |
| ↳ UIKit / controls | 0 | 0 | 14 | 0 | 14 |
| ↳ SwiftUI / view graph | 0 | 0 | 13 | 0 | 13 |
| ↳ CoreAnimation / compositor | 0 | 0 | 13 | 0 | 13 |
| ↳ Metal / IOSurface / gpu | 0 | 0 | 15 | 0 | 15 |
| ↳ SpringBoard / apple services | 0 | 0 | 15 | 0 | 15 |
| ↳ iOS apps / app services | 0 | 0 | 12 | 0 | 12 |
| ↳ Android host integration | 0 | 0 | 13 | 0 | 13 |
| ↳ device boot and kernel ownership | 0 | 0 | 0 | 4 | 4 |

## merged main and incoming work

confirmed evidence is drawn only from merged main. the [live provenance snapshot](repo-status.json) records recent branches, which never count until merged and reviewed.

main source: `1dd112410e43`

- [PR #94](https://github.com/kallmek3nny/ios-27/pull/94) · incoming, not counted
- [PR #96](https://github.com/kallmek3nny/ios-27/pull/96) · incoming, not counted
- [PR #95](https://github.com/kallmek3nny/ios-27/pull/95) · incoming, not counted
- [PR #93](https://github.com/kallmek3nny/ios-27/pull/93) · incoming, not counted
- [PR #92](https://github.com/kallmek3nny/ios-27/pull/92) · incoming, not counted
- [PR #89](https://github.com/kallmek3nny/ios-27/pull/89) · incoming, not counted
- [PR #87](https://github.com/kallmek3nny/ios-27/pull/87) · incoming, not counted
- [PR #83](https://github.com/kallmek3nny/ios-27/pull/83) · incoming, not counted

## parsed branch diffs (provisional, not merged)

the purple dashed outlines identify components with new source or test changes on tracked agent branches. they do not increase verified compatibility percentages.

[Live branch diff inventory and CI evidence](BRANCHES.md) · 17 active candidate(s) with recognized code changes.

- **libsystem_pthread**: 6 unmerged candidate branch(es), not counted
- **mach-o parser and native loader**: 1 unmerged candidate branch(es), not counted
- **dyld / image graph**: 1 unmerged candidate branch(es), not counted
- **mach ipc model**: 1 unmerged candidate branch(es), not counted
- **bsd syscall boundary**: 1 unmerged candidate branch(es), not counted
- **guest security / isolation**: 1 unmerged candidate branch(es), not counted

## recently merged source changes awaiting audit

blue outlines mark merged source code that is not yet credited by a specific evidence-reviewed compatibility target. this can include passing CI; CI is not an Apple ABI conformance test.

latest scanned merged PRs: **19** · [source diffs](BRANCHES.md).

- **libsystem_pthread**: 8 recently merged code PR(s), compatibility review pending
- **mach-o parser and native loader**: 2 recently merged code PR(s), compatibility review pending
- **dyld / image graph**: 1 recently merged code PR(s), compatibility review pending
- **mach ipc model**: 1 recently merged code PR(s), compatibility review pending
- **ObjC host prototype (not Apple libobjc)**: 5 recently merged code PR(s), compatibility review pending
- **libSystem guest model (not dylib)**: 3 recently merged code PR(s), compatibility review pending

## realistic ceilings for every tracked area

[Why these ceilings are not numerical guarantees](CEILINGS.md).

### system libraries / api targets

- **libsystem_kernel / bsd** · **conditional** (needs major compatibility layer). selected xnu bsd calls can be translated, but guest ownership, cancellation, darwin errno, rights and file isolation need proof. only seven syscall probe selectors currently work in a tiny interpreter, not as linked libsystem exports.
- **libsystem_c / libc** · **plausible** (limited subset feasible). portable c functions could be implemented or safely forwarded after abi and guest address-space isolation; these are representative targets, not extracted iphone exports.
- **libsystem_pthread** · **conditional** (needs major compatibility layer). darwin thread startup, tls, signals, cancellation and workqueues require more than linux pthread wrappers.
- **libdispatch / gdc** · **conditional** (needs major compatibility layer). dispatch object ownership and queues may be portable, but apple qos, workqueue and runloop integration need dedicated tests.
- **libdyld / dynamic loading** · **conditional** (needs major compatibility layer). mapped native fixtures do not export a compatible libdyld ABI to unmodified apple programs.
- **libobjc / objc runtime** · **conditional** (needs major compatibility layer). open objc runtimes provide a possible research starting point; apple objc abi and arm64e dispatch are not validated here.
- **Swift runtime / ABI** · **conditional** (needs major compatibility layer). these are selected swift runtime contract families rather than exact mangled exports; true swift abi interoperability is absent.
- **CoreFoundation** · **unknown** (official Apple compatibility unproven). apple corefoundation binaries and exact cf ownership/event-loop behavior have not run; targets are representative public functions.
- **Foundation / classes** · **unknown** (official Apple compatibility unproven). selected class contracts, not every foundation method or real symbols. apple foundation loading has not been demonstrated.
- **Security / keychain** · **unknown** (official Apple compatibility unproven). keychain, signing, attestation and entitlements rely on an apple-specific trust model that the android host does not provide.
- **CFNetwork / Network.framework** · **conditional** (needs major compatibility layer). the network path may use android transports through an isolated proxy; authentic apple policy and entitlement behavior is unproven.
- **ObjC host prototype (not Apple libobjc)** · **conditional** (needs major compatibility layer). Standalone ObjC2 parser and trusted host-callback runtime, not Apple objc_msgSend ABI, guest IMP bindings, categories, arm64e or UIKit.
- **libSystem guest model (not dylib)** · **conditional** (needs major compatibility layer). Standalone bounded guest trap dispatcher. Not real linked libSystem API exports, Darwin kernel traps, or host filesystem integration.

### darwin runtime / native compatibility

- **mach-o parser and native loader** · **plausible** (limited subset feasible). merged phase18a demonstrates bounded LC_MAIN argv/envp and a guarded guest stack for reviewed SDK-free ARM64 fixtures. no general Darwin process, TLS initialization, syscall integration, fault containment, or sandbox exists.
- **dyld / image graph** · **conditional** (needs major compatibility layer). merged phase18b proves bounded native 3-dylib graphs and explicit guest-path @rpath resolution with no host filesystem fallback. only a dependency-first initializer plan exists; no automatic constructors, Apple dyld cache, or arm64e.
- **mach ipc model** · **conditional** (needs major compatibility layer). phase18c adds synthetic task-local port sets, bounded 128-byte single OOL descriptors and sequence trailers; all operations are synchronous fixture calls, not kernel Mach traps, audit tokens, full OOL VM or entitlement-aware processes.
- **bsd syscall boundary** · **conditional** (needs major compatibility layer). a narrow interpreter responds to six identity calls plus a virtual exit, not general darwin syscalls from native processes.
- **arm64e and abi compatibility** · **unknown** (official Apple compatibility unproven). native aarch64 is not proof of arm64e abi; authenticated pointer formats are deliberately rejected rather than bypassed.
- **guest security / isolation** · **conditional** (needs major compatibility layer). the current reviewed fixture runner executes in-process. boot chain writes and weakening selinux are explicitly excluded.

### apple ui / android integration

- **UIKit / controls** · **unknown** (official Apple compatibility unproven). official apple framework code, event dispatch and rendering are not working. all entries are target contracts, not sdk export enumeration.
- **SwiftUI / view graph** · **unknown** (official Apple compatibility unproven). genuine swiftui binaries and dynamic view graph machinery have not run.
- **CoreAnimation / compositor** · **unknown** (official Apple compatibility unproven). renderer compatibility requires apple semantics converted into isolated android-compatible surfaces.
- **Metal / IOSurface / gpu** · **unknown** (official Apple compatibility unproven). apple metal command and resource semantics are not a one-to-one vulkan translation; apple iosurface ownership is not present.
- **SpringBoard / apple services** · **unknown** (official Apple compatibility unproven). genuine apple private processes, entitlements and dependencies are untested on this phone.
- **iOS apps / app services** · **unknown** (official Apple compatibility unproven). legitimate ios binaries still depend on dyld, frameworks, entitlements and security. no unrestricted app execution is demonstrated.
- **Android host integration** · **conditional** (needs major compatibility layer). any bridge should remain voluntary, non-root and not alter android recovery or verified boot.
- **device boot and kernel ownership** · **excluded** (intentionally not in scope). full ios kernel or flash-based ios replacement is not a deliverable under the hard no-boot-chain-change constraints; excluded blocks are never counted as working.

## counting rules and barriers

- **not an exhaustive total:** the catalog is a selected, versioned set of functions and contracts. no complete apple sdk/export baseline was scanned, so do not call these percentages of system libraries translated.
- **no fake implementation:** a symbol stub, parser match, green unit test or constant return cannot count as real Apple ABI support. each amber/green group requires a real evidence path and registered test; promotion needs meaningful end-to-end proof.
- **native-fixture is NOT general iOS execution:** actual arm64 machine code can run with a narrow test-only ABI, but real SpringBoard, framework processes, full Darwin traps and Apple private services do not.
- **syscall distinction:** seven selector behaviors in the interpreter are counted as synthetic probes, not functioning libsystem calls callable by real apps.
- **ceiling does not mean a percentage:** plausible = limited userspace subset technically possible; conditional = major unbuilt compatibility components; unknown = no evidence of authentic Apple behavior; excluded = would violate established kernel/boot/device restrictions.
- **update:** edit docs/compat-targets.json and run a bounded command: timeout 15s node tools/compat_blocks.cjs. verify output with timeout 15s node tools/compat_blocks.cjs --check.
- **safety:** no bootloader or verified-boot changes, kernel replacement, persistent root, Apple proprietary redistribution, or running untrusted binaries in the in-process fixture runner.

## upstream target inventories and context

- [XNU BSD syscall master](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/syscalls.master)
- [Apple ObjC runtime source](https://github.com/apple-oss-distributions/objc4)
- [Swift runtime source](https://github.com/swiftlang/swift)
- [ios-27 architecture and limitations](../docs/official-ios-ui-roadmap.md)

snapshot date: 2026-10-28. no historical API counts or guaranteed numeric ceiling invented.

