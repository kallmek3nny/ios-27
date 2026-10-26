# Architecture and reality check

## Goal

Investigate running genuine, legally obtained ARM64 Darwin/iOS userland components **as processes inside Android** on the Galaxy S25+. Keep Android's verified boot, recovery, modem, and everyday UI unchanged.

## Device safety: Samsung S25+ / DirtyFrag

The documented DirtyFrag implementation provides **ephemeral** root (lost on reboot). Its README specifically recommends read-only partition protection. Newer Samsung firmware can report a **D2 error when entering Odin/download mode if a lock-screen credential is set**; see the upstream project's releases and Odin information. That error is a recovery/download-mode restriction, not proof of bootloader corruption.

**Hard project constraints (not just suggestions):**

- No use of `su`, DirtyFrag, KernelSU, `insmod`, kernel patches, or privileged startup in any ios-27 execution path.
- No writing or flashing `boot`, `init_boot`, `vendor_boot`, `vbmeta`, `dtbo`, `recovery`, or other boot/verified-boot partitions.
- No Odin/download-mode adjustment, unlock attempt, antirollback change, or bypass of the D2 protection.
- No automatic launch at boot. Only voluntarily started ordinary Android/Linux userspace binaries.
- Diagnose actual risk separately from kernel crashes: a reboot clears DirtyFrag's temporary root but **cannot undo persistent writes** made while rooted.

Sources: https://github.com/mitschud/DirtyFrag and https://github.com/mitschud/DirtyFrag/releases

## Implemented in phase 2

- Small, explicitly bounded **interpreter** for ARM64 MOVZ, MOVK, NOP, SVC #0x80, and RET, applied to thin freestanding Mach-O test images.
- Darwin BSD syscall-class decoding via `x16`, including host-mapped identity queries `getpid`, `getuid`, `geteuid`, `getppid`, `getgid`, `getegid`; `exit` is a virtual guest-only action.
- Unsupported calls, including file operations and Mach traps, are rejected with a Darwin-style ENOSYS result (carry set).
- Instruction limit, executable-segment-only instruction fetch, and static rejection of images needing dyld, dylibs, fixups, or decryption.
- Runnable `ios27 probe-darwin <synthetic-macho>` command that works without root or native ARM64 execution.

This is **not** a real iOS application runtime. The interpreter has almost no instruction-set coverage, supports no guest memory accesses, and intentionally cannot access any device partitions.

## Implemented in phase 1

- Dependency-free, C11 Mach-O 64-bit **thin ARM64** header/load-command parser.
- Rejects truncated input, malformed command sizing, invalid segment ranges, malformed dylib names, and duplicate entry commands.
- Reports file type, segments, entry offset, linked-library requirement, dyld requirement, fixups, encryption status and Apple build version metadata.
- Extremely limited AArch64 *in-process* mapper: reserves a bounded address range, places segments, enforces writable/non-executable mapping followed by final protections, and calls an `int(void)` entry point.
- Synthetic fixture executes an ARM64 `mov w0,#42; ret` instruction pair in the test runner **only on ARM64**. It is not an iOS application.
- Host-side parser tests run on ordinary Linux desktops.

## NOT implemented

- iOS 27, SpringBoard, UIKit, Foundation, ObjC, dyld, complete Darwin syscalls or Mach IPC.
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
2. Introduce an isolated process runner and expand the deny-by-default Darwin syscall boundary.
3. Resolve dyld/loader relocations for test binaries with a synthetic compatibility library.
4. Prototype Mach messaging and a first tiny, legal Darwin userspace test program.
5. Only then evaluate graphics and framework bridges.
