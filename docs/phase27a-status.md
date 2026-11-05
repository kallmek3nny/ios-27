# phase 27a: integrated Objective-C 2.0 execution readiness

## status

**INCOMPLETE / DRAFT.** No compiler-linked ARM64 Darwin Objective-C application has
been executed by this branch. Never count a host callback, a native ARC call,
registered metadata, linked Mach-O object or copied ARM64 IMP as a guest app
launch. The only finished work here is an isolated loader-neutral startup
transaction, a host-adapter integration test for existing ownership and
lifecycle contracts, and a strict SDK-free linked binary provenance gate.

## verified upstream inventory (reviewed before implementation)

| PR | frozen dependency head | verified subsystem | remaining boundary |
|---|---|---|---|
| #102 phase25c | `70d7d9826a034b3076a8c066d81aa085dbced285` | ARC/weak/pools native | not guest binder integration |
| #103 phase25e | `ae4bdd5788437d4eb31e90c29cc7840a127b85c7` | six MH_DYLIB, three MH_EXECUTE linked, inspected | no guest execution |
| #104 phase25d | `7c434d616cf2935e4dbf9f2a0ccd44840e4a1744` | +load/+initialize scheduling | host-dispatched callback adapters |
| #105 phase25a | `a9bcda8e7432e65d3b0f0a9d581163c91526a23e` | objc2 metadata binder | non-executing metadata inspection |
| #106 phase25b | `99e3678e6241c9298dc6b8271d0828d848931ac6` | real ARM64 compiler-produced leaf IMP dispatch | not full dylib/app execution |

These PRs remain separate and open. The integration CI copies only the exact
#102/#103/#104 files it uses from immutable commit objects into a build
directory. Every change in a dependency head is rejected until consciously
reviewed and repinned. No code from another agent is copied into the PR diff;
no shared loader, build system, application container or runtime is modified.

## implementation (this branch)

1. `phase27a_startup` checks *host-owned* prevalidated image descriptors,
   exactly one executable LC_MAIN offset, executable and nonwritable text,
   dependency closure, unique IDs/names, no cycles or duplicate dependency
   edges, 4-byte-aligned entry offset within a bounded __TEXT file interval.
   Invalid graph or argument vectors produce zero activation callbacks.
2. Constructs bounded, copied, NUL-terminated argc/argv/env/apple vectors
   with strict per-string and total-byte quotas and rejects duplicate
   environment keys. The `enter_main` hook receives this **host adapter**
   stack. Only a verified native guest runtime may implement that hook later.
3. Activates dependencies before consumers and rolls back every attempted
   activation in reverse order, including an activation which fails after
   partial resource allocation; rollback failures are explicitly surfaced
   with the original cause preserved. Main entry failures/nonzero exit codes
   trigger teardown. Teardown also occurs after normal exit.
4. Host adapter exercise combines #104 lifecycle callbacks, concurrency-safe
   first-message scheduling and pins with #102 allocation, weak zeroing,
   retained weak loads, pools and deallocation. It does not unify the
   separate #102 object identity with #106 dispatch receiver ownership.
5. Uses #103 unchanged SDK-free clang/lld-linked binaries for linked-metadata
   checks, negative fixture injections and provenance, independently of
   whether any executable guest call is available. Guest-execution gate remains
   explicitly false.

## reproduction

Locally for the self-contained startup test:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude \
  src/phase27a_startup.c tests/phase27a-startup.c -o /tmp/phase27a-startup
/tmp/phase27a-startup
```

GitHub Actions runs `phase27a-verify.py` on Ubuntu x86_64 and arm64, both
normal and ASan/UBSan, with checked, frozen PR dependencies. `-Werror` is
never disabled. Requires clang and ld64.lld. The evidence JSON from the
unchanged phase25e fixture is retained as an Actions artifact.

## hard blockers before any phase27a progress gate may be marked complete

- Real relocation-resolved ObjC class/metaclass/selref/category/protocol
  registration to the same runtime used by the guest `objc_msgSend` entry.
- A single coherent object allocation / dispatch-pin / weak identity across
  phase25b and phase25c; no duplicate allocation or stale receiver lifetimes.
- Lifecycle callbacks executed as compiler-produced *Darwin guest methods*
  in dependency order, not modeled host lambdas.
- Verified executable Mach-O segment mappings/fixups, dependency dylibs,
  stable ABI transfer to `LC_MAIN`, and guest `argc/argv/env/apple` in the
  appropriate address space; correct native process termination.
- App runtime trace from compiler-linked ARM64 Darwin `MH_EXECUTE` with
  independently corroborated native `objc_msgSend` and ownership transitions;
  negative ABI cases and rollback from the same execution path.
- Frozen-head green x86_64 and arm64 CI, sanitizer results, and independent
  integration review. No UIKit, Swift, full Apple framework, arm64e or
  general iOS application support is implied.

The phase 27a full execution milestone, guest send counts, guest ownership
counts and native application exit status are **unverified**, not zero-filled
successes. Active engineering hours are not inferred from CI elapsed time.
