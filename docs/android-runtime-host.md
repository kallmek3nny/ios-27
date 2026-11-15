# Android runtime host

The Android app packages the original native runtime, a separate single-threaded
launcher and services loader, and a compiler-produced ARM64 Mach-O workload.
It exercises the named file, mapping and heap interfaces on Android. It contains
no Apple binaries, general framework loader, graphics bridge or iOS system UI.

Version 0.2.0 also requires a real internal global-pointer rebase in the workload
and reports the actual fixup count. See [static pointer rebases](static-pointer-rebases.md).

## Build and install

Requirements: Java 17+, Python 3, Android SDK platform 35, NDK 29.0.13846066,
CMake 3.22.1. Set ANDROID_HOME or an ignored android/local.properties sdk.dir.
The Gradle wrapper pins Gradle 8.13; the app pins AGP 8.10.1. No root is needed.

```sh
bash android/gradlew -p android :app:assembleDebug :app:lintDebug
adb -s DEVICE_SERIAL install -r android/app/build/outputs/apk/debug/app-debug.apk
adb -s DEVICE_SERIAL shell am start -n org.ios27.host/.MainActivity
```

On Windows use android\gradlew.bat instead of bash android/gradlew. The debug APK
is android/app/build/outputs/apk/debug/app-debug.apk. It supports Android 11+
(API 30+) with arm64-v8a and x86_64 host binaries. x86_64 performs metadata
validation and explicitly reports unsupported native guest execution.

Tap **Run bundled ARM64 workload**. The app verifies the bundled Mach-O SHA-256
against its generated symbol manifest, copies it to private app storage and
starts the packaged ELF launcher from nativeLibraryDir. The launcher parses and
validates the image against the actual host page size before invoking the
existing exec supervisor. The services loader receives a sealed payload in a
fresh address space and supplies the bounded service binding. Execution still
requires all existing containment setup, seccomp and kernel resource limits.
The guest's expected return value is 42; signals, timeouts, non-42 returns and
setup failures are reported as failures. No readiness fallback is a pass.

Native executables use .so filenames so Android extracts them into its executable
native library directory; they are PIE executables, not JNI libraries. Java never
calls the fork runner inside the multithreaded application process. The Activity
runs work off the UI thread, enforces a launcher deadline and kills its launcher
when destroyed. The app requests no network, root, storage or overlay permissions.
Only the bundled reviewed fixture is accepted; there is no arbitrary import UI.

## Automated physical-device check

```sh
python tools/android-smoke.py --serial DEVICE_SERIAL \
  --apk android/app/build/outputs/apk/debug/app-debug.apk \
  --output out/android-device-report.json \
  --screenshot out/android-device-check.png
```

This installs/updates only org.ios27.host, clears its previous result, and
performs two cold app launches using a debug-only test extra. Each launch must
produce a fresh hash-verified result with a positive internal rebase count,
guest return 42 and launcher exit 0 by default.
The report records APK SHA-256, model, SDK, ABI, page size, selected mode and both
actual result texts, excluding device serial. The screenshot is optional. Reports/screenshots
are local ignored artifacts; inspect before sharing.

## Debug-only supervised failure modes

The smoke script also accepts `--mode trap`, `--mode deadline`, and
`--mode raw-syscall`. Omission (or `--mode success`) retains two normal-return
checks. Each selection still performs two fresh launches; it is not enough for
any failure, crash, or hang to occur:

- `trap`: supervised `SIGTRAP`, with a 2000 ms supervisor deadline.
- `deadline`: `TIMED_OUT` followed by `SIGKILL`, with a 1000 ms deadline.
- `raw-syscall`: supervised `SIGSYS`, with a 2000 ms deadline.

The Java debug intent accepts `fixture_mode` only with `run_fixture=true` in a
debug build. The ordinary button still selects success. Unknown selectors are
rejected, not interpreted as guest paths or arbitrary arguments. All modes use
the same hash-verified original fixture and sealed exec loader. A matching
mode-specific PASS line, positive rebase count and exact launcher exit 0 are
required; a normal return from a failure mode is not a pass. The supervisor
wall deadline includes child setup, so it does not measure guest-only CPU time.

Host-only checks are `python -B tests/test_android_smoke.py` (all ADB subprocesses
mocked) and CTest `android-runner-modes` (700 synthetic outcome combinations and
7 invalid selectors). They verify selection and fail-closed result handling,
not Android Intent delivery or actual signal behavior. **The failure modes are
implemented; their physical-device runs remain pending.** No device install,
launch, or other ADB operation was performed for the 2026-11-15 host checks.

On that date, the 15 offline Python tests and the C mode test passed on x86_64;
the C test also passed ASan+UBSan with strict warnings. An isolated offline
Gradle `:app:assembleDebug :app:lintDebug` run compiled both native ABIs and the
Java Activity (52 tasks). Lint reported 0 errors and 5 existing-configuration/UI
warnings: target API, backup rules, application icon and two hardcoded strings.
This is build/host-test evidence, not an on-device failure-mode result.

## Physical evidence and limits

On 2026-11-05, the SM-S936U1 S25+ (ARM64, API 37, 4096-byte pages) executed
the packaged original Mach-O successfully through this APK. The initial run
exposed an ELF global-alignment assumption in the service arena. Explicit
16-byte alignment in service_bridge.c fixes it; the service-table test checks
the actual exported arena pointer. Desktop incidental alignment was insufficient.

This proves the original workload on the tested Android device, not Apple
framework execution or broad Android/16 KiB device compatibility. There is no
SpringBoard, UIKit/SwiftUI, CoreAnimation/Metal bridge or general Darwin runtime.
The native execution boundary remains trusted-fixture failure containment,
not a sandbox for arbitrary downloaded Mach-O code. The small heap and virtual
mapping restrictions in named-service-shim.md still apply.
