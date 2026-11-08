# Apple runtime inputs and measured dependencies

Actual Apple binaries are now available locally for read-only dependency work.
The source is Apple's ARM64 iOS 27.0 Simulator runtime, build 24A434. This is
the simulator platform (Mach-O platform 7), not an iPhone-device firmware image.
No Apple binaries, archive keys or proprietary source are committed or packaged
in the APK. The runtime is not yet able to execute these Apple components.

## Acquisition and integrity

Apple documents its simulator catalog in the
[Xcode release notes](https://developer.apple.com/documentation/xcode-release-notes/xcode-16-release-notes)
and its normal simulator installation flow in
[Xcode component documentation](https://developer.apple.com/documentation/xcode/downloading-and-installing-additional-xcode-components).
tools/apple-runtime-source.py selects an exact build from the live
[Apple index](https://devimages-cdn.apple.com/downloads/xcode/simulators/index2.dvtdownloadableindex)
and retrieves its asset metadata from Apple's HTTPS asset service.

The asset endpoint uses an Apple private root not present in this host's normal
TLS store. The tool obtains that root from [Apple PKI](https://www.apple.com/certificateauthority/)
over normally verified HTTPS and loads it only into the request's SSL context.
Hostname/certificate verification stays enabled; system trust stores are not
changed. The asset response's JWS payload is decoded, not independently signature
verified. Transport acquisition uses authenticated HTTPS, and downloaded bytes
must match the asset's published SHA-256 before the partial file is renamed.

The selected archive is 8,053,063,680 bytes with verified SHA-256
`4dc160434bfb959ceaa86a41407f6a77bddac53bbee7e41f5a8635c8d1f75346`.
The resumable download completed and matched that measurement. The tool requires
inputs outside the repository and retains incomplete files on interruption.
Python 3.11+ is required. For example, with an external local directory:

```sh
python tools/apple-runtime-source.py --directory C:/AppleRuntimeInputs \
  --version 27.0 --build 24A434 --download
```

The local raw asset response includes archive key material; keep that file out
of Git and reports. The generated source-report.json excludes the key. Decryption
used the server-provided symmetric key with the checksum-verified upstream
ipsw v3.1.742 tool. The resulting AppleArchive contains an APFS simulator DMG;
its selected files and shared-cache family were extracted with 7-Zip 24.09.
Both archives and extracted components stay outside the checkout. Extraction is
data processing, not execution of an Apple program.

## Observed components

The [sanitized machine-readable inventory](apple-runtime-inventory-27.0.json)
records source URLs, archive verification, binary hashes and actual dependency
paths. Counts include declared strong, weak, reexport and upward imports; they
are not counts of mandatory services or implemented APIs.

| Component | Observation |
| --- | --- |
| SpringBoard.app/SpringBoard | 133,872-byte unencrypted generic ARM64 executable; its single undefined symbol is _SBSystemAppMain. |
| Executable's direct libraries | SpringBoard.framework, Foundation, libobjc and libSystem. |
| SpringBoard.framework/SpringBoard | 36,539,312-byte generic ARM64 dylib; 228 direct dependencies (39 public frameworks, 179 private frameworks, 10 other libraries). |
| UIKitCore in shared cache | 103 declared direct dependencies. |
| Foundation in shared cache | 31 declared direct dependencies. |
| libobjc in shared cache | 5 declared direct dependencies. |
| libSystem in shared cache | 34 declared direct dependencies. |
| Shared-cache family | Two main binary files totaling 3,829,317,632 bytes; 1,826 listed dylib images. |

SpringBoard.app and SpringBoard.framework contain different binaries with the
same basename. Preserve distinct output directories when extracting them.
The repository's own audit-ios command successfully inspected both. Its
read-only inventory capacity was raised from 128 to 512 declared imports, with
228/512/513 boundary tests, so the real framework can be inventoried in full.
Execution eligibility was not widened by that inventory change.

## What blocks launching the UI

The first executable import is _SBSystemAppMain from the actual SpringBoard
framework; it is not among the project's original service shim exports. The
native executable path still refuses unresolved imports and framework
dependencies. General dyld/shared-cache loading, Objective-C/Swift initialization,
Mach/XNU-facing services, window/input lifecycle and CoreAnimation/Metal/IOSurface
integration remain required. The new internal pointer-rebase support removes
one narrower loader gap, demonstrated only by the original bundled workload.
None of these observations constitutes a SpringBoard launch or rendered Apple UI.
