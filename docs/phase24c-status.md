# phase24c: objc2 metadata, typed resolution, and real linked Darwin binary graphs (partial)

## locally verified milestones
1. Fail-closed ABI type-encoding whitelist with unsupported aggregate/variadic/block/mixed-FP rejection.
2. Transactional cross-image class registration with checked ivar bounds, superclass cycle and duplicate class rejection, and protocol/category record checks.
3. Deterministic instance/class method, inherited, category and lexical-super lookup under concurrency: 200,006 metadata resolutions across eight threads.
4. Actual Clang-produced ARM64 Darwin ObjC MH_OBJECT compilation followed by LLD linking of SIX MH_DYLIBs and THREE MH_EXECUTEs, with real dependency load commands and chained fixups verified by bounded Mach-O parser. No Darwin method bodies were run.
5. x86_64 local -Werror ordinary and ASAN/UBSAN tests both passed. CI includes x86_64 and ARM64 Linux + sanitizer lanes. Check exact PR head before claiming CI complete.

## linker provenance and restrictions
Swift-built LLD 17 rejects linking Mach-O objects with LC_BUILD_VERSION targeting macOS. The linker verifier uses the existing phase19b method of bounded MH_OBJECT platform-command removal and build-time TAPI .tbd export declarations; it does NOT change the linked binary's ObjC implementation body. Genuine Clang/LLD binaries contain linked ObjC sections and dependencies; unimplemented libobjc symbols may remain unresolved via -undefined dynamic_lookup. Native runtime execution is NOT verified.
All test commands have bounded timeouts. Run: timeout 600s bash tools/phase24c-verify.sh; sanitizers: timeout 600s env PHASE24C_SANITIZE=1 bash tools/phase24c-verify.sh.

## unimplemented
Zero actual Darwin IMP calls and zero real retain/release/weak transitions. Native objc_msgSend entrypoint, mapped relocation and ownership-checked executable pages, +load/+initialize coordination, ownership, weak/finalization, generation-safe unload and runtime app startup remain missing. Metadata lookups must not be presented as method calls. Do not claim Apple libobjc compatibility.
Three genuine active engineering hours were not completed or claimed. Retain as draft until missing functionality and all frozen-head CI gates pass, and main can be verified unchanged with expected-SHA lease.
