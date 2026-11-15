# Native Android runtime integration

The target remains **native ARM64 on the S25+**. The user explicitly retained
that requirement when offered an x86-64 PC simulator. CPU translation is not
part of this integration.

## Why this backend

Upstream Darling now contains native ARM64 support, Android install-layout
handling and a `DARLING_NONROOT=1` mode that avoids namespaces. These provide
a substantially more complete starting point for Darwin process, thread,
Mach IPC and libSystem support than the project's small fixture runtime.
This is source capability, not proof that Darling or SpringBoard boots here.

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
Linux ASan/UBSan. On the S25+ it reached the final foreign-FIFO fixture, whose
creation Android denied with `EACCES`; the **whole device test did not pass**.
The test and implementation are original and based on the linked POSIX
contract. File backing consumes runtime filesystem space; it is not a tmpfs.

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
`upstream.json`. This exercised native ELF startup only. The guest-side
Darwin libraries and a complete Darling boot remain under development.

## Remaining integration

1. Establish a complete native Android core build and namespace-free startup.
2. Verify original Darwin programs against its process/thread/Mach interfaces.
3. Integrate the simulator-specific host services and matching dyld startup.
4. Connect native graphics presentation and input to the Android host.
5. Verify actual SpringBoard startup and an Apple-rendered frame.

No Darling guest, SpringBoard entry point or Apple UI frame has yet been
verified through this integration.
