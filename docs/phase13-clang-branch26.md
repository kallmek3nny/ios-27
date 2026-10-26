# phase 13: true Clang Mach-O external BRANCH26 relocations

**Scope:** unlinked, SDK-free ARM64 Darwin MH_OBJECT files, not a general linker or iOS executable.

## Verified tooling limitation

Clang 17 emits genuine ARM64 Mach-O objects, but the available ld64.lld rejects
the macOS platform link with: 'This version of lld does not support linking
for platform macOS'. No claim of a linked Darwin executable is made.

## Implementation

- Two independent redistributable C files, arm64_darwin_fixture.c and
  arm64_darwin_consumer.c, are compiled in CI with clang --target=arm64-apple-darwin
  -ffreestanding -O2 -c. No Apple SDK or proprietary objects are involved.
- ios27_darwin_branch26_inspect checks the real MH_OBJECT header, ARM64_ALL
  subtype, macOS build-version metadata, bounded executable __TEXT,__text
  section, one relocation, symbol and string table bounds, external
  _fixture_consumer definition and exactly one undefined _fixture_entry.
- Only an aligned external ARM64_RELOC_BRANCH26 (2) with PC-relative
  four-byte BL instruction and initial zero displacement is permitted.
  arm64e/PAC, arbitrary imports, other relocations, dyld dependencies and
  linked executables are refused rather than guessed or silently skipped.
- ios27_darwin_branch26_apply calculates the signed 26-bit branch field
  in a caller-owned copy, checks alignment and +/-128MiB range, and commits
  only after all checks pass. Failure leaves the copy unchanged.
- Strict mutation tests cover bad relocation address/type/count, incorrect
  extern or PC-relative flags, unsupported instruction, subtype, invalid
  symbol-table range, forward/backward patching and out-of-range rollback.
- ARM64 Linux CI places the two trusted compiler-produced instruction
  sequences in private anonymous pages, applies the validated relocation
  and seals RW -> RX (never RWX), then calls the resolved function natively.
  The expected return value is 43, with no CPU emulator.
- x86_64 CI performs read-only validation and deterministic patch tests.

## Limits and device safety

MH_OBJECT is NOT a linked Mach-O app, signed executable, iOS userland,
dyld, libsystem, XNU service, arm64e runtime, UIKit or official SpringBoard.
The native fixture runner is NOT a sandbox; never execute untrusted code.
Linux ARM64 CI is not a test on the Galaxy S25+.
Do not alter bootloader, boot partitions, firmware, recovery, Odin/D2
restrictions, verified boot, SELinux, persistent root or kernel state.

## Reproduce on trusted Linux

    timeout 120s cmake -S . -B build -DIOS27_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
    timeout 120s cmake --build build --parallel 2
    timeout 120s ctest --test-dir build --timeout 30 --output-on-failure

## Next

1. Find or build a trustworthy open-source ARM64 Darwin linker and
   reproduce a genuine freestanding linked MH_EXECUTE on both CI runners.
2. Independently test additional actual compiler-produced data relocations
   while enforcing an explicit allowlist and W^X.
3. Research real arm64e/PAC, XNU/libsystem, dyld shared caches, private
   framework/entitlement requirements and Apple Metal/CoreAnimation
   feasibility on unchanged Android/Linux before promising the official UI.