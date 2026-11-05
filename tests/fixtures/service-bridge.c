#include "ios27/service_bridge.h"

/* Original SDK-free C program. The five section/global shapes deliberately
 * match the existing bounded phase19a source linker, including dirty-file BSS.
 * Every service below is an actual indirect native C call through the table. */
#if defined(__APPLE__)
__attribute__((used, section("__DATA_CONST,__const")))
#else
__attribute__((used))
#endif
static const volatile int weights[8] = {3, 1, 4, 1, 5, 9, 2, 6};
__attribute__((used)) static volatile int initialized = 11;
__attribute__((used)) static volatile int zero_tail[64];

__attribute__((noinline))
static int same(const char *a, const char *b) {
    unsigned i = 0;
    if (!a || !b) return 0;
    while (i < 128u && a[i] && a[i] == b[i]) ++i;
    return i < 128u && a[i] == b[i];
}

__attribute__((noinline))
static int put_text(const ios27_service_api *api, uint64_t offset, const char *text) {
    unsigned length = 0;
    char *destination;
    while (length < 128u && text[length]) ++length;
    if (length == 128u) return 0;
    destination = api->memory(offset, length + 1u, 1u);
    if (!destination) return 0;
    for (unsigned i = 0; i <= length; ++i) destination[i] = text[i];
    return 1;
}

static int64_t call3(const ios27_service_api *api, uint64_t number,
                       uint64_t a, uint64_t b, uint64_t c) {
    return api->syscall(number, a, b, c, 0, 0, 0);
}

#ifndef IOS27_SERVICE_ENTRY
#define IOS27_SERVICE_ENTRY main
#endif

int IOS27_SERVICE_ENTRY(int argc, char **argv, char **envp, char **apple,
                        const ios27_service_api *api) {
    int sum = 0;
    int64_t fd, mapping;
    const unsigned char *readback;
    unsigned char *writable;
    if (!api) return 201;
    if (api->version != IOS27_SERVICE_ABI_VERSION || api->size != sizeof(*api) ||
        !api->syscall || !api->memory || !api->error) return 202;
    if (initialized != 11 || zero_tail[0] || zero_tail[63]) return 203;
    for (unsigned i = 0; i < 8u; ++i) sum += weights[i];
    if (sum != 31) return 204;
    if (argc == 2 && (!same(argv[0], "service-fixture") ||
        !same(argv[1], "vectors") || !envp || !apple ||
        !same(envp[0], "MODE=guest") || envp[1] ||
        !same(apple[0], "service=explicit") || apple[1])) return 205;

    /* These IDs are deliberate virtual model values, never host identities. */
    if (call3(api, 20, 0, 0, 0) != 101 ||
        call3(api, 24, 0, 0, 0) != 501 ||
        call3(api, 47, 0, 0, 0) != 20) return 206;
    if (call3(api, UINT64_C(0x100000014), 0, 0, 0) != -1 ||
        api->error() != 78) return 207;
    if (call3(api, 20, 0, 0, 0) != 101 || api->error() != 78) return 208;
    if (api->memory(UINT64_MAX, 2, 0) || api->error() != 14 ||
        api->memory(0, 1, 0) || api->memory(65535, 2, 0)) return 209;

    if (!put_text(api, 1024, "/tmp/native-service.txt") ||
        !put_text(api, 1152, "native service data")) return 210;
    /* A prior helper invocation must not leave this virtual file behind. */
    if (call3(api, 5, 1024, 0, 0) != -1 || api->error() != 2) return 211;
    fd = call3(api, 5, 1024, 0x0202, 0); /* Darwin O_RDWR | O_CREAT */
    if (fd != 3) return 212;
    if (call3(api, 4, (uint64_t)fd, 1152, 19) != 19 ||
        call3(api, 199, (uint64_t)fd, 0, 0) != 0) return 213;
    if (call3(api, 3, (uint64_t)fd, 65535, 2) != -1 || api->error() != 14)
        return 214;
    if (call3(api, 3, (uint64_t)fd, 1280, 19) != 19) return 215;
    readback = api->memory(1280, 20, 0);
    if (!readback || !same((const char *)readback, "native service data")) return 216;
    if (call3(api, 4, 1, 1152, 19) != 19 ||
        call3(api, 6, (uint64_t)fd, 0, 0) != 0 ||
        call3(api, 3, (uint64_t)fd, 1280, 1) != -1 || api->error() != 9)
        return 217;
    if (!put_text(api, 1024, "/tmp/../escape") ||
        call3(api, 5, 1024, 0x0202, 0) != -1 || api->error() != 13) return 218;

    mapping = api->syscall(197, 0, 8192, 3, 0x1002, UINT64_MAX, 0);
    if (mapping != 32768) return 219;
    writable = api->memory((uint64_t)mapping, 8192, 1);
    if (!writable || writable[0] || writable[8191]) return 220;
    writable[0] = 0x5a;
    if (call3(api, 74, (uint64_t)mapping, 8192, 1) != 0 ||
        api->memory((uint64_t)mapping, 1, 1)) return 221;
    readback = api->memory((uint64_t)mapping, 1, 0);
    if (!readback || readback[0] != 0x5a ||
        call3(api, 73, (uint64_t)mapping, 8192, 0) != 0 ||
        api->memory((uint64_t)mapping, 1, 0)) return 222;
    if (call3(api, 116, 2048, 0, 0) != 0 || !api->memory(2048, 16, 0))
        return 223;

    /* Compiler-produced fault workloads run AFTER service mutations, so a
     * later successful launch must demonstrate fresh service/global state. */
    if (argc == 1 && same(argv[0], "trap")) __builtin_trap();
    if (argc == 1 && same(argv[0], "loop"))
        for (;;) zero_tail[0] = 1;
    if (argc == 1 && same(argv[0], "raw-syscall")) {
#if defined(__aarch64__)
        __asm__ volatile("mov x8, #172\nsvc #0" ::: "x8", "memory");
#else
        return 224;
#endif
    }
    initialized = sum;
    zero_tail[0] = 1;
    zero_tail[63] = 1;
    return initialized + 11;
}
