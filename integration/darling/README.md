# Native Android runtime integration

The target remains **native ARM64 on the S25+**. The user explicitly retained
that requirement when offered an x86-64 PC simulator. CPU translation is not
part of this integration.

## Why this backend

Upstream Darling now contains native ARM64 support, Android install-layout
handling and a `DARLING_NONROOT=1` mode that avoids namespaces. These provide
a substantially more complete starting point for Darwin process, thread,
Mach IPC and libSystem support than the project's small fixture runtime.
A native Darwin guest now passes the integrated checks through Darling's
custom-init path. Genuine SpringBoard startup remains the next integration goal.

The pinned sources are recorded in [upstream.json](upstream.json). The Darling
source and its core submodules are kept in an external Linux checkout. Local
cross-build patches are kept in `patches/`; `source-patches.json` pins their
resulting source-file hashes. The helper rejects other tracked modifications.
Upstream GPL and submodule licenses remain applicable to any future distribution.

Apply the patches to the pinned checkout before running the helper:

```sh
git -C /path/to/pinned/darling apply /path/to/ios-27/integration/darling/patches/0001-cross-compiler-version.patch
git -C /path/to/pinned/darling/src/external/bootstrap_cmds apply /path/to/ios-27/integration/darling/patches/0002-mig-host-resource-headers.patch
git -C /path/to/pinned/darling apply /path/to/ios-27/integration/darling/patches/0003-guest-resource-header-scope.patch
git -C /path/to/pinned/darling apply /path/to/ios-27/integration/darling/patches/0004-android-posix-shared-objects.patch
git -C /path/to/pinned/darling/src/external/darlingserver apply /path/to/ios-27/integration/darling/patches/0005-system-config-test-headers.patch
git -C /path/to/pinned/darling/src/external/darlingserver apply /path/to/ios-27/integration/darling/patches/0006-ptrace-request-type.patch
git -C /path/to/pinned/darling apply /path/to/ios-27/integration/darling/patches/0007-bsdln-portable-strrchr.patch
git -C /path/to/pinned/darling apply /path/to/ios-27/integration/darling/patches/0008-nonroot-namespace-identities.patch
git -C /path/to/pinned/darling/src/external/dyld apply /path/to/ios-27/integration/darling/patches/0009-simulator-signature-registration.patch
git -C /path/to/pinned/darling/src/external/darlingserver apply /path/to/ios-27/integration/darling/patches/0010-shared-cache-signatures.patch
git -C /path/to/pinned/darling/src/external/xnu apply /path/to/ios-27/integration/darling/patches/0011-simulator-kernel-callbacks.patch
git -C /path/to/pinned/darling/src/external/libsystem apply /path/to/ios-27/integration/darling/patches/0012-libsystem-callback-tag.patch
git -C /path/to/pinned/darling/src/external/dyld apply /path/to/ios-27/integration/darling/patches/0013-simulator-host-lookup.patch
git -C /path/to/pinned/darling apply /path/to/ios-27/integration/darling/patches/0014-executable-boot-hash.patch
git -C /path/to/pinned/darling apply /path/to/ios-27/integration/darling/patches/0015-tsd-user-address-mask.patch
git -C /path/to/pinned/darling/src/external/dyld apply /path/to/ios-27/integration/darling/patches/0016-simulator-thread-registers.patch
git -C /path/to/pinned/darling/src/external/dyld apply /path/to/ios-27/integration/darling/patches/0017-simulator-private-code-pages.patch
git -C /path/to/pinned/darling/src/external/libpthread apply /path/to/ios-27/integration/darling/patches/0018-pthread-workgroup-callbacks.patch
git -C /path/to/pinned/darling/src/external/xnu apply /path/to/ios-27/integration/darling/patches/0019-kernel-bootargs.patch
git -C /path/to/pinned/darling/src/external/xnu apply /path/to/ios-27/integration/darling/patches/0020-native-kevent-workqueue.patch
git -C /path/to/pinned/darling/src/external/xnu apply /path/to/ios-27/integration/darling/patches/0021-simulator-workqueue-features.patch
git -C /path/to/pinned/darling/src/external/libpthread apply /path/to/ios-27/integration/darling/patches/0022-pthread-workqueue-capabilities.patch
git -C /path/to/pinned/darling/src/external/xnu apply /path/to/ios-27/integration/darling/patches/0023-native-workloops.patch
```

The first patch fixes a configure-time Clang version probe that linked and
executed a target program. Preprocessing the documented `__clang_major__`
macro needs neither a target SDK nor target execution. Ten native/cross,
minimum-version and invalid-compiler checks pass:

```sh
python3 integration/darling/tests/compiler-version.py \
  --module /path/to/pinned/darling/cmake/clang_version_check.cmake
```

Patch provenance: the existing open-source build module and Clang's public
predefined-macro interface. No Apple binary inspection informed this fix.
The second patch keeps the build-machine MIG compiler's headers separate from
the target compiler's headers and honors the existing `HOST_CC` setting.
Two different Clang resource directories had caused `stdint.h`'s shared header
guard to suppress the required integer types during the host MIG build.
The third patch scopes explicit compiler-resource headers to Darwin targets;
native C++ targets need libc++'s headers ahead of the C resource headers.
The fourth supplies the missing Android `shm_open`/`shm_unlink` backend using
file-backed objects beneath the runtime's temporary directory. It preserves
cross-process mapping visibility, descriptor close-on-exec, atomic exclusive
creation, truncation and unlink lifetime. Its contract test passes under
Linux ASan/UBSan. The adb-shell run on the S25+ reached the final foreign-FIFO
fixture, whose creation Android denied with `EACCES`. The **same unchanged
binary passes the complete test in the installed host app's debug context**
(`run-as org.ios27.host`), including that fixture, without taking the screen.
The test and implementation are original and based on the linked POSIX
contract. File backing consumes runtime filesystem space; it is not a tmpfs.
The fifth patch restricts the system-configuration test's imported headers to
the two it needs, so libc++ does not accidentally select Darwin's `mbstate_t`
declaration when compiling for Android. Its test assertions are retained.
The sixth uses the host's public ptrace request type in the thread-discovery
test instead of a glibc-only enum name.
The seventh uses standard `strrchr` in `bsdln` instead of the legacy `rindex`
entry point absent from Bionic.
The eighth fixes namespace-free startup on kernels without user namespaces or
with restricted namespace handles. Duplicate-server detection retains the
real-UID, pinned executable and exact-prefix checks. Non-root mode compares
the kernel's namespace-link identities; it requires matching mount identities
and accepts a missing user-namespace interface only when missing on both
processes. The launcher server/mode suites, including malformed, missing and
mismatched identity cases, pass on Linux under ASan/UBSan.
The ninth routes simulator-dyld signature registration through Darling's
implemented `F_ADDFILESIGS_RETURN` operation. The server's code-signing policy
and the loader's verified-coverage check continue to apply. The original
`F_ADDFILESIGS_FOR_DYLD_SIM` operation returned `EINVAL` on the first simulator
boot attempt. The updated host dyld now enters Apple's `dyld_sim` on the S25+;
the native Darwin guest regression still exits normally with status zero.

The tenth locates code signatures in `dyld_v1` shared-cache containers before
using the existing CodeDirectory parser and signature-policy path. The
ordinary executable parser still rejects cache containers. Bounds, truncated
headers, malformed signatures, changed code pages and policy enforcement are
covered by the original `tests/shared-cache-signature.c` and
`tests/shared-cache-policy.cpp` contracts. These and Darling's existing
`codesign` and `code_signing` host suites pass under ASan/UBSan. Both real
24A434 cache parts' signature metadata parse successfully on the host. The
existing enforcement verification-size limit is retained; large-cache
metadata parsing is not a claim of whole-cache cryptographic verification.

```sh
python3 integration/darling/tests/shared-cache.py \
  --source /path/to/pinned/darling/src/external/darlingserver
# Optional: --host-prefix /path/to/private/usr --cache /path/to/shared-cache
```

With patch ten deployed, Apple's loader maps both cache parts and reaches
the real simulator `libSystem` initializer on the S25+. A native ptrace trace
then identified a callback ABI collision: Apple's v5 allocator callback was
being read as Darling's private `dyld_func_lookup` callback, entering malloc
before allocator initialization. Patches eleven through thirteen tag
Darling-owned tables, normalize only the public versioned prefix for Apple
tables, and pass the host lookup separately through the loader's `apple[]`
parameters. An external table without Darling's fork hooks returns `ENOSYS`
for forking `posix_spawn`; that bridge remains to be integrated.
The handoff retains a complete contiguous `KernelArgs` vector: modern
`dyld_sim` reconstructs it from `argv[-2]` rather than using the separately
passed `apple` pointer. Pointer formatting uses no early libc locale state.

```sh
python3 integration/darling/tests/kernel-callbacks.py \
  --source /path/to/pinned/darling/src/external/xnu
python3 integration/darling/tests/simulator-args.py \
  --source /path/to/pinned/darling/src/external/dyld
```

The original callback contracts pass under ASan/UBSan, including truncated
older tables, the observed v5 collision, and malformed/duplicate handoffs.
The actual argument-construction block also passes a host ASan/UBSan harness
that reconstructs `KernelArgs` from `argv[-2]` and checks the copied vectors.
The corrected handoff now passes the simulator's kernel, platform, pthread
and C-library initialization calls on the S25+. The next observed stop is
Apple's allocator requiring the missing `executable_boothash` metadata.
`tests/native-trace.c` is the optional native ARM64 Linux fault tracer used
headlessly for register and stack evidence; it does not emulate instructions.

Patch fourteen supplies the allocator's opaque `executable_boothash` metadata.
The native loader combines a random, prefix-private key, the Linux boot UUID
and the executable's registered CDHash using HMAC-SHA256, then exposes the
first 20 bytes as 40 hex digits. The value is stable for the same executable,
prefix and boot, and changes when any of those identities changes. This
implements the documented bucketing-identity properties; it does not claim
byte-for-byte identity with XNU's salted-hash implementation. The key is kept
in the prefix's mode-0600 `.darling-boothash-key`, with locking across processes.

```sh
python3 integration/darling/tests/boot-hash.py --source /path/to/pinned/darling
# Optional: --host-prefix /path/to/private/usr
```

The host test passes under ASan/UBSan with an independent HMAC vector,
boot/executable changes, and eight concurrent processes creating one key.
The same contract also passes natively on the S25+ with one explicit skip:
Android denies creating the hardlink rejection fixture (covered by the host
run). The native Darwin guest still exits zero. Apple's allocator now returns
from initialization; the next captured fault is `dyld_sim` reading its thread
pointer from Darwin's `TPIDRRO_EL0`, which Linux leaves zero.

Patches fifteen and sixteen extend the existing native TSD adapter for the
newer 48-bit pointer mask and run it when the host maps `dyld_sim`. Only
bounded instruction sections are scanned, before entering the simulator
loader. Matching instruction pairs become native loads from the loader's
Darwin-TSD TLS slot; page protections are restored to RX after instruction
cache invalidation. The upstream TSD-rewrite host suite, including the new
mask cases, passes under ASan/UBSan. A static copy of the pinned `dyld_sim`
contains two matching pairs, including the captured fault site; both are
adapted by the helper. The native Darwin guest regression passes with both
patches deployed.
The first device run reached the adapter but Android refused the transition
back to executable permission on modified file-backed pages (`EACCES`).
Patch seventeen prepares anonymous copies of those owned pages, preserves
their virtual addresses and original file contents, and enables execute
permission after copying and instruction-cache invalidation. The standalone
`tests/android-code-pages.c` probe verifies that distinction on the S25+:
file-backed restoration is denied, while original probe code executes from
anonymous and fixed-address replacement pages, returning 42 and 43.
With patch seventeen deployed, the native Darwin guest still exits zero and
simulator startup passes the earlier TSD fault. The next captured stop is
dispatch initialization calling the missing host export
`pthread_install_workgroup_functions_np` through dyld's `0xbad4007` marker.
The simulator service-port handshake has not yet completed.

```sh
clang -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I/path/to/pinned/darling/src/startup/mldr \
  /path/to/pinned/darling/tests/mldr/tsd_rewrite.c -o /path/to/tsd-rewrite-test
/path/to/tsd-rewrite-test
```

The actual simulator mapping-adapter block also has a host contract harness.
It uses real protected pages to check instruction-section bounds, data-section
exclusion, idempotence, RX/RW transitions, cache invalidation and protection
failures. Its ARM64 instruction words are data only. The Android CI job runs
this harness and the word-level suite from three hash-pinned open-source inputs:

```sh
python3 integration/darling/tests/simulator-thread-registers.py --fetch-pinned
# Or test an existing modified checkout:
python3 integration/darling/tests/simulator-thread-registers.py --source /path/to/pinned/darling
```

`tests/thread-register-inputs.json` pins the original files. The fetch mode
applies the maintained patches in order and checks the resulting hashes against
`source-patches.json` before compiling the tests. It needs neither runtime
assets nor a phone.
The mapped-page harness also models Android's observed `execmod` refusal,
checks temporary-copy cleanup on mapping failures, and verifies that adapting
two sections sharing a page preserves both edits. The old in-place path fails
this regression contract.

Patch eighteen backports the pthread workgroup callback ABI from Apple's
open-source libpthread-539. Dispatch installs its process-lifetime callback
table once; `pthread_create_with_workgroup_np` forwards every argument and the
result to dispatch, which owns thread creation and workgroup membership.
Registration rejects pre-v1 tables before reading a callback field and
accepts the known prefix of newer tables. Repeated registration retains the
upstream client-crash behavior.

```sh
python3 integration/darling/tests/pthread-workgroup.py --fetch-pinned
# Or: --source /path/to/pinned/darling/src/external/libpthread
```

The host ASan/UBSan harness compiles the actual function bodies and table
declaration. Its 74 checks cover forwarding, returned errors and thread
handles, registration order, truncated older tables and a future-version
prefix. The pinned source and API reference are recorded in
`tests/pthread-workgroup-inputs.json`; this contract also runs in Android CI.
On the S25+, registration now returns, system and Swift initializers finish,
and execution reaches `launchd_sim`'s main entry. The native guest regression
still exits zero. `launchd_sim` next fails its `kern.bootargs` query; its error
report then traps because the host pthread backend advertises no maintenance
QoS support. This is startup progress, with the service-port handshake and
SpringBoard entry still unverified.

Patch nineteen exposes `kern.bootargs` as a read-only, empty, NUL-terminated
string: this runtime has no configured Darwin kernel boot arguments.
`tests/guest-bootargs.c` exercises the complete guest sysctl path, including
size queries, named/numeric lookups, short buffers, and rejected writes.
All 11 checks pass on the S25+ (the same binary fails with `ENOENT` before
deployment), and the native startup guest still exits zero. `launchd_sim`
passes its boot-argument query; normal startup logging now reaches the
remaining dispatch workqueue capability failure.

Patch twenty adds asynchronous process-workqueue event delivery. Registration
applies changes immediately; a native pthread polls the kqueue and hands
events to the existing native worker pool. Forty-eight owned delivery slots
keep callback storage alive, with up to 64 returned changes per callback and
1,024 tracked registrations. Event-manager callbacks are serialized while
ordinary events can continue on other workers.

```sh
python3 integration/darling/tests/kevent-workqueue.py --fetch-pinned
# Or: --source /path/to/pinned/darling/src/external/xnu
```

The actual adapter and delivery declaration pass 8,286 host ASan/UBSan checks
for capacity, receipts, returned changes, buffer ownership and failures.
The native `tests/guest-kevent-workqueue.c` workload passes on the S25+,
including a callback blocked on another event, event-manager serialization,
and callback-driven rearming. `tests/guest-workqueue.c` also completes 384
callbacks across six QoS classes and both overcommit modes, reusing workers.
Both tests and the original startup guest exit zero. These contracts first
passed with a zero advertised feature mask.

Patches twenty-one and twenty-two recognize external libSystem initialization
and advertise the tested workqueue subset (`0x40000053`), returning default
main-thread QoS metadata. The host pthread initializer accepts workqueue
capabilities independently of optional thread-control SPIs. The callback ABI
contract now passes 44 checks.

Both original workqueue probes were also built as platform-7 ARM64 executables
and run through Apple's `dyld_sim` and iOS 27 libSystem on the S25+. They exit
zero: 384 worker callbacks with the advertised mask, plus asynchronous event
delivery, manager serialization and rearming. Their 19 and 18 imports were
checked against the runtime before linking. `launchd_sim` now passes the
earlier capability trap and stops on the separate, unimplemented `kevent_id`
workloop API. The simulator service-port handshake remains pending.

Patch twenty-three implements the native `kevent_id` workloop path, including
worker handoff, request consumption, debounce receipts, ownership, synchronous
wait/wake and returned-change errors. Timer and user-event identifiers are
isolated between workloops, with generation checks for canceled registrations.
The bounded registry reuses quiescent entries.

```sh
python3 integration/darling/tests/workloops.py --fetch-pinned
```

The actual workloop code passes 8,042 ASan/UBSan checks with 282 synthetic
deliveries; the revised event adapter passes 8,288. The native
`tests/guest-workloops.c` contract passes 62 checks with five callbacks under
both Darling's libraries and Apple's iOS simulator libraries, including
same-ID timers on separate loops and a genuinely blocked sync waiter.
The Apple-library probe reports feature mask `0x400000d3`. The event regression
and startup guest still exit zero. `launchd_sim` now passes dispatch startup
and reports an absent `/private/var/tmp` directory in its new prefix.

x86Sim is a useful reference for `launchd_sim` startup, SimFramebuffer,
SimulatorHID, IOSurface and Metal host-service protocols. Its current runtime
depends on x86-64 CPU translation and is **not** the native Android backend.
Its simulator-specific patches are not assumed to be included in the pinned
upstream Darling revision.

## Cross-toolchain preparation

`tools/darling-android.py` prepares an out-of-source core build on x86-64 Linux.
The compiler wrappers select:

- the Android NDK sysroot for native ARM64 ELF runtime components;
- Darling's existing open-source SDK path for explicit ARM64 Darwin targets;
- separate, real x86-64 host compilers for build-time tools.

The check phase compiles and inspects all three object architectures, then
links Android C and C++ probes. It does not execute target code on the PC.
These checks have passed with the installed NDK 29 and Linux Clang 21. The
Android route explicitly selects compiler-rt; the first link attempt exposed
the Linux compiler's unsuitable `-lgcc` default. Wrapper contents are hashed
into the build configuration so a changed compiler setup cannot silently
reuse an older configured build.

```sh
timeout 360s python3 tools/darling-android.py \
  --source /path/to/pinned/darling --build /path/to/private/build \
  --ndk /path/to/android-ndk --sigrtmin 42 --sigrtmax 64 --phase check
```

The signal range is not copied from Linux/glibc. The original
`tools/android-runtime-abi.c` probe measured **42–64** on the connected S25+
(SM-S936U1, API 37, 4 KiB pages, UID 2000). It also found a `sem_open` symbol;
that alone does not prove named-semaphore semantics. The probe and its private
device directory were removed afterward.

Use `--phase configure --timeout 600` to configure the pinned core with the
verified wrappers, and `--phase build --timeout 1800` for a bounded build.
Both phases also require `--dependencies` naming the native Android library
prefix. Prepare that prefix with:

```sh
python3 tools/darling-android-deps.py --output /path/to/private/dependencies
```

This extracts eight version- and SHA-256-pinned packages from the official
Termux repository. It does not run package scripts. The printed prefix
contains native Android OpenSSL, libxml2, libucontext, named-semaphore support
and their dependencies. Package versions and download hashes are in
`termux-packages.json`; package notices remain in the extracted tree.
Downloading, hash verification and AArch64 ELF checks have passed locally.

The build helper compiles the named-semaphore library from the separately
pinned MIT-licensed Termux source in `semaphore-source.json`. It sets the
temporary-file directory to the selected runtime prefix and supplies an ELF
SONAME. The prebuilt package otherwise refers to Termux's private temporary
directory. The downloaded source retains its copyright and license notice;
its source and output hashes are recorded under the build's `native-deps/`.
The configured library passed the original `tests/semaphore.c` workload on
the S25+: independent-process open/post/wait, `EAGAIN`, and lifetime after
unlink. `dladdr` confirmed that `sem_open` resolved to this library. The test
and its runtime-prefix directory were removed after verification.

The PC additionally needs CMake, Ninja, Clang/LLD, Bison, Flex, pkg-config and
the host development packages for OpenSSL, libxml2, libbsd, libpng and zlib.
Darling builds its Mach-O linker and other build tools for the PC separately.

On WSL, keep the source, build, dependencies and NDK headers/libraries on the
Linux filesystem. Cross-mount header and library access has exceeded even
the compiler-probe deadline. The helper filters Windows mount entries out of
the build's `PATH`, limits nested builds to two jobs and kills the complete
command process group when its timeout expires.

Build defaults to all configured core targets; `--host-only` selects just
the launcher, mldr and darlingserver while bringing up the native side.
The helper does not install packages, set device permissions, mount anything
or start a guest. A complete runtime deployment and original-program smoke
test must succeed before attempting a simulator bootstrap.

## Current local artifacts

`out/native-runtime-2026-11-15/` contains source-location records and the
Android ABI observation. The first Windows checkout exceeded its hard
timeout. A subsequent checkout in the Linux filesystem completed at the
pinned revision with the selected core submodules and SDK symlinks intact.
Compiler and configuration results are recorded in the external build
directory; build success is not inferred merely from source availability.

The three native programs (`darling`, `mldr`, `darlingserver`) now build as
AArch64 ELF using `/system/bin/linker64`. Each loaded its dependencies and
returned its expected no-argument diagnostic on the S25+. Their hashes are in
`upstream.json`. This exercised native ELF startup only. The complete core
build and private install/package also completed, including the ARM64 Darwin
libraries, dyld and launchd.

The first headless S25+ guest boot stopped before guest execution because the
launcher required namespace handles: `/proc/self/ns/user` is absent and
following `/proc/self/ns/mnt` returns `EACCES`, while reading its link identity
works. Patch eight fixes that observed failure.

The next shell-context attempt reached the Mach server but could not bind its
filesystem Unix socket (`EACCES`). A native probe confirmed that the shell can
create ordinary files and abstract sockets but cannot bind pathname sockets.
The installed app's debug context successfully executed the same native probe
and bound both pathname datagram and stream sockets. The runtime is installed
in the app-owned prefix `/data/user/0/org.ios27.host/files/darling`. Execution
uses the installed debug app's `run-as` context without a foreground activity.

`tests/guest-startup.c` is the integrated Mach-O guest check for constructors,
arguments/environment, heap allocation, pthread TLS/destruction and Mach IPC.
It **passes natively on the S25+**, linked against the built Darwin libraries:

```text
DARWIN_GUEST_STARTUP_OK constructors=ok pthread_tls=ok mach_ipc=ok heap=ok
```

`tests/init-runner.c` supervises the guest through Darling's `DSERVER_INIT`
entry point and records the child's actual `waitpid` result: normal exit,
status zero. The external controller ends the waiting supervisor after the
test and cleans up remaining executables under the private runtime prefix.
The first successful run used package SHA-256
`304dc5b1b113d9be1a917669dd916a5c0e966b63f82680aa6b37e2bc79b85920`
and guest SHA-256
`66660caa27717fb3c2a566f59894d76702c9f9efce80436da99d706b43a93e28`.

The default launchd path executes Darwin `vchroot`, `launchd`, `launchctl`,
CoreFoundation and shellspawn initializers, but still times out waiting for
shellspawn readiness; its server log records a guest `SIGSEGV`. The passing
custom-init check does not establish that the default boot is complete.

`sim-env-exec.c` applies a bounded simulator environment immediately before
exec, after the host helper has initialized. Ten argument/environment and
rejection checks pass under Linux ASan/UBSan:

```sh
python3 integration/darling/tests/sim-env-exec.py --program /path/to/host-built/sim-env-exec
```

The full iOS 27.0 `24A434` runtime has also been extracted into the app's
private `files/ios27-runtime` directory. Device hashes match the two
SpringBoard images, both cache parts, dyld_sim, launchd_sim and xpcproxy_sim.
The native framebuffer/input helper build and its verified raw Linux IPC
adapter are described in [shims/README.md](shims/README.md).

## Remaining integration

1. Integrate the simulator-specific host services and matching dyld startup.
2. Resolve the default launchd/shellspawn startup failure.
3. Connect native graphics presentation and input to the Android host.
4. Verify actual SpringBoard startup and an Apple-rendered frame.

SpringBoard entry and an Apple-rendered UI frame remain unverified.
