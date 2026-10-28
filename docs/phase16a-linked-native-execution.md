# phase 16a: narrow native linked MH_EXECUTE loading

## goal and trust boundary

Run the original-source, SDK-free ARM64 Mach-O `MH_EXECUTE` produced by the existing phase 15a build target on an ARM64 Linux/Android host, **without** CPU emulation or invoking Apple's dyld. The test-only entry ABI is `int entry(void)`. No iOS process bootstrap, system calls, frameworks, Apple UI, or security sandbox is implemented. Run only reviewed fixtures.

## linked metadata

The phase 15a executable has an `LC_LOAD_DYLINKER` declaration and an `LC_DYLD_CHAINED_FIXUPS` header. Mere presence of these commands does **not** demonstrate that runtime relocations or imports are needed. Phase 16a allows only the exact `/usr/lib/dyld` declaration with a strictly zero-import, zero-rebase fixup payload, while still refusing actual dynamic linking. The preflight also rejects unresolved N_UNDF symbols, nonzero undefined or indirect dynamic symbols, unlinked section relocations, unrecognized load commands, and malformed linkedit ranges.

The original parser reports the metadata honestly (`needs_dyld`, `has_fixups`). Only the dedicated freestanding preflight decides whether the corresponding payload is inert.

## mapping and entrypoint

- Parsing, source-metadata equality checks, command inspection, ARM64-all subtype checks, and address/protection checks finish before `mmap`.
- `LC_MAIN.entryoff` denotes a **file offset**, translated to the target VM address using the `__TEXT` segment's `fileoff` and `vmaddr`. The entry must be aligned, file-backed, executable and beyond the load-command header.
- For actual host page sizes 4 KiB or 16 KiB, non-`__PAGEZERO` segment mappings must occupy disjoint host granules. This may deliberately reject a legitimate 4K-linked image on a 16K host when segments share a 16K page, rather than silently merge RX and RW permissions.
- Reject W+X in both initial and maximum segment protections, overlaps in source file ranges, overlapping mapped pages, invalid segment spans and page-rounding overflow.
- Reserve the complete guest span as anonymous `PROT_NONE`; stage each segment RW, copy its file content, leave BSS and gaps zero-filled, flush instruction cache if needed, then switch to the declared final protection. No page is RWX. Any mapping/protection failure unmaps the **entire** reservation.
- Return value is published to the caller only after the native entry returns; validation/mapping failures leave it untouched.

## deterministic verification

The pre-existing `macho-parser` CTest now optionally consumes the built `phase15a-executable` from the CTest working directory (produced by the existing ALL target). On x86_64, it checks layout and corruptions but never executes ARM64 instructions. On native ARM64 Linux/Android, it additionally checks that the linked two-object entry returns **43** using `ios27_run_freestanding`. Mutation cases reject malformed `LC_MAIN`, W+X, arm64e, nonempty fixup imports, unresolved symbols, unexpected interpreter and unlinked relocations, with failed runs leaving the exit-code sentinel unchanged.

The existing synthetic 4K/16K tests remain intact. This does **not** claim a successful 16K native run of the 4K-linked phase 15a fixture or any Galaxy S25+ deployment.

## verification discipline

Build using `timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON`, `timeout 180s cmake --build build -j 2`, and `timeout 120s ctest --test-dir build --output-on-failure` on supported hosts. Do not remove `-Werror`, change CMake ownership, or weaken legacy tests. The definitive signal is the PR-triggered x86_64 and arm64 CI on an exact, frozen head.
