# phase23c: objc2 native-code bridge (bounded preview)

## milestone scope

1. Validate compiler-produced arm64 Darwin MH_OBJECT class metadata through read-only phase22d and objc_metadata. Refuse unknown protocol/category/+load tables and malformed files.
2. Register independently compiled image classes in one namespace. Reject duplicate images/classes. Allow host-trusted categories in one image to override instance methods in another.
3. Reuse phase22d typed integer/double dispatch, superclass search and exactly-once +initialize. Strong-pin sends, track retain/release, and forward zeroing weak references.
4. Exercise two simultaneous independent image imports, eight concurrent dispatch workers, 512,002 successful typed sends, 256,000 explicit retain/release pairs, 64 separate runtime registration/dispatch/teardown cycles, deterministic failed imports and unsupported-metadata rejection. Test both x86_64 and arm64 Linux hosts with ASAN/UBSAN.

## explicit blockers

Not actual Apple libobjc or native execution of mapped Darwin IMPs. Protocol conformance, metaclass method validation, compiler-produced categories, +load, ARC, arm64e/PAC, external-superclass metadata, and native image unloading remain unsupported. Category registration is for trusted host IMPs only. The bridge refuses unload rather than leaving reachable 22d class state. The caller must quiesce threads before destroy.

## local evidence

Started 2026-10-29 with verified main 638b180aeed62ca452848252fedfcba8ccb3b9ed. PR #94 later verified successful standalone phase23c CI on x86_64 and arm64 Linux, both regular and ASAN/UBSAN, at frozen head 1646e36b3b8b6bf6ecb77fc114a0844434abd4ed. Subsequent concurrency/replay changes require another complete head-specific CI run. Clang 17 -Werror syntax checks succeeded and 4 SDK-free objective-c arm64 Darwin MH_OBJECT fixtures compiled. Local ld64.lld rejected Darwin dylib linkage, so linked dylibs and native execution were not verified. Do not infer elapsed engineering hours from test-operation counts; complete frozen-head CI is required before merging.
