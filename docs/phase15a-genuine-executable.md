# phase 15a: sdk-free native arm64 Darwin executable

The fixture is original, freestanding C source compiled with Clang to real
`arm64-apple-darwin` `MH_OBJECT` inputs, linked with the open-source
LLVM `ld64.lld` Mach-O linker. No Apple SDK, proprietary binaries, or device
modifications are needed.

## Reproduce

With CMake, Clang (Darwin ARM64 target) and Python 3 installed.\nIf `ld64.lld` is installed, it is used preferentially. Otherwise the\nlimited in-tree original-source Python linker resolves the one ARM64\nBRANCH26 relocation into a genuine, SDK-free `MH_EXECUTE` fixture:\n

```sh
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
llvm-objdump --macho --private-headers build/phase15a-executable
```

The build script compiles the two independent C files with `-ffreestanding`,
`-fno-builtin`, `-fno-stack-protector`, unwind suppression and `-O2`.
A cross-object function call requires an external ARM64 BRANCH26 relocation.
The linker resolves the call and emits the genuine `MH_EXECUTE` image with
`LC_MAIN`, `LC_BUILD_VERSION`, `__PAGEZERO`, `__TEXT` and
`__LINKEDIT`; the output has no unresolved external imports.

Some Swift-distributed builds of `ld64.lld` reject *any* input object
bearing `LC_BUILD_VERSION`, reporting "This version of lld does not
support linking for platform macOS". The build script retries **only** for
this specific diagnostic: it removes the *object's* platform load command
without altering section contents, relocation data or symbol offsets. The
linker's output still contains a genuine macOS `LC_BUILD_VERSION`.
Upstream LLVM LLD ordinarily links the untouched compiler objects.

## Validation boundaries

`ios27_darwin_executable_inspect` is inspection-only and fail-closed.
It validates the real linked file's ARM64 Mach-O header, `LC_MAIN` entry
inside `__TEXT,__text`, permissions (W^X), bounded file/VM segments and
nonoverlap, section ranges and relocation counts, load-command allowlist,
symbol tables, zero undefined symbols and zero chained-fixup imports.
Mutation tests cover malformed offsets, protections, entrypoints,
relocations, import metadata and truncated files. On ARM64 Linux the
trusted test copies only the validated internal `__text` section into
W^X-mapped native memory, calls the resolved entry and expects 43.
No untrusted file, Apple app, dyld process or iOS service is executed.

**Not an iOS runtime**: A genuine Mach-O file and successful object
linking do not imply SpringBoard, iOS frameworks, dyld, codesigning, ABI or
real iOS apps can execute on Android. This stage never touches root,
bootloader, firmware or partitions.
