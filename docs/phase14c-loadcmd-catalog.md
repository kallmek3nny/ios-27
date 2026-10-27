# phase14c: Mach-O runpath metadata and explicit guest image catalog

## implemented
- Decode actual thin ARM64 Mach-O LC_RPATH, LC_ID_DYLIB and five documented dylib import forms, preserving load-command dependency ordinal order.
- Bound command/string offsets and NUL terminations; cap at 16 runpaths, 32 dependencies and 512 bytes per name. Reject duplicate IDs and incomplete tables transactionally.
- Match decoded imports against explicit guest virtual image paths, with first-rpath precedence, canonical path deduplication and rejected ambiguous catalog identities. A match is a catalog index, not a loaded framework.
- Distinguish not found (return 1) from invalid catalog/traversal (return -1), with zeroed output on all non-success results.

## regression coverage
The pre-existing dyld test target gains original SDK-free ARM64 Mach-O load command fixtures for every decoded command kind; invalid offsets, missing NUL, duplicate LC_ID_DYLIB, oversized counts and transactional rollback. Guest catalog matching is tested for precedence independent of catalog order, miss, duplicate normalized identities and invalid later entries.

## limitations
No Apple proprietary files, framework execution, dyld cache, actual dynamic linking, symbols, code signing, symlink evaluation, Android host path lookup or privileged phone operations. The caller must supply the virtual catalog and guest loader/main identities explicitly.

## reproducibility
- timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
- timeout 120s cmake --build build --parallel 2
- timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
