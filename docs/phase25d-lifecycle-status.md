# phase25d lifecycle, concurrency and image retirement

## scope and provenance

Branch starts from main a6cc52fbf54edc2e00fdfae8a31205a53049e9d6.
PR #99 (phase24b) and PR #100 (phase24c) were open drafts at branch creation.
All changes are in phase25d-owned paths. The existing image loader,
constructor runner, and ObjC dispatch/binding workstreams are read-only.

This is an **isolated host scheduler**. It is not Apple libobjc and does not
claim that an ARM64 Darwin ObjC method, +load, or +initialize has executed.

## four substantial implementation milestones

1. Dependency-first image activation and superclass-first +load dispatch.
   Stable numeric class/category ordering; once-only callback execution.
   Categories execute after their image classes and retain target-class image.
   Sticky failures abort dependent image activation. An exclusive, timed
   +load gate keeps concurrent callbacks from executing out of order.
2. Lazy +initialize is requested by first_message/begin_method; superclass
   initialization precedes subclass initialization. Same-thread recursive
   initialization succeeds without invoking the initializer twice. Other
   first-message callers wait on a condition variable with deadlines.
3. Wait-for graph detects cross-thread initialization deadlocks, returning
   a typed error. Timeouts, explicit failure states, exception conversion,
   pinned callback ownership and deterministic wakeups are tested. The
   scheduler does NOT forcibly terminate or cancel a blocked guest callback.
4. Active-call leases pin class and owner image across method execution.
   Category and class lifecycle callbacks also pin metadata/executable owners.
   Retirement is refused while active pins, subclasses, dependencies,
   cross-image categories or in-flight initialization exist. Generation
   checks reject stale and duplicated lease completion. Retired identities
   are unusable, with explicit registration of new generations.

## native interface contract (not yet integrated)

The prospective metadata binder must verify compiler-linked Objective-C 2.0
class, metaclass, superclass, selector, category, +load, +initialize and
image ownership metadata before calling register_image/register_class/
register_category. It must supply deterministic category order IDs and
validated executable guest PC addresses.

mark_macho_ready is an explicit handoff after the external Mach-O loader
has finished dependencies, fixups, W^X and constructor prerequisites.
The engine does NOT run __mod_init_func, __mod_term_func, objc_msgSend,
Mach-O linking, relocation or memory mapping. Dispatch(Event) is an
externally supplied callback; its raw integer callback_pc MUST NOT be
cast to an unverified host pointer.

For a real native method invocation, begin_method gives a pinned lease
before calling guest code; end_method must be reached after return or
exception/failure. The image owner must not unmap code until retire_image
returns ok. Destructor and dlclose sequencing remain owned by phase24b.

+load same-thread reentry into an incompletely loaded image returns
reentrant, not speculative invocation. Init waiters can time out, but
running guest callbacks are not preempted. There are no native image
loader/dispatcher adapters yet.

## reproducible test evidence

Run locally:

    timeout 300s python3 tools/phase25d-verify.py
    PHASE25D_SANITIZE=1 timeout 300s python3 tools/phase25d-verify.py

Both passed in the initial local x86_64 sandbox with -Wall -Wextra -Werror
-pedantic, also under AddressSanitizer/UndefinedBehaviorSanitizer.

- 5,200 fresh four-image dependency graphs executed 234,000 modeled
  lifecycle state transitions and 10,400 initialization requests.
- Eight worker threads exercised 120,000 additional method entry /
  initialization requests; a shared +initialize ran once.
- Deterministic six-image +load and category order; superclass-first
  initialization; repeated first-message idempotence; same-thread reentry;
  two-thread wait cycle; injected +initialize failure; thrown +load
  exception; stale/double-issued lease and generation mismatch;
  cross-image category dependency and safe teardown all passed.
- Six genuine Clang-compiled ARM64 Darwin ObjC2 **MH_OBJECT** translation
  units contain hierarchy, +load, +initialize, categories and cross-image
  references. Three ARM64 Darwin app MH_OBJECTs contain compiler-emitted
  objc_msgSend sites.
- Linker test failed on the local Swift LLVM 17 toolchain:
  "ld64.lld: error: This version of lld does not support linking for platform macOS".
  Linked dylibs = 0 and executable programs = 0 in that environment.
- Native guest Darwin +load and +initialize executed: **0 verified**.
  All scheduler callback dispatch in these tests uses a host adapter.

## blockers and merge rules

Missing: six linked Darwin dylibs, three linked native integration programs,
an independently verified Mach-O mapper and guest ARM64 execution bridge,
validated metadata-specific +load order from compiled images, actual native
+initialize requests, and cancellation of long-running guest callbacks.
No 3+ hours of genuine active engineering were demonstrated or recorded.
CI minutes and timestamps are not evidence of engineering hours.

Keep draft and unmerged until those gaps are addressed. Frozen PR head CI
must be green and main compared with an explicitly verified expected SHA
before considering a merge. Do not claim passing host tests prove Apple
framework/iOS app support.
