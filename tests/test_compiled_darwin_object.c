#include "ios27/darwin_object.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) && defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void wr32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(v >> (8u * i));
}
static void rejection(uint8_t *data, size_t length) {
    ios27_darwin_object info;
    char error[160] = {0};
    assert(ios27_darwin_object_inspect(data, length, &info,
                                      error, sizeof(error)) != 0);
    assert(error[0]);
}
static size_t command(const uint8_t *bytes, size_t size, uint32_t wanted) {
    size_t pos = 32u;
    assert(size >= 32u);
    for (uint32_t i = 0u; i < u32(bytes + 16u); ++i) {
        assert(pos < size && size - pos >= 8u);
        uint32_t cmdsize = u32(bytes + pos + 4u);
        assert(cmdsize >= 8u && cmdsize <= size - pos);
        if (u32(bytes + pos) == wanted) return pos;
        pos += cmdsize;
    }
    assert(!"expected Mach-O load command absent");
    return 0u;
}
int main(int argc, char **argv) {
    FILE *f;
    long length;
    size_t file_size;
    uint8_t *bytes, *copy;
    ios27_darwin_object info;
    char error[160];
    size_t segment_command, sym_command, build_command;
    assert(argc == 2 && argv[1]);
    f = fopen(argv[1], "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    length = ftell(f);
    assert(length > 32 && length <= 1024L * 1024L);
    assert(fseek(f, 0, SEEK_SET) == 0);
    file_size = (size_t)length;
    bytes = malloc(file_size);
    copy = malloc(file_size);
    assert(bytes && copy);
    assert(fread(bytes, 1u, file_size, f) == file_size);
    assert(fclose(f) == 0);

    assert(ios27_darwin_object_inspect(bytes, file_size, &info,
                                      error, sizeof(error)) == 0);
    assert(info.platform == 1u);
    assert(info.minos != 0u);
    assert(info.text_size >= 4u && info.text_size <= 16384u);
    assert(info.entry_offset < info.text_size);
    assert(info.symbol_count >= 2u);
    assert(info.text_file_offset <= file_size &&
           info.text_size <= file_size - info.text_file_offset);

    segment_command = command(bytes, file_size, 0x19u);
    sym_command = command(bytes, file_size, 0x02u);
    build_command = command(bytes, file_size, 0x32u);
    assert(segment_command + 152u <= file_size);
    assert(u32(bytes + segment_command + 64u) >= 1u);

    memcpy(copy, bytes, file_size);
    wr32(copy + 12u, 2u); /* MH_EXECUTE is not an object */
    rejection(copy, file_size);
    memcpy(copy, bytes, file_size);
    wr32(copy + 8u, 2u); /* arm64e/PAC subtype explicitly unsupported */
    rejection(copy, file_size);
    memcpy(copy, bytes, file_size);
    wr32(copy + build_command + 8u, 2u); /* iOS platform is not macOS */
    rejection(copy, file_size);
    memcpy(copy, bytes, file_size);
    wr32(copy + segment_command + 72u + 60u, 1u); /* needs a linker */
    rejection(copy, file_size);
    memcpy(copy, bytes, file_size);
    wr32(copy + sym_command + 8u, UINT32_MAX);
    rejection(copy, file_size);
    memcpy(copy, bytes, file_size);
    wr32(copy + sym_command + 20u, UINT32_MAX);
    rejection(copy, file_size);
    memcpy(copy, bytes, file_size);
    {
        uint32_t symoff = u32(bytes + sym_command + 8u);
        assert(symoff < file_size && file_size - symoff >= 16u);
        copy[symoff + 4u] = 0u; /* unsupported undefined symbol */
    }
    rejection(copy, file_size);
    /* No mutation should have modified the original parser input. */
    assert(ios27_darwin_object_inspect(bytes, file_size, &info,
                                      error, sizeof(error)) == 0);

#if defined(__aarch64__) && defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *mapping;
        uintptr_t target;
        int (*function)(void);
        assert(page >= 4096L && (uint64_t)page >= info.text_size);
        mapping = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(mapping != MAP_FAILED);
        memcpy(mapping, bytes + (size_t)info.text_file_offset,
               (size_t)info.text_size);
#if defined(__GNUC__)
        __builtin___clear_cache((char *)mapping,
                                (char *)mapping + (size_t)info.text_size);
#endif
        assert(mprotect(mapping, (size_t)page, PROT_READ | PROT_EXEC) == 0);
        target = (uintptr_t)(mapping + (size_t)info.entry_offset);
        memcpy(&function, &target, sizeof(function));
        /* True Clang/Darwin-produced arm64 instructions executed without
         * emulation, and only this trusted zero-relocation local fixture. */
        assert(function() == 42);
        assert(munmap(mapping, (size_t)page) == 0);
    }
#else
    puts("Mach-O inspection only: native execution requires ARM64 Linux");
#endif
    free(copy);
    free(bytes);
    puts("compiler-produced Darwin Mach-O object inspection passed");
    return 0;
}
