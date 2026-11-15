# The Simulator host-library blocker

**SpringBoard cannot start from the downloaded Simulator cache alone.**
Read-only inspection on 2026-11-15 found that three fundamental libraries
contain zero-filled instruction sections in the local iOS 27.0 / 24A434 cache.
Their export names exist, but the exported locations have no working code.

| Cached library | Entire `__TEXT,__text` section |
| --- | ---: |
| `/usr/lib/system/libsystem_kernel.dylib` | 6,020 zero bytes |
| `/usr/lib/system/libsystem_pthread.dylib` | 828 zero bytes |
| `/usr/lib/system/libsystem_platform.dylib` | 652 zero bytes |

The sections declare ARM64 instructions (`0x80000400`). A zero ARM64 word
decodes as `udf #0`, an undefined instruction. This is a static observation;
these entry points were not executed. Representative exported targets include
`getpid`, `mach_msg2_trap`, `pthread_getspecific` and the three library
initialization routines.

The full size/SHA-256-verified planning run found **224 import entries / 224
bind sites in 47 standalone images**, covering **60 distinct requested host
symbol names**. Alias names may differ at the final provider. These are not
counts of all required host APIs: prebound references within cached images
were not enumerated. The [sanitized findings](simulator-host-imports-27.0.json)
list the imported names and per-provider counts.

## Why these are host dependencies

The cache's `libsystem_sim_{kernel,platform,pthread}.dylib` libraries reexport
corresponding `libsystem_sim_*_host.dylib` images. Those reexport the three
libraries above. Apple's open-source simulator cache builder explicitly lists
the same paths in `sMacOSHostLibs` and loads them from the **host filesystem**
before walking the Simulator RuntimeRoot:

- [Host library list](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/cache_builder/update_dyld_sim_shared_cache.cpp#L59-L63)
- [Host files supplied to the cache builder](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/cache_builder/update_dyld_sim_shared_cache.cpp#L211-L223)

The existing Android service table is an original research ABI, not these
Darwin library exports. Resolving a symbol to an address inside a placeholder
does not supply Mach IPC, pthread/TLS semantics, or the platform routines.
Copying macOS libraries would still require their Darwin kernel interfaces;
symbol replacement alone would not establish Android compatibility.

An optional [original platform-memory replacement](platform-memory-bridge.md)
now supplies twelve memory/string routines. It retargets 109 of the observed
standalone bindings and plans the relevant v4 cache pointer updates. This
does not implement the remaining 115 standalone host imports or establish UI
startup; the counts in this document describe the original unmodified inputs.

## Corrected report semantics

`tools/apple-cache-audit.py` now recognizes the observed placeholder profile
only for the three documented host libraries in a cache. It validates the
instruction section's VM/file correspondence and RX declaration and scans
at most 1 MiB of code per library. Nonzero code remains unclassified: that
does not prove it is executable or compatible.

- `host_runtime.zero_filled_host_libraries` records observed section counts
  and zero-code byte counts.
- `host_runtime.required_api_probes` distinguishes a zero-filled host target,
  a missing export, and an export whose execution is unverified.
- Chained-import census counts such matches as `host_placeholder`, rather
  than ordinary `resolved` entries, and names the provider in its findings.
- The non-executable fixup stager still permits pointer-byte inspection, but
  adds per-image `host_placeholder_imports`, total import-entry and bind-site
  counts, and the same `host_runtime` report. These counts cover **standalone
  chained imports only**, not every prebound reference inside cached images.

The earlier 505,706 staged fixups remain a byte-staging result. Likewise,
zero deferred export lookups did not mean zero missing runtime implementations.
`ready_to_execute`, `springboard_entered` and `rendered_ios_frame` remain false.

## Effect on the requested UI

The immediate missing implementation is a compatible host runtime for these
imports, plus dyld bootstrap/override integration. The subsequently required
Objective-C/Swift initialization, BackBoard/FrontBoard and graphics presentation
remain additional work. Passing original constructor fixtures on the S25+
does not implement any of those Apple services.

The placeholder regression tests use original synthetic data. No Apple code
was executed and no phone files or settings were changed during this audit.

## Verification

- All 18 cache-audit tests passed on Windows and Linux.
- All 22 loader tests passed on Linux; Windows passed 17 and skipped its five
  native-Linux mapping cases.
- The Linux runs used `PYTHONMALLOC=debug` and byte-identical copies of the
  five tool/test files in a private Linux directory. An earlier run from the
  Windows-mounted checkout timed out starting the CLI after 10 seconds; the
  local-copy run passed with the original timeout and assertions.
- The final real-input planning run completed in 217.4 seconds. It retained
  the 68-image/505,706-fixup non-executable staging result while exposing the
  placeholder findings. It did not run `--map` or execute Apple instructions.

Raw reports, the private-copy verification script and source hashes are under
ignored `out/simulator-host-imports-2026-11-15/`. [PR #126](https://github.com/kallmek3nny/ios-27/pull/126)
tracks the published revision and separate CI results.
