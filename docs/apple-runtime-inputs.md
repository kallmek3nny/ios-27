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

## SpringBoard dependency and binding audit

`tools/apple-cache-audit.py` reads both parts of the 24A434 simulator cache,
validates the subcache UUID and mapping ranges, and uses cache VM addresses to
find Mach-O headers and export tries, including `__LINKEDIT` in another part.
It walks the declared dependency graph and audits generic ARM64 chained bind
sites in standalone images. A local RuntimeRoot supplies binaries that Apple
ships outside the cache.

```sh
python tools/apple-cache-audit.py \
  --cache /path/to/dyld_sim_shared_cache_arm64 \
  --app /path/to/SpringBoard.app/SpringBoard \
  --dylib /path/to/SpringBoard.framework/SpringBoard \
  --runtime-root /path/to/RuntimeRoot
```

The main cache and explicit dylib alone left 48 required paths unresolved.
Extracting 66 additional standalone dylibs from the same simulator disk image
closed the required load-command graph. The final walk found 1,465 cached
images and 67 standalone dylibs, plus the executable. Four missing paths
were reachable only through weak or lazy dependencies. The reachable images
declare 557 initializer slots and 106 Objective-C non-lazy list entries.
These are metadata counts. They do not predict initializer order or runtime
registrations.

| Static check | Measured result |
| --- | --- |
| App entry | `LC_MAIN` at file offset `0x618`; its branch stub loads the single chained bind slot at image offset `0x18008`. |
| App import | Ordinal 1 names SpringBoard.framework. Its export trie places `_SBSystemAppMain` at `__TEXT` offset `0x8afc`. |
| SpringBoard.framework fixups | 44,605 bind sites and 189,335 rebase sites. Its 9,741 chained import entries have 9,739 static export matches and two process-wide weak lookups. |
| All standalone images | 116,080 bind sites and 389,626 rebase sites. Of 41,706 chained import entries used by these sites, 41,694 have static export matches and 12 need process-wide weak lookup. |
| Executable preflight | The current runner rejects the real app as `not a freestanding LC_MAIN executable` with both 4 KiB and 16 KiB mapping granules. |

The audit matches exports by install name, including self binds and reexports.
It reports process-wide lookups separately and does not apply fixups or load
executable pages. Cache slide application, dynamic loads, initializer order,
and Objective-C/Swift registration remain outside the audit.

The first instructions of `_SBSystemAppMain` dereference libSystem's
`___stack_chk_guard`. The initial call sequence also reaches
`_objc_unsafeClaimAutoreleasedReturnValue` and `_objc_autoreleasePoolPush`.
A live launch therefore needs a process loader that maps the cache and
standalone dependencies, applies fixups and initializes those runtimes before
transferring control to the app. Mach/XNU-facing services, window/input
lifecycle and CoreAnimation/Metal/IOSurface integration are separate runtime
requirements. SpringBoard has not entered Apple code or rendered a frame here.
