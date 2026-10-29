# phase24e: bounded native linked application startup

## implementation boundary

This phase is a **new standalone harness**, not changes to shared core,
CMakeLists.txt, HANDOFF.md, phase23e, or any phase24a-d owned files. It
uses only APIs on the starting merged main:
`ios27_macho_parse`, `ios27_macho_entry_vm`,
`ios27_dyld_inspect_image`, `ios27_exports_parse`,
`ios27_images_load_with_paths`, `ios27_images_symbol`, and
`ios27_images_unload`. It intentionally does not call the unverified
`ios27_images_entrypoint` for dyld-linked binaries.

The standalone bridge permits only **trusted original-source, SDK-free,
compiler-linked ARM64 Darwin binaries** with an exported `_main` in
an executable segment, exactly at the parsed LC_MAIN target. It verifies
the mapping and all imports through the merged transactional graph loader.
This is **not** implementation of the Apple dynamic linker, platform
process entry, or a security boundary for arbitrary binaries. The
presence of LC_LOAD_DYLINKER does not cause Apple dyld code to execute.

## milestones and behavior

1. Strict catalog, dependency manifest, bounded argc/argv/envp/apple
   payload, startup contract, duplicate image, LC_MAIN and export
   identity validation. Invalid and unsupported images fail closed.
2. Multi-image relocations and executable W^X mapping via the existing
   merged loader. Real guest `main(argc,argv,envp,apple)` uses an
   isolated 1 MiB mapped stack with guard pages and independent string
   copies on ARM64 Linux/Android; each invocation allocates a fresh graph.
3. Guest-owned counter `_phase24e_guest_operations`, verified exit
   code, 100 repeated startups of each of three independently linked
   executables, cross-library function and shared-global assertions.
   The expected total on native ARM64 is 512,000 guest-verified
   operations. On x86_64, native operation count is **zero**.
4. Negative tests: 128 seeded malformed header mutations, native
   constructor/destructor section rejection, ObjC/thread/Mach/syscall
   interface rejection, deterministic replay hashes, and zero live
   graph and stack resources after any completed call.
5. Dedicated 4-job PR workflow on x86_64 and ARM64 with strict
   `-Werror`, normal and ASan/UBSan configurations, source-linked
   original ARM64 Mach-O fixtures, and machine-readable JSON report
   artifacts.

## exact native evidence rules

Only `ios27_24e_run` on an actual ARM64 Linux/Android host may set
`actual_guest_entry` and count guest-owned operations, and it only
does so after guest return, result verification and counter equality.
The 3 applications are calc (3072 checked operations/startup),
state (1024/startup), and pipeline (1024/startup). Direct guest function
calls and global state checks occur inside the compiled apps, not
synthetic host callbacks. Guest code mutates private argv/env/apple
copies without modifying host inputs.

The replay hash is pointer/ASLR-independent, covers the supplied
image bytes and guest inputs, and includes checked native results.
Structured reports explicitly distinguish real guest code from
metadata-only work, unsupported hosts, unsupported ABI, malformed
binaries, link failures, and guest return mismatch. These are
verification checkpoints; there is **no instruction-by-instruction
tracer**, so `instruction_trace_available` remains false.
Function-call assertions and counters do not establish an exact
instruction count.

## explicit blockers

- Compiler-produced initializers, destructors and
  `__init_offsets` remain unsupported until a verified lifecycle
  API is **merged**. The `ctor` dylib is a **negative** fixture.
- General libsystem, ObjC, pthread, Mach services, syscalls, exception
  unwinding, TLS and arbitrary Apple frameworks remain unsupported for
  app startup. No unmerged phase24a-d symbols are used.
- Isolation is graph/guest-stack lifetime isolation **inside the host
  process**, not kernel process isolation. A malicious guest may
  access host address space; never execute untrusted Mach-O images.
- Per-instruction execution traces, real dyld process bootstrap and
  complete Darwin compatibility are not claimed.

## bounded reproduction

```sh
timeout 120s cmake -S . -B build/phase24e-core -DIOS27_BUILD_TESTS=OFF -DCMAKE_C_FLAGS="-Wall -Wextra -Wpedantic -Werror"
timeout 300s cmake --build build/phase24e-core --parallel 2
timeout 300s python3 tools/phase24e-build.py --core build/phase24e-core/libios27_core.a --output-dir build/phase24e-results --runs 100
```

The test tool gives every compiler/linker/test subprocess a hard
timeout. PR CI additionally bounds checkout commands, dependency
installation, configuration, build and test duration. Inspect the
exact PR frozen-head four-job workflow and its JSON artifacts before
attributing any numerical native results.

## session duration and release gate

The requested three hours of genuinely active engineering are **not
established** by this session. No waiting, CI runtime, or speculative
future development is counted as human-like coding time. Treat the
PR as review-only until ARM64 native execution is independently
confirmed with green CI on its frozen head and main's SHA is verified
unchanged. Never merge from a stale main or on partial CI success.
