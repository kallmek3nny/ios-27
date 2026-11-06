#include "ios27/darwin_shim.h"

/* This client has no table parameter or callback-table include. It calls
 * named functions, resolved by LLVM across two real compiler-produced objects. */
#if defined(__APPLE__)
__attribute__((used, section("__DATA_CONST,__const")))
#else
__attribute__((used))
#endif
static const volatile int weights[8] = {3, 1, 4, 1, 5, 9, 2, 6};
static volatile int initialized = 11;
static volatile int zero_tail[64];
/* Real linked pointer rebases must survive arbitrary native load addresses. */
static const char *volatile heap_message = "named shim data";

__attribute__((noinline))
static int same(const char *a, const char *b) {
    unsigned i = 0;
    if (!a || !b) return 0;
    while (i < 128u && a[i] && a[i] == b[i]) ++i;
    return i < 128u && a[i] == b[i];
}

int main(int argc, char **argv, char **envp, char **apple) {
    int32_t fd;
    int sum = 0;
    char output[32] = {0};
    unsigned char *mapping;
    if (getpid() == -1 && *__error() == 78) return 201;
    if (getpid() != 101) return 231;
    if (initialized != 11 || zero_tail[0] || zero_tail[63]) return 203;
    for (unsigned i = 0; i < 8u; ++i) sum += weights[i];
    if (sum != 31) return 204;
    if (argc == 2 && (!same(argv[0], "service-fixture") ||
        !same(argv[1], "vectors") || !envp || !apple ||
        !same(envp[0], "MODE=guest") || envp[1] ||
        !same(apple[0], "service=explicit") || apple[1])) return 205;
    if (open("/tmp/named-shim", IOS27_DARWIN_O_RDONLY) != -1 || *__error() != 2)
        return 232;
    fd = open("/tmp/named-shim", IOS27_DARWIN_O_CREAT | IOS27_DARWIN_O_RDWR, 0600);
    if (fd != 3 || write(fd, heap_message, 16) != 16 || lseek(fd, 0, 0) != 0)
        return 233;
    if (read(fd, output, 16) != 16 || !same(output, "named shim data")) return 234;
    if (lseek(fd, -1, 2) != 15 || read(fd, output, 1) != 1 || output[0] != 0)
        return 235;
    if (close(fd) != 0 || read(fd, output, 1) != -1 || *__error() != 9) return 236;
    *__error() = 37;
    if (getpid() != 101 || *__error() != 37) return 237;
    if (write(1, "named shim data", 16) != 16 || *__error() != 37) return 238;
    if (open("/tmp/../escape", IOS27_DARWIN_O_CREAT | IOS27_DARWIN_O_RDWR, 0600) != -1 ||
        *__error() != 13) return 239;
    if (write(1, output, 4097) != -1 || *__error() != 22) return 240;

    mapping = mmap(0, 8192, IOS27_DARWIN_PROT_READ | IOS27_DARWIN_PROT_WRITE,
                   IOS27_DARWIN_MAP_PRIVATE | IOS27_DARWIN_MAP_ANON, -1, 0);
    if (mapping == IOS27_DARWIN_MAP_FAILED || mapping[0] || mapping[8191]) return 241;
    mapping[0] = 'S';
    if (write(1, mapping, 1) != 1 || mprotect(mapping, 8192, IOS27_DARWIN_PROT_READ))
        return 242;
    fd = open("/tmp/named-shim", IOS27_DARWIN_O_RDWR);
    if (fd != 3 || read(fd, mapping, 1) != -1 || *__error() != 14 ||
        read(fd, output, 1) != 1 || output[0] != 'n' || close(fd)) return 243;
    if (mprotect(mapping, 8192, IOS27_DARWIN_PROT_NONE) ||
        write(1, mapping, 1) != -1 || *__error() != 14 || munmap(mapping, 8192))
        return 244;
    if (write(1, mapping, 1) != -1 || *__error() != 14) return 245;

    /* Real named allocator callsites; every supervised launch starts fresh. */
    {
        unsigned char *heap = calloc(2, 4096), *grown;
        void *blocks[8];
        if (!heap || ((uintptr_t)heap & 15u) || heap[0] || heap[8191]) return 246;
        heap[0] = 19; heap[8191] = 71;
        grown = realloc(heap, 12288);
        if (!grown || grown[0] != 19 || grown[8191] != 71) return 247;
        if (realloc(grown, UINT64_MAX) || *__error() != 12 || grown[8191] != 71)
            return 248;
        free(grown);
        if (calloc(UINT64_MAX, 2) || *__error() != 12) return 249;
        for (unsigned i = 0; i < 8; ++i) {
            blocks[i] = malloc(1);
            if (!blocks[i]) return 250;
        }
        if (malloc(1) || *__error() != 12) return 251;
        for (unsigned i = 0; i < 8; ++i) free(blocks[i]);
        heap = calloc(1, 32768);
        if (!heap || heap[0] || heap[32767]) return 252;
        free(heap);
    }

    if (argc == 1 && same(argv[0], "trap")) __builtin_trap();
    if (argc == 1 && same(argv[0], "loop")) for (;;) zero_tail[0] = 1;
    if (argc == 1 && same(argv[0], "raw-syscall")) {
#if defined(__aarch64__)
        __asm__ volatile("mov x8, #172\nsvc #0" ::: "x8", "memory");
#endif
    }
    initialized = sum;
    zero_tail[0] = 1;
    zero_tail[63] = 1;
    return initialized + 11;
}
