# phase 19a: bounded native arm64 darwin startup

this phase executes *trusted, freestanding, sdk-free* clang-produced darwin
`MH_EXECUTE` code directly on arm64 linux/android hardware. it does not
emulate arm64, load apple frameworks, or claim arbitrary ios app support.

## implemented

- `ios27_run_executable_context` enters darwin `LC_MAIN` with x0=argc,
  x1=argv, x2=envp, x3=apple. the legacy argv-only entrypoint remains.
- arguments, environment and explicit apple strings are copied into a fresh,
  16-byte-aligned, private stack with unreadable guard pages. none of the
  host process environment, paths or tokens is injected implicitly.
- vectors each have 128-entry limits, each string 4095 bytes plus terminator,
  and all vectors combined have a 64-kib payload limit. the requested stack
  is capped at 8 mib; invalid vectors do not allocate or expose mappings.
- the executable is independently staged in rw, then sealed to its final
  segment protection before entry. no writable+executable pages are allowed.
  sparse gaps remain inaccessible, and zero-fill sections are cleared even
  if their nominal file region contains dirty bytes.
- exit results are committed only after unmapping both the code image and
  guest stack. every invocation starts with fresh globals and bss.
- `LC_MAIN` may not target an rx literal section, even if its enclosing
  `__TEXT` page is executable.
- both 4-kib and 16-kib loader granules are validated in deterministic tests.

## compiler-produced integration

`tests/fixtures/phase19a-process.c` supplies recursion, cross-function
branches, an aligned guest stack, cstring literals, const globals, initialized
writable globals and a 256-byte bss. its actual clang relocations
(`ARM64_RELOC_BRANCH26`, `PAGE21`, `PAGEOFF12`, `ADDEND`) are resolved
by llvm ld64.lld when available, or by the intentionally strict
`tools/phase19a-link-fallback.py` fixture-only linker. the fallback refuses
unknown sections, unresolved relocations and unexpected source structure.

the existing compiled executable ctest **requires** this new workload, runs
32 fresh native invocations on arm64, verifies private mutations and exit
codes, and checks rejected malformed `LC_MAIN` and page permission metadata.
neither `CMakeLists.txt` nor another agent's files are modified.

to run from repo root:

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 300s cmake --build build --parallel 2
timeout 180s ctest --test-dir build --timeout 30 --output-on-failure
```

## deliberately unsupported

darwin tls/thread-local variable descriptors, x18-based thread-local storage,
dyld initializers, objc, darwin syscalls, host-guest exception isolation and
untrusted process sandboxing remain unsupported. proprietary binaries and
frameworks are not loaded. normal entrypoint return is required. do not
mistake this narrow C ABI for an ios process loader.
