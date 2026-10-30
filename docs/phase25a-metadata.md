# phase25a: arm64 objective-c metadata relocation and global binding

## scope and trust model

phase25a is a **non-executing** native-binary metadata binder. it accepts bounded
real clang + ld64.lld `MH_DYLIB` arm64 Mach-O files, builds an isolated
synthetic guest address space with an independent slide per image, decodes
validated `LC_DYLD_CHAINED_FIXUPS` 64-bit bind/rebase pointers, and resolves
objective-c metadata without ever invoking IMPs on the x86_64 host.

no borrowed files from unmerged phase24a (#97) or phase24c (#100) are used.
there is no shared build configuration edit, and the runner only imports
`src/phase25a_bind.py` and builds phase25a-owned C files and fixtures.

## implementation milestones

1. validate arm64 `MH_DYLIB` headers, bounded load commands, segment and
   section ownership, `W^X` protections, valid chained-fixup payload extents,
   chained pointer formats `DYLD_CHAINED_PTR_64` and
   `DYLD_CHAINED_PTR_64_OFFSET`, import table names, and rebases with a unique
   image slide; reject arm64e, classic bind/rebase streams and other formats.
2. walk relocated objc2 classlist, class, metaclass, class_ro, method lists,
   protocols, ivars, selector references, class references, super references,
   and category metadata. support independently bounded classic 24-byte method
   entries and 12-byte relative entries. validate 4-byte-aligned IMPs against
   executable, non-writable mappings; never execute code.
3. commit registration transactionally under a lock, canonicalize named class
   identity across dylibs, bind external superclass and selector names, reject
   conflicting class definitions and cycles, order categories by dependency
   registration, and bump generations on registration/retirement. reject
   unloaded dependencies and retain prior category-method versions for safe
   reverse-order category retirement.
4. verify real linked six-image dependency graph with shared ancestry and
   categories, O0/O2 LLVM/Clang codegen, 160,000 metadata lookups per profile,
   deterministic malformed mutation corpus, concurrent reader lookup, teardown,
   and x86_64 host C preflight under address/undefined sanitizers.

## independent invocation

```sh
timeout 180s python3 tools/phase25a-verify.py
timeout 180s python3 tools/phase25a-verify.py --sanitize
```

requires upstream Mach-O-capable `ld64.lld-18` or `ld64.lld-19` and clang.
**fail closed on missing/broken linker; no fake linked libraries.** some
swift toolchain builds include a `ld64.lld` that intentionally rejects
macos-target linking; install an upstream LLVM `lld` package instead.

## important limitations and integration gates

- not a darwin `dyld`, executable guest dispatcher or production objc runtime.
  metadata address relocation is checked in a simulated guest VM; actual guest
  arm64 instruction execution and native in-process mapping are not claimed.
- only explicitly supported 64-bit chained pointers are relocated. classic
  dyld bind/rebase streams, authenticated arm64e pointers, relative indirect
  protocol imports, weak imports, arbitrary dynamic symbol interposition,
  attached code signatures and broad legacy objc ABI features remain unsupported.
- class method and category ordering are bounded to deterministic image
  registration order; category retirement is conservative.
- before merging require frozen-head PR CI green, correct actual linked metadata
  emitted by LLVM, an unchanged main SHA, security review, and integration
  compatibility checks with merged phase24 paths. test success alone is not
  sufficient to merge.

## development duration

active development time must be recorded from actual elapsed work and must
not be padded by sleeps, CI delay, or an artificial timer. the implementation
has not been certified as satisfying a three-hour active development request.
