#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ios27/darwin_shim.h"
#include "darwin_shim_internal.h"
#include "service_bridge_internal.h"

static void heap_cases(void) {
    uint8_t *p, *grown;
    void *blocks[8];
    *__error() = 37;
    free(NULL);
    p = calloc(2, 4096);
    assert(p && !((uintptr_t)p & 15u) && *__error() == 37);
    for (unsigned i = 0; i < 8192; ++i) assert(p[i] == 0);
    p[0] = 17; p[8191] = 93;
    grown = realloc(p, 12288);
    assert(grown && grown[0] == 17 && grown[8191] == 93);
    assert(realloc(grown, UINT64_MAX) == NULL && *__error() == 12);
    assert(grown[0] == 17 && grown[8191] == 93);
    assert(realloc(grown, 2) == grown);
    assert(realloc(grown, 0) == NULL);
    assert(write(1, grown, 1) == -1 && *__error() == 14);
    assert(calloc(UINT64_MAX, 2) == NULL && *__error() == 12);
    assert(malloc(UINT64_MAX) == NULL && *__error() == 12);
    for (unsigned i = 0; i < 8; ++i) {
        blocks[i] = malloc(0);
        assert(blocks[i]);
        for (unsigned j = 0; j < i; ++j) assert(blocks[j] != blocks[i]);
    }
    assert(malloc(1) == NULL && *__error() == 12);
    assert(realloc(blocks[0], 8192) == NULL && *__error() == 12);
    assert(write(1, blocks[0], 1) == 1);
    for (unsigned i = 0; i < 8; ++i) free(blocks[i]);
    free(blocks[0]);
    assert(*__error() == 22);
    p = realloc(NULL, 32768);
    assert(p);
    p[32767] = 99;
    free(p);
    p = calloc(1, 32768);
    assert(p && p[32767] == 0);
    assert(realloc(p + 1, 1) == NULL && *__error() == 22);
    free(p + 1);
    assert(*__error() == 22);
    free(p);
}

int main(void) {
    const ios27_service_api *service = ios27_service_bridge_initialize();
    char buffer[32] = {0}, long_path[97];
    int32_t fd;
    uint8_t *mapped;
    assert(getpid() == -1 && *__error() == 78);
    ios27_shim_binding.api = service;
    assert(getpid() == 101 && *__error() == 78);
    heap_cases();
    assert(open(NULL, IOS27_DARWIN_O_RDONLY) == -1 && *__error() == 14);
    memset(long_path, 'x', sizeof(long_path));
    assert(open(long_path, IOS27_DARWIN_O_RDONLY) == -1 && *__error() == 14);
    fd = open("/tmp/shim-test", IOS27_DARWIN_O_RDWR | IOS27_DARWIN_O_CREAT, 0600);
    assert(fd == 3);
    assert(write(fd, "hello", 5) == 5 && lseek(fd, 0, 0) == 0);
    assert(read(fd, buffer, 5) == 5 && !memcmp(buffer, "hello", 5));
    assert(lseek(fd, -1, 2) == 4 && read(fd, buffer, 1) == 1 && buffer[0] == 'o');
    assert(read(fd, NULL, 0) == 0 && write(fd, NULL, 0) == 0);
    assert(write(fd, NULL, 1) == -1 && *__error() == 14);
    assert(read(fd, NULL, 1) == -1 && *__error() == 14);
    assert(write(fd, buffer, 4097) == -1 && *__error() == 22);
    assert(write(fd, (const void *)UINTPTR_MAX, 1) == -1 && *__error() == 14);
    assert(close(fd) == 0 && close(fd) == -1 && *__error() == 9);
    *__error() = 37;
    assert(getpid() == 101 && *__error() == 37);

    mapped = mmap(NULL, 8192, 3, 0x1002, -1, 0);
    assert(mapped != IOS27_DARWIN_MAP_FAILED && mapped[0] == 0 && mapped[8191] == 0);
    mapped[0] = 'M';
    assert(write(1, mapped, 1) == 1);
    assert(mprotect(mapped, 8192, 1) == 0);
    fd = open("/tmp/shim-test", IOS27_DARWIN_O_RDWR);
    assert(fd == 3);
    assert(read(fd, mapped, 1) == -1 && *__error() == 14);
    assert(read(fd, buffer, 1) == 1 && buffer[0] == 'h');
    assert(close(fd) == 0);
    assert(mprotect(mapped, 8192, 0) == 0);
    assert(write(1, mapped, 1) == -1 && *__error() == 14);
    assert(munmap(mapped, 8192) == 0);
    assert(write(1, mapped, 1) == -1 && *__error() == 14);
    assert(mmap(NULL, UINT64_MAX, 3, 0x1002, -1, 0) == IOS27_DARWIN_MAP_FAILED);
    assert(*__error() == 22);
    assert(mprotect(buffer, 4096, 1) == -1 && *__error() == 22);
    ios27_service_api invalid = *service;
    invalid.version = 2u;
    ios27_shim_binding.api = &invalid;
    assert(getpid() == -1 && *__error() == 78);
    puts("named shim: C pointer marshalling, errno, file lifecycle, map protections and invalid inputs passed");
    return 0;
}
