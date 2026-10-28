# phase 19d: bounded arm64 darwin mh_object linking

this phase adds a transactional relocation engine and a small sdk-free, compiler-object linker. it does not implement dyld, an ios runtime, apple frameworks, a guest sandbox, or pointer-authentication signing.

## integration surfaces

- `ios27_darwin_arm64_apply_batch`: caller-resolved relocations applied to a private staging buffer and committed only if every record succeeds. supports arm64 unsigned64, subtractor+unsigned64, branch26 (b/bl), page21 (adrp), pageoff12 (add/unsigned integer load/store), got_load_page21/pageoff12 (adrp/ldr x), pointer_to_got (signed32) and 24-bit addend prefixes.
- `ios27_darwin_link_objects`: loads up to 12 independent clang arm64 macos mh_object files, resolves same-object and cross-object symbols, deduplicates got slots, stages executable text, read-only constants, writable data and zero-filled bss, then applies section relocations. uses 4k/16k page separation for rx/r/rw; it does not alter permissions or execute code itself.
- all modifications to caller output occur after full preflight, symbol resolution, section mapping and relocation have succeeded. failures retain the original caller bytes and zero the output layout.
- the permitted arm64e-safe subset is ordinary non-authenticated arm64 relocation encodings. arm64e object execution, authenticated_pointer, tlvp, thread-local variables, ptrauth keys and ios platform dependencies are **not** supported and are rejected where encountered rather than guessed.

## test coverage

registered ctest targets `compiled-darwin-branch26` and `compiled-darwin-got-load` now exercise the generic engine and the multi-object linker against genuine sdk-free clang-compiled mach-o objects, not only hand-built fake headers. on native arm64 runners they seal rx and read-only pages, clear instruction cache, and execute the resulting compiled function calls. repeated 4k/16k layouts, backward/forward branches, page and got relocation pairs, bounds/alignment, pac rejection, duplicate symbols, and rollback are covered; a four-object mixed call/constant-data graph is linked and checked.

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 300s cmake --build build --parallel 2
timeout 180s ctest --test-dir build --timeout 30 --output-on-failure
```

limitations remain intentional: only a known subset of clang sdk-free darwin object section types and relocations is accepted; no system libraries, framework imports, objc metadata or general ios binaries can be run by this linker. every executable mapping must be a trusted caller-controlled buffer and cannot be treated as a sandbox.
