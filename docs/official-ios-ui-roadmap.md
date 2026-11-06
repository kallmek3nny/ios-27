# Official iOS UI runtime target (NOT a theme)

## Non-negotiable goal

Run **Apple's actual SpringBoard, UIKit/SwiftUI/CoreAnimation-backed Liquid Glass interface,
Control Center, lock-screen system UI, and iOS apps** above Android on the S25+.
No android imitation launcher. Do not conflate a similar-looking Android UI with Apple code.

Android retains its own Linux kernel, verified boot, hardware drivers, normal phone
services and emergency recovery path. Running Apple's real closed-source system UI
is a **research objective** whose feasibility is not established.

## Native execution vs compatibility

Qualcomm Snapdragon and modern iPhones share ARM64 instruction sets, so generic
AArch64 instructions can execute on CPU hardware directly. An `arm64e` Mach-O
additionally depends on Apple's pointer-authenticated ABI, which is a separate
compatibility obstacle. Native CPU execution is **not** native iOS framework, Mach
kernel, dyld or Apple graphics compatibility.

## Dependency order

| Area | Runtime contract | Actual implementation state |
| --- | --- | --- |
| Mach-O | validate, select native ARM64 slice, read loads/imports | thin parser + FAT/FAT64; SDK-free compiler-linked ARM64 `MH_EXECUTE` fixtures, multisegment W^X mapping, bounded `LC_MAIN` argc/argv/envp/apple startup, and transactional cross-object relocation; **no general Darwin process ABI or Apple app compatibility** |
| dyld | link Apple's private/system binaries with fixups, chained binds, closures, @rpath semantics | **partial fixture-only**: classic/chained binds, bounded explicit image DAG, cross-image relocation, W^X native mapping; no real Apple dyld/shared cache |
| Darwin / XNU | syscall, threads, pthread workqueues, Mach ports/messages, entitlement-facing APIs | native SDK-free Mach-O fixture calls into a bounded virtual file/memory service table under exec supervision; synthetic task-local Mach queues and register-selector facade; **no Darwin SVC translation or real XNU services** |
| ObjC / Swift | metadata, libobjc, autorelease, Swift ABI, Foundation | **not implemented** |
| UIKit / SwiftUI | render genuine controls, view tree and Liquid Glass using Apple's own framework components | **not implemented** |
| Graphics | translate CoreAnimation, IOSurface and Metal operations into Android-compatible Vulkan/GPU surfaces | **not implemented** |
| SystemUI | Apple's SpringBoard, backboardd, launchd, FrontBoard, SpringBoardServices, security and app lifecycle | **not implemented** |
| Apps | launch signed or legally acquired app executables and their linked services without CPU emulation | **not implemented** |
| Security | preserve Android SELinux/verified boot, isolate guest resources, protect host files and phone state | trusted-fixture fork and fresh-address-space exec runners, guarded stacks, sealed exec payloads, mandatory seccomp, wall-clock supervision and kernel CPU bounds; the original workload passed in the Android host on a 4 KiB S25+; **not** an untrusted-code sandbox or general Android compatibility |

## Concrete next technical milestone

The repository has progressed beyond the original Phase 13 object-call proof.
The [phase19a startup workload](phase19a-startup.md) is a real SDK-free,
compiler-produced linked executable with recursion, read-only data, writable
globals, BSS and explicit Darwin C startup vectors. It can run under
[fork supervision](contained-native-process.md) or the
[fresh-address-space exec loader](exec-guest-loader.md) on ARM64 Linux.

[PR #126](https://github.com/kallmek3nny/ios-27/pull/126) adds a configurable
exec CLI, explicit guest startup vectors and real kernel CPU-expiry tests,
including guest termination while the launcher is stopped. It also connects
a compiler-produced Mach-O client to the original bounded userspace service
model through a separate [native service helper](native-service-bridge.md).
The client makes real native callback calls for virtual files, checked
memory, mapping protections and errno under the mandatory syscall filter.
Both LLVM and the unchanged narrow source-linker outputs are tested. Its native ARM64
and x86_64 CI runs exercise the compiler-produced workload and clearly labeled
synthetic fault cases. This work is under review; consult the PR's exact head
and checks before treating it as merged.

The explicit research callback-table boundary and a
[named static service shim](named-service-shim.md) are exercised together with
resource ownership, failure reporting and teardown. A separately compiled
four-argument client imports fourteen Darwin-named functions, including bounded
malloc/calloc/realloc/free for compiled runtime allocation; LLVM resolves them
against an original shim, and the loader binds only a validated private data
record while clearing x4. An object-symbol allowlist and native callsite tests
make that boundary auditable. General dynamic libSystem/dylib binding, more
POSIX behavior and thread-local errno remain future boundaries.
General Darwin startup, TLS, dyld/shared-cache loading, arm64e, graphics and
official Apple services remain separate blockers. The
[Android runtime host](android-runtime-host.md) now packages the original
Mach-O workload into an APK; two fresh launches passed on the physical S25+
with 4096-byte pages. This is native fixture execution evidence, not Apple
framework compatibility or a general isolation guarantee.

### Longer-term audit task

Use `ios27 audit-ios <file>` on a **legitimately obtained**, unencrypted
ARM64 Mach-O to list the direct dylib imports and identify actual constraints.
The command **does not execute** code, launch SpringBoard, read privileged
partitions, decrypt code, or grant root.

Then build a test-only userland `dyld` subset that resolves dependencies
from an explicit isolated directory of non-Apple test libraries, followed by a
Mach message simulation over userspace sockets, and native AArch64 sample
programs exercising those boundaries. Do not claim official iOS UI until
real official binaries visibly work in an Android graphics container.

## Samsung / DirtyFrag safety constraints

- Never alter bootloader, `boot`, `init_boot`, `vendor_boot`, `vbmeta`,
  `dtbo`, recovery images, partition layouts, secure boot or antirollback.
- Never use Odin/Download Mode or interfere with firmware's D2 lock-down.
- No required kernel code, SELinux policy changes, root, exploit, or autorun.
- If optional root is considered in the future, it must be an opt-in,
  separate and reviewed feature with read-only access by default.
- A reboot ends DirtyFrag's ephemeral root, but **cannot undo any persistent
  changes made while root was active**.
- No Apple firmware or proprietary framework files are distributed here.

## How to measure progress

Measure milestones by actual **tested capability**, not fake completion
percentages or added placeholder class names. An importer parsing UIKit is
not evidence UIKit loads. Successful execution of a synthetic Mach-O is not
evidence iOS runs. A rendered imitation of Liquid Glass is not Apple UI.
