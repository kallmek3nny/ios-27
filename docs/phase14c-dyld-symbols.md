# phase 14c: bounded dyld path and export metadata groundwork

scope: only src/dyld_paths.c, include/ios27/dyld_paths.h, tests/test_dyld_paths.c, src/exports.c, include/ios27/exports.h, tests/test_exports.c, and this document. all bytes in test mach-o export tries are original redistributable synthetic fixtures.

## guarantees and supported metadata

- guest-virtual absolute paths and @loader_path, @executable_path and @rpath expansion remain purely lexical; no Android filesystem access or path resolution. all string scans and results are bounded to 512 bytes, with stable first-seen runpath order and canonical duplicates removed. candidate output is committed atomically, or cleared on error.
- export trie reader recognizes symbol kinds regular, thread-local and absolute; weak definition, reexport and stub-and-resolver metadata. it rejects reserved kinds/flags, nonminimal/truncated ULEBs, invalid ordinals, cycles in the trie, duplicate names, oversized names, invalid offsets and malformed terminals. it reports original image-relative addresses, never pointers to mapped code.
- an explicit indexed synthetic dependency DAG can resolve a named symbol from a selected image via reexports, with 1-based ordinals, alias handling (empty alias means unchanged name), and a bounded 32-image graph. validation rejects cycles and unknown dependencies even when unrelated to the symbol. return 0=resolved, 1=absent, -1=invalid. pointers in the result belong to caller tables.
- weak definition is **reported as metadata**, not ranked/merged with other candidates. no host address calculation, lazy binding, dyld shared cache, real framework loading, implicit dependency lookup, closure generation, Apple SDK binaries, UIKit execution or thread-local allocation is implemented. unsupported semantics are not marked successful.

## original fixture tests

test_dyld_paths: normalization and lexical parent handling, loader/executable expansion, runpath ordering/dedup, atomic rollback after invalid later runpath, root confinement, bounded strings and invalid argument handling.

test_exports: original thin ARM64 Mach-O metadata fixture, modern and legacy export commands, basic exported symbols, reexports, stub/resolver and weak/absolute/thread-local kinds, malformed ULEB, terminal and reserved flag rejection, a three-library two-hop reexport chain, missing symbol distinction, whole-graph cycle/out-of-bounds detection and empty import-name alias resolution.

## reproducibility

run with explicit hard timeouts and the pre-existing strict compiler flags:

\`\`\`sh
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
\`\`\`

the project is synthetic userspace research and **does not run genuine ios 27 apps or frameworks**. no hardware/firmware/bootloader/privileged state is touched.
