# ios-27 | audited technical ceilings and hard blockers

**audit baseline:** merged `main` at [`b3b66fef5a72ba`](https://github.com/kallmek3nny/ios-27/commit/b3b66fef5a72b3bcbecc71466d257b6eebae8797) (2026-10-28). The [live provenance file](repo-status.json) tracks newer main commits and recent unmerged branches. A new commit **does not** automatically prove a capability. Re-audit any changed component before modifying its status.

## what "ceiling" means

There is no defensible hard percentage limit for iOS 27 compatibility. A source-code inspection cannot determine the maximum future portion of SpringBoard or iOS apps that could run on Android. Instead, distinguish **demonstrated today**, **bounded userspace goal**, **requires major new subsystems**, **unverified feasibility**, and **excluded by project rules**.

| domain | demonstrated on merged main | realistic next supported boundary | core blocker |
| --- | --- | --- | --- |
| Mach-O / AArch64 | phase19a compiler-linked freestanding startup plus phase19d transactional cross-object relocation/execute fixtures | selected reviewed Darwin programs with bounded stack and object linking | full process lifecycle, tls, syscalls, untrusted sandbox |
| dyld / image loader | phase19b special symbol ordinals and dependency finalizer planning plus linked multi-dylib fixture | broader freestanding graphs with safe unload/constructor behavior | Apple shared cache, closures, arm64e bindings and lifecycle |
| Mach IPC | phase19c bounded task-local thread identity, fixture exception messages, memory copy/zero and serialized port rights | increasingly faithful guest-local Mach services | kernel XNU traps, authenticated audit tokens, real VM protections, MIG compatibility |
| Darwin BSD / libsystem | seven limited interpreter selector behaviors (six identity calls + virtual exit), **not libc exports** | isolate and translate selected syscall interfaces | filesystem policy, process/thread/syscall ABI, isolation and signals |
| C / pthread / dispatch | phase19e isolated guest read/write/close, fake identity and quota-tracked allocations, **not linked libSystem ABI exports** | isolated libc/POSIX subset | syscall and process ABI, TLS, workqueues, signals and ownership |
| ObjC / Swift | standalone Clang ObjC2 metadata parser, bounded host selectors/classes/messaging and one relocation-free native method, **not Apple's libobjc ABI** | integrate host prototypes and add verified guest-call ABI | objc_msgSend ABI, guest IMP fixups, ARC, categories, Swift, arm64e |
| CoreFoundation / Foundation / Security | **not implemented** | selected functions may be reproduced in isolated userland | Apple implementation/services, entitlement/keychain trust and ownership |
| UIKit / SwiftUI | **not implemented** | cannot establish a viable genuine-Apple-framework ceiling yet | private/public Apple framework dependencies, view graph and event routing |
| Metal / CoreAnimation / IOSurface | **not implemented** | potentially a separately designed Android graphics bridge | resource, command and synchronization semantics; compositor interoperability |
| SpringBoard / apps | **no genuine apps or official system UI running** | an auditable dependency graph and sandboxed app bootstrap proof | BackBoard/FrontBoard/launchd, Apple services, graphics, entitlements, arm64e |
| Android device | **no documented S25+ end-to-end compatibility run** | isolated, explicitly started Android userspace host with native ARM64 fixture tests | secure process boundary, storage, audio, graphics, input and permission mediation |
| native XNU / replacing Android | **not in project scope** | none without explicitly changing the goal | Android verified boot/firmware/kernel safety constraints |

## phase19 evidence audit

The latest merged phase19a/b/c/d work extends native compiler-produced ARM64 test execution and bounded Mach/dyld models. Phase19e's Objective-C and libSystem components are **standalone, original-source, host-controlled** foundations. They are **not linked into `ios27_core`**, and none of the standard `objc_msgSend`, `libsystem_kernel.dylib`, Foundation, Swift or Apple UI entrypoints are provided. The dedicated standalone harness must pass on x86_64 and ARM64 CI for the new experimental target groups.

**The workflow refresh only updates repository/branch provenance and regenerates images from manually reviewed target catalogs. It cannot automatically determine feature correctness or count newly created code.**

## high-impact blockers to investigate next

1. **Native process isolation first.** The phase18a guest stack still executes reviewed code in-process. Do not execute arbitrary downloaded Mach-O files with it. A genuine guest-virtual memory space, syscall isolation and process teardown are prerequisites for widening execution.
2. **Darwin ABI is not Linux ABI.** Sharing an AArch64 instruction set does not make syscalls, pthreads, Mach ports, task ports, workqueues, dyld, or entitlement services compatible. A seven-selector interpreter is not a linked `libsystem_kernel.dylib`.
3. **arm64e pointer authentication is a separate architecture/ABI obstacle.** Apple's authenticated dyld cache contains signed pointers and different fixup formats. Avoid claiming this can be solved just by a Snapdragon ARM64 CPU, and fail closed on unsupported forms.
4. **Graphics and private services have unknown feasibility.** Translating CoreAnimation/Metal/IOSurface or running original SpringBoard is a different undertaking from rendering an Android lookalike. No genuine Apple service tree is demonstrated.
5. **App and firmware availability are separate constraints.** No unencrypted legitimate Apple framework inventory has been used to establish full dependency coverage, and the project does not grant itself entitlements, code signing, decryption capability, XNU services or boot-chain access.

## evidence and upstream references

- Merged project docs: [native multi-segment fixture](phase17a-native-multisegment-execution.md), [bounded LC_MAIN stack and argv](phase18a-darwin-process-startup.md), [linked dylib graph](phase17b-linked-multiimage.md), [phase18b explicit guest-path graphs](phase18b-linked-graph.md), [synthetic Mach lifecycle](phase17c-mach-lifecycle.md), [phase18c port sets and trailers](phase18c-mach-sets-and-trailers.md), [official UI target](official-ios-ui-roadmap.md).
- Apple XNU Mach message interface: https://github.com/apple/darwin-xnu/blob/main/osfmk/mach/message.h
- Apple dyld shared-cache layout, including `AUTH` and `AUTH_CONST` segments: https://github.com/apple-oss-distributions/dyld/blob/main/doc/CacheLayout.md
- Apple chained-fixup structures and arm64e variants: https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/fixup-chains.h
- Apple pointer-authentication ABI documentation: https://developer.apple.com/documentation/security/preparing-your-app-to-work-with-pointer-authentication

**counting rule:** per-function colored squares are a *curated sample* of API and runtime contracts, not an exhaustive export inventory or a percentage of Apple's real system. Green = supported native fixture under a restricted ABI. Amber = synthetic parser/model/probe. Neither says the Apple library or UI is working. Unmerged phase work is listed in [live provenance](repo-status.json) but never counted as confirmed.

**revision policy:** future agents must compare actual merged main code and test behavior to these claims, add real fixture/test evidence before marking a target complete, and keep missing or ambiguous areas marked unproven.
