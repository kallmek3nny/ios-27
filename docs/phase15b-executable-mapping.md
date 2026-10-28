# phase 15b: native freestanding MH_EXECUTE loading

## implementation

- The Mach-O parser remains non-executing and rejects truncated load-command/segment/file bounds.
- `ios27_macho_executable_layout` is host-independent: both 4096- and 16384-byte host page granules are checked on all CI machines.
- The preflight reparses the original bytes, rejects inconsistent/stale metadata, enforces freestanding ARM64 `MH_EXECUTE` with a single valid `__TEXT` and `LC_MAIN`, denies dyld dependencies, fixups and encrypted contents, and verifies a four-byte-aligned file-backed RX entrypoint.
- Every included VM segment must have valid protections with neither initial nor maximal W+X. Overlaps are checked at *host page* granularity, so two disjoint 4k segments sharing one 16k page are refused instead of becoming RWX.
- Abnormal `__PAGEZERO` mappings are rejected, while conventional no-access zero-file-size pagezero reservations are excluded from the host user-space span.
- Mapping reserves one private `PROT_NONE` span. Individual segments become temporary RW for copying, then their final permissions are sealed before entry invocation. Failure unmaps the whole reservation. No fixed host address, privilege or bootloader access.
- Address-space gaps remain inaccessible, and the anonymous reservation zero-fills BSS tails. Entry is never called unless every segment maps and seals correctly.
- Tests exercise 4k success, 16k rejection for subpage overlaps, 16k success with separate granules, stale layout information, bad `LC_MAIN`, invalid W+X, and overlap rejection. ARM64 CI also executes the trusted `mov w0,#42; ret` fixture.

## limitations

This is *not* a sandbox or general iOS executable runtime. `ios27_run_freestanding` invokes only trusted, self-contained fixture code with the non-Darwin `int(void)` entry convention. No dyld, Apple system binaries, Darwin syscalls, ObjC, arbitrary linked image dependencies, shared cache, PAC entry support or native iOS app compatibility is claimed. ARM64 Linux/Android host user-space is required for execution and 4k/16k page sizes are supported; unfamiliar page sizes fail closed.

## reproducibility

Run each with a hard timeout:

```sh
timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
```

No proprietary Apple binaries or privileged device changes are needed.
