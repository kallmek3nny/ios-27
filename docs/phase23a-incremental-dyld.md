# Phase 23a: bounded incremental ARM64 Mach-O loading

## Scope and invariants

This is a **real, isolated compatibility layer**, not macOS dyld, and it does not
execute proprietary Apple code. It wraps the existing read-only
`ios27_images_load` mapping/linking pipeline rather than bypassing its
chained/classic fixup validation or W^X sealing. No shared build files, other
phases, or loader implementation files are modified.

The caller supplies a catalog (maximum `IOS27_IMAGES_MAX`) of trusted,
Clang/LLVM-produced ARM64 Darwin `MH_DYLIB` bytes with their exact ordered
`LC_LOAD_DYLIB` names. The manager borrows those bytes and arrays for its
lifetime. `open(name)` traverses that root's dependency closure with DFS,
rejects cycles/missing mandatory dependencies/ordinal mismatches, and loads
**only** that closure using the existing transactional graph loader. The
graph becomes visible only after all relocations and mappings succeed.
Duplicate opens on the same root reuse its mapping and increment references.

Every open returns a 64-bit `(generation, slot)` handle. Close decrements
its direct reference count, and the final close retires that root's entire
closure. Generation increments on final close and wraps fail-closed.
A dependency remains mapped until every referencing root *cohort* retires;
reference-count observations count logical root reachability. A root's
`dlsym` is limited to its exports (including already-supported reexports).
A deterministic flat search considers loaded cohorts in catalog order, and
prefers strong definitions to weak ones with stable ties. Local/private
symbols never become exports by guessing `nlist` entries. All operations
on the manager use a mutex to serialize graph changes, lookups, and
reference counts. Counters record completed operations and failures.

## Verification

`tools/phase23a-verify.py` compiles **five original Clang ARM64 Darwin
objects**, links them with `ld64.lld` into five actual `MH_DYLIB` files,
and verifies their Mach-O fixup/export load commands and five interconnected
dependency edges (six edges total). Dependency linker inputs are minimal,
locally generated TAPI link stubs, never Apple system libraries.

`tests/phase23a-integration.c` checks genuine cross-image functions and
data imports, duplicate handles, transitive references, ABA prevention,
weak-vs-strong flat resolution, missing and stale symbols, malformed
dependency cycles, and recovery following rejected loads. It runs 240,000
symbol lookup calls, 140,000 duplicate reference transitions, concurrent
requests from four host threads, and repeated real map/unmap cycles.
On ARM64 Linux it calls compiler-produced mapped Mach-O functions; on
x86_64 it only validates parsing, mapping, and symbol addresses without
attempting to execute ARM64 code. AddressSanitizer/UBSan are run for both
architectures in the phase-specific pull-request workflow.

All subprocesses have explicit hard timeouts. Manual invocation:

```sh
timeout 900s python3 tools/phase23a-verify.py --output-dir build/phase23a
timeout 900s python3 tools/phase23a-verify.py --sanitize --output-dir build/phase23a-san
```

## Exact limitations and unresolved work

* This is **per-root cohort mapping**, not a single global dyld image table.
  If separate top-level roots both depend on one dylib, they can each map
  their own copy with independent data globals. No cross-cohort relocation
  or pointer identity guarantee is claimed.
* The manager does not automatically invoke guest constructors or
  destructors. There is no Darwin thread-local runtime, Objective-C
  initialization guarantee, ARM64e authenticated pointers, Swift ABI, or
  general Apple framework/SDK compatibility.
* No lazy runtime binding, changing `@rpath` catalogs, network/filesystem
  discovery, or arbitrary host library loading. It uses a closed, explicit
  install-name catalog. Native guest calls must be trusted and quiescent
  before their final handle is closed; a returned symbol address is borrowed.
* Locking guards manager metadata, **not the lifetime of guest calls after
  the lookup returns**. Callers must synchronize symbol use against close.
  Concurrent manager destruction is unsupported; join workers first.
* Unsupported relocation types fail closed in the pre-existing loader, which
  owns all relocation format checks, rollback, and segment protection sealing.
* Tests do not establish compatibility with full macOS/iOS dyld, actual
  platform initialization, or untrusted binary execution.

Do not merge unless the four isolated CI matrix jobs and any required base
branch checks pass for an unchanged PR head and `main` has not moved.
