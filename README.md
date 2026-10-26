# ios-27\n\n**Target: official iOS user interface and apps running natively above Android, NOT an iOS-style launcher.** Liquid Glass/SpringBoard/Apple frameworks are not working yet. The feasibility of the complete target remains unknown.

Experimental ARM64 Darwin/iOS **userspace compatibility research** on Android/Linux, with the Samsung Galaxy S25+ as a target. This project does not install iOS, bypass Android verified boot, or alter the device boot chain.

## Device safety first

This is a **userspace-only** experiment. On modern Samsung firmware, DirtyFrag documents an Odin/download-mode **D2** restriction in some lock-screen configurations. No step here depends on DirtyFrag, root, bootloader access, a boot-chain change, or partition writes. Root is never required.

**Do not attempt a system/bootloader flash** as part of this project. Read [the safety and architecture notes](docs/architecture.md).

## Milestones

Phase 2 adds a tiny Darwin ARM64 syscall probe. It recognizes a handful of instructions and can interpret carefully constructed Mach-O fixtures that invoke getpid/getuid or virtual exit. It denies filesystem and Mach operations, has a strict instruction limit, and cannot run real iOS frameworks. This is a test harness, not an iOS 27 port.

## Phase 4: native dyld path groundwork

A new dependency-free resolver maps Apple's `@loader_path`,
`@executable_path`, and `@rpath` install-name expressions into guest-virtual
candidate paths. This is pure string processing: it never accesses host
filesystems or interprets/emulates CPU instructions.

```sh
./build/ios27 resolve-dyld '@rpath/UIKit.framework/UIKit' \
  '/Applications/Demo.app/Demo' '/Applications/Demo.app/Demo' \
  '/System/Library/Frameworks'
```

Only path resolution is implemented, not dyld loading, rebases, chained binds,
imports, exports, shared-cache mapping, code-signature validation, or actual
iOS framework execution.

## Phase 3: audit real framework dependencies

The new `audit-ios` command accepts thin ARM64 and universal FAT/FAT64
Mach-O files and lists direct dynamic-library imports such as UIKit and
private frameworks. It is read-only and runs without root. It does **not**
execute Apple binaries or make their libraries available.

```sh
./build/ios27 audit-ios /path/to/legitimately-obtained-unencrypted-Mach-O
```

The official iOS system UI needs a real dyld, Mach/XNU services, runtime
frameworks, CoreAnimation/Metal graphics bridging and SpringBoard services.
See [the official UI roadmap](docs/official-ios-ui-roadmap.md).

## First milestone

Build a native, dependency-free **Mach-O 64-bit ARM64 inspector** with strict bounds checks, then a deliberately restricted **freestanding execution proof of concept** for hand-written ARM64 test images with no iOS frameworks, dyld, system calls, or runtime dependencies.

A functioning iOS 27 SpringBoard, Apple services, App Store apps, and graphics framework are **not** implemented. Root is not needed for inspection and should not be assumed to make iOS binaries compatible.

## Build

Requires CMake 3.16+ and a C11 compiler. On Android, use an ARM64 native toolchain (such as Termux clang); on other platforms, inspection and parser tests work, but execution is ARM64-only.

```sh
cmake -S . -B build -DIOS27_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/ios27 inspect path/to/arm64-binary
./build/ios27 probe-darwin path/to/synthetic-test-macho\n./build/ios27 audit-ios path/to/arm64-or-fat-macho
```

See [docs/architecture.md](docs/architecture.md) for design and limitations. Avoid running untrusted Mach-O code: the freestanding runner has no sandbox and is intended only for deliberately constructed local test fixtures.
