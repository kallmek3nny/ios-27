/* Original-source SDK-free Clang AArch64 Mach-O program for phase 18a.
 * Exercises the Darwin main register ABI and a real, independently mapped
 * stack, with cross-segment PC-relative data and native stack frames. */
__attribute__((used, section("__DATA_CONST,__const")))
static const volatile int constant = 37;
__attribute__((used)) static volatile int counter = 5;
__attribute__((used)) static volatile int zero_tail[8];

__attribute__((used, noinline))
int main(int argc, char **argv, char **envp, char **apple) {
    volatile unsigned char scratch[2048];
    unsigned long sp;
    int n = constant + counter + zero_tail[7];
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    if (argc != 3 || !argv || !envp || !apple)
        return 101;
    if (!argv[0] || !argv[1] || !argv[2] || argv[3])
        return 102;
    if (argv[0][0] != 'i' || argv[1][0] != 'o' ||
        argv[2][0] != 'p' || argv[1][1] != 'a')
        return 103;
    if (!envp[0] || envp[1] || envp[0][0] != 'M' || apple[0])
        return 104;
    if ((sp & 15ul) != 0ul ||
        (unsigned long)argv[0] <= sp ||
        (unsigned long)argv[0] - sp > 2ul * 1024ul * 1024ul)
        return 105;
    for (unsigned i = 0; i < sizeof(scratch); ++i)
        scratch[i] = (unsigned char)i;
    if (scratch[2047] != 255u)
        return 106;
    /* Guest writes must not modify caller-owned argv/envp buffers. */
    argv[1][0] = 'X';
    envp[0][0] = 'Y';
    counter = n;
    zero_tail[7] = counter;
    return counter;
}
