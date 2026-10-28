# phase 18a: isolated arm64 darwin LC_MAIN startup

## boundary

This is **native**, in-process execution of original-source, freestanding Clang-linked ARM64 Darwin `MH_EXECUTE` on ARM64 Linux/Android. It is **not** a complete Darwin process, isolation/security sandbox, Mach syscall subsystem, dyld, Objective-C runtime, SpringBoard, or iOS. Do not execute untrusted, third-party or proprietary Apple binaries. No root, firmware, or bootloader changes.

## entry ABI and stack

* `ios27_run_executable_argv` presents `main(int argc, char **argv, char **envp, char **apple)` using AArch64 registers x0–x3 and a 16-byte-aligned SP. The apple vector is a supported empty vector; no dyld/apple-specific variables are invented.
* A dedicated assembly entry trampoline saves host x29/x30 and x19/x20, switches to a fresh guest stack, calls the validated LC_MAIN entry, then restores the host stack after a **normal** return. The C compiler never sees a live changed SP.
* The guest stack is private anonymous memory, bounded to an 8 MiB maximum explicit LC_MAIN stack request; zero stacksize means a 1 MiB default. There is a `PROT_NONE` guard page at each end. Argument and environment strings and NULL-terminated pointer vectors are copied into writable guest stack memory before entry. The host's argv/environment are not borrowed.
* Startup input is bounded to 128 argument entries, 128 environment entries, individual strings under 4096 bytes, 64 KiB total string payload, and at least 16 KiB of remaining stack headroom. Invalid requests fail without mapping guest executable memory or modifying the caller's exit code.
* Both 4 KiB and 16 KiB image-layout preflight reject an LC_MAIN stack request above the implemented limit. Existing W^X transaction, ELF-host runtime gating, BSS zeroing, segment isolation, and rollback remain in effect.

## actual compiler-produced integration

`tests/fixtures/phase18a-startup.c` exercises ABI arguments, stack alignment, a 2 KiB volatile guest stack frame, copy isolation (writes argv/envp in guest), multi-segment mutable/readonly data and BSS, and successful native return of 42. The phase18a builder compiles using the open Clang `arm64-apple-darwin` target, links with open-source ld64.lld when available and otherwise uses the existing intentionally narrow phase17a fallback (only for its supported exact section/relocation profile). It has bounded subprocess timeouts and requires no CMake changes. The existing `compiled-darwin-real-mh-execute` CTest starts the fixture builder, checks 4K/16K preflights on both CI architectures and executes it twice on ARM64. Negative cases include wrong arguments, invalid argc, oversized LC_MAIN stack, and non-W^X layout. The original compiled fixtures remain exercised unchanged.

## supported / unsupported

No Darwin dynamic dependencies, TLV/TLS initialization, dyld-managed sections, stack unwinding across entry, guest fault recovery, syscalls, or `exit()` are supported. A guest may return an integer but must not perform process termination or trap. Using a host's TLS register for Darwin TLS would corrupt the Linux thread state and is deliberately not attempted. Full process lifecycle isolation and Darwin thread creation require future out-of-process runtime architecture and distinct syscall/ABI implementations.

## bounded commands

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 300s cmake --build build --parallel 2
timeout 180s ctest --test-dir build --timeout 30 --output-on-failure
```
