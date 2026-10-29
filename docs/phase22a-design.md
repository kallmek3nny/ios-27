# phase22a: bounded synthetic guest address spaces

## scope and evidence

This is an independent *native host-side prototype*, not a Darwin kernel,
MMU, isolated executable guest process, or a full operating-system sandbox.
No raw guest executable is loaded or run. The existing Mach-O loader is used
read-only as design context, not integrated yet. The native module is compiled
independently because shared CMakeLists.txt is owned by other workstreams.

### implemented milestones

1. **bounded mappings:** fixed 128 region slots, configured region/byte quotas,
   guest page zero rejection, guard-page reservations with host PROT_NONE,
   collision rejection, non-executable guard-wrapped stacks and mmap/mprotect rollback.
2. **checked translation:** overflow and alignment checks, contiguous
   cross-mapping preflight, permission-checked atomic copyin/copyout, RX/RW
   separation, and single-mapping borrows pinned against removal.
3. **lifetime and failure handling:** generation-checked region and pin IDs,
   stale reference rejection, bounded pin slots, deterministic map-failure
   injection, closed state, atomic metadata under a mutex, resource snapshots,
   post-reservation and post-mprotect rollback injection, and teardown that
   refuses active borrows.
4. **native concurrency and test evidence:** seeded 309,020-operation stress
   across four threads, close-vs-copy workers, quota/fault regression,
   two optimizations and two compilers under x86_64/ARM64 CI, ASan/UBSan,
   a real compiler-produced ARM64 Darwin Mach-O callsite check, and three
   child-only native protection faults verifying both guards and RX write denial.
5. **restricted child probe (Linux x86_64/AArch64 only):** pre-compiled trusted
   child is forked and checked with NO_NEW_PRIVS plus a seccomp BPF policy.
   Whitelist: exit, exit_group, rt_sigreturn, and write to precisely
   one inherited private pipe descriptor. Explicit deny probes for
   openat, socket, and stdout writes. There is no execve, no guest
   callback, and no claim of comprehensive process sandboxing. On unsupported
   hosts the test prints unsupported rather than claiming isolation.

## invariants and boundaries

- Guest addresses are uint64_t offsets in a configured bounded virtual
  range, never direct host pointers. No mapping may include guest page zero.
- Guard pages reserve guest offsets and use inaccessible anonymous host pages.
- A mapping is published only after host reservation and permissions succeed.
  Any failure leaves quotas and externally visible handles unchanged.
- copyout validates every byte and permission seam first, so a later fault
  cannot leave a partly written guest range. Protected by one mutex.
- W+X is forbidden; use RW for construction then switch to RX. No guest code
  is executed through this interface.
- Pin leases are single-use and generation checked. Active pins prevent
  protect, unmap, close, and destroy. External users must not retain
  or dereference a borrowed pointer after unpin.
- close may race with operations, which return CLOSED after closing.
  **destroy requires external threads joined first**, as no C API can
  safely free its own control block while unknown callers still hold it.
- Hard failure and allocation limits matter more than full VM performance;
  a linear lookup is acceptable at the configured limit of 128 maps.

## reproducibility

Run CC=gcc timeout 300s python3 tools/phase22a-standalone.py and repeat
with CC=clang. Set PHASE22A_SANITIZE=1 CC=clang for ASan/UBSan. Each child
compile/run command has its own Python subprocess timeout. The stress test
uses fixed xorshift64 seeds, disjoint worker guest ranges, 309,020 expected
operations and the reproducible checksum 15,291,755. Thread scheduling may
vary; individual workers cannot overwrite one another's offsets.

## known gaps

- No Darwin vm_map syscall ABI or integration with existing loader yet.
- Guard and W^X protections depend on host Linux mmap/mprotect; native
  host page size must be 4096 bytes, otherwise creation fails explicitly.
- No real hostile guest execution or container/rootfs isolation. seccomp is
  limited to a trusted built-in probe, not an arbitrary binary runner.
- External borrows require disciplined callers and cannot automatically
  prevent misuse after release. destroy must not race unknown callers.
- Sanitizer and hardware isolation claims require actual workflow results.
