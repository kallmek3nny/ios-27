# phase 17a: compiler-produced native multi-segment MH_EXECUTE

## scope and security model

This phase makes **genuine, original-source ARM64 Darwin Mach-O** executables execute natively on ARM64 Linux/Android. It remains a narrowly trusted test ABI (`int main(void)` as `LC_MAIN`), **not** a Darwin process, syscall layer, general dyld, sandbox, iOS framework loader, SpringBoard or iOS 27 boot path. Do not execute third-party or Apple proprietary binaries with this in-process runner. No root, bootloader, firmware, or kernel changes are involved.

## mapping and safety

1. Reparse the source and require exact metadata match, ARM64-all, validated `LC_MAIN`, supported load commands, zero import/rebase fixups and no unresolved symbol/section relocations. Actual dyld imports, chained rebases, indirect pointers, TLS and unsupported section types fail closed.
2. Verify linked section ownership, section-to-segment VM/file correspondence and disjoint virtual extents. Regular sections must be file-backed by exactly the indicated segment offset. `S_ZEROFILL` sections have VM extent but are not backed by a meaningful file offset, even where a linker rounds the segment `filesize` across BSS.
3. Reject W+X at both max and initial permission levels and reject host-page overlap at **4 KiB and 16 KiB**. 4K-linked files sharing a single 16K host granule with different final rights remain unsupported and are safely rejected.
4. Reserve the image span as `PROT_NONE`. Stage **all** segments RW and copy real segment bytes. Explicitly initialize every `S_ZEROFILL` section to zero (including when file padding is poisoned). Only after staging succeeds, seal all pages to their requested protections: RX text, R const/linkedit, RW data. Only then call `LC_MAIN`. Any failed stage or seal unmaps the complete reservation; the caller's exit code remains untouched on failures.
5. PC-relative ARM64 `ADRP` / load-store instructions are executed directly on host ARM64 hardware with relocated program counters and data addresses. This is **not** CPU emulation.

## deterministic compiler integration

`tests/fixtures/phase17a-multisegment.c` is original C code that references a readonly `__DATA_CONST,__const` value (37), an initialized `__DATA,__data` word (5), and a zero-filled `__DATA,__bss` array. The executable returns 42, modifies two writable words, and uses actual Clang-generated page-relative accesses. The two native invocations use independent mappings and must both return 42.

`tools/phase17a-build.py` compiles this source with `--target=arm64-apple-darwin`, no Apple headers or libraries, bounded subprocess calls, and produces an `MH_EXECUTE`. When `ld64.lld` is available, it links normally with readonly `__DATA_CONST` segment protection; when absent, `tools/phase17a-link-fallback.py` strictly links **only** the known compiler object profile with decoded `ARM64_RELOC_PAGE21`, `PAGEOFF12`, and `ADDEND` relocations. The fallback is intentionally **not** a general Darwin linker. It rejects unknown layouts, symbols, relocations, section types and dependencies.

The existing `compiled-darwin-real-mh-execute` test invokes this fixture builder under a hard timeout without modifying CMake. Both hosts verify the real linked image's `LC_MAIN`, actual `ADRP` instructions, 4K/16K layouts, readonly/writable protections and malformed linked image rejection. On ARM64, it executes and asserts the real `42` result. The existing phase 15a fixture and every previous test remain in place. Separately, `macho-parser` exercises synthetic section layouts and zero-fill corruption on either host without needing an external linker.

## validation

The repository's PR-triggered x86_64 and arm64 jobs are the frozen-head acceptance gate. Each job applies hard timeouts to CMake configure/build, CTest and CLI usage. To reproduce locally:

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
```

The SDK-free test fixture does not imply general iOS app compatibility, user-space isolation, working Apple dynamic linking or full iOS UI support.
