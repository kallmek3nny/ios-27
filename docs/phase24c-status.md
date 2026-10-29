# phase24c: objc2 metadata namespace and fail-closed typed resolver (partial)

## implemented and reproduced locally

- strict, complete objc2 method-encoding whitelist: void no-arg, 64-bit word/pointer up to four arguments, one float or double argument. rejects aggregates, small scalars, blocks, variadics, mixed fp arguments and unverified type encodings.
- transactional cross-image class registration using prevalidated ios27_objc20a_image descriptions from existing read-only objc2 parser, checked ivar bounds, duplicate rejection and superclass cycle checks.
- deterministic category order, class versus instance method search, lexical-super validation and separate unresolved-superclass errors.
- eight-thread stress test for 200,006 metadata resolutions, compiled with -Werror; six arm64 Darwin objc library translation units and three application translation units compiled to genuine MH_OBJECT files.

## local verification

Run: timeout 600s bash tools/phase24c-verify.sh
Sanitizers: timeout 600s env PHASE24C_SANITIZE=1 bash tools/phase24c-verify.sh
Both commands passed locally on x86_64 with Clang 17. GitHub Actions checks remain head-specific; x86_64 and arm64 Ubuntu both have normal and sanitizer lanes.

## blocked and not claimed

NO Darwin native IMPs executed. The local ld64.lld failed on -platform_version macos with: This version of lld does not support linking for platform macOS.
Zero real linked Darwin dylibs, zero real linked Darwin apps, zero native method calls, zero real runtime ownership transitions. The 200k operations are metadata lookup, not objc dispatch.
Still unimplemented: real dynamic image relocation/mapping; address ownership and arm64e authentication; compiler-linked objc_msgSend execution; object alloc/retain/release/weak/finalization; +load/+initialize semantics; unload pins; full application startup.
Input must already have been checked by the read-only objc20a parser. No integration to the loader yet; this code accepts in-memory metadata descriptions and never casts method offsets to host function pointers.
No claim of three genuine active engineering hours. Do not merge before frozen-head CI is completely green AND fresh expected-SHA verification against an unchanged main.
