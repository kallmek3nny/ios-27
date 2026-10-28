# phase 21a: Darwin pthread ABI boundary

This is an original-source, standalone, opt-in bridge. It is **not** Apple's libpthread and is not currently integrated into the production loader.

## three milestones

1. **ABI envelopes:** 64-bit Darwin mutex (64 bytes), condition (48), rwlock (200), once (16), attribute (64), and opaque attribute records. The fixed layouts are statically asserted on Clang's real `arm64-apple-darwin` target. None contain Linux pthread types.
2. **Guest boundaries:** all guest pointers are offsets into a trusted stable arena, never raw host pointers. Validate address underflow, span, alignment and overflow before touching memory. 128 synchronized host slots manage separate mutex/condition/rwlock objects with signature and generation tokens, safe slot pinning and deterministic EFAULT, EINVAL, EBUSY, EAGAIN paths. Stale, copied and corrupted records fail closed.
3. **Symbols and verification:** explicit allowlist of pthread mutex, condition and rwlock operations. Unknown exports return ENOSYS. Supported symbols validate arguments; mutex attributes support init/destroy/gettype/settype and recursive locking without a Linux attribute layout cast. Unsupported condition/rwlock attributes and callback-based functions return ENOTSUP. Native contention tests, recursive attributes, malformed output alias rejection, exhaustion, 10,000 reuse cycles, deterministic failure cases, and real Clang-generated ARM64 Darwin Mach-O objects with pthread imports run under independent x86_64 and arm64 CI.

Run: `timeout 180s python3 tools/phase21a-standalone.py`. ASan/UBSan: `PHASE21A_SANITIZE=1 timeout 180s python3 tools/phase21a-standalone.py`.

## explicitly unsupported

pthread_create, pthread_join, pthread_self, pthread_once callback execution, TLS, cancellation, condition waits, process-shared attributes and all guest callback trampolines require further integration. Known Darwin normal/error-checking/recursive mutex, condition and rwlock static initializers are lazily materialized only after validating their zero-filled opaque bytes. The native-error-checking default is a safe compatibility approximation. The signatures used for live objects are bridge-private, not Apple-internal signatures. The underlying host arena must be exclusively managed; this bridge is not yet a hardened guest memory system. Host ERRORCHECK mutex semantics are deliberately not claimed identical to Darwin's default mutex. No Apple binaries, SDKs, emulation or bootloader operations used.
