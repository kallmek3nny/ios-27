# ios-27 | audited technical ceilings and hard blockers

**audit baseline:** merged `main` at [`b3b66fef5a72ba`](https://github.com/kallmek3nny/ios-27/commit/b3b66fef5a72b3bcbecc71466d257b6eebae8797) (2026-10-28). The [live provenance file](repo-status.json) tracks newer main commits and recent unmerged branches. A new commit **does not** automatically prove a capability. Re-audit any changed component before modifying its status.

## what "ceiling" means

There is no defensible hard percentage limit for iOS 27 compatibility. A source-code inspection cannot determine the maximum future portion of SpringBoard or iOS apps that could run on Android. Instead, distinguish **demonstrated today**, **bounded userspace goal**, **requires major new subsystems**, **unverified feasibility**, and **excluded by project rules**.

| domain | demonstrated on merged main | realistic next supported boundary | core blocker |
| --- | --- | --- | --- |
| Mach-O / AArch64 | compiler-produced sdk-free ARM64 test executables, multiple segments, and bounded phase18a LC_MAIN argv/envp plus guarded native guest stack | reviewed userland Darwin programs with proper stack, process and TLS ABI | no full Darwin process lifecycle, TLS or syscall support; no untrusted-code sandbox |
| dyld / image loader | bounded phase18b three-dylib native graph, guest-path @rpath resolution and staged fixups, verified with original-source fixtures | broader freestanding multi-image graphs with correct lifecycle | real Apple shared cache, process initializers/closures, arm64e authenticated fixups, TLS |
| Mach IPC | bounded synchronous task-local port rights, send-once, phase18c port sets, single small OOL buffers and sequence trailers | more complete guest-local IPC, ports, MIG behavior | real XNU traps, audit-trailer identity, full OOL VM, concurrency and entitlement-aware services |
| Darwin BSD / libsystem | seven limited interpreter selector behaviors (six identity calls + virtual exit), **not libc exports** | isolate and translate selected syscall interfaces | filesystem policy, process/thread/syscall ABI, isolation and signals |
| C / pthread / dispatch | **no linked Apple-compatible libsystem API surface** | subsets of portable C and POSIX-like operations | ABI and ownership, darwin TLS, workqueues, dispatch object behavior |
| ObjC / Swift | **not implemented** | research compatible public-runtime subset | libobjc dispatch, Swift metadata, ARC, concurrency and arm64e |
| CoreFoundation / Foundation / Security | **not implemented** | selected functions may be reproduced in isolated userland | Apple implementation/services, entitlement/keychain trust and ownership |
| UIKit / SwiftUI | **not implemented** | cannot establish a viable genuine-Apple-framework ceiling yet | private/public Apple framework dependencies, view graph and event routing |
| Metal / CoreAnimation / IOSurface | **not implemented** | potentially a separately designed Android graphics bridge | resource, command and synchronization semantics; compositor interoperability |
| SpringBoard / apps | **no genuine apps or official system UI running** | an auditable dependency graph and sandboxed app bootstrap proof | BackBoard/FrontBoard/launchd, Apple services, graphics, entitlements, arm64e |
| Android device | **no documented S25+ end-to-end compatibility run** | isolated, explicitly started Android userspace host with native ARM64 fixture tests | secure process boundary, storage, audio, graphics, input and permission mediation |
| native XNU / replacing Android | **not in project scope** | none without explicitly changing the goal | Android verified boot/firmware/kernel safety constraints |

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
