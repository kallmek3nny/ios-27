# phase 18b: bounded native Darwin dylib graphs

Phase 18b introduces **explicit guest-path dyld resolution** without ever
opening the host filesystem or loading an Apple binary. The existing
`ios27_images_load` API remains unchanged; new
`ios27_images_load_with_paths` accepts canonical absolute guest paths for
each short-name image spec and a guest main-executable path.

For `@loader_path`, `@executable_path` and `@rpath`, image-local
`LC_RPATH` values are evaluated in declared order, followed by runpaths
from the supplied main image (expanded in the declaring main image context).
Only an exact catalog match can satisfy a dependency. Unknown mandatory
libraries, duplicate/unnormalized guest paths, malformed tokens, unsupported
dyld features, cycles, or a bad ordinal fail the entire transaction.
Absent weak libraries may be omitted only with weak fixup imports.
A dependency-first initialization *plan* is exposed, but arbitrary guest
constructors are **not automatically called**. This distinction matters for
safe future Darwin process startup.

Exports can cross explicit trie aliases and `LC_REEXPORT_DYLIB` forwarding,
with a strict graph-depth bound. Classic and chained bindings complete in
private staging buffers before the mappings are exposed. Chained references
in sealable `__DATA_CONST` with writable `maxprot` are staged, then sealed
to their requested read-only runtime permissions. No RWX window, implicit
host-library fallback, emulator, root change or bootloader edit is involved.

## Real compiler/linker integration

`phase18b-build.py` uses Clang, ld64.lld and original source C. Three real
dylibs form a two-ordinal graph: Top imports Middle and Leaf; Middle also
imports Leaf, including an initialized global. Each link uses only a minimal
TAPI symbol stub for the *dependency*, without copying any Apple SDK code.
The standalone verifier checks the genuine LC_LOAD_DYLIB/LC_RPATH graph,
mapping addresses, initialized globals (40 and 2), dependency ordering and
failed-transaction recovery. On ARM64 Linux/Android it directly runs machine
code through all three dylibs and requires the final result **167**.

All commands have hard time limits:

```sh
timeout 120s python3 tools/phase18b-build.py --output-dir build/phase18b
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON
timeout 300s cmake --build build -j 2
timeout 180s ctest --test-dir build --timeout 45 --output-on-failure
timeout 120s cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wpedantic \
  -Iinclude tests/fixtures/phase18b-verify.c build/libios27_core.a \
  -o build/phase18b-verify
timeout 90s build/phase18b-verify \
  build/phase18b/phase18b-top.dylib \
  build/phase18b/phase18b-middle.dylib \
  build/phase18b/phase18b-leaf.dylib
```

The existing `test_images` CTest target also covers token resolution,
canonicalization failures, source-byte preservation, topological order and
transactional cleanup, so no CMakeLists changes are required.

## not yet supported

This is **not** full dyld or UIKit/SpringBoard compatibility. Arbitrary
nested inherited `@rpath` chains beyond the main image, Darwin process TLS,
authenticated arm64e pointers, process initializer execution and Apple
framework APIs are not yet implemented. Those limitations remain fail-closed
rather than silently executing unvalidated guest code.
