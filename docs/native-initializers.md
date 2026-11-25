# Native C image-graph startup

The image graph now has an explicit startup API:
`ios27_images_run_contained(images, root, context, options, result, error, size)`.
It connects the existing Mach-O mapper, dependency order, guarded Darwin stack
and supervised native ARM64 entry trampoline. This is original-source C
runtime infrastructure; **it does not start SpringBoard or render Apple UI**.

## Implemented path

1. `ios27_initializers_inspect` reads modern `S_INIT_FUNC_OFFSETS` /
   `__TEXT,__init_offsets` tables. Each 32-bit value is relative to `__TEXT`'s
   preferred VM address, not its file offset. Tables are bounded to 256 entries
   per image. Every target and `LC_MAIN` must be an aligned instruction in a
   file-backed RX code section. Malformed input clears the entire output.
   Ordinary 12-byte ARM64 symbol-stub sections are accepted as image content,
   but are excluded from initializer and LC_MAIN target ranges.
2. The graph runner validates **all** startup metadata in the selected root's
   transitive closure before dispatching anything. It preserves dependency-first
   image order and within-image table order. Unrelated catalog images are omitted.
3. A private startup stack receives explicit argv, envp and apple vectors.
   Initializers also receive Darwin's five-pointer `ProgramVars` record in x4:
   the mapped main header and pointers to private argc, argv, environment and
   program-name cells. The supervised child enters each initializer through
   the ARM64 trampoline, then enters four-argument main on the same guest stack.
4. Constructors and main both run under the existing syscall filter, CPU limit,
   signal reset and deadline. Faults, raw syscalls and loops produce the existing
   supervised outcomes. The parent retains its original COW image state, so the
   next invocation initializes a fresh child rather than reusing mutated globals.

Only trusted, original SDK-free C fixtures are supported. The caller keeps the
graph and immutable source buffers alive for the whole synchronous call. The
single-threaded launcher/default-SIGCHLD restrictions of
[contained execution](contained-native-process.md) apply.

Legacy initializer pointer tables, `LC_ROUTINES_64`, termination hooks, Darwin
TLS, Objective-C and Swift startup requirements are detected and refused by
this runner. Mapping a file still does not implicitly run constructors.
General dylib initialization and unloading are not supplied by this API.

## Genuine compiler-produced fixtures

`tools/initializer-build.py` uses Clang and LLVM's Mach-O linker to create:

- `Provider.dylib`: two constructors in table order, establishing a global value.
- `Main`: an importing constructor that depends on the provider's initialized
  value, and a four-argument main that returns **42 only after all three ran**.

The provider independently declares the documented `ProgramVars` layout. It
checks the root executable header and all four variable cells, mutates the
argc/name cells in its first constructor, and verifies/restores them in its
second. This checks the actual fifth-argument ABI on native execution. The
layout and its consumers are documented in Apple's open-source
[DyldProcessConfig.h](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/DyldProcessConfig.h#L91)
and [crt_externs.c](https://github.com/apple-oss-distributions/Libc/blob/main/sys/crt_externs.c).
The record and mutable cells occupy guest stack storage; they do not point at
host globals. The ordinary service-table x4 interface remains separate.

No Mach-O instruction bytes are fabricated. The builder verifies that LLVM
emitted two and one modern initializer offsets respectively. It requires a real
LLVM Mach-O linker; there is no synthetic fallback. Dedicated constructor modes
trap, loop or issue a raw Linux syscall, followed by a normal recovery case.

`compiled-darwin-initializers` verifies real linked metadata, mapped addresses,
catalog reversal, failure clearing, unsupported runtime markers and corruption
cases on the host. `contained-native-initializers` is separate: it verifies
actual constructor dispatch, ordering, result 42, unchanged parent globals and
supervised failures on **native ARM64**. It skips on x86_64 and on ASan builds
whose runtime is incompatible with the existing guest filter. A host metadata
pass is never counted as an ARM64 execution pass.

## Local verification, 2026-11-15

- WSL x86_64, 4 KiB pages, Clang 21.1.8, strict warnings preserved.
- Full Debug CTest: **46 passed, 7 skipped, 0 failed** (53 entries). The added
  native-execution test is one of the architecture skips.
- Direct Clang ASan+UBSan builds of the inspector/mapper and guest-stack tests:
  passed with leak detection enabled, including `ProgramVars` bounds, cell
  isolation, empty arguments and failed/double attachment. Two initial sanitized CMake
  configuration attempts did not complete; no successful full sanitized CTest
  run is claimed for this revision.
- Android API 30 / NDK 29 ARM64 native runner and loader compile/link passed.
- **Physical S25+ (SM-S936U1), Android API 37, 4096-byte pages:** the metadata
  and native initializer tests passed under ordinary adb shell UID 2000. The
  native test completed eight fresh startups plus trap, loop, blocked syscall
  and recovery cases. The updated five-argument `ProgramVars` fixture also
  passed, including main return 42 and unchanged parent state.
- Uploaded executable/fixture hashes were checked on-device. The three files
  and their unique temporary directory were removed after each run. No APK
  installation, root operation, device-setting change or Apple-code execution
  was involved. [Sanitized device evidence](s25-initializers-2026-11-15.json).
- Real 16 KiB hardware and frozen-head CI remain unverified. The LLVM workflow
  requires the native test to pass without skipping on ARM64 when submitted.

Reports and the direct sanitized reproduction script are under ignored
`out/native-initializers-2026-11-15/`; final `ProgramVars` host/device results
and the bounded device script are under `out/s25-startup-2026-11-15/program-vars/`.
Two concurrent WSL build attempts were interrupted; a single-job completion
and final full CTest run passed. These measurements describe the local source
snapshot; [PR #126](https://github.com/kallmek3nny/ios-27/pull/126) tracks the
published revision and its separate CI results.

```sh
timeout 180s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 180s cmake --build build --target test_initializers --parallel 2
timeout 60s ctest --test-dir build -R initializers --output-on-failure
```

## Relation to SpringBoard

The [real-input loading checkpoint](springboard-loading.md) is still
non-executable. These constructor calls address one missing C startup mechanism;
they do not provide Apple's libSystem/Simulator host ABI, dyld cache override
patches, Objective-C/Swift registration, BackBoard/FrontBoard, or the
CoreAnimation/IOSurface/Metal presentation bridge. Those remain blockers to
the requested genuine UI.
