# Phase 24a: shared native-image identity (bounded first milestone)

## Base and ownership

Started from verified `main` commit `527adf78a63890e219812bed8ff773af2f5cd70f`.
Phase23a PR #92 was open and **not merged** at this point, so phase24a
does not depend on phase23a headers or implementation. Only
`src/phase24a_*`, `include/ios27/phase24a_*`, `tests/phase24a-*`,
`tools/phase24a-*`, `docs/phase24a-*` and
`.github/workflows/phase24a-*.yml` are touched.

## Implemented and observable

1. **One shared mapped graph within one manager**: the first successful
   `open` maps a fully validated caller-supplied catalog once using the
   existing `ios27_images_load_with_paths` backend. Subsequent independent
   opens receive distinct handle tokens referring to that **same** mapped
   graph. Cross-library imports and writable data use the same mapping.
   The backend independently validates fixups, relocations, and W^X
   protection before publishing it. A failed map publishes no handles.
2. **Root-closure accounting and lease-safe usage**: DFS checks the supplied
   install-name graph and detects recursion. Each direct handle contributes
   one logical reachability reference to every unique dependency. Generation
   counters reject stale/duplicate closes. Thread-safe pinned `dlsym`
   results keep the mapped graph alive even after the last handle closes,
   until the matching pin is released exactly once.
3. **Bounded namespace and path decisions**: canonical absolute guest paths
   only; supports expansion of `@rpath`, `@loader_path` and
   `@executable_path` via the existing strict path helper, in declared
   order. Cross-library bindings use the existing two-level graph backend,
   and lookup is limited to actual validated export tries/reexports.
   Deterministic flat lookup considers only live root closures and prefers
   strong definitions over weak ones, with catalog-order ties.
4. **Real compiler artifacts and stress**: the standalone verifier compiles
   **six original arm64-apple-darwin C dylibs** and **two separately linked
   Darwin executables** with LLVM ld64.lld, genuine dependencies, exported
   globals, and cross-image code/data references. The native host test
   exercises 160,000 direct pinned lookups and 48,000 additional concurrent
   pinned lookups, over 96,000 combined open/close transitions, reference
   accounting, ABA handling, cross-image identities, fault rejection,
   and repeated clean map/unmap. On ARM64 Linux it calls the trusted
   mapped compiler-produced functions (expected return values 47 and 21).
   On x86_64 hosts it does **not** execute ARM64 instructions. The CI
   workflow separately runs standard and ASan/UBSan on x86_64 and arm64.

Commands (all children and outer wrapper bounded):

```sh
timeout 900s python3 tools/phase24a-verify.py --output-dir build/phase24a
timeout 900s python3 tools/phase24a-verify.py --sanitize --output-dir build/phase24a-san
```

## Important limitations (not claimed as completed)

- This is **one manager and one catalog**, not global image identity across
  independent processes or separately created managers, and it does not
  incrementally map a new node after first open. All catalog entries are
  eagerly mapped in a single transaction. A malformed unrelated entry
  can cause the entire catalog load to fail. The existing graph backend
  limits the catalog to `IOS27_IMAGES_MAX` (currently 8).
- The two compiled executables are artifact-validated, **not executed** by
  this phase's host test. Native guest execution evidence currently covers
  compiler-linked dylib functions only.
- Lazy binding is **not implemented**. Supported fixups are eagerly applied
  by the baseline backend; unsupported fixup formats fail closed.
- Weak missing-library behavior, re-exports, main-executable ordinals and
  interposition remain subject to the existing mapped-image backend.
  There is no claim of general Darwin dyld, Apple framework compatibility,
  ARM64e PAC, arbitrary untrusted guest execution, or constructor invocation.
- Catalog bytes and dependency strings must remain valid through destroy,
  and registry destruction requires externally joined threads. A raw mapped
  pointer may be accessed only while its explicit pin is held.
- The eager catalog constraint means this milestone does not yet solve
  the original request's *fully global incremental* semantics; that requires
  a mapper which can extend an already-live process graph without moving
  existing address identities.

## CI and merge gate

Only claim tested milestones after job results exist for the **frozen PR head**.
Do not merge without green required jobs and verification that `main`
still equals the known base SHA. Any movement of `main` requires explicit
reconciliation and a new validation run. Never synthesize three hours of
activity: document real elapsed active engineering time separately from
the requested duration, and leave PR open when the requirement is not met.
