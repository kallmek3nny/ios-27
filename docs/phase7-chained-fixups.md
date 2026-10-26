# phase 7: bounded modern dyld chained fixups

This is a userspace, fixture-only native Mach-O relocation milestone. It does **not**
execute Apple's dyld, iOS frameworks, SpringBoard, Liquid Glass or iOS apps.

## Implemented slice

- Reads one unencrypted thin ARM64 Mach-O's LC_DYLD_CHAINED_FIXUPS,
  version 0 header, per-segment page starts and uncompressed symbol imports.
- Supports DYLD_CHAINED_PTR_64_OFFSET (pointer format 6) and
  DYLD_CHAINED_IMPORT (format 1), positive library ordinals, signed
  8-bit per-pointer addends, and 4 KiB or 16 KiB chain-page declarations.
- Walks single-start chains using the 4-byte stride, rejecting page crossings,
  non-writable/non-file-backed segment targets, invalid offsets/counts,
  unsupported formats, reserved/tagged pointers, and multistart pages.
- Copies relocation values into a bounded plan. Resolves explicitly selected
  fixture symbols and stages **all** rebase/bind writes before updating the
  caller's private byte buffer; failures leave the buffer unchanged.
- Tests synthetically generated Mach-O data on both x86_64 and ARM64 Linux;
  ARM64 CI also exercises a native call through a chained-fixup-resolved
  separate synthetic provider function with W^X mapped pages.

## Intentional non-support

- arm64e authenticated/PAC pointer formats and Apple arm64e ABI.
- DYLD_CHAINED_PTR_64 (non-offset format) and other pointer encodings.
- weak imports, special ordinals, addend import tables, compressed symbols,
  MULTI starts, high8 tagged pointer rebases, shared caches, kernel caches.
- real dyld semantics, loader mapping/lifecycles, lazy dependency loading,
  ObjC/Swift runtime, Darwin Mach and Apple GPU framework services.
- applying patches directly to mapped executable regions or host files.
- root, custom firmware, Samsung boot chain, persistent device changes.

## Reproducing on trusted Linux without root

Each invocation has a hard timeout:

```sh
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --output-on-failure
```

## Next engineering work

Isolated native multi-image mapping with explicitly bounded virtual image
layouts, relocations and W^X, then testable Darwin/Mach message contracts.
Real iOS UI feasibility remains unproven and must be evaluated separately.
