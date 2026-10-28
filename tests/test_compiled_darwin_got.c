#include "ios27/darwin_got.h"
#include "ios27/darwin_branch26.h"

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


/* The two relocations here come from genuine independent Clang Mach-O
 * objects. Exercise the generic ARM64e-safe (non-PAC) transaction with
 * the same real instructions as the legacy fixture-specific binder. */
static void phase19d_real_got(const uint8_t *original,
                             const ios27_darwin_got_plan *plan) {
    ios27_darwin_arm64_reloc ops[2] = {{0}};
    uint8_t *first, *legacy, *before;
    size_t n = (size_t)plan->text_size;
    char error[160] = {0};
    first = malloc(n);
    legacy = malloc(n);
    before = malloc(n);
    assert(first && legacy && before);
    memcpy(first, original, n);
    memcpy(legacy, original, n);
    memcpy(before, original, n);
    ops[0].offset = plan->ldr_offset;
    ops[0].type = 6u; ops[0].length = 2u;
    ops[0].target = UINT32_C(0x101018);
    ops[1].offset = plan->adrp_offset;
    ops[1].type = 5u; ops[1].length = 2u;
    ops[1].pcrel = 1u;
    ops[1].target = ops[0].target;
    assert(ios27_darwin_arm64_apply_batch(first, n, UINT32_C(0x100000),
                                          ops, 2u, error,
                                          sizeof(error)) == 0);
    assert(ios27_darwin_got_apply(legacy, n, plan,
                                  UINT32_C(0x100000),
                                  UINT32_C(0x101018),
                                  error, sizeof(error)) == 0);
    assert(memcmp(first, legacy, n) == 0);
    memcpy(first, before, n);
    ops[1].target = UINT64_C(0x200001018);
    assert(ios27_darwin_arm64_apply_batch(first, n, UINT32_C(0x100000),
                                          ops, 2u, error,
                                          sizeof(error)) == -1);
    assert(memcmp(first, before, n) == 0);
    ops[1].target = ops[0].target;
    ops[0].target += 1u;
    assert(ios27_darwin_arm64_apply_batch(first, n, UINT32_C(0x100000),
                                          ops, 2u, error,
                                          sizeof(error)) == -1);
    assert(memcmp(first, before, n) == 0);
    free(first); free(legacy); free(before);
}


static void phase19d_linked_got(const uint8_t *consumer, size_t consumer_size,
                                const uint8_t *provider, size_t provider_size) {
    ios27_darwin_link_input inputs[2] = {
        {consumer, consumer_size}, {provider, provider_size}
    };
    ios27_darwin_link_layout layout = {0};
    char error[160] = {0};
    uint8_t *stage = malloc(4u * 16384u);
    assert(stage);
    for (unsigned pass = 0u; pass < 48u; ++pass) {
        const size_t page = (pass & 1u) ? 4096u : 16384u;
        if (ios27_darwin_link_objects(
                inputs, 2u, "_fixture_read_data",
                stage, 4u * 16384u, UINT32_C(0x100000),
                page, &layout, error, sizeof(error)) != 0) {
            fprintf(stderr, "phase19d GOT linking failed: %s\n", error);
            assert(!"phase19d multi-object GOT link failed");
        }
        assert(layout.rx_bytes == page);
        assert(layout.ro_bytes == page);
        assert(layout.rw_bytes == 0u);
        assert(layout.image_bytes == 2u * page);
        assert(layout.entry_offset < layout.rx_bytes);
    }
#if defined(__aarch64__) && defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *map;
        uintptr_t addr;
        int (*entry)(void);
        assert(page == 4096L || page == 16384L);
        map = mmap(NULL, 4u * (size_t)page,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(map != MAP_FAILED);
        assert(ios27_darwin_link_objects(
                   inputs, 2u, "_fixture_read_data",
                   map, 4u * (size_t)page, (uintptr_t)map,
                   (size_t)page, &layout, error, sizeof(error)) == 0);
        __builtin___clear_cache((char *)map,
                                (char *)map + layout.rx_bytes);
        assert(mprotect(map, layout.rx_bytes,
                        PROT_READ | PROT_EXEC) == 0);
        assert(mprotect(map + layout.rx_bytes, layout.ro_bytes,
                        PROT_READ) == 0);
        addr = (uintptr_t)(map + layout.entry_offset);
        memcpy(&entry, &addr, sizeof(entry));
        assert(entry() == 43);
        assert(munmap(map, 4u * (size_t)page) == 0);
    }
#endif
    free(stage);
}


/* Four separately compiled Mach-O objects, two independent external
 * bindings, shared executable layout and a const-data/GOT subgraph. */
static void phase19d_quad_link(const char *consumer_path,
                               const uint8_t *data_consumer, size_t dc_size,
                               const uint8_t *data_provider, size_t dp_size) {
    const char *slash = strrchr(consumer_path, '/');
    char path[1024];
    size_t prefix, branch_size, provider_size;
    uint8_t *branch_consumer, *branch_provider, *map;
    ios27_darwin_link_input inputs[5];
    ios27_darwin_link_layout layout = {0};
    char error[160] = {0};
    assert(slash);
    prefix = (size_t)(slash - consumer_path);
    assert(prefix < 900u);
    assert(snprintf(path, sizeof(path), "%.*s/arm64_darwin_consumer.o",
                    (int)prefix, consumer_path) > 0);
    branch_consumer = load(path, &branch_size);
    assert(snprintf(path, sizeof(path), "%.*s/arm64_darwin_fixture.o",
                    (int)prefix, consumer_path) > 0);
    branch_provider = load(path, &provider_size);
    inputs[0] = (ios27_darwin_link_input){data_consumer, dc_size};
    inputs[1] = (ios27_darwin_link_input){data_provider, dp_size};
    inputs[2] = (ios27_darwin_link_input){branch_consumer, branch_size};
    inputs[3] = (ios27_darwin_link_input){branch_provider, provider_size};
    inputs[4] = inputs[2]; /* duplicate export must fail before output write */
    map = malloc(4u * 16384u);
    assert(map);
    for (unsigned pass = 0u; pass < 32u; ++pass) {
        size_t page = (pass & 1u) ? 4096u : 16384u;
        assert(ios27_darwin_link_objects(
                   inputs, 4u, "_fixture_consumer", map,
                   4u * 16384u, UINT32_C(0x100000), page,
                   &layout, error, sizeof(error)) == 0);
        assert(layout.rx_bytes == page && layout.ro_bytes == page);
        assert(ios27_darwin_link_objects(
                   inputs, 4u, "_fixture_read_data", map,
                   4u * 16384u, UINT32_C(0x100000), page,
                   &layout, error, sizeof(error)) == 0);
        assert(layout.entry_offset < layout.rx_bytes);
    }
    memset(map, 0x3d, 4u * 16384u);
    assert(ios27_darwin_link_objects(
               inputs, 5u, "_fixture_consumer", map,
               4u * 16384u, UINT32_C(0x100000), 4096u,
               &layout, error, sizeof(error)) == -1);
    for (size_t i = 0u; i < 4u * 16384u; ++i)
        assert(map[i] == 0x3du);
#if defined(__aarch64__) && defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *memory;
        uintptr_t address;
        int (*fn)(void);
        assert(page == 4096L || page == 16384L);
        memory = mmap(NULL, 4u * (size_t)page, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(memory != MAP_FAILED);
        assert(ios27_darwin_link_objects(
                   inputs, 4u, "_fixture_consumer", memory,
                   4u * (size_t)page, (uintptr_t)memory, (size_t)page,
                   &layout, error, sizeof(error)) == 0);
        /* Both entrypoints are exported; the second is found by a second
         * private link transaction into the same fresh writable mapping. */
        __builtin___clear_cache((char *)memory,
                                (char *)memory + layout.rx_bytes);
        assert(mprotect(memory, layout.rx_bytes,
                        PROT_READ | PROT_EXEC) == 0);
        assert(mprotect(memory + layout.rx_bytes, layout.ro_bytes,
                        PROT_READ) == 0);
        /* layout.entry_offset currently points at _fixture_consumer. */
        address = (uintptr_t)(memory + layout.entry_offset);
        memcpy(&fn, &address, sizeof(fn));
        assert(fn() == 43);
        assert(munmap(memory, 4u * (size_t)page) == 0);
    }
#endif
    free(map);
    free(branch_consumer);
    free(branch_provider);
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

    phase19d_real_got(consumer + (size_t)plan.text_file_offset, &plan);
    phase19d_linked_got(consumer, consumer_size, provider, provider_size);
    phase19d_quad_link(argv[1], consumer, consumer_size, provider, provider_size);

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
        {
            ios27_darwin_arm64_reloc relocations[2] = {{0}};
            relocations[0].offset = plan.ldr_offset;
            relocations[0].type = 6u;
            relocations[0].length = 2u;
            relocations[0].target =
                (uintptr_t)(mapping + (size_t)page);
            relocations[1].offset = plan.adrp_offset;
            relocations[1].type = 5u;
            relocations[1].length = 2u;
            relocations[1].pcrel = 1u;
            relocations[1].target = relocations[0].target;
            assert(ios27_darwin_arm64_apply_batch(
                       mapping, (size_t)plan.text_size, (uintptr_t)mapping,
                       relocations, 2u, error, sizeof(error)) == 0);
        }
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
