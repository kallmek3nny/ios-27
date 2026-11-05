#include "ios27/phase27b_libsystem.h"

/* Independently compiled four-argument LC_MAIN client. Every service call
 * uses an ordinary unresolved C name, linked against separate shim objects.
 * There is NO callback-table entry parameter and NO host file descriptor. */
#if defined(__APPLE__)
__attribute__((used, section("__DATA_CONST,__const")))
#else
__attribute__((used))
#endif
static const volatile int layout_witness[2] = {17, 25};
static volatile int exited;
static void on_exit_first(void) { exited = exited * 10 + 1; }
static void on_exit_second(void) { exited = exited * 10 + 2; }
static int compare_int(const void *left, const void *right) {
    int a = *(const int *)left, b = *(const int *)right;
    return (a > b) - (a < b);
}
int main(int argc, char **argv, char **envp, char **apple) {
    char *fmt, *text, *output, *copy, *key, *value, *duplicate;
    int *numbers;
    int fd, result = 1;
    int64_t wall[2] = {0, 0};
    uint8_t stat_bytes[144] = {0};
    int wanted = 4;
    (void)argv; (void)envp; (void)apple;
    if (argc < 1 || layout_witness[0] + layout_witness[1] != 42) return 1;
    fmt = malloc(64u);
    text = malloc(64u);
    output = calloc(1u, 128u);
    copy = malloc(128u);
    key = malloc(16u);
    value = malloc(16u);
    numbers = malloc(5u * sizeof(*numbers));
    if (!fmt || !text || !output || !copy || !key || !value || !numbers) return 2;

    /* Phase26a checks string sources against owned live allocations. */
    {
        const char pattern[] = "file=%s num=%d";
        const char payload[] = "alpha";
        const char name[] = "MODE";
        const char item[] = "27b";
        for (size_t i = 0; i < sizeof(pattern); ++i) fmt[i] = pattern[i];
        for (size_t i = 0; i < sizeof(payload); ++i) text[i] = payload[i];
        for (size_t i = 0; i < sizeof(name); ++i) key[i] = name[i];
        for (size_t i = 0; i < sizeof(item); ++i) value[i] = item[i];
    }
    if (setenv(key, value, 1) || !getenv(key) ||
        strcmp(getenv(key), value) || unsetenv(key) || getenv(key)) return 3;
    if (snprintf(output, 128u, fmt, text, 27) != 17) return 4;
    if (strlen(output) != 17u || strnlen(output, 128u) != 17u) return 5;
    if (memcpy(copy, output, 18u) != copy || memcmp(copy, output, 18u)) return 6;
    if (memmove(copy + 1, copy, 18u) != copy + 1 || copy[1] != 'f') return 7;
    if (memset(copy, 0, 128u) != copy || strcpy(copy, output) != copy ||
        strncmp(copy, output, 17u) || strcmp(copy, output)) return 8;
    duplicate = strdup(text);
    if (!duplicate || strcmp(duplicate, text)) return 9;
    copy = realloc(copy, 192u);
    if (!copy || copy[0] != 'f') return 10;
    if (strncpy(copy, text, 6u) != copy || strcmp(copy, text)) return 11;
    numbers[0] = 5; numbers[1] = 1; numbers[2] = 4; numbers[3] = 2; numbers[4] = 3;
    qsort(numbers, 5u, sizeof(*numbers), compare_int);
    if (numbers[0] != 1 || numbers[4] != 5 ||
        !bsearch(&wanted, numbers, 5u, sizeof(*numbers), compare_int)) return 12;
    if (atexit(on_exit_first) || atexit(on_exit_second)) return 13;

    fd = openat(IOS27_27B_AT_FDCWD, "/tmp/phase27b",
                IOS27_DARWIN_O_CREAT | IOS27_DARWIN_O_RDWR, 0600);
    if (fd != 3 || fcntl(fd, IOS27_27B_F_GETFL) !=
        (IOS27_DARWIN_O_CREAT | IOS27_DARWIN_O_RDWR)) return 14;
    if (write(fd, output, 17u) != 17 || lseek(fd, 0, 0) != 0) return 15;
    if (read(fd, copy, 17u) != 17 || memcmp(copy, output, 17u)) return 16;
    if (pread(fd, copy, 17u, 0) != 17 || memcmp(copy, output, 17u)) return 17;
    if (lseek(fd, 0, 1) != 17) return 18; /* positioned read preserved offset */
    if (pwrite(fd, text, 5u, 5) != 5 || lseek(fd, 0, 1) != 17) return 19;
    if (pread(fd, copy, 17u, 0) != 17) return 20;
    if (copy[5] != 'a' || copy[9] != 'a') return 21;
    if (fstat(fd, stat_bytes)) return 22;
    if (stat_bytes[96] != 17 || stat_bytes[97]) return 23;
    if (close(fd) || read(fd, copy, 1u) != -1 || *__error() != 9) return 24;
    if (openat(42, "/tmp/phase27b", IOS27_DARWIN_O_RDONLY) != -1 ||
        *__error() != 45) return 25;
    if (gettimeofday(wall, NULL) || wall[0] != 1700000000 || wall[1]) return 26;
    *__error() = 37;
    if (getpid() != 101 || *__error() != 37) return 27;
    ios27_27b_run_exit_handlers(); /* explicit until loader gains lifecycle hook */
    if (exited != 21) return 28;
    result = 42;
    free(numbers); free(duplicate); free(value); free(key);
    free(copy); free(output); free(text); free(fmt);
    return result;
}
