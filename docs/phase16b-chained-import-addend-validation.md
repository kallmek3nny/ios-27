# phase 16b: native graph binding and rollback validation

This phase advances **trusted, explicit, SDK-free** ARM64 Mach-O test images only.
It does not implement Apple's dyld, PAC/arm64e, sandboxing or the Darwin ABI,
and cannot load official iOS frameworks or SpringBoard yet.

## Supported

- Chained 64-bit offset pointers with 4 KiB and 16 KiB encoded page sizes.
- Basic (format 1) and signed 32-bit addend (format 2) chained imports.
- Positive dylib ordinals resolved against an ordered, caller-supplied DAG.
- Runtime rebases constrained to actual mapped image segments, including
  mapped zero-fill storage, with bounds rechecked before the first write.
- Native export addresses in mapped RX text, read-only data, and writable data.
- Early rejection of out-of-image rebase targets, overlong page descriptors,
  unknown pointer formats, weak imports, and symbol resolution failures.
- Fully staged binding and relocation before memory mapping is writable and
  then sealed to the original segment permissions. On failure, the graph is
  unmapped and the output handle remains null.

## Deliberate fail-closed scope

Arm64e authentication, chained import format 3, multistart pages, tagged
rebases, weak/reexport/upward or lazy dependency modes, unsupported special
ordinals, and encrypted/proprietary binaries remain unsupported. This work
does **not** authorize running downloaded executables.

## Tests

`tests/test_chained.c` covers import addend signs, invalid rebases and
cross-page starts, plus atomic failure paths. `tests/test_images.c`
checks cross-image data export binding and full graph rollback when a symbol
is missing. Existing native ARM64 fixture execution remains required to pass,
along with the full `-Wall -Wextra -Werror -Wpedantic` suite on x86_64 and
arm64, for the exact frozen PR head.
