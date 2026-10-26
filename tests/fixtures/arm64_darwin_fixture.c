/*
 * Self-contained public-source Darwin-target fixture. Generated at build
 * time by clang, never bundled as an Apple SDK binary. No imports, runtime
 * services, Objective-C, system headers, heap allocations, or syscalls.
 */
__attribute__((used, noinline, visibility("default")))
int fixture_entry(void) {
    return 42;
}

__attribute__((used, noinline, visibility("default")))
int fixture_other(void) {
    return 7;
}
