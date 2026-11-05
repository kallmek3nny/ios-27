# phase 27e: installable native graphical Android host

This is **an original software-rendered diagnostic scene**, not execution of UIKit, SwiftUI,
SpringBoard, real iOS applications, or an Apple framework. It deliberately retains the
phase26e scene/event/lifecycle APIs and compiles its existing C implementation from PR #112.
The presentation backend is original RGBA8888 drawing, with an 8 MiB bounded direct-buffer
staging limit. No proprietary binaries, root, privileged permissions, firmware, kernel,
or bootloader changes.

## Integration contract and ownership

* `android/phase27e` is a separate Gradle application, id `dev.ios27.phase27e`,
  minSdk 28, target/compile SDK 35, Java 17, AGP 8.7.3, Gradle 8.10.2,
  CMake 3.22.1, NDK 27.2.12479018, arm64-v8a only.
* JNI owns a bounded four-entry native host registry with monotonic tokens (never
  Java-exposed addresses), serializes access, retires tokens before reuse, and
  rejects nonexistent handles, stale generations, invalid dimensions, malformed
  Unicode and non-direct/undersized frame buffers.
* Input goes to `phase26e_pointer`, then through `phase26e_drain` to the original
  `p27_scene_pointer`. Scene and window identifiers come from `phase26e_scene_add`
  and `phase26e_window_add`, not synthetic Java identifiers.
* `p27_scene_render` produces a validated RGBA8888 frame, then Java copies to an
  ARGB_8888 `Bitmap` and presents via `SurfaceHolder.lockCanvas()` and
  `unlockCanvasAndPost()`. `phase26e_present` only follows successful posting,
  guarded by the same surface generation.
* This is the explicit swap boundary for agent 3's surface provider and agent 4's
  scene provider: a caller-owned direct RGBA buffer, validated width/height/stride,
  a surface generation token, normalized native events, and deterministic timestamp.
  Their phase27c/27d integration is **not yet claimed**; replace the original
  demo scene provider only after those changes exist and are tested together.
* The app redraws on touch/IME/focus/resume/resize and for at most 700 ms following
  touch. There is no background 120 Hz loop. `onPause`, surface retirement, and
  focus loss cancel tracked touches. Rotation creates a new generation.
* No audio output or runtime permission surface is implemented. This demo requires
  no permission and correctly avoids requesting privileged APIs.

## Build (on a machine with the Android SDK installed)

```sh
cd android/phase27e
gradle :app:assembleDebug --no-daemon --console plain
# signed debug artifact: app/build/outputs/apk/debug/app-debug.apk
```

The GitHub Actions workflow `.github/workflows/phase27e-android.yml` provisions
pinned SDK/NDK dependencies, runs sanitizer tests on x86_64 and ARM64, builds the
APK and publishes `phase27e-arm64-debug-apk`. There is no Gradle wrapper binary;
use Gradle 8.10.2 (`gradle --version`) before building locally.

## Android emulator validation

Choose an ARM64 Android API 35 AVD on a native ARM64 host, then run:

```sh
adb devices -l
adb install -r android/phase27e/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.ios27.phase27e/.MainActivity
adb shell input tap 200 500
adb shell input swipe 200 500 700 850 450
adb shell input keyevent 3  # HOME; return using launcher
adb logcat -d -s ios27-27e AndroidRuntime DEBUG > phase27e-emulator.log
```

Check: background/resume, multi-pointer interaction by hand, rotation with AVD
controls, IME text entry, screen-size changes, absence of ANR/crash, and lifecycle
counters. Do not label an unrun emulator test as passing.

## Physical Galaxy S25+ verification checklist (not yet run)

Without rooting or altering the device, enable normal Android developer debugging:

```sh
adb devices -l
adb shell getprop ro.product.model
adb shell getprop ro.build.version.release
adb shell getprop ro.product.cpu.abi
adb shell getconf PAGE_SIZE
adb install -r android/phase27e/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.ios27.phase27e/.MainActivity
adb logcat -c
# manually test multitouch, drag, keyboard/IME, 0/90/180/270 rotation,
# HOME/app switching, resuming, surface destroy/recreate, and termination
adb logcat -d -s ios27-27e AndroidRuntime DEBUG > phase27e-device.log
adb shell dumpsys meminfo dev.ios27.phase27e > phase27e-memory.txt
adb shell am force-stop dev.ios27.phase27e
```

Capture model, Android version, ABI, system page size, install result, a screenshot
of actual software-rendered frames, touch/rotation/resume results, native heap peak,
frame rate, ANRs and crash logs. **No physical device is accessible in this session**;
there is no fabricated Samsung test evidence.

## Limits and remaining milestone

Actual Android `SurfaceView` output and native interactive pixels are implemented.
The C reference renderer and JNI source are locally syntax/sanitizer validated.
The Android APK still requires a compatible SDK/NDK build and Android runtime test.
The Objective-C guest, GPU fences, IOSurface, CoreAnimation renderer, UIKit view
hierarchy, audio bridge, and general guest app loading are explicitly out of scope
for this original demonstration and remain integration work.