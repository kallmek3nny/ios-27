# Named Darwin service calls from a four-argument client

The original static shim lets an SDK-free ARM64 Darwin client call named
`getpid`, `open`, `read`, `write`, `close`, `lseek`, `mmap`, `mprotect`,
`munmap` and `__error` functions. LLVM resolves those client imports against
a separately compiled original shim object. The resulting `MH_EXECUTE` uses
an ordinary `main(argc, argv, envp, apple)` entry; it does not receive or read
the earlier callback-table argument in x4.

This implements a declared, bounded subset with Darwin symbol names and
fixed-width C prototypes. Apple libSystem, dynamic dylib imports, general
Darwin SVC translation and Apple app compatibility remain unimplemented.

## Run the original workload

Build with Clang's ARM64 Darwin target and LLVM `ld64.lld` available. On
native ARM64 Linux:

```sh
./build/ios27 run-exec ./build/ios27-services-loader ./build/service-shim-executable --unsafe
# exec ARM64 entry returned 42
```

The same four startup arguments are exercised with:

```sh
./build/ios27 run-exec ./build/ios27-services-loader ./build/service-shim-executable --unsafe \
  --arg service-fixture --arg vectors --env MODE=guest --apple service=explicit
# exec ARM64 entry returned 42
```

The client source includes only the named shim declarations. It has no
service-table parameter or callback-table include. It uses ordinary string
literals, stack buffers, a variadic `open(..., 0600)` call and mapped pointers.
Its own checks exercise file lifecycle, negative seeks, errno preservation,
read-only destination refusal without advancing the file position, virtual
mapping protections and stale-map refusal. Fresh globals/BSS and virtual
files are checked on every launch. Authored trap, loop and raw-Linux-syscall
modes test child teardown followed by successful recovery.

## Loader binding record

The shim defines one 16-byte `__DATA,__ios27_api` record:

| Byte offset | Field | On-disk value |
| --- | --- | --- |
| 0 | 32-bit binding version | 1 |
| 4 | 32-bit record size | 16 |
| 8 | 64-bit service-table pointer | 0 |

`ios27_service_binding_inspect` performs a read-only preflight. Generic
Mach-O validation checks command and section bounds, VM/file correspondence,
overlap and unsupported runtime fixups. Binding validation requires exactly
one correctly sized/aligned, file-backed record in a RW/non-executable
`__DATA` segment, a supported header and a zero pointer. Duplicate records,
invalid alignment, unsupported headers and prebound pointers fail with a
cleared plan. Images without a record continue to use the earlier behavior.

Only the services helper writes the approved pointer into its private staged
copy. It does this before final image protections and before guest readiness;
the source file and sealed payload are not modified. For a bound shim the
entry trampoline explicitly clears x4. Older callback-table clients without
the record retain their versioned fifth-argument interface. The ordinary
helper leaves the record unbound; named calls then fail with ENOSYS and the
fixture returns 201.

## Named-call boundary

The shim copies ordinary C path/buffer contents through reserved scratch
space in the private service arena. Guest pointers are not passed to the
model's offset-based syscall interface. The first 32 KiB of the arena remain
private scratch/model space; the shim's virtual mmap returns pointers into
the upper half. The shim checks those mapped spans against current model
read/write protections before marshalling I/O.

| Area | Supported boundary |
| --- | --- |
| Paths | At most 95 bytes plus NUL; model `/tmp` and `/app` policy applies. |
| Read/write | At most 4096 bytes per call; oversized calls return EINVAL. |
| Open | Read/write/create flags from the bounded model; the variadic mode argument is accepted but virtual permission bits are not implemented. |
| Errno | Guest-local `__error()` storage; successful calls preserve it. One guest thread, without TLS errno. |
| Mappings | At most 32 KiB total in the virtual model; page-aligned mprotect/munmap addresses and 4096-byte-multiple lengths. |
| Host effects | No host file or syscall passthrough. Virtual stdout/stderr remain captured in model buffers. |

Ordinary client buffers must be valid trusted C objects. Invalid non-arena
pointers can fault the supervised child. Virtual mapping protections are
model checks on the shim interfaces, not kernel-enforced restrictions on
direct C pointer dereferences. This remains trusted-fixture containment.

## Import audit and validation

`tools/service-shim-build.py` separately compiles the client and shim as real
ARM64 Darwin objects, then checks:

- The client imports exactly the ten named functions listed above.
- The shim has no unresolved runtime imports and only the expected object
  definitions, including its private binding record.
- The final linked executable has no unresolved symbols.

The generated `service-shim-symbols.json` records those sets, the declared
four-argument entry, hashes of the client/shim sources and ABI headers, and
the linked executable hash. The older one-object fallback linker is not
extended to pretend to support this multi-object link. When LLVM is absent,
the named Mach-O integration tests explicitly skip; host shim tests remain.

CTest adds `bounded-named-service-shim`, `service-binding-metadata` and
`contained-native-service-shim`. The metadata tests mutate the real linked
record, verify failure clearing and read-only inspection, and on ARM64 verify
setup refusal before readiness and recovery with the valid image. Existing
native and CLI suites exercise the four-argument client with normal returns,
startup vectors, faults, deadlines, blocked syscalls and repeated fresh state.
The LLVM-installing CI workflow runs the named shim and metadata checks too.
