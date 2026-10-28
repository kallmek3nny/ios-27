# phase 15b: supplementary executable safety checks

this change is layered on top of the already merged native mh_execute layout implementation. it preserves that implementation rather than overwriting it.

- ensure entry instructions cannot be taken from the mach-o header or load-command bytes.
- reject malformed, nonzero-address __pagezero ranges, or a pagezero range which overlaps mapped VM pages.
- validate 4k vm/file segment congruence before allocating or protecting host pages.
- deny unsupported custom lc_main stacksize in the trusted int(void) runner rather than silently ignoring a process-startup request.
- ensure the multi-image loader validates maxprot bitfields and that initprot is contained within maxprot.
- include regressions for malformed lc_main, pagezero, file offset, and multi-image max protection.

limitations: no arbitrary iOS executable support, dynamic linker, apple frameworks, Darwin services or privileged device changes. all executable tests use locally constructed fixtures, with W^X and cleanup retained from the base implementation.

ci: retain existing CMakeLists.txt, strict -Werror warnings and all tests. verify x86_64 and ARM64 on an identical PR head.
