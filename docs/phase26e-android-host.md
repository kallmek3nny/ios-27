# phase26e: isolated android host diagnostic, not UIKit

This workstream supplies a **standalone Android host package**, bounded C11 scene
fixtures, and an independent native test runner. It does not contain Apple's
UIKit, CoreAnimation, SpringBoard, SwiftUI, a Mach-O guest loader, or iOS apps.
All Java/Canvas rendering is explicitly diagnostic and not an Apple UI shim.

## Milestones and boundaries

1. **Scene and responder model:** monotonic non-reused scene/window/view IDs,
   bounded capacities, nested parent relationships, view removal with descendant
   retirement, clipped hit testing, stable z ordering and local-to-window point
   conversion. Frame and event ownership is process-local.
2. **Input contract:** up to 16 simultaneous pointer identities; down capture,
   move/up/cancel delivery, bounded tap recognition, bounded UTF-8 committed and
   composing text, key events, focus loss and basic accessibility focus routing.
   Malformed coordinates/UTF-8/IDs are rejected. An event log of 512 entries
   uses oldest-event eviction and counts drops; routing counters do not rely on
   retention in the diagnostic log. No external input reaches guest memory.
3. **Lifecycle and graphics adapter:** foreground/background/terminated states,
   strict surface dimension and rotation validation, generation invalidation,
   automatic pointer cancellation and coalesced frame requests. A future
   compositor must own actual image generation and call the generation-checked
   present contract; the current Android Canvas paints diagnostic text only.
4. **Android shell and verification:** userspace-only SurfaceView activity,
   Choreographer frame pacing, native host mutex protection, no special
   permissions, explicit `P26_DENIED` for audio/permission services, separate
   Werror native stress test and Android build CI.

The native core uses pthread mutexes to serialize shared state. `terminate` is
idempotent and prevents new operations. Callers **must join all threads before
`phase26e_destroy`**, because destruction itself cannot race arbitrary calls.
No sandbox boundary is provided for executing unknown native guest binaries.
The shell never loads one. Malformed events cannot mutate OS settings.

## Reproduce (Linux or arm64 Android host toolchain)

From the repository root:

```bash
timeout 180s bash tools/phase26e-verify.sh
PHASE26E_SANITIZE=1 timeout 180s bash tools/phase26e-verify.sh
```

Test runner covers 100,000 pointer moves, 10,000 suspend/resume transitions,
10,000 surface recreations, stale-frame rejection, invalid UTF-8, input and
view cancellation, hit testing, and concurrent termination. Counts are from
fixture stress, not OS or UIKit compatibility.

Android APK build, from `android/phase26e`, requires JDK 17, Android SDK 35,
CMake 3.22.1, NDK 27.2.12479018, and Gradle 8.10.2:

```bash
timeout 300s gradle :app:assembleDebug --no-daemon --console plain
```

CI emits `app-debug.apk` as an artifact only when compilation succeeds.
Android SDK and Gradle are not bundled. Do not claim the APK exists until a
successful build artifact is verified.

## Physical Galaxy S25+ verification (NOT EXECUTED)

1. On normal stock firmware, enable developer debugging through Android settings
   only if comfortable. No root, partition writes, bootloader unlock, SELinux
   changes, flashing or kernel changes are part of this test.
2. Install the CI-built diagnostic APK with a user-approved ordinary package
   installer or `timeout 60s adb install -r app-debug.apk`.
3. Open the app and rotate portrait/landscape 20 times; watch `adb logcat`
   for JNI errors, ANR, surface crashes, and resource failures.
4. Perform simultaneous 2–5 finger gestures, back/foreground transitions,
   keyboard composition, hardware-key navigation, and accessibility focus.
   Verify cancellations after orientation changes and app switches.
5. Repeat force-stop/launch 30 times, inspect memory and frame timing with
   Android Studio profiler or `dumpsys gfxinfo`/`dumpsys meminfo` (read-only).
6. Test app permission denial, ensure no system-level permissions are requested,
   and check no background CPU loop after the activity pauses.

All device outcomes are **UNVERIFIED** until actual instrumented evidence exists.

## Integration contract

Future phase26c/26d code should use an explicit adapter around
`phase26e_request_frame`, `phase26e_get_metrics` (surface generation), and
`phase26e_present`. No phase26c/26d symbol or header is imported here. Real
`UIApplication`/`UIWindow`/`UIView`/`UIViewController` ABI compatibility remains
outside phase26e's demonstrated capabilities.