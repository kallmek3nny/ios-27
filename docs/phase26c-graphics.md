# phase 26c: bounded graphics resources (headless first)

status: prototype only. no real Apple Metal, IOSurface, UIKit, CoreAnimation,
SpringBoard or Vulkan execution has been demonstrated. no Samsung hardware
verification was performed. this phase has no effect on compatibility metrics.

## integration surface for phase 26d

`include/ios27/phase26c_graphics.h` is a standalone C11 ABI. compile with
`src/phase26c_graphics.c` and link pthreads. it does not depend on a renderer,
android host, SDK, or changed global build file. handles identify the owning
guest/device (16-bit namespace), slot (16-bit), and generation (32-bit).

- **milestone 1:** strongly typed devices, queues, buffers, textures, surfaces,
  command lists, frames; 512 slots; checked dimensions, row strides, format,
  padding, alignment, per-device allocation quota, checked offsets, and
  rejections for unsupported combinations. currently RGBA8/BGRA8/R8 only.
- **milestone 2:** retain/release with resource pins in pending commands;
  exclusive CPU maps with tokens; stale generation and cross-device guards;
  no implicit destruction while mapped; device loss can be simulated and
  outstanding handles can be released cleanly. for shutdown, release all
  resources and maps before `p26_device_shutdown`, then destroy. destroy is
  only valid after successful shutdown and may not race active API callers.
- **milestone 3:** ordered synchronous CPU command buffer submission with
  preflight validation, buffer memmove, buffer-to-texture upload, 4-channel
  RGBA/BGRA swizzle, image copy, terminal success/failure semantics, and
  monotonically sequenced, *already completed* queue fences. these are not
  actual GPU timeline semaphores or async submissions.
- **milestone 4:** presentation snapshots with independent lifetime and
  deterministic content hashing (64-bit FNV-1a, dimension and format encoded,
  visible bytes only). frame bytes are immutable through public writing and
  mapping APIs. a frame remains live after its surface is retired.

The standalone headless harness runs 10,000 frame snapshots, 100,000
allocation/deallocation cycles with stale-handle checks, 100,000 additional
multithreaded retain/release pairs, hash comparisons with independent reference,
failed-command rollback, invalid dimensions and formats, map exclusion,
resource exhaustion, isolated namespace rejection, and simulated device loss.

### Android bridge (unverified)

`android/phase26c/ahb_bridge.{h,c}` has optional Android API 26+
`AHardwareBuffer` CPU-staging import/export for supported RGBA8 buffers. It
is a **copy**, not a zero-copy shared IOSurface or an Android GPU import.
Host builds deliberately return `P26_UNSUPPORTED`. The code has not been
compiled or run with an Android NDK/device here. No native Vulkan or GPU
backend has been confirmed. No Metal API call reaches Android hardware.

### usage

```sh
timeout 120s bash tools/phase26c-verify.sh
P26_SANITIZE=1 timeout 180s bash tools/phase26c-verify.sh
```

The API is currently CPU-only. Do not hand out mapped pointers to threads
that can tear down the device; release maps and references explicitly. Command
recording retains source/destination until successful submission or command
release; failed submits leave the command pending to permit correction or
cancellation by releasing it. Queue submissions are synchronous and return a
completion fence. Frame snapshot memory counts against the device quota.

### next required verification

- run native arm64 Linux CI (not a substitute for Samsung/Android execution);
- compile bridge with supported Android NDK and test AHardwareBuffer
  allocation, usage flags, fence FD handoff, and device loss on-device;
- implement a separately verified Vulkan backend with real buffer/image
  memory barriers, ownership transitions, and GPU fences before claiming GPU
  command submission;
- integrate with Apple framework surfaces only after validated framework ABI
  and IOSurface/CoreAnimation semantics exist.

No shared metric files, phase26d compositor code, or phase26e host files
are modified by this phase.