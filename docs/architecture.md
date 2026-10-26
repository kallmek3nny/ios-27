# Architecture and reality check

## Goal

Investigate running genuine, legally obtained ARM64 Darwin/iOS userland components **as processes inside Android** on the Galaxy S25+. Keep Android's verified boot, recovery, modem, and everyday UI unchanged.

## Implemented in phase 1

- Dependency-free, C11 Mach-O 64-bit **thin ARM64** header/load-command parser.
- Rejects truncated input, malformed command sizing, invalid segment ranges, malformed dylib names, and duplicate entry commands.
- Reports file type, segments, entry offset, linked-library requirement, dyld requirement, fixups, encryption status and Apple build version metadata.
- Extremely limited AArch64 *in-process* mapper: reserves a bounded address range, places segments, enforces writable/non-executable mapping followed by final protections, and calls an `int(void)` entry point.
- Synthetic fixture executes an ARM64 `mov w0,#42; ret` instruction pair in the test runner **only on ARM64**. It is not an iOS application.
- Host-side parser tests run on ordinary Linux desktops.

## NOT implemented

- iOS 27, SpringBoard, UIKit, Foundation, ObjC, dyld, Darwin syscalls or Mach IPC.
- Code signing, Apple MobileFileIntegrity, protected binary decryption, arm64e pointer-authentication emulation.
- Interoperation with Android UI, audio, GPU, modem, storage or permissions.
- Native XNU kernel. DirtyFrag/KernelSU root is optional for inspection and neither unlocks the bootloader nor provides XNU APIs.
- A secure sandbox for foreign code.

## Rationale

Most iOS executables have `LC_LOAD_DYLINKER` and `LC_LOAD_DYLIB` commands. These will be **rejected** by the restricted runner. Real iOS execution would require Darwin ABI translation, dyld/linker behavior, frameworks, IPC and bridging. Treat a parsed Mach-O as a *data artifact*, not evidence that it can execute.

## Android / Termux example

In an ARM64 Termux environment with clang and cmake installed:

```sh
cmake -S . -B build -DIOS27_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/ios27 inspect ./a-real-arm64-macho
```

`run-freestanding <file> --unsafe` executes code in the current process. **Only run your own reviewed and specially-built fixtures**, not downloaded iOS executables. No root is required and there is no security isolation.

## Next milestones

1. Expand parser support for fat binaries, dylib metadata and export/import inventories.
2. Introduce an isolated process runner and a narrow Darwin syscall compatibility boundary.
3. Resolve dyld/loader relocations for test binaries with a synthetic compatibility library.
4. Prototype Mach messaging and a first tiny, legal Darwin userspace test program.
5. Only then evaluate graphics and framework bridges.
