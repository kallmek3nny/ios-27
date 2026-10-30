# phase25b: restricted real arm64 objective-c message dispatch (experimental)

## engineering milestones

1. standalone phase25b registry and complete type-encoding whitelist for exact
   64-bit word0..word6, void0, single f32 and f64 arguments/returns, including
   explicit self/_cmd offsets. Fail-closed rejects aggregates, blocks, unions,
   vectors, small scalars, mixed FP, >6 arguments and inconsistent frame sizes.
2. true AArch64 assembly entrypoints, not C pointers masquerading as objc_msgSend.
   save/restore x0..x8, q0..q7, retain 16-byte SP alignment, implement lexical
   objc_msgSendSuper2 starting at the immediate superclass, and preserve return
   x0/x1 and q0/q1 while releasing pins after the actual method returns.
3. W^X native mapping for allowlisted compiler-produced Darwin method bytes;
   concurrent method lookup/replacement protected by a mutex and epoch-stamped
   cache; lifetime pins on active mapped images and receivers. Category replacement
   is an explicit, ordered method-install operation; no dylib categories auto-load.
4. six independently linked genuine clang/ld64.lld arm64 Darwin ObjC dylibs and
   three MH_EXECUTE programs with dependencies, class references, and chained
   fixups; inspect exact compiler-generated instructions and require an exact
   relocation-free instruction allowlist before native execution.
5. x86_64 host metadata and concurrency verification: 240,005 pinned lookup
   operations across eight reader threads with 1,000 method replacements.
   Normal and ASAN/UBSAN local runs pass. **These are NOT native ObjC calls.**
6. arm64 CI test is designed to invoke 720,000+ genuine native message sends to
   compiler-linked Darwin IMP bodies through phase25b_objc_msgSend and
   phase25b_objc_msgSendSuper2, including x2..x7, FP and concurrent replacement.
   **This does not become a verified result until that exact runner passes.**

## real native execution versus host simulation

The native runner extracts exactly allowlisted leaf method instructions from
linked Darwin alpha/beta/gamma dylibs, maps an independent runtime-owned
RX page and sends through phase25b's AArch64 dispatch entrypoint. It does not
execute the original dylib as a fully relocated Mach-O, invoke Apple's libobjc,
load constructor methods or execute the three compiled application entrypoints.
It does not execute beta's compiler-generated `[super add:]` body because its
branch to `_objc_msgSendSuper2` relies on Mach-O classrefs and dyld fixups.
Instead the native harness constructs a valid objc_super and calls the genuine
super dispatch entrypoint, which reaches separately compiled root methods.
Do not count host metadata lookups or compiler links as native dispatches.

## disassembly inspected (clang --target=arm64-apple-darwin -O2, ld64.lld)

- `+[P25Root kind]`: `mov w0, #25; ret`
- `-[P25Root add:]`: `add x0, x2, #5; ret`
- `-[P25Root twice:]`: `fadd d0, d0, d0; ret`
- `-[P25Root triple:]`: `fadd s1, s0, s0; fadd s0, s1, s0; ret`
- `-[P25Root echo:]`: `mov x0, x2; ret`
- `-[P25Root six:b:c:d:e:f:]`: `add x0, x7, x2; ret`
- `-[P25Child add:]`: stack setup, load classref/selref, `bl` to
  `_objc_msgSendSuper2` stub, plus 20 to return; not relocation-free and NOT run.
- `-[P25Root(Replacement) add:]`: `add x0, x2, #9; ret`

## bounded commands

- git: 30s, cmake configure: 120s, build: 300s, tests: 180s,
  gh operations: 60s. Standalone verification uses 5-180s subprocess bounds.
- `timeout 180s bash tools/phase25b-verify.sh`
- `timeout 180s env PHASE25B_SANITIZE=1 bash tools/phase25b-verify.sh`
- arm64 CI matrix requires successful non-sanitized and ASAN/UBSAN runner logs
  from the *same frozen commit*; no skipping failed lanes or weakening -Werror.

## current blockers / required next steps

- Actual full linked MH_DYLIB loading, selector/selref rebasing, chained fixups,
  compiler-generated cross-dylib sends, and running MH_EXECUTE apps.
- Untyped varargs callsites cannot prove the true argument count or signature
  from registers; registration validates an ABI whitelist, but the caller must
  provide the correct prototype. No general ABI-safety guarantee.
- Super pointer must address a readable compiler-created objc_super structure.
  Arbitrary invalid super memory addresses cannot be safely dereferenced.
- Not a cross-image ObjC class metaclass metadata binder or object ownership
  system; class tokens and objects are private to this standalone test registry.
- No load/unload while active sends; runtime retirement refuses referenced
  images and release refuses pinned receiver. No lock-free dispatch/ARC.
- Native wall-time benchmark printed on arm64, not yet locally measurable on
  the x86_64 development host. Benchmark compares different method paths and
  is indicative only.

## active engineering duration

Work started 2026-10-30 at approximately 10:40 UTC and continues until the
final recorded action for this session. This is not a three-hour session.
No artificial sleeping, inflated development durations or false claims of
completion. Do not merge until frozen-head CI is green, all blockers required
by phase25b are addressed, and main is unchanged under an expected-SHA lease.
