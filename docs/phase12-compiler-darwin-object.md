# phase 12: true clang-produced arm64 Darwin relocatable fixture

this phase deliberately narrows "real Darwin binary" to an **SDK-free
compiler-produced Mach-O MH_OBJECT**, not an iOS executable, dynamically
linked app, signed Apple binary, dyld cache image, or Apple's system UI.

## construction and verification

- `tests/fixtures/arm64_darwin_fixture.c` contains redistributable plain C
  functions returning fixed integer values. it includes no Apple headers,
  proprietary framework assets, syscalls, build-time download or root access.
- normal cmake builds invoke installed clang with
  `--target=arm64-apple-darwin -ffreestanding -O2 -c` and an explicit 30s
  hard timeout, producing `build/arm64_darwin_fixture.o` on **both**
  x86_64 and ARM64 CI hosts.
- `ios27_darwin_object_inspect` validates real compiler-produced
  `mach_header_64`, ARM64 ALL CPU subtype, MH_OBJECT type, macOS
  `LC_BUILD_VERSION`, proper `__TEXT,__text` section bounds, external
  `_fixture_entry` nlist symbol within that section, and bounded, terminated
  Mach-O symbol/string tables. Unknown/undefined symbol forms and
  section relocations are rejected rather than silently linked.
- `test_compiled_darwin_object` reads the actual clang output, inspects its
  section and symbol offsets, rejects deliberate corruptions of platform,
  subtype, section relocation count, symbol-table ranges and symbol type.
- on native Linux ARM64 the test copies **only the verified trusted
  relocation-free __text section** into a private userspace page, switches
  RW to RX (never RWX), invalidates the instruction cache, and calls the
  compiler-produced `_fixture_entry` symbol. the expected return value
  is 42 with **no CPU emulator**. on x86_64, native execution is omitted
  and metadata parsing is tested only.

## limits

- MH_OBJECT contains unlinked sections and a Mach-O object may report
  RWX as **segment metadata**. this is NOT permission for runtime RWX
  mapping or generic application execution. only trusted __text is
  temporarily mapped RW and then sealed RX in the ARM64 test.
- no relocation handling is claimed for the compiler object, no loader
  for arbitrary Darwin executables is added, and no binary is taken
  from Apple. the test deliberately rejects arm64e/PAC and unknown
  object layouts. clang's Darwin target is not an iOS SDK.
- a separate full Darwin link with this environment's ld64.lld did
  **not** work. passing this phase proves an ARM64 Mach-O compiler object
  is emitted and optionally executable as a freestanding section. it does
  not prove code signing, dyld, real Darwin syscalls, UIKit, SpringBoard
  or Liquid Glass.
- no changes to Samsung bootloader, firmware, partitions, Odin/D2,
  recovery, root/KernelSU/DirtyFrag state, privileged kernel APIs or
  persistent device data. **Never execute untrusted Mach-O code.**

## reproducible test steps (trusted Linux)

```sh
timeout 120s cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DIOS27_BUILD_TESTS=ON
timeout 120s cmake --build build --parallel 2
timeout 120s ctest --test-dir build --timeout 30 --output-on-failure
```

## next gated milestones

1. link a simple freestanding, legally redistributable arm64 Darwin
   **executable** using a verified open-source linker. inspect its actual
   LC_MAIN/dyld/segments/relocations and compare against the restricted
   Mach-O loader. do not force unsupported linker flags to fabricate
   an executable profile.
2. run independently verified W^X fixtures on an actual 16 KiB
   Linux/Android host; a 16 KiB dyld chain declaration is not proof.
3. build targeted xnu/dyld/libsystem ABI conformance checks using
   open-source headers and synthetic test inputs; no private Apple
   SDK/framework redistribution.
4. tackle real iOS ARM64e/PAC and Apple system-services/graphics blockers
   before claiming any SpringBoard UI progress.
