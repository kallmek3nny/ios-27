#include "ios27/darwin_got.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) && defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void wr32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(n >> (i * 8u));
}
static size_t command(const uint8_t *bytes, size_t size, uint32_t op) {
    size_t pos = 32u;
    assert(size >= 32u);
    for (uint32_t i = 0u; i < rd32(bytes + 16u); ++i) {
        uint32_t cmdsize;
        assert(pos < size && size - pos >= 8u);
        cmdsize = rd32(bytes + pos + 4u);
        assert(cmdsize >= 8u && cmdsize <= size - pos);
        if (rd32(bytes + pos) == op) return pos;
        pos += cmdsize;
    }
    assert(!"Mach-O command missing");
    return 0u;
}
static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    long length;
    uint8_t *p;
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    length = ftell(f);
    assert(length >= 32 && length <= 1024L * 1024L);
    assert(fseek(f, 0, SEEK_SET) == 0);
    p = malloc((size_t)length);
    assert(p);
    assert(fread(p, 1u, (size_t)length, f) == (size_t)length);
    assert(fclose(f) == 0);
    *size = (size_t)length;
    return p;
}
static void consumer_rejected(const uint8_t *p, size_t size) {
    ios27_darwin_got_plan plan;
    char error[160] = {0};
    assert(ios27_darwin_got_inspect(p, size, &plan, error,
                                   sizeof(error)) == -1);
    assert(error[0]);
}
static void provider_rejected(const uint8_t *p, size_t size) {
    ios27_darwin_const_plan plan;
    char error[160] = {0};
    assert(ios27_darwin_const_inspect(p, size, &plan, error,
                                     sizeof(error)) == -1);
    assert(error[0]);
}
static void apply_rejected(uint8_t *text, size_t size,
                           const ios27_darwin_got_plan *plan,
                           uintptr_t pc, uintptr_t got) {
    uint8_t *before = malloc(size);
    char error[160] = {0};
    assert(before);
    memcpy(before, text, size);
    assert(ios27_darwin_got_apply(text, size, plan, pc, got,
                                  error, sizeof(error)) == -1);
    assert(error[0]);
    assert(memcmp(before, text, size) == 0);
    free(before);
}

int main(int argc, char **argv) {
    uint8_t *consumer, *provider, *bad, *staged, *original;
    size_t consumer_size, provider_size, seg, sym, reloff, provider_seg;
    ios27_darwin_got_plan plan;
    ios27_darwin_const_plan data_plan;
    char error[160] = {0};

    assert(argc == 3);
    consumer = load(argv[1], &consumer_size);
    provider = load(argv[2], &provider_size);
    bad = malloc(consumer_size > provider_size ? consumer_size : provider_size);
    assert(bad);
    assert(ios27_darwin_got_inspect(consumer, consumer_size, &plan,
                                    error, sizeof(error)) == 0);
    assert(ios27_darwin_const_inspect(provider, provider_size, &data_plan,
                                      error, sizeof(error)) == 0);
    assert(plan.text_size >= 12u && plan.text_size < 4096u);
    assert(plan.adrp_offset + 4u == plan.ldr_offset);
    assert(plan.entry_offset < plan.text_size);
    assert(data_plan.data_size == 4u);
    assert(data_plan.data_file_offset <= provider_size - 4u);
    assert(rd32(provider + data_plan.data_file_offset) == 41u);
    assert(rd32(consumer + plan.text_file_offset + plan.adrp_offset) ==
           UINT32_C(0x90000008));
    assert(rd32(consumer + plan.text_file_offset + plan.ldr_offset) ==
           UINT32_C(0xf9400108));

    seg = command(consumer, consumer_size, 0x19u);
    sym = command(consumer, consumer_size, 2u);
    assert(rd32(consumer + seg + 64u) == 1u);
    reloff = rd32(consumer + seg + 72u + 56u);
    assert(reloff < consumer_size && consumer_size - reloff >= 16u);
    assert(rd32(consumer + reloff) == plan.ldr_offset);
    assert(rd32(consumer + reloff + 8u) == plan.adrp_offset);

    memcpy(bad, consumer, consumer_size);
    wr32(bad + reloff + 4u, rd32(bad + reloff + 4u) ^
                               (UINT32_C(1) << 28u));
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + reloff + 12u, rd32(bad + reloff + 12u) ^
                                (UINT32_C(1) << 24u));
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + reloff + 4u, rd32(bad + reloff + 4u) ^
                               (UINT32_C(1) << 27u));
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + reloff + 8u, UINT32_MAX);
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + reloff + 12u, rd32(bad + reloff + 12u) ^ 1u);
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + seg + 72u + 60u, 3u);
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + sym + 8u, UINT32_MAX);
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + 8u, 2u);
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + (size_t)plan.text_file_offset + plan.adrp_offset,
         UINT32_C(0xd503201f));
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + (size_t)plan.text_file_offset + plan.ldr_offset,
         UINT32_C(0xd503201f));
    consumer_rejected(bad, consumer_size);
    memcpy(bad, consumer, consumer_size);
    wr32(bad + seg + 72u + 48u, UINT32_MAX);
    consumer_rejected(bad, consumer_size);

    provider_seg = command(provider, provider_size, 0x19u);
    assert(rd32(provider + provider_seg + 64u) == 2u);
    memcpy(bad, provider, provider_size);
    wr32(bad + provider_seg + 72u + 80u + 40u, 12u);
    provider_rejected(bad, provider_size);
    memcpy(bad, provider, provider_size);
    wr32(bad + provider_seg + 72u + 80u + 60u, 1u);
    provider_rejected(bad, provider_size);
    memcpy(bad, provider, provider_size);
    wr32(bad + 8u, 2u);
    provider_rejected(bad, provider_size);

    staged = malloc((size_t)plan.text_size);
    original = malloc((size_t)plan.text_size);
    assert(staged && original);
    memcpy(original, consumer + (size_t)plan.text_file_offset,
           (size_t)plan.text_size);
    memcpy(staged, original, (size_t)plan.text_size);
    assert(ios27_darwin_got_apply(staged, (size_t)plan.text_size, &plan,
                                  UINT32_C(0x100000), UINT32_C(0x101018),
                                  error, sizeof(error)) == 0);
    assert(rd32(staged + plan.adrp_offset) == UINT32_C(0xb0000008));
    assert(rd32(staged + plan.ldr_offset) ==
           (UINT32_C(0xf9400108) | (3u << 10u)));

    memcpy(staged, original, (size_t)plan.text_size);
    assert(ios27_darwin_got_apply(staged, (size_t)plan.text_size, &plan,
                                  UINT32_C(0x102000), UINT32_C(0x101018),
                                  error, sizeof(error)) == 0);
    assert((rd32(staged + plan.adrp_offset) & UINT32_C(0x9f00001f)) ==
           UINT32_C(0x90000008));
    memcpy(staged, original, (size_t)plan.text_size);
    apply_rejected(staged, (size_t)plan.text_size, &plan,
                   UINT32_C(0x100000), UINT64_C(0x200001018));
    apply_rejected(staged, (size_t)plan.text_size, &plan,
                   UINT32_C(0x100000), UINT32_C(0x101003));
    apply_rejected(staged, (size_t)plan.text_size, &plan,
                   UINT32_C(0x100002), UINT32_C(0x101018));
    staged[plan.ldr_offset] ^= 1u;
    apply_rejected(staged, (size_t)plan.text_size, &plan,
                   UINT32_C(0x100000), UINT32_C(0x101018));

#if defined(__aarch64__) && defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *mapping;
        uintptr_t slot_value, address;
        int (*fn)(void);
        assert(page >= 4096L && page <= 65536L);
        assert((uint64_t)page >= plan.text_size);
        mapping = mmap(NULL, (size_t)page * 3u, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(mapping != MAP_FAILED);
        memcpy(mapping, original, (size_t)plan.text_size);
        memcpy(mapping + (size_t)page * 2u,
               provider + (size_t)data_plan.data_file_offset,
               (size_t)data_plan.data_size);
        slot_value = (uintptr_t)(mapping + (size_t)page * 2u);
        memcpy(mapping + (size_t)page, &slot_value, sizeof(slot_value));
        assert(ios27_darwin_got_apply(
                   mapping, (size_t)plan.text_size, &plan,
                   (uintptr_t)mapping, (uintptr_t)(mapping + (size_t)page),
                   error, sizeof(error)) == 0);
        __builtin___clear_cache((char *)mapping,
                                (char *)mapping + (size_t)page);
        assert(mprotect(mapping, (size_t)page,
                        PROT_READ | PROT_EXEC) == 0);
        assert(mprotect(mapping + (size_t)page, (size_t)page,
                        PROT_READ) == 0);
        assert(mprotect(mapping + (size_t)page * 2u, (size_t)page,
                        PROT_READ) == 0);
        address = (uintptr_t)(mapping + (size_t)plan.entry_offset);
        memcpy(&fn, &address, sizeof(fn));
        assert(fn() == 43);
        assert(munmap(mapping, (size_t)page * 3u) == 0);
    }
#else
    puts("native GOT data call skipped: host is not ARM64 Linux");
#endif
    free(staged);
    free(original);
    free(bad);
    free(consumer);
    free(provider);
    puts("real Darwin GOT data relocation fixture passed");
    return 0;
}
