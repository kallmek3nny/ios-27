/* Phase 19a: genuinely compiled, SDK-free ARM64 Darwin process workload.
 * Uses no libc, emulation, Apple framework, or host calls.
 * Exercises recursive frames, cross-segment PC-relative accesses,
 * readonly globals, writable initializers, BSS, and x0-x3 startup state.
 */
__attribute__((used, section("__DATA_CONST,__const")))
static const volatile int weights[8] = {3, 1, 4, 1, 5, 9, 2, 6};
__attribute__((used)) static volatile int initialized = 11;
__attribute__((used)) static volatile int zero_tail[64];

__attribute__((noinline))
static int accumulate(int n, volatile unsigned char *scratch) {
    volatile unsigned char frame[192];
    if (n == 0)
        return (int)scratch[0];
    frame[0] = (unsigned char)n;
    frame[191] = (unsigned char)(n + 1);
    return accumulate(n - 1, scratch) +
           (int)frame[0] + (int)frame[191];
}

__attribute__((noinline))
static int same(const char *s, const char *expected) {
    unsigned i = 0;
    if (!s || !expected)
        return 0;
    while (i < 256u && s[i] && s[i] == expected[i])
        ++i;
    return i < 256u && s[i] == expected[i];
}

__attribute__((used, noinline))
int main(int argc, char **argv, char **envp, char **apple) {
    volatile unsigned char scratch[1024];
    unsigned long stack_pointer;
    int sum = 0;
    __asm__ volatile ("mov %0, sp" : "=r"(stack_pointer));
    if (argc == 0)
        return 101;
    if (argc != 4 || !argv || !envp || !apple)
        return 102;
    if (!same(argv[0], "ios27") || !same(argv[1], "oak") ||
        !same(argv[2], "pine") || !same(argv[3], "cedar") || argv[4])
        return 103;
    if (!same(envp[0], "MODE=phase19a") ||
        !same(envp[1], "REPEAT=1") || envp[2])
        return 104;
    if (!same(apple[0], "executable_path=/ios27/phase19a") ||
        !same(apple[1], "ios27.mode=native") || apple[2])
        return 105;
    if ((stack_pointer & 15ul) || (unsigned long)argv[0] <= stack_pointer ||
        (unsigned long)apple[0] <= stack_pointer)
        return 106;
    if (initialized != 11 || zero_tail[63] != 0 ||
        zero_tail[0] != 0)
        return 107;
    for (int i = 0; i < 8; ++i)
        sum += weights[i];
    if (sum != 31)
        return 108;
    for (unsigned i = 0; i < sizeof(scratch); ++i)
        scratch[i] = (unsigned char)(i & 255u);
    if (accumulate(9, scratch) != 99)
        return 109;
    /* All writes must remain private to this invocation. */
    argv[1][0] = 'O';
    envp[0][0] = 'X';
    apple[0][0] = 'X';
    initialized = sum;
    zero_tail[0] = initialized;
    zero_tail[63] = 1;
    return initialized + 11;
}
