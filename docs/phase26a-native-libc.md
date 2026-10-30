# phase 26a: bounded, compiler-linked ARM64 Darwin libc slice

## isolation and truth boundary

This phase adds original-source, freestanding C code with a bounded image-local
heap and a deliberately narrow libc ABI. It does **not** ship Apple's
`libSystem.B.dylib`, implement system libmalloc zones, expose untrusted guest
memory, provide arbitrary guest stack/foreign-image pointer access, or permit
general macOS/iOS binaries. No host malloc or host kernel services are
forwarded. Each freshly mapped libc dylib has separate heap/environment state.

The native image loader on verified main is used only as an integration
dependency; shared loader, Objective-C, existing libc, and compatibility
dashboard files were not edited. No changes to central CMake are required.

## four engineering milestones

1. **Bounded allocations:** 16-byte-aligned static arena; 128 KiB live quota;
   256 total allocation identities per image; validated calloc overflow,
   realloc preservation on failure, ownership-sensitive free, double-free
   detection, freed-data zeroing, and stable errors. Addresses are never
   reused within one image mapping to reduce stale-pointer aliasing.
2. **Checked bytes and strings:** memcpy, memmove, memset, memcmp, strlen,
   strnlen, strcmp, strncmp, strcpy, strncpy and strdup, with exclusive live
   allocation validation. memcpy rejects overlapping spans; memmove supports
   overlap. Unterminated strings and invalid spans fail closed with explicit
   error state. A zero-byte operation may accept a null pointer.
3. **Process-local environment:** setenv, getenv and unsetenv, restricted to
   16 keys of at most 31 characters and values of at most 127 characters.
   Returned values are confined to the mapped image's owned environment.
4. **Compiler-linked ARM64 graph and regression:** 6 genuine Clang/LLD
   MH_DYLIB outputs (libc provider plus 5 independent caller dylibs), 2
   genuine MH_EXECUTE callers, plus an original-source image-loader test.
   The standalone harness checks Mach-O filetype, LC_MAIN, LC_LOAD_DYLIB
   and exports, then requests native calls through verified cross-image
   binds. Host stress executes 840,000 memory operations per run. The
   x86_64 job may inspect/map but **must not execute** ARM64 code; native
   execution evidence requires a passing ARM64 job.

## intended ABI and important limitations

| symbol set | intended current behavior | verification status before ci |
|---|---|---|
| malloc, calloc, realloc, free | image-local allocations, no host passthrough | implementation + tests staged; await ARM64 CI |
| memcpy, memmove, memset, memcmp | only ranges of live heap allocations | await ARM64 CI |
| strlen, strnlen, strcmp, strncmp, strcpy, strncpy, strdup | bounded live buffers, not string literals from foreign images | await ARM64 CI |
| getenv, setenv, unsetenv | limited process/image scope | await ARM64 CI |
| snprintf, printf formatting, strtol/strtoul, strerror | not implemented in this slice | unimplemented |
| qsort, bsearch, atexit | unchecked callback pointers are explicitly unsupported | unimplemented |
| pthreads, Mach services, files, dylib initializers | out of scope | no target promotion |

Names above represent callable **subset contracts**, not complete POSIX/Apple
semantics. In particular, reading a string literal outside the heap is
currently rejected, meaning generic callers cannot use these symbols as
ordinary unrestricted libc. The native test supplies writable heap strings
and uses imports from a trusted compiler-produced second image. Per-image
global state is not concurrency safe and must not be treated as thread-local
errno; concurrent requests and async callbacks are unsupported.

## standalone verifier

```sh
timeout 900s python3 tools/phase26a-verify.py
timeout 900s env PHASE26A_SANITIZE=1 python3 tools/phase26a-verify.py
```

Internally all compiler/linker invocations have 30–120s hard timeouts,
configure 120s, build 300s, and individual execution/tests 180s. Both
native x86_64 and aarch64 Linux PR jobs are required; x86_64 also uses
ASan+UBSan with `-Werror`.

## target-promotion proposal (do not edit shared dashboard)

**No immediate promotion solely from code submission.** If the ARM64
PR-triggered frozen-head workflow passes and proves actual separate
Mach-O `_malloc`, `_calloc`, `_realloc`, `_free`, and string/memory
symbol calls, propose promoting only individually observed subset
contracts to **fixture ABI verified**, not globally compatible. Keep
`snprintf`, numeric conversions, `strerror`, `qsort`, `bsearch`,
`atexit` and remaining targets as models/unimplemented pending
independent integration. Promotion must be reconciled with the canonical
35-target dashboard list, with explicit evidence links and semantics.

The verifier must not borrow unmerged work. CI failures prevent merge.
An unchanged-main-SHA integration lease and frozen PR-head green checks are
mandatory before any merge.
