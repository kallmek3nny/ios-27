# Internal pointer rebases in native executable images

The native executable path now supports a bounded subset of unauthenticated
chained pointer rebases. This is needed for compiler-generated global pointers
and, eventually, Objective-C metadata. It does not supply dynamic imports,
Objective-C class registration, authenticated arm64e pointers or Apple frameworks.

## Accepted image contract

The complete existing executable-layout, section-geometry, entry, W^X and startup
checks still apply. Nonempty chained metadata must pass the existing chained
parser plus ios27_chained_static_plan. Only internal PTR_64/PTR_64_OFFSET targets
inside mapped image segments are accepted, within the existing 256-patch quota.
Imports, bind pointers, tagged/reserved layouts, overlapping slots, executable or
read-only patch segments and zero-fill slots are refused. The service binding
record cannot overlap a relocation in the native runner. Failure clears the plan.
Classic bind/rebase opcodes and dynamic library dependencies remain unsupported
in this executable path; the separate multi-image research loader is unchanged.

The runner reserves an anonymous file-sized staging copy as PROT_NONE, then
enables RW access with mprotect. This matches the existing stage-one syscall
filter; allowing an initial RW mmap would violate that filter. The existing
transactional chained applier resolves all values before writing the copy.
Segment bytes are then copied into the non-executable image reservation, BSS is
zeroed, the staging copy is unmapped and the approved service slot is filled.
Final protections are sealed before readiness/entry. Source bytes and the sealed
payload remain unchanged. No syscall-filter relaxation was introduced.

## Actual executable evidence

The separately compiled service client contains a volatile global string pointer
that cannot be folded into a direct literal reference. The named-shim link
explicitly requests chained fixups. Its native write call dereferences the
rebased pointer, so an unrelocated image cannot pass the workload.

The Android 0.2.0 launcher prints its actual internal rebase count. The device
smoke script requires a positive count, fixture hash verification, guest return
42 and launcher exit 0 on two fresh launches. This passed on the physical
SM-S936U1 S25+, API 37, 4096-byte pages. This is original fixture execution;
Apple code was not executed.

static-executable-rebases checks the real compiler-produced image at 4 KiB and
16 KiB layout granules, transactional relocation of a private copy, unchanged
source bytes, import/bind/tagged/read-only/zero-fill refusals, cleared outputs
and 128 deterministic command mutations. On ARM64 it also checks invalid-image
setup refusal and three subsequent successful supervised launches. Existing
native/CLI suites retain their fault, deadline and fresh-state checks.

## Remaining integration

Read-only data-constant fixup targets, general binding, shared-cache loading,
class/metaclass/selector initialization, Objective-C ABI/ARC, Swift and graphics
still need implementation and runtime evidence. A valid internal pointer does
not establish that an Apple method, service or framework can run.
