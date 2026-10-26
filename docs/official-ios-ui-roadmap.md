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
| Mach-O | validate, select native ARM64 slice, read loads/imports | thin parser + FAT/FAT64; genuine SDK-free Clang ARM64 Darwin MH_OBJECT validation, real external BRANCH26 relocation and trusted native ARM64 cross-object call returning 43; **no linked Darwin executable** |
| dyld | link Apple's private/system binaries with fixups, chained binds, closures, @rpath semantics | **partial fixture-only**: classic/chained binds, bounded explicit image DAG, cross-image relocation, W^X native mapping; no real Apple dyld/shared cache |
| Darwin / XNU | syscall, threads, pthread workqueues, Mach ports/messages, entitlement-facing APIs | deny-by-default probe plus **synthetic task-local Mach message queues, checked guest arena, transactional task teardown and register-selector facade**; no real XNU traps/services |
| ObjC / Swift | metadata, libobjc, autorelease, Swift ABI, Foundation | **not implemented** |
| UIKit / SwiftUI | render genuine controls, view tree and Liquid Glass using Apple's own framework components | **not implemented** |
| Graphics | translate CoreAnimation, IOSurface and Metal operations into Android-compatible Vulkan/GPU surfaces | **not implemented** |
| SystemUI | Apple's SpringBoard, backboardd, launchd, FrontBoard, SpringBoardServices, security and app lifecycle | **not implemented** |
| Apps | launch signed or legally acquired app executables and their linked services without CPU emulation | **not implemented** |
| Security | preserve Android SELinux/verified boot, isolate guest resources, protect host files and phone state | synthetic private anonymous W^X mappings and rollback only; **not** a sandbox |

## Concrete next technical milestone

**Phase 13 is verified:** [PR #22](https://github.com/kallmek3nny/ios-27/pull/22) validates a genuine Clang-emitted ARM64 Darwin BRANCH26 relocation between two independently compiled C objects and natively executes the patched function returning 43 with W^X on ARM64 Linux. This is **not a linked Mach-O process, iOS application, Apple framework or official UI**. The available ld64.lld rejects macOS platform linking. **Phase 14:** establish a reproducible open-source ARM64 Darwin linker or rigorously extend bounded object relocation handling; investigate XNU, arm64e, signing, dyld caches and graphics blockers.

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
