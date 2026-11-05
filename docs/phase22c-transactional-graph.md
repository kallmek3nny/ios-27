# phase 22c: transactional dyld graph controller (experimental)

this work is an isolated integration layer, not a new dynamic linker. the existing
mach-o parser, images loader, classic binds, chained fixups and export trie are
**read-only** dependencies. no shared cmake or preexisting loader code was edited.

## concrete implementation

1. **bounded dependency planning**: up to `IOS27_IMAGES_MAX` images;
   exact LC_LOAD_DYLIB count/order matching, name uniqueness, duplicate-edge
   validation, whole-catalog cycle detection, stable dependency-first order,
   canonical guest-path matching with inherited `@rpath` search contexts,
   and structured diagnostics with image and dependency indices. invalid
   inputs never yield a graph.
2. **cross-image lookup**: real export-trie metadata; explicit positive
   dependency ordinals, self (0), main (-1), deterministic flat (-2), and
   weak flat (-3); re-export traversal is delegated to the existing
   `ios27_exports_resolve`. strong definitions outrank weak definitions
   during flat lookup. missing results have an explicit return code.
   symbols are *metadata offsets*, not guest executable host pointers.
3. **transactional loader handoff**: a read-only preflight validates classic
   binds and active chained-fixup import ordinals/names, including missing
   mandatory imports, identifying the importer index, patch index, and
   symbol where available. only after preflight does `ios27_images_load`
   stage existing fixups and W^X mappings. if the loader rejects any image,
   no graph mapping is retained. errors are surfaced, never stubbed.
4. **lifecycle and unload**: per-lease transitive dependency pin tracking,
   64-slot quota, generation-tagged leases and double-release detection;
   pinned images prevent unload. initializer/finalizer ordering uses
   *explicit trusted host callbacks*, not automatic `__mod_init_func`
   invocation; failing initialization finalizes only completed nodes in
   reverse order. external callers must release every lease before destroy.

## test evidence

- `tests/phase22c-graph.c`: linked-format synthetic ARM64 Mach-O images
  with a real fixup pointer, dependency ordering, actual mapped export
  lookup, importer-indexed missing-symbol diagnostics, invalid import rollback,
  resource pins, invalid catalog rejection,
  cycle rejection, 64-slot lease exhaustion, initializers' rollback, 2,500
  seeded malformed graph preflight/load mutations, and 50,000
  live-graph acquire/release operations.
- `tests/phase22c-fixture-{provider,consumer}.c`: original-source,
  SDK-free clang-produced arm64 Darwin Mach-O objects with an external
  function reference; no proprietary Apple SDK or framework dependency.
- `tests/phase22c-compiled.c`: parses those compiler-produced objects,
  performs 40,000 deterministic image mutations, and, when a functional
  Mach-O `ld64.lld` is present, exercises compiled MH_DYLIB loading and
  unloading. unsupported linked fixtures are an explicit failure if linked.
- `tests/phase22c-linked.c`: independent 4-image native integration using
  existing read-only `tools/phase19b-build.py` compiler-produced dylibs;
  validates inherited `@rpath`, real cross-library symbol lookup, ARM64
  execution of trusted original-source code on native Linux ARM64, ownership
  and 32 clean load/unload cycles on both target architectures.
- `tools/phase22c-test.sh`: bounded configure/build/test and optional
  sanitizer run. dedicated PR CI includes x86_64 and ARM64 Linux runners,
  with and without ASAN/UBSAN. use `IOS27_PHASE22C_SANITIZE=1` locally
  for sanitizer coverage. PR CI installs a functional open-source linker
  and sets `IOS27_PHASE22C_REQUIRE_LINKED=1`, so linked fixture failures
  fail the job instead of being silently skipped.

as verified on head `c432c865d43bf41e0262da77e7631d2da0f00613` before the
subsequent preflight-diagnostic changes:
CI run `40793418699` had **four of four successful** independent jobs,
covering x86_64 and native ARM64 Linux with and without ASAN/UBSAN;
the native ARM64 log reports genuine 4-dylib execution and 32 teardowns.
this proves the bounded freestanding fixtures, not general-purpose dyld.

the linked-dylib stage prints **NOT VERIFIED** when the local linker is
missing or is a stub. this cannot be counted as a successful linked
dylib test. do not infer that LLVM arm64 Mach-O objects alone demonstrate
native multi-library execution.

## honest limitations and next blockers

- graph maximum remains 8 images, and imports must be explicitly cataloged.
  no host filesystem/network probing, Apple shared cache, closures,
  proprietary framework loading, or arbitrary guest execution.
- native guest callbacks, `__mod_init_func` and `__mod_term_func`
  scheduling, ObjC/Swift initialization and TLS are **not implemented**.
  the host callback lifecycle proves sequencing and rollback only.
- some real linked Mach-O section formats and relocation kinds may be
  rejected by `ios27_images_load`. the loader remains fail-closed.
- no authenticated arm64e pointer handling; do not execute unauthenticated
  arm64e pointers or call native guest dylib code on an x86_64 host.
- the API is single-owner/externally serialized. parallel operations on
  the same graph need a future locking layer; independent graphs can be
  tested concurrently.
- `ios27_22c_destroy` refuses to free a pinned graph. the owner must
  release leases and retry to prevent invalidating mapped code.

## completion criteria and time

this PR remains **draft** pending the required genuine three-hour active
engineering duration and a fresh frozen-head main lease: `main` moved after
this work branched from `e8124a374e04114c218028d82ce300ab70d9631e`.
CI green alone is insufficient to override those constraints.

four implementation areas are present in the branch; they are **not**
evidence of 3+ hours of continuous engineering. exact active human/agent
engineering time must be measured by a separate actual wall-clock work
log; Git commit timestamps or CI run duration alone are not evidence of
active time. merge only with green PR-triggered CI from the frozen head
and a separate main-head lease check; no claim of this requirement is
made merely by writing this document.
