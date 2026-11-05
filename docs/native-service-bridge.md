# Native service bridge for trusted fixtures

`ios27-services-loader` connects an original, compiler-produced ARM64 Mach-O
executable to the existing bounded phase20e userspace service model. The
program runs in the exec helper's fresh address space, makes real native C
calls into the service table, and returns through the normal supervised
report protocol. It uses the same mandatory syscall filter as `ios27-loader`.

This is an original research ABI for reviewed SDK-free fixtures. It is not
Apple's libSystem, standard Darwin symbol binding, SVC/syscall translation,
general iOS application compatibility or an untrusted-code sandbox.

## Run the compiler-produced workload

On native ARM64 Linux, build the standard CMake targets, then run:

```sh
./build/ios27 run-exec ./build/ios27-services-loader ./build/service-bridge-executable --unsafe
# exec ARM64 entry returned 42
```

The unchanged service fixture opens a virtual `/tmp/native-service.txt`,
writes and seeks, checks failed-read rollback, reads the bytes back and
closes the descriptor. It verifies forbidden paths, bounded memory spans,
virtual mappings and protection changes, Darwin errno, virtual identity and
the synthetic clock. Its result is computed from fresh initialized globals,
BSS and read-only data. It uses no Apple code, SDK, firmware or CPU emulation.

All five entry arguments can be exercised explicitly:

```sh
./build/ios27 run-exec ./build/ios27-services-loader ./build/service-bridge-executable --unsafe \
  --arg service-fixture --arg vectors --env MODE=guest --apple service=explicit
# exec ARM64 entry returned 42
```

The regular `ios27-loader`, fork runner and in-process runner supply no
extension in x4. This fixture returns 201 when the service table is absent,
and 202 for a mismatched table version/size. Existing four-argument fixtures
can also run through the services helper and simply ignore the extra argument.
Neither helper is meant to be invoked directly.

## Deliberate ABI and resource boundary

The [named static shim](named-service-shim.md) builds on this model. It
marshals ordinary C pointers for ten declared functions and receives its
table through a validated private data record. Its client keeps a normal
four-argument `main` and the entry trampoline clears x4. The explicit table
interface described below remains supported for the original callback client.

The first four registers keep the existing bounded `LC_MAIN` C ABI:
x0=argc, x1=argv, x2=envp, x3=apple. Only the separate services helper places
a pointer to `ios27_service_api` in x4. The table in
`include/ios27/service_bridge.h` has version and size fields followed by three
integer-only callback pointers. Its 64-bit layout is checked at compile time.

| Callback | Contract |
| --- | --- |
| `syscall(number, a0, ..., a5)` | Raw Darwin BSD selectors; pointer arguments are service-arena offsets. Returns -1 on error. Selectors wider than 32 bits fail with ENOSYS instead of wrapping. |
| `memory(offset, length, write_access)` | Returns a checked span in the helper-private 64 KiB arena. Access must be 0 (read) or 1 (write). Invalid spans fail with EFAULT. |
| `error()` | Returns the model's Darwin errno; successful operations preserve an earlier error. |

The underlying [phase20e model](phase20e-services.md) supplies read/write/open/
close, virtual identity, mmap/mprotect/munmap, gettimeofday and lseek. Its
file contents, descriptors, mappings and error slot are private to each helper.
Every exec starts with an empty virtual filesystem and fresh arena. The
virtual PID 101, UID 501 and GID 20 are explicit model values, not host IDs.
The clock is deliberately synthetic; it is not a claim of host-time support.
Guest stdout/stderr writes remain in the model's bounded buffers and are not
forwarded to the host terminal or returned to the launcher by this ABI.

No host file path, host environment or host syscall capability is imported
through the table. Direct native Linux syscalls remain subject to the existing
filter and a disallowed call still terminates the guest with SIGSYS. The
helper resolves its ELF memory/string functions eagerly before the filter;
it does not broaden the allowlist to support lazy dynamic linking.

The memory callback performs model checks. It does not make a returned
pointer into a kernel-enforced capability: a trusted guest must stop using a
span after a virtual protection change or unmap. Native C pointers and the
helper's runtime mappings are visible in the same process. Guest threads,
untrusted guests, arbitrary host calls and Apple framework loading remain
outside the contract. Crashes and timeout teardown are process-contained.

## Evidence and checks

The build compiles `tests/fixtures/service-bridge.c` with
`clang --target=arm64-apple-darwin` and links a real `MH_EXECUTE` using LLVM
ld64.lld or the existing unchanged, narrow phase19a source-linker profile.
The build always produces and checks the fallback output, even when LLVM is
installed, and native tests run both linked outputs. Their section layouts
are checked for both 4 KiB and 16 KiB granules. The same C
client is also compiled as a native-host fixture; these are distinct execution
paths and the host build is not evidence that Mach-O entry ran.
The linked-dylib CI workflow installs LLVM and runs both service execution
tests on its native ARM64 runner. The service fixture's CMake discovery
recognizes version-suffixed LLVM linker names as well as `ld64.lld`, so an
installed linker is not silently missed while the fallback alone is tested.
Older fixtures retain their existing linker selection and load-command profiles.

CTest includes:

- `bounded-userspace-services`: the original phase20e regression suite, now
  integrated into the standard build and x86_64/ARM64 CI.
- `bounded-service-table`: wide selector refusal, checked spans, file lifecycle
  and state reset through the public table shape.
- `contained-service-callbacks`: the real host-compiled client under mandatory
  seccomp, table-version refusal and fork-private model state.
- `contained-native-services`: the linked Mach-O client through the services
  exec helper, startup vectors, repeated fresh launches, ordinary-helper
  refusal, compiler-produced traps/loops/raw syscalls and recovery afterward.
  This suite explicitly skips on x86_64.
- `contained-native-services-fallback`: the same native execution and recovery
  checks against the source-linker output, including when LLVM is installed.
- `contained-process-cli`: end-to-end CLI service-helper invocation and the
  same signal, timeout and recovery outcomes.

The trap/loop/raw-syscall modes are deliberately authored C/inline assembly
test workloads. Service behavior comes from the original bounded userspace
model, not Linux syscall passthrough. Native ARM64 Linux CI is required to
validate the Mach-O bridge; Android/physical-device behavior remains unverified.
