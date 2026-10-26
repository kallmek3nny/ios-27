# phase 11: fail-closed task teardown and narrow darwin fixture abi

this change develops a **synthetic in-process compatibility test boundary**.
it does **not** implement xnu kernel mach traps, apple ipc services, actual
darwin task processes, entitlement checks, or official ios ui execution.

## code

- `ios27_mach_v2_task_teardown` is a transactional teardown for one
  simulated task's name table and receive ports. it preflights **all**
  in-flight credit records in the task namespace and all ports owned by the
  task. when any queued transfer is involved, teardown returns `BAD_RIGHT`
  without changing any rights or queues. this deliberately fails closed
  rather than inventing untested mach dead-name notification behavior.
- on success it drops the task's rights, removes its owned ports, cancels
  transfers still queued **on those ports** and revokes every other task's
  aliases referencing the retired port objects. task-local numeric names
  continue monotonically across teardown, preventing stale handle recycling.
- `ios27_darwin_fixture_call` exposes explicit **private, synthetic**
  selector ids, not apple syscall or mach trap numbers. it accepts 8 x64
  register values, a caller-approved synthetic task id and an optional
  bounded guest arena. operations: port allocation, grant/drop rights,
  inspect rights, destroy port, task teardown, and send/receive messages.
- guest bytes go only through the phase-10 validated offset-based
  `ios27_mach_guest_call` api. register file pointers cannot overlap
  each other or the arena. 64-bit register arguments are range checked
  before reducing them to 32-bit task/name/timeout arguments.
- unsupported selectors are denied. on errors, output registers are
  unchanged except `TOO_LARGE` receive, where the required message size
  is returned without consuming the queued message.
- regression tests cover task-local alias revocation, pending incoming
  rights rejection, pending one-use reply rights, revoked reply rights after
  port death, monotonic names, cross-task authority, invalid selectors,
  malformed memory/register arguments and a fixture request/reply exchange.
- both the existing phase-9 api and phase-10 api remain supported.

## explicit safety and limitations

this is neither an apple mach syscall number registry nor a trap interpreter.
there is no os process authentication, service discovery, xnu messaging,
darwin objc/swift, dyld shared cache, ios graphics, apple code signing, or
galaxy s25+ runtime validation. native arm64 linux tests prove only
ordinary native arm64 execution of synthetic redistributable fixture code.

no privileged operations, persistent root, odin/d2 interaction, bootloader,
firmware, partitions, android selinux policy, or boot-time hooks. do not
run untrusted mach-o images in the unsandboxed synthetic native runner.

## reproduce on trusted linux

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
```

## phase 12 proposals

1. expand message rights lifecycle, including deterministic dead-name and
   port-destruction/queued-transfer policy, without relaxing rights checks.
2. independently compile a small redistributable arm64-apple-darwin
   source fixture with a known toolchain and verify its non-framework
   mach-o metadata on both ci architectures before trying to load it.
3. explicitly test a 16k-host memory layout; do not assume passing dyld
   16k chain page tests proves runtime 16k mapping works.
4. assess arm64e/pac, apple dyld shared cache, framework and rendering
   feasibility before making claims about springboard or liquid glass.
