# phase 22d: concurrent objc-compatible runtime substrate

## objective evidence and scope

This phase extends the existing read-only ObjC work with a separate, bounded
runtime. It does **not** replace Apple's libobjc, dyld, the Mach-O executable
loader, Apple frameworks, ARC, Swift ABI, exceptions, arm64e PAC or forwarding.
The build artifacts must not be treated as evidence of native iOS app execution.

- **milestone 1, registration:** two-phase transactional class registration,
  selector pointer interning, class/metaclass identities, forward superclass
  resolution, duplicate rejection, cycle checks, instance sizing and quotas.
- **milestone 2, dispatch:** inherited and lexical-super method resolution,
  atomic category override/installation, exact native function pointer types
  for 0..4 unsigned-word arguments and 1 double argument, and rejection of
  unsupported signatures rather than guessing at ABI layouts.
- **milestone 3, concurrency:** synchronized registration, method lookup and
  object identity checks; serial process-wide parent-first +initialize gate,
  owner-thread recursion and retry on initializer failure, with diagnostics.
  Method IMP invocations occur outside the lock and pin a live receiver.
- **milestone 4, ownership:** explicitly managed +1 allocations, guarded
  retain/release, single deallocator callback, weak-reference zeroing before
  deallocation, atomic weak-load-retained, tombstones preventing pointer reuse
  within a runtime, quota enforcement and state validation.
- **milestone 5, compiled metadata:** the existing unmodified
  objc_metadata_inspect parser validates real Clang-created ARM64 Darwin
  MH_OBJECT compiler output before a trusted, explicitly ABI-typed binding
  table can be registered. Each class and every instance-method selector,
  method type encoding, superclass and instance size must match. No foreign
  machine-code IMP pointer is ever made executable by this stage.

## compiled validation

Run independently, with hard command limits:

```sh
timeout 900s python3 tools/phase22d-verify.py
timeout 900s env PHASE22D_SANITIZE=1 python3 tools/phase22d-verify.py
```

Both GNU C and Clang compile the C stress/integration program with
`-std=c11 -Wall -Wextra -Wpedantic -Werror`. Clang also compiles actual
SDK-free Objective-C source into ARM64 Darwin MH_OBJECT at -O0 and -O2.
A strict Mach-O header/section/symbol check and the existing production ObjC2
metadata parser inspect those artifacts, including negative tests and 512
deterministic mutated images per host/compiler/optimization combination.
Sanitizer mode uses AddressSanitizer + UndefinedBehaviorSanitizer under Clang.
Dedicated PR CI runs on x86_64 and ARM64 Ubuntu runners.

The stress program performs 120,000 actual typed C IMP invocations across
six workers, at least 240,000 explicit retain/release requests, plus automatic
receiver pins. It covers parent/child +initialize recursion and ordering,
class/metaclass methods, inherited and super dispatch, method ABI mismatch,
runtime category replacement during dispatch, concurrent class registration,
cycles, duplicates, invalid selectors, failed initializers, weak zeroing,
destructor exactly-once and invariant checks. These are compiled C host-native
calls, not ARM64 Mach-O guest execution.

## important limitations

- The native Darwin ARM64 ObjC fixture is compiler-produced and inspected,
  **not run** by the host x86_64 runtime. No native Apple libobjc ABI success is
  claimed without an ARM64 Darwin guest execution environment.
- Only explicitly trusted function pointers and exact supported type encodings
  are callable. 32-bit scalar ABI, structs, vectors, floating-point mixtures,
  variadic methods, small/relative ObjC method lists, swizzling across images,
  categories' compiler metadata binding, ARC side tables, and exceptions are
  not implemented. Compiler-discovered class methods require independent
  trusted registration; the read-only parser exposes instance methods only.
- Objective-C +load behavior and image unload synchronization are not modeled.
  A runtime must not be destroyed while workers are still using it. Callers
  own weak-location storage and must unregister it before freeing that storage.
- Methods must not use an object after its last owned release. Dispatch pins
  objects for its own call duration, but callers must keep a separate strong
  reference while submitting calls.
- Per-runtime object allocations are tombstoned until destroy, trading a
  bounded memory quota for stable identity / stale-pointer rejection.
- Initialization callbacks must not wait for completion of foreign worker
  threads attempting first-message initialization in the same runtime.
  Registration is rejected while initialization is active.
- Timing claims must be taken from actual CI logs, not estimated or padded.
