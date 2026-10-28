# phase 17a: native loader validation follow-up

This follow-up is based on main after the concurrent phase 17a implementation,
which already has real Clang-produced multi-segment fixtures, BSS initialization
even when file padding is nonzero, W^X staging/commit, PC-relative ARM64
execution, a bounded linker fallback, and 4K/16K validation. This change does
not replace those implementations or touch other agents' files.

## extra fail-closed conditions

- The common executable mapping preflight disallows executable non-text
  segments, writable text, and non-read-only `__LINKEDIT`. Both initial and
  maximum protections are considered so a segment cannot acquire unsupported
  execution or writable permissions in later transitions. Existing W^X and
  page-granule disjointness checks remain in force.
- The narrower linked-image inspector invokes the actual 4K mapping preflight,
  preventing it from incorrectly accepting an image the runner cannot map.
- Some real LLVM `ld64.lld` variants encode *inert* `LC_DYLD_INFO_ONLY`
  rather than `LC_DYLD_CHAINED_FIXUPS`. Inspect either representation,
  but never both, and accept legacy only after strict zero-length
  rebase/bind validation. Dynamic imports, rebases, section relocations,
  unsupported dependencies and unknown load commands still fail.

## deterministic tests

- The original `macho-parser` test adds executable-data and writable-linkedit
  negative cases, checking that rejected layouts remain cleared.
- The original `compiled-darwin-real-mh-execute` test transforms **only**
  an inert chained-fixup header in a real compiler-built phase 15a executable
  into a well-formed inert legacy dyld-info command. It verifies both
  inspection and 4K mapping, then ensures a nonempty rebase stream is rejected.
  The original linked text and data bytes are unchanged.
- The existing phase 17a real compiler-linked ARM64 native execution fixture
  remains the main integration test, returning 42 on ARM64.

No changes to CMake, HANDOFF, bootloader, root, firmware, or Apple binaries.
Both pull-request CI architectures must pass on a frozen head before merging.
