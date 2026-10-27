# phase 14: compiler-produced Darwin GOT load relocations

The local Linux Clang 17 toolchain produces genuine AArch64 Mach-O objects.
The installed ld64.lld rejects `-platform_version macos` and iOS variants
("This version of lld does not support linking for platform macOS").
We cannot honestly claim a genuine linked `MH_EXECUTE`.

## New independently compiled C fixtures

- `arm64_darwin_data_provider.c` defines a 4-byte `_fixture_data = 41` in
  `__TEXT,__const`.
- `arm64_darwin_data_consumer.c` references that external symbol and adds 2.
  Clang emits an ADRP + LDR pair and genuine external
  `ARM64_RELOC_GOT_LOAD_PAGE21` (5) and
  `ARM64_RELOC_GOT_LOAD_PAGEOFF12` (6) relocation records.
- Inspection requires ARM64_ALL MH_OBJECT, macOS build version, exactly those
  two relocations with matching symbol indices, expected instruction bytes,
  bounded symbol/string tables and narrowly allowlisted imports and exports.
- On ARM64 Linux, the test maps separate host pages for text, GOT and constant
  data, binds a writable GOT slot to the constant data, patches the two
  instructions after validation, and seals code RX, GOT and data R.
  Expected result from native ARM64 execution is 43, without CPU translation.
- On x86_64, tests cover parser acceptance/rejection, relocation field
  corruption, bounds, ±ADRP offsets, 4GiB reach and no-write failures.

Only trusted fixtures may be executed; the runner is **not a sandbox**.
This remains controlled Linux ABI execution of unlinked Darwin objects, not
actual iOS, dyld, Apple's arm64e/PAC, a linked executable, or SpringBoard.
No device, firmware, partition, root or kernel changes are involved.

Reproduce:

    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
