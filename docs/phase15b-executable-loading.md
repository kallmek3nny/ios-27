# Phase 15B: bounded native executable loading

This phase extends the existing **trusted-fixture** Mach-O work. It does not run
SpringBoard, general iOS executables, dyld, Apple's frameworks, or arbitrary code.
No emulation, root requirement, device partitions or proprietary binaries are used.

## Loader contract

- Thin little-endian ARM64 Mach-O load commands are parsed with bounded counts,
  command sizes, segment file ranges, VM bounds and overflow checks.
- The multi-image `MH_EXECUTE` path requires `LC_MAIN.entryoff` to be aligned,
  outside the load-command table, and within unique file-backed RX code. It
  translates the entry as an offset from the `__TEXT` guest VM base.
- The graph loader rejects overlapping file or guest VM regions, invalid
  `maxprot/initprot` relationships, and W+X protections. `__PAGEZERO` is
  zero-file-size and inaccessible. Freestanding images additionally use the
  existing host-independent preflight on both 4 KiB and 16 KiB pages.
- Freestanding execution uses the main-branch preflight, which reparses the
  exact input before mapping. Stale/caller-edited metadata is rejected.
- Both loaders stage code in anonymous `PROT_NONE` allocations and temporarily
  writable **nonexecuting** pages, then seal the executable segments as RX.
  Failure paths unmap all reservations and discard staging.
- `ios27_images_entrypoint` returns an entry pointer only after the graph is
  `READY` and LC_MAIN was validated, and only for images that do not require
  a system dyld. The graph must remain loaded while the pointer is used.
- Runtime page sizes are taken from `sysconf(_SC_PAGESIZE)`. The graph loader
  requires each loaded segment to align to the actual host page. This supports
  4 KiB and 16 KiB layouts when disjoint page protections are representable;
  it **rejects** split 4 KiB RX/RW pages on a 16 KiB host rather than silently
  making an executable page writable.

## Deterministic fixture coverage

`test_macho` (already on main) exercises 4 KiB / 16 KiB native layout
preflight, malformed inputs, W^X, VM page aliasing and stale metadata.

`test_images` constructs independent in-memory MH_EXECUTE and MH_DYLIB
fixtures, loads chained and classic bind formats, resolves the provider's
export, checks the executable entrypoint and runs its cross-image ARM64 call
on ARM64 Linux. It also checks PAGEZERO, file/VM overlap, protection errors,
missing imports and that a failed graph never poisons the next load.

To run locally (explicit hard timeouts, warnings treated as errors):

```sh
timeout 60s cmake -S . -B build -DIOS27_BUILD_TESTS=ON
timeout 120s cmake --build build --parallel 2
timeout 90s ctest --test-dir build --output-on-failure
```

On x86_64 the ARM64 machine instructions are **not executed**. ARM64
pull-request CI must be checked separately, on the exact frozen PR head.

## Deliberate limitations

Entrypoint calling uses a purpose-built `int(void)` ABI fixture. Genuine
Darwin `LC_MAIN` semantics, stack setup, dynamic loader launch, ASLR slides,
TLS, dyld fixups used by real linked binaries, signatures, platform APIs and
system-call compatibility remain future work. Do not treat native fixture
execution as a general iOS application runtime.
