# Feasibility review: Apple's real SpringBoard path (iOS 27.0)

**Status:** evidence review for HANDOFF.md phase 15, step 5. It is not a
compatibility estimate, a schedule or a launch claim. This project has not
executed any Apple code, and no Apple binary is committed or packaged.

## Scope

The target is the actual SpringBoard from Apple's iOS 27.0 (build 24A434)
ARM64 Simulator runtime, acquired and inventoried as described in
[Apple runtime inputs](apple-runtime-inputs.md). This review covers seven areas
on the path from that executable to visible Apple UI on the Android host:

1. arm64e and pointer authentication (PAC)
2. dyld, the dyld shared cache and launch closures
3. XNU/Mach and libSystem services
4. Objective-C and Swift runtimes
5. Code signing and entitlements
6. Metal, CoreAnimation and IOSurface
7. Licensing

## Method and rules

Each area answers four questions:

- **Needs:** what the real SpringBoard path requires, from public primary
  sources (Apple developer documentation, Apple open-source XNU and dyld,
  LLVM) or from this repository's sanitized measurements.
- **Proven today:** what this repository demonstrates, with the file and the
  commit that introduced it.
- **Blocker:** the concrete missing capability.
- **Next test:** one measurable check that would move the area forward.

Rules for every section:

- Unproven stays unproven. Parsing an import is not loading a library, and
  running an original synthetic fixture is not executing an Apple framework.
- No percentages or completion estimates.
- Apple binaries appear only as sanitized names, hashes and counts. They are
  never run, copied or quoted.
- Existing design documents are linked rather than restated:
  [official UI roadmap](official-ios-ui-roadmap.md),
  [phase 19e Objective-C and libSystem foundations](phase19e-architecture.md),
  [phase 22d Objective-C runtime substrate](phase22d-objc-runtime.md),
  [named service shim](named-service-shim.md) and
  [static pointer rebases](static-pointer-rebases.md).

## Measured starting point

These facts come from [Apple runtime inputs](apple-runtime-inputs.md) and the
[sanitized inventory](apple-runtime-inventory-27.0.json). Dependency counts
include weak, reexport and upward imports, so they are not counts of
mandatory startup services.

| Component | Measurement |
| --- | --- |
| SpringBoard.app/SpringBoard | 133,872-byte unencrypted generic ARM64 executable; one undefined symbol, `_SBSystemAppMain`; four direct libraries (SpringBoard.framework, Foundation, libobjc, libSystem) |
| SpringBoard.framework/SpringBoard | 36,539,312-byte generic ARM64 dylib; 228 direct dependencies (39 public frameworks, 179 private frameworks, 10 other libraries) |
| UIKitCore, Foundation, libobjc, libSystem | 103, 31, 5 and 34 declared direct dependencies |
| Shared-cache family | 1,826 listed dylib images in two main files totaling 3,829,317,632 bytes |
| Platform | iOS Simulator (Mach-O platform 7), not iPhone device firmware |

## 1. arm64e and pointer authentication (PAC)

**Needs.** Apple describes arm64e as an architecture that adds pointer
authentication codes (PACs). A PAC is a signature kept in otherwise unused high pointer
bits and checked when the pointer is used
([Apple: preparing your app to work with pointer authentication](https://developer.apple.com/documentation/security/preparing-your-app-to-work-with-pointer-authentication)).
On supported Apple chips, Apple's system software and built-in apps use PAC to
protect return addresses and function pointers
([Apple Platform Security: operating system integrity](https://support.apple.com/guide/security/operating-system-integrity-sec8b776536b/web)).

The ABI has five keys (IA, IB, DA, DB, GA). By default it signs return
addresses (IB, salted with the stack pointer) and C function pointers (IA,
discriminator 0). It also signs C++ vtable pointers and entries, plus
Objective-C `isa`, `super`, method and class-metadata pointers. The table and metadata pointers
generally combine address diversity with a 16-bit constant discriminator.
Ordinary C data pointers are not signed by default
([Clang pointer authentication](https://clang.llvm.org/docs/PointerAuthentication.html)).
The loader signs pointers stored in global data from relocation metadata. In
Mach-O, that metadata is the arm64e chained-fixup family in Apple's
[fixup-chains.h](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/fixup-chains.h).
It covers pointer formats 1 (`ARM64E`), 7 (`ARM64E_KERNEL`), 9 (`ARM64E_USERLAND`),
10 (`ARM64E_FIRMWARE`), 12 (`ARM64E_USERLAND24`), 13 (`ARM64E_SHARED_CACHE`,
A keys only) and 14 (`ARM64E_SEGMENTED`). The authenticated rebase and bind
layouts carry a 2-bit `key`, a 1-bit `addrDiv` flag and a 16-bit `diversity`.

The measured inputs narrow the question. SpringBoard.app/SpringBoard and
SpringBoard.framework/SpringBoard are generic ARM64 (subtype ALL), not arm64e.
They are Simulator binaries, and Apple notes that pointer authentication
can't be tested in Simulator. This review has not checked the architecture of
the shared-cache files or the chained pointer formats inside any Apple image.

**Proven today.** The repository only detects and refuses arm64e. It does not sign,
authenticate or load any arm64e pointer.

- `src/inventory.c` (added in 0ead290) recognizes the arm64e subtype
  (lines 9 and 113) and prefers a generic ARM64 slice over arm64e in a FAT
  file (line 70). `tests/test_inventory.c:42-52` checks that preference.
  `ios27 audit-ios` prints the result as `arm64e=` (`src/main.c:267`).
- The chained-fixup parser accepts only pointer formats 2 (`PTR_64`) and 6
  (`PTR_64_OFFSET`) (`src/chained.c:8-9` and `src/chained.c:241-242`). Tests
  require refusal of format 1 (`tests/test_chained.c:278`,
  `tests/test_images.c:655`). They also require refusal of the arm64e subtype in linked executables
  and compiler objects (`tests/test_macho.c:334`,
  `tests/test_compiled_darwin_object.c:84`,
  `tests/test_compiled_darwin_reloc.c:282`). The MH_OBJECT mutation suite
  from 733c9cc includes the arm64e subtype among its mutated header values
  (`tests/test_darwin_inspect_mutations.c:445`).
- The internal pointer-rebase path from 4c0f4cb refuses authenticated and
  tagged layouts ([static pointer rebases](static-pointer-rebases.md)).

**Blocker.** The repository has no arm64e loader. Running arm64e code would
need a loader that signs every authenticated fixup with the guest's keys. Each
signature must use the fixup's recorded key, address diversity and discriminator,
and PAC must be enabled for the guest process. On Linux, the kernel generates random key values at
`exec()`, shares them across a process's threads and keeps them across
`fork()`. `PR_PAC_RESET_KEYS` only regenerates keys and
`PR_PAC_SET_ENABLED_KEYS` only enables or disables them. Setting chosen key
values needs ptrace register sets under `CONFIG_CHECKPOINT_RESTORE`
([Linux arm64 pointer authentication](https://docs.kernel.org/arch/arm64/pointer-authentication.html)).
Apple describes the IA key used for ordinary function pointers as fixed across
processes. It is unproven whether that difference matters for a guest that signs its own
fixups at load time. It has not been measured whether the S25+ CPU and its Android kernel
advertise `HWCAP_PACA` and `HWCAP_PACG`. No arm64e requirement has been measured
for the two SpringBoard binaries.

**Next test.** Parse only. Add two facts to the sanitized inventory: the architecture
named in each shared-cache file header, and the chained pointer formats
declared by SpringBoard.app and SpringBoard.framework. Any arm64e architecture
or format puts this blocker on the Simulator-input path. If there is none, it stays off that path.

## 2. dyld, the dyld shared cache and launch closures

**Needs.** UIKitCore, Foundation, libobjc and libSystem were measured inside
the shared-cache family, not as standalone files
([Apple runtime inputs](apple-runtime-inputs.md)). Loading them means
reading that cache or something equivalent. Apple's
[dyld_cache_format.h](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld_cache_format.h)
defines the format. `dyld_cache_header` starts with a 16-byte `magic` and
records `platform`, `osVersion` and a `simulator` flag ("for simulator of
specified platform"). Sub-caches are listed through `subCacheArrayOffset` and
`subCacheArrayCount`. Each entry carries a UUID, a `cacheVMOffset` and a
file suffix. Unmapped local symbols live in a separate file named by
`symbolFileUUID`. Mapping flags include `DYLD_CACHE_MAPPING_AUTH_DATA`,
`DYLD_CACHE_MAPPING_DIRTY_DATA`, `DYLD_CACHE_MAPPING_CONST_DATA`,
`DYLD_CACHE_MAPPING_TEXT_STUBS` and `DYLD_CACHE_MAPPING_CONST_TPRO_DATA`.
The header defines slide-info versions 1 to 5, which tell the loader how to
rebase cache pointers. Versions 1, 2 and 4 have no authenticated-pointer
fields. Version 3 gives authenticated pointers a 2-bit key, an
address-diversity bit and 16 bits of diversity. Version 5 uses the arm64e
shared-cache authenticated rebase layout. Apple's
[cache layout notes](https://github.com/apple-oss-distributions/dyld/blob/main/doc/CacheLayout.md)
map `TEXT` read/execute, `DATA` read/write and `LINKEDIT` read-only. arm64e
caches add `AUTH` and `AUTH_CONST` mappings for authenticated pointers.
Large caches are split into several files, each with nearby text and data.

Apple's
[PrebuiltLoaderSet policy](https://github.com/apple-oss-distributions/dyld/blob/main/doc/PrebuiltLoaderSet_Policy.md)
describes a PrebuiltLoaderSet, formerly a launch closure, as a read-only set of
precomputed loaders, one per image. Sets for OS programs may be embedded in
the shared cache. The header locates them through fields such as
`dylibsPBLSetAddr`, `programsPBLSetPoolAddr` and `programTrieAddr`. Other
sets are saved under `~/Library/Caches/com.apple.dyld/`. dyld4 uses a valid
set when one exists. Otherwise it launches with JustInTimeLoaders, and
`DYLD_USE_CLOSURES=0` forces that path. dyld does not use a set when there is
no dyld cache, when the set's version does not match dyld, when interposing is
present, or when the launch sets `DYLD_INSERT_LIBRARIES`, `DYLD_*_PATH` or
`DYLD_IMAGE_SUFFIX`. For each image,
[PrebuiltLoader.h](https://github.com/apple-oss-distributions/dyld/blob/main/dyld/PrebuiltLoader.h)
records the path, validation data including the cdHash, regions, dependents,
bind targets, fixups, Objective-C fixups and whether the image has
initializers. In these sources a prebuilt set is an optimization: dyld can
launch without one by building just-in-time loaders. The policy does not
discuss simulators, so it is unproven whether the 27.0 Simulator cache embeds
prebuilt sets.

**Proven today.** The repository loads only standalone, original Mach-O
files. It has no shared-cache reader and no prebuilt-loader support.

- `src/dyld_paths.c` (added in 8f5e14e) resolves install names against
  guest-only paths, including `LC_RPATH` entries.
- `src/binds.c` (added in 6420629) applies classic dyld binds from
  `LC_DYLD_INFO` and `LC_DYLD_INFO_ONLY`.
- `src/chained.c` (added in e9491cf) applies chained fixups for pointer
  formats 2 and 6 only (section 1).
- `src/images.c` (added in fadbaf2) loads several images in isolation with
  W^X and transactional binding. It also handles `LC_DYLD_EXPORTS_TRIE`,
  `LC_REEXPORT_DYLIB` and `LC_LOAD_WEAK_DYLIB` (`src/images.c:25-34` and
  `src/images.c:125-137`).
- Phase 18b (a424b90,
  [bounded native Darwin dylib graphs](phase18b-linked-graph.md)) adds
  explicit guest-path resolution for `@loader_path`, `@executable_path` and
  `@rpath`. Only an exact catalog match satisfies a dependency. It exposes a
  dependency-first initialization plan but does not call guest constructors.
  Its integration test links three original dylibs with Clang and ld64.lld
  and runs them natively on ARM64.
- Searches of `src/` and `tests/` for `dyld_v1`, `shared_cache` and
  `prebuilt` find no matches. In `src/`, the only `closure` matches are
  dependency-graph comments at `src/images.c:467-469`.

**Blocker.** The current loader accepts only standalone images. UIKitCore,
Foundation, libobjc and libSystem sit inside a multi-file cache, and no code
here reads its header, mappings, sub-caches or slide info. Several facts about
the 27.0 Simulator cache are unmeasured: its header magic, its `simulator`
flag, its slide-info versions, whether any mapping carries
`DYLD_CACHE_MAPPING_AUTH_DATA`, and whether it embeds PrebuiltLoaderSets.
Scale is also untested. SpringBoard.framework declares 228 direct
dependencies, while the phase 18b integration graph has three dylibs. Phase
18b also lists nested inherited `@rpath` chains beyond the main image, Darwin
process TLS and process initializer execution as not yet implemented.

**Next test.** Parse only, with no mapping or execution. For each main and
sub-cache file of the 27.0 Simulator cache, add sanitized header facts to the
inventory: the architecture named in the magic, `platform`, the `simulator`
flag, the sub-cache count, each mapping's flags and slide-info version, and
whether `dylibsPBLSetAddr` and `programsPBLSetPoolAddr` are nonzero. This
test shares its header read with the section 1 test.

## 3. XNU/Mach and libSystem services

**Needs.** XNU gives user code two kernel entry tables. The Mach trap table in
[syscall_sw.c](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/syscall_sw.c)
has 128 slots (`MACH_TRAP_TABLE_COUNT`). It holds Mach VM, port, messaging,
semaphore, task, timer and IOKit traps. `mach_reply_port` is trap 26,
`task_self_trap` is trap 28 and `mach_msg2_trap` is trap 47 on LP64. The BSD
table in
[syscalls.master](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/syscalls.master)
runs to syscall 557. Beyond the POSIX calls, it lists thread and workqueue setup
(`bsdthread_create` 360, `bsdthread_register` 366, `workq_open` 367,
`workq_kernreturn` 368), event queues (`kevent_qos` 374, `kevent_id` 375),
user-space lock waits (`sys_ulock_wait` 515, `sys_ulock_wake` 516,
`sys_ulock_wait2` 544), process and code-signing queries (`proc_info` 336,
`csops` 169, `csops_audittoken` 170) and shared-region setup
(`shared_region_check_np` 294, `shared_region_map_and_slide_2_np` 536).
`psynch_mutexwait` (301) and `__mac_syscall` (381) depend on the kernel
configuration. libSystem, which SpringBoard.app links directly, declares 34
direct dependencies in the measured cache. This section lists interfaces only.
It has not measured which traps or syscalls SpringBoard's startup actually
issues.

**Proven today.** The repository models a bounded part of Mach IPC and
libSystem as host-side C code. Nothing here answers a trap or syscall issued
by Apple code.

- Mach IPC models: `src/mach_ipc.c` (ea1a4ed) adds bounded userspace Mach IPC
  with transactional send rights. `src/mach_v2.c` (f690f81) adds task-local
  port namespaces, send-once replies and isolated descriptor handles.
  `src/mach_guest.c` (8263d7a) adds a bounded guest-memory boundary for those
  messages. `src/darwin_mach_abi.c` (62d5db1) adds transactional task
  teardown and a bounded Darwin fixture ABI. Messages are capped at
  `IOS27_MACH_MSG_MAX` (`src/mach_ipc.c:168`, `src/mach_v2.c:559`).
- `src/darwin_probe.c` (041bae7) adds a Darwin SVC compatibility probe.
- libSystem models: `src/libsystem_user.c` (a4441f2) dispatches a bounded,
  deterministic subset of BSD traps in userspace. The subset covers read,
  write, close, the guest pid and a synthetic guest identity, with no host
  passthrough ([phase 19e](phase19e-architecture.md)).
  `src/libsystem20e_services.c` (41b96cd) adds more bounded libSystem
  userspace services. The thread, synchronization, TLS and condition-variable
  reference models are in `src/libsystem20d_threads.c` (3e4950a),
  `src/libsystem21b_lifecycle.c` (4c46677), `src/libsystem21c_sync.c`
  (51d84a0), `src/libsystem21e_tls.c` (1f951ef), `src/libsystem21g_cond.c`
  (dd39b31) and `src/libsystem21h_lifecycle.c` (9d522c1). They track owners
  with host pthreads (for example `src/libsystem21c_sync.c:131`).
- The [named service shim](named-service-shim.md) (927010f) gives an
  SDK-free ARM64 Darwin client 14 named functions, from `getpid` to `free`.
  The functions have fixed-width prototypes and no host file or syscall
  passthrough. `src/service_bridge.c` (fca99bc) bridges native Mach-O
  fixtures to the bounded userspace services.
- Phase 19e says that the standard `objc_msgSend` and libSystem ABI entry
  points are not exported, and it calls its services bounded host-side
  foundations. The shim document says that Apple libSystem, dynamic dylib
  imports and general Darwin SVC translation remain unimplemented.
- A case-insensitive search of `src/` for `bootstrap`, `launchd`, `xpc`,
  `kevent`, `workq`, `ulock`, `bsdthread`, `csops`, `proc_info` and
  `shared_region` finds no matches.

**Blocker.** No code here loads Apple's libSystem. Loading it would first need
the shared-cache reader that section 2 lacks. The repository has nothing that
corresponds to the workqueue, event-queue, ulock, code-signing or
shared-region calls listed above. Nobody has measured which traps and syscalls
the 27.0 Simulator libSystem and its dependencies issue, so the size of the
missing service surface is unknown.

**Next test.** Parse only. Reuse the parse that produced libSystem's
34-dependency count and add two things to the sanitized inventory: the
install names of those 34 dependencies, and their exported symbol names that
match the traps and syscalls listed above. Mark each matched interface as
either modeled in this repository or absent.

## 4. Objective-C and Swift runtimes

**Needs.** SpringBoard.app links libobjc directly. In the measured cache,
libobjc declares five direct dependencies, including
`/usr/lib/objc/libobjcMsgSend.dylib`, `/usr/lib/libobjc-env.dylib` and
`/usr/lib/libc++abi.dylib`. Apple's
[objc-runtime-new.h](https://github.com/apple-oss-distributions/objc4/blob/main/runtime/objc-runtime-new.h)
defines two method-list layouts. A big entry holds three absolute pointers
(`name`, `types` and `imp`), 24 bytes on 64-bit targets. A small list sets
`smallMethodListFlag` (`0x80000000`), and each entry holds three signed 32-bit
offsets, each relative to its own field. Inside the shared cache, a small
entry's name resolves from `sharedCacheRelativeMethodBase()` rather than
through a selector reference. The header also describes preoptimized method
caches (`preopt_cache_t`), which `RW_NOPREOPT_CACHE` and `RW_NOPREOPT_SELS`
turn off. On arm64e it signs method-list pointers with
`ptrauth_key_method_list_pointer`, class data with
`CLASS_DATA_BITS_RO_SIGNING_KEY` and `CLASS_DATA_BITS_RW_SIGNING_KEY`, and
superclass pointers with `ISA_SIGNING_KEY`. These signing rules matter only
for an arm64e cache, and the inventory records Foundation and libobjc with
subtype `ARM64` (section 1). Swift classes set `FAST_IS_SWIFT_LEGACY` or
`FAST_IS_SWIFT_STABLE` in their packed class-data bits, and `class_ro_t` has
an `RO_HAS_SWIFT_INITIALIZER` flag.

Swift is in the measured graph as well. UIKitCore declares 20 direct
dependencies under `/usr/lib/swift/`, including `libswiftCore.dylib` and
`libswift_Concurrency.dylib`, and it also links SwiftUICore. Foundation
declares 14 of its 31 direct dependencies under `/usr/lib/swift/`. The Swift
[ABI stability manifesto](https://github.com/swiftlang/swift/blob/main/docs/ABIStabilityManifesto.md)
lists what such libraries depend on: data layout, type metadata, name
mangling, a calling convention with call-context and error registers, and
runtime entry points for memory management, reference counting, dynamic
casting, reflection and metadata creation. On Apple platforms it also requires
class metadata and object references that are compatible with Objective-C.

**Proven today.** The repository has several bounded Objective-C models.
They read Clang-compiled `MH_OBJECT` files or call trusted host C functions.
None of them loads Apple's libobjc, and nothing here models Swift.

- `src/objc_metadata.c`, `src/objc_runtime.c` and `src/objc_bridge.c`
  (a4441f2, [phase 19e](phase19e-architecture.md)) parse ObjC2 metadata from
  Clang `MH_OBJECT` files and register it with a host runtime. They accept
  only 24-byte absolute method entries, not relative methods. On native ARM64
  Linux, the tests copy one relocation-free compiled method, `-[Root answer]`,
  into a W^X-sealed page and run it.
- `src/objc20a_reader.c` and `src/objc20a_registry.c` (6fc645a,
  [phase 20a](phase20a-objective-c-metadata.md)) walk class, protocol,
  category and selector-reference lists. Phase 20a says it does not handle
  relative or small method lists, Swift metadata, arm64e PAC or `+load`, and
  calls its registry a bounded host-side model, not a drop-in libobjc.
- `src/objc20b_dispatch.c` and `src/objc20b_entry.S` (b83af49,
  [phase 20b](phase20b-dispatch.md)) add a single-threaded dispatcher. Its
  `_objc_msgSend` and `_objc_msgSendSuper2` entry points compile to ARM64
  Mach-O code. Phase 20b says native ARM64 Darwin execution of them is still
  pending.
- `src/objc20c_lifetime.c` (31782fb, [phase 20c](phase20c-ownership.md))
  models explicit reference counting, weak references and autorelease pools.
  Phase 20c says this is not ARC and has no `objc_retain`/`objc_release`
  symbol compatibility, nonpointer isa or tagged pointers.
- `src/phase22d_runtime.c` (3c5b7ec) and `src/phase22d_metadata.c` (3cbc24a)
  add a concurrent runtime substrate
  ([phase 22d](phase22d-objc-runtime.md)). Phase 22d says it does not replace
  libobjc, ARC, the Swift ABI, exceptions, arm64e PAC or forwarding, and that
  its ARM64 Darwin fixture is inspected, not run.
- `src/objc_runtime.c:171-172` leaves the native isa layout to the image
  loader and records object identity out of band.
- A case-insensitive search of `src/` for `swift` finds no matches. In the
  `src/objc*.c` and `src/phase22d*.c` files, searches for `relative`,
  `small method`, `preopt`, `objc_opt` and `shared cache` find none.

**Blocker.** Apple's libobjc sits in the shared cache, so loading it first
needs the cache reader that section 2 lacks. The method-list parsers here
accept only big absolute entries, but objc4 also defines the small relative
layout, whose names resolve against the shared cache. Phase 20b's message-send
entry points have not run, and no model here is a drop-in libobjc. Nothing
implements the Swift runtime, yet UIKitCore and Foundation link Swift
libraries directly. It is unmeasured whether the 27.0 Simulator images use
small method lists, preoptimized caches or Swift classes.

**Next test.** Parse only, after the section 2 header read. For libobjc,
Foundation and UIKitCore in the 27.0 Simulator cache, add three sanitized
counts to the inventory: method lists with `smallMethodListFlag` set, method
lists without it, and classes whose data bits set `FAST_IS_SWIFT_LEGACY` or
`FAST_IS_SWIFT_STABLE`.

## 5. Code signing and entitlements

**Needs.** Apple says iOS requires that "all executable code be signed using
an Apple-issued certificate". Built-in apps are signed by Apple, system
executables may link only to system libraries, and dynamic libraries are
validated when a process launches
([Apple Platform Security: app code signing process](https://support.apple.com/guide/security/app-code-signing-process-sec7c917bf14/web)).
XNU's
[cs_blobs.h](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/cs_blobs.h)
defines the signature format. An embedded signature is a SuperBlob
(`CSMAGIC_EMBEDDED_SIGNATURE`, `0xfade0cc0`) with typed slots: the
CodeDirectory (0), requirements (2), entitlements (5), DER entitlements (7),
alternate CodeDirectories (`0x1000`) and the signature (`0x10000`). A
CodeDirectory (`CSMAGIC_CODEDIRECTORY`, `0xfade0c02`) has versions up to
`CS_SUPPORTSLINKAGE` (`0x20600`) and hash types SHA-1 (1), SHA-256 (2),
truncated SHA-256 (3) and SHA-384 (4). The header also defines code-signing
flags, including `CS_VALID`, `CS_ADHOC`, `CS_GET_TASK_ALLOW`, `CS_HARD`,
`CS_KILL`, `CS_RUNTIME`, `CS_LINKER_SIGNED` and `CS_PLATFORM_BINARY`, and
exec-segment flags, including `CS_EXECSEG_MAIN_BINARY`,
`CS_EXECSEG_ALLOW_UNSIGNED` and `CS_EXECSEG_JIT`. Apple's
[TN3126](https://developer.apple.com/documentation/technotes/tn3126-inside-code-signing-hashes)
says a CodeDirectory holds one hash per code page plus negative special
slots for metadata, and hashing the CodeDirectory gives the cdhash. Special
slot -5 hashes the property-list entitlements and slot -7 the DER
entitlements, so changing either breaks the signature. SHA-256 is current,
and Apple silicon code never includes a legacy SHA-1 CodeDirectory.

Earlier sections touch the same data. A PrebuiltLoader's validation data
includes the image's cdHash (section 2), and the BSD table has `csops` (169)
and `csops_audittoken` (170) for code-signing queries (section 3). None of
the sources above discusses the Simulator. It is unmeasured whether the 27.0
Simulator binaries carry embedded signatures or entitlements, and how the
Simulator treats them.

**Proven today.** The repository bounds where a signature sits and reads
nothing inside it. It does not parse, verify or create signatures, and it
grants no entitlements.

- `src/macho.c:27` defines `LC_CODE_SIGNATURE`, added in 6ae6714.
  `src/macho.c:325-330` accepts it only as a 16-byte linkedit command whose
  data range lies inside the file, the same check it applies to
  `LC_FUNCTION_STARTS` and `LC_DATA_IN_CODE`. The blob is never read.
- A case-insensitive search of `src/` for `LC_CODE_SIGNATURE`,
  `code_signature`, `codesign`, `cdhash`, `CodeDirectory`, `entitlement`,
  `CS_VALID` and `amfi` matches only those two `macho.c` lines. Searches of
  `tests/` and `tools/` for code-signing and entitlement terms find nothing.
  Section 3's search of `src/` found no `csops` handling.
- The [architecture notes](architecture.md) list code signing and Apple
  MobileFileIntegrity as not implemented, and [ceilings](CEILINGS.md) says
  the project does not grant itself entitlements or code signing.
- The [sanitized inventory](apple-runtime-inventory-27.0.json) has no
  signature facts. A search of it for `signature` finds no matches.

**Blocker.** Nothing here reads an embedded signature, computes a cdhash or
holds entitlements, and nothing answers `csops` or `csops_audittoken`. A
loader on this host cannot produce Apple-issued signatures, and this review
has not established whether the Simulator path requires them. It is
unmeasured whether SpringBoard.app and SpringBoard.framework carry a
signature, which hash types and flags it declares, and whether it has an
entitlements or DER entitlements slot.

**Next test.** Parse only. For SpringBoard.app/SpringBoard and
SpringBoard.framework/SpringBoard, add sanitized signature facts to the
inventory: whether `LC_CODE_SIGNATURE` is present, the SuperBlob slot types,
each CodeDirectory's version, hash type, page size, flags and exec-segment
flags, and whether slot 5 or slot 7 is present. Record presence and counts,
never entitlement contents.

## 6. Metal, CoreAnimation and IOSurface

**Needs.** Apple's public documentation describes three frameworks between an
app's layers and the screen. Core Animation exists to "Render, compose, and
animate visual elements", and it "accelerates the rendering by handing over
most of the work to dedicated graphics hardware"
([Apple: Core Animation](https://developer.apple.com/documentation/quartzcore)).
Its programming guide gives every layer matching objects in a model layer
tree, a presentation tree and a render tree, and says "Objects in the render
tree perform the actual animations and are private to Core Animation"
([Apple: Core Animation Basics](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/CoreAnimationBasics/CoreAnimationBasics.html)).
Metal "gives your app direct access to a device's graphics processing unit
(GPU)" ([Apple: Metal](https://developer.apple.com/documentation/metal)), and
`CAMetalLayer` is "A Core Animation layer that Metal can render into,
typically displayed onscreen"
([Apple: Core Animation](https://developer.apple.com/documentation/quartzcore)).
IOSurface "provides a framebuffer object suitable for sharing across process
boundaries"
([Apple: IOSurface](https://developer.apple.com/documentation/iosurface)).

The [sanitized inventory](apple-runtime-inventory-27.0.json) shows that the
two measured UI images link this stack directly:

- UIKitCore lists IOSurface.framework, QuartzCore.framework,
  `libswiftMetal.dylib` and `libswiftQuartzCore.dylib` among its 103 declared
  direct dependencies.
- SpringBoard.framework lists IOSurface.framework and QuartzCore.framework
  among its 228 declared direct dependencies.
- No inventoried image lists Metal.framework directly. That does not show
  that SpringBoard runs without Metal, because the inventory does not record
  the dependencies of QuartzCore or IOSurface themselves.

**Proven today.** None of this. The repository's status pages record no
implementation:

- [Ceilings](CEILINGS.md) marks Metal / CoreAnimation / IOSurface as **not
  implemented** (be616c6) and says graphics and private services have unknown
  feasibility.
- The [official UI roadmap](official-ios-ui-roadmap.md) marks the
  translation of CoreAnimation, IOSurface and Metal operations into
  Android-compatible Vulkan/GPU surfaces as **not implemented**.
- [Progress](PROGRESS.md) leaves Metal-to-Android GPU integration, IOSurface
  sharing and display integration, and the CoreAnimation compositor bridge
  unchecked.
- [Branches](BRANCHES.md) lists two candidate branches with passing checks,
  `phase26c-headless-graphics-interop-20261030` (PR #111) and
  `phase26d-coreanimation-compositor` (PR #108). Both are unmerged and not
  credited, so this review does not count them.

**Blocker.** No committed code loads QuartzCore, IOSurface or Metal, and
three gaps have no measured design. First, the render tree is private to Core
Animation, so the public documentation reviewed here does not define what it
hands to the screen. Second, Metal expects direct access to the device GPU,
while the Android host exposes its own GPU interfaces. How Metal's resource,
command and synchronization semantics would map onto them is unmeasured.
Third, IOSurface shares buffers across processes, and the repository has no
equivalent shared-buffer model and no path to an Android display surface.

**Next test.** Parse only. Add the declared direct dependencies of
QuartzCore.framework/QuartzCore and IOSurface.framework/IOSurface, and of
Metal.framework/Metal if the measured cache lists it, to the sanitized
inventory as names and counts. That shows which further services the
graphics stack declares and whether QuartzCore links Metal directly.

## 7. Licensing

This section quotes the license that governs the Simulator runtime and
records where the project goal conflicts with it. It is not a legal opinion
and does not decide how the terms apply in any jurisdiction. Quotes fix
spacing errors from PDF text extraction, and the all-capitals opening
notice is quoted in sentence case.

**Needs.** Apple documents Simulator runtimes as additional Xcode
components
([Apple: Xcode components](https://developer.apple.com/documentation/xcode/downloading-and-installing-additional-xcode-components)).
The [Xcode and Apple SDKs Agreement](https://www.apple.com/legal/sla/docs/xcode.pdf)
defines the Apple SDKs to include "header files, APIs, libraries,
simulators, and software (source code and object code) labeled as part of
the iOS SDK" and included in the Xcode Developer Tools package. The copy
fetched on 2026-11-08 is marked EA2002 and 06/08/2026. Four of its terms
bear directly on this project:

- The notice before section 1 says use of Apple Software "is authorized
  only for execution on an Apple-branded product running macOS" and that
  "any other download or use of Apple Software is not authorized and is in
  breach of this agreement".
- Section 2.2.A licenses installation only "on Apple-branded computers that
  are owned or controlled by You". Under 2.2.A(iii), the Apple SDKs other
  than the macOS SDK may be used "solely to test and develop Applications
  that are specifically for use with the applicable Apple-branded products
  for which the SDK is targeted, unless otherwise permitted by Apple in
  writing".
- Section 2.5 says "You are expressly prohibited from separately using the
  Apple SDKs or attempting to run any part of the Apple Software on
  non-Apple-branded hardware."
- Section 2.7 says the grants do not permit You to "install, use or run the
  Apple Software or Apple Services on any non-Apple-branded computer or
  device", or to "upload to or host on any website or server, sell,
  redistribute, or sublicense" it. It also says You may not "copy (except as
  expressly permitted under this Agreement), decompile, reverse engineer,
  disassemble, attempt to derive the source code of, modify, decrypt, or
  create derivative works of the Apple Software or Services, or any part
  thereof". These restrictions give way only "to the extent any foregoing
  restriction is prohibited by applicable law" or where the licensing terms
  of included open-source components or sample code permit.

[HANDOFF.md](../HANDOFF.md) sets the goal of researching how to run Apple's
actual SpringBoard and genuine iOS applications on a Samsung Galaxy S25+.
That phone is not an Apple-branded device, so running them there is the use
that sections 2.5 and 2.7 name.

**Proven today.** The repository keeps Apple bytes out of what it
distributes:

- [Apple runtime inputs](apple-runtime-inputs.md) says "No Apple binaries,
  archive keys or proprietary source are committed or packaged in the APK".
  It keeps the inputs outside the checkout and leaves the archive key out of
  the generated source report (present at 0099a61).
- [HANDOFF.md](../HANDOFF.md) says "Do not bundle or redistribute
  proprietary Apple SDK, framework blobs, firmware or unauthorized decrypted
  executables."
- The [sanitized inventory](apple-runtime-inventory-27.0.json) records
  names, hashes and counts only, and this review follows the same rule.

These measures cover redistribution. They do not cover where the runtime
was downloaded and processed. The same inputs document says decryption
"used the server-provided symmetric key" with the upstream ipsw tool and
that 7-Zip extracted the files, and its example command uses a Windows
directory. It calls extraction "data processing, not execution of an Apple
program". Section 2.7 restricts copying, decryption and disassembly as well
as execution, so that distinction does not settle the question. No
repository document records a macOS host, Apple-branded hardware or written
permission from Apple for these steps.

**Blocker.** The reviewed agreement grants no license to run any part of
the Simulator runtime on non-Apple hardware, and it names that use as
prohibited. The repository records no separate written permission from
Apple and no legal review that applies the applicable-law exception to this
project. Without one of those, running SpringBoard or any other Apple
component on the Android host has no recorded license basis, whatever
sections 1 to 6 achieve technically. This review did not examine the
licenses for iPhone device software or App Store applications, so the
goal's genuine iOS applications have no reviewed license basis either.

**Next test.** Documentation only. For each Apple input, add to the
sanitized inventory the governing license URL, its printed version and date
(EA2002 and 06/08/2026 for this agreement), the kind of host that ran
acquisition and decryption, and the clause or written permission that
covers each use. Keep every Apple component parse-only, and run none of it
on the Android host, until an entry names a clause or permission that
covers execution there.
