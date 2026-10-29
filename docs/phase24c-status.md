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
160,000 direct calls to an isolated, compiler-linked zero-relocation Darwin `+kind` IMP verified on ARM64 normal CI (not runtime-dispatched). Zero `objc_msgSend` calls and zero real retain/release/weak transitions. Native objc_msgSend entrypoint, mapped relocation and ownership-checked executable pages, +load/+initialize coordination, ownership, weak/finalization, generation-safe unload and runtime app startup remain missing. Metadata lookups must not be presented as method calls. Do not claim Apple libobjc compatibility.
Three genuine active engineering hours were not completed or claimed. Retain as draft until missing functionality and all frozen-head CI gates pass, and main can be verified unchanged with expected-SHA lease.

## native execution probe added to CI

The isolated `tests/phase24c-native.c` checker now extracts the compiler-linked, relocation-free `+[Phase24cRoot kind]` method from `alpha.dylib`. It validates the Mach-O class method symbol and exactly two ARM64 instructions (`mov w0,#1; ret`), maps an independent anonymous page RW, seals RX, and on AArch64 Linux invokes the extracted actual Darwin method body 160,000 times directly using an explicit two-argument ABI prototype. On x86_64 only format and instruction inspection are possible. CI logs are authoritative: these are **direct IMP calls**, not `objc_msgSend`/runtime class dispatch, and do not validate dependencies, object lifetime, or Apple libobjc. No runtime ownership transitions occur. This direct-call probe does not establish full phase24c completion.

### sanitizer follow-up
ARM64 sanitizer CI identified a pre-entry read at the indirect function pointer, consistent with UBSan function-type instrumentation. The native probe now maps a separate read-only preamble page immediately before the RX method page without disabling sanitizers. Final outcomes must be checked against the new frozen head.
