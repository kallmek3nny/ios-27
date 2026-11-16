# Native simulator display/input helpers

These are native ARM64 ports of the pinned GPL-3.0 x86Sim source interfaces:
SimFramebuffer, SimulatorHID and the IOSurface service. Source hashes and the
upstream license hash are in `sources.json`; source is downloaded at build time.

`simfb-native.patch` replaces the translator-specific Linux calls with the
actual AArch64 syscall ABI in `linux-arm64.h`. On the S25+, an independent
process blocked through libc's Linux futex interface is correctly awakened by
this header's raw syscall. Its `MADV_REMOVE` call also succeeds and the removed
file-backed range reads as zero. The test is `../tests/linux-arm64.c`.

All three helpers compile and link as ARM64 iOS Simulator images. Their
89/113/97 imported symbols respectively resolve in the local 24A434 simulator
runtime. Build-time TAPI files declare those verified symbols; the linked
images name the real runtime libraries, not the TAPI files.

```sh
python3 tools/darling-simulator-shims.py \
  --darling-build /path/to/configured/darling-build \
  --runtime-root /path/to/iOS.simruntime/Contents/Resources/RuntimeRoot \
  --output /path/to/private/simulator-shims
```

The helper uses Darling's open-source SDK, its configured ARM64 compiler and
LLVM's Darwin linker. It retains x86Sim's license with the build products.
The output report records source, patch, header and binary hashes. It also
packages the two framework bundles with `Info.plist`; SimulatorHID declares
`SimHIDSystem` as its principal class for the simulator's bundle loader.

The simulator environment uses `SIMULATOR_FRAMEBUFFER_FRAMEWORK` for the
SimFramebuffer binary and `SIMULATOR_HID_SYSTEM_MANAGER` for the SimulatorHID
bundle directory. Cross-process IOSurface operation additionally needs the
`com.apple.IOSurface.Remote` Mach service from `iosurfaced` and removal of
`IOSURFACE_REMOTE_EMULATED`. These are configured only after minimal simulator
bootstrap succeeds.

**Integration status:** linked helpers and native IPC are verified. Execution
inside the iOS runtime, actual frame output and Android touch delivery are
still pending simulator startup.
