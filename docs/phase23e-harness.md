# phase 23e: compiler-linked application integration and ABI truthfulness

## scope and evidence

This isolated workstream imports only published headers and links the unchanged
`ios27_core` archive. No shared CMake configuration, runtime implementation,
bootloader interface, Apple SDK, host dyld fallback or proprietary frameworks.

* Six original-source ARM64 Darwin `MH_DYLIB` images: base, math, logic,
  state, pipeline, and a deliberately unsupported constructor-bearing dylib.
* Three original-source ARM64 Darwin `MH_EXECUTE` images: calc, state and
  pipeline. Each uses a real LC_MAIN and linked LC_LOAD_DYLIB declarations.
* Executables carry argc/argv, envp and apple-vector ABI checks, 16-byte stack
  alignment assertions, independent guest-global-state invariants and 1024
  cross-library calls/state transitions. **These executable instructions are
  not reported as executed while the merged runtime cannot enter them.**
* The five-library freestanding `phase23e_pipeline_eval` path is called
  natively on ARM64 Linux/Android through the existing validated symbol API.
  The test checks every return: on each new load the result must equal
  `43 + iteration`. 100 cycles × 1024 actual calls = 102,400 verified
  native operations, only if running on ARM64. On x86_64 this count is ZERO.
* 128 reproducibly seeded malformed Mach-O magic-header mutations must be
  rejected, followed by recovery of an unmodified graph. Source bytes are
  caller-owned and a failed graph must not leak a loaded object.
* JSON output records per-app rejection status, graph size, replay hashes,
  real native operation counts, fuzz seed and host architecture.

## key limitation: executable startup is not yet an executable capability

`ios27_images_entrypoint` refuses a guest `LC_MAIN` whose parsed image
requires the unimplemented dyld startup path. Modern LLVM-linked MH_EXECUTE
images include `LC_LOAD_DYLINKER`. Even if a future graph permits entry,
the merged loader has no approved isolated guest-stack entry path for
multi-image process startup. The harness **never calls a raw `_main` export
to bypass this validation**, never counts synthetic successful launches, and
does not claim constructor, Objective-C, Mach, or thread ABI implementation.

`ios27_23e_application_probe` validates exact manifest and load-command
dependencies, guest argument bounds and image lifecycle where possible;
after startup verification it returns `UNSUPPORTED_ABI`. A parser,
dependency graph, global-data mapping, native dylib export call, and
complete OS-level app process startup are different capability levels.

The constructor-bearing library is rejected by inspecting real
`__mod_init_func` section types before any mapping. Mach/ObjC/pthread
requests are rejected explicitly until merged, verified bridges exist.
No threading/service test is misrepresented as having run.

## local test recipe

Requires clang, ld64.lld, a C11 host compiler and a built baseline core:

```sh
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Debug
timeout 300s cmake --build build --parallel 2
timeout 300s python3 tools/phase23e-build.py \
  --core build/libios27_core.a --output-dir build/phase23e --runs 100
```

The dedicated workflow tests x86_64 parser behavior and real native ARM64
freestanding library dispatch, plus ASan/UBSan builds on both runner families.
All spawned commands have hard subprocess timeouts; the GitHub job itself
also has a deadline. Generated binaries and reports stay in build/, not
shared repository source paths.

## roadmap blockers

1. Integrate a verified, independently guarded application startup API that
   deliberately handles LC_LOAD_DYLINKER and the LC_MAIN dyld handoff.
2. Establish ordered and exactly-once constructor/destructor execution before
   accepting the constructor fixture.
3. Gate thread, objc and Mach requests on concrete merged ABI integration,
   then add native application tests that invoke those services.
4. Prove arbitrary guest code process isolation before accepting untrusted
   user-provided binaries. The current native tests execute **trusted**
   original-source fixtures in-process and are not a security sandbox.

Development duration must be measured from actual engineering activity. CI
elapsed test duration is not a substitute for three hours of developer work.
