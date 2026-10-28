# phase 20a: independent arm64 clang objc2 metadata and image registration

## scope

- parses actual clang-created `arm64-apple-macos` MH_OBJECT files, traversing relocation-backed `__objc_classlist`, `__objc_protolist`, `__objc_catlist`, and `__objc_selrefs`.
- validates ObjC2 class and metaclass layouts, superclass links (including external `_OBJC_CLASS_$_` symbols), class/instance method lists and text IMP addresses, protocol method descriptions and optional methods, ivar offset cells and declared sizes, property attributes, adoptions, and class categories.
- maps relocations to bounded section/symbol references. Uses little-endian loads, bounds checks, duplicate checks, quotas, per-image selector deduplication, and cleared outputs on rejection.
- has an isolated registration layer with staging and atomic commit, forward parent resolution, category target requirements, protocol adoption validation, duplicate rejection, cycle detection, and global resource caps.
- original SDK-free Objective-C fixtures test real clang graph metadata, inherited classes, external superclass and categories, protocol declarations, ivars, property metadata, and both `-O0` / `-O2`. Host test mutation corpus and registry exhaustion/rollback run without the project CMake.

## independent checks

```sh
timeout 180s python3 tools/phase20a-standalone.py
timeout 180s env PHASE20A_SANITIZE=1 python3 tools/phase20a-standalone.py
```

The script applies bounded subprocess timeouts and compiles `objc20a_*.c` using `-std=c11 -Wall -Wextra -Werror -pedantic`. It requires clang with `--target=arm64-apple-macos11`, but no Apple SDK or proprietary binaries.

## integration boundaries

This **inspects compiler output**, not an executable iOS app. MH_OBJECT references are resolved through local symbol/relocation records or retained as external superclass/category names. This code **does not** perform dyld chained fixups, mapping, IMP execution, ABI-callable `objc_msgSend`, ObjC `+load`, runtime superclass/layout fixups, arm64e PAC, relative/small method lists, Swift metadata, or system framework loading. No execution of untrusted code occurs. Graph registration is a bounded host-side model, not a drop-in libobjc. Dependencies must be registered before a dependent image (within-image parent forward declarations are allowed). Object size/ivar offsets are reported as compiled, not finalized for runtime allocation.

All files are new and scoped to `objc20a_*`, `phase20a-*`, and this document. `CMakeLists.txt`, `HANDOFF.md`, and other agents' files are not modified. Standard repo CI does **not** automatically exercise these new tests until a dedicated integration owner adds CI registration. Standalone tests are the authoritative phase20a checks.
