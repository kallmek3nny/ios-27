#include "ios27/darwin_branch26.h"
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

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void wr32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(n >> (i * 8u));
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
static uint8_t *load(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long length;
    uint8_t *buf;
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    length = ftell(f);
    assert(length > 32 && length <= 1024L * 1024L);
    assert(fseek(f, 0, SEEK_SET) == 0);
    buf = malloc((size_t)length);
    assert(buf);
    assert(fread(buf, 1u, (size_t)length, f) == (size_t)length);
    assert(fclose(f) == 0);
    *out_size = (size_t)length;
    return buf;
}
static void rejected(const uint8_t *f, size_t n) {
    ios27_darwin_branch26_plan info;
    char error[160] = {0};
    assert(ios27_darwin_branch26_inspect(f, n, &info, error,
                                         sizeof(error)) == -1);
    assert(error[0]);
}


/* Phase 19d: property-style deterministic relocation transactions. */
static void phase19d_batch_tests(const uint8_t *file,
                                const ios27_darwin_branch26_plan *plan) {
    uint8_t code[64] = {0}, saved[64];
    ios27_darwin_arm64_reloc ops[4] = {{0}};
    char error[160] = {0};
    const uintptr_t base = UINT32_C(0x01000000);
    assert(plan->branch_offset + 4u <= plan->text_size);
    /* This instruction is copied from a genuinely compiler-emitted object. */
    wr32(code, rd32(file + plan->text_file_offset + plan->branch_offset));
    ops[0].type = 2u;
    ops[0].length = 2u;
    ops[0].pcrel = 1u;
    ops[0].external = 1u;
    for (unsigned step = 1u; step < 257u; ++step) {
        wr32(code, UINT32_C(0x94000000));
        ops[0].target = base + 4u * step;
        assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                              ops, 1u, error,
                                              sizeof(error)) == 0);
        assert((rd32(code) & UINT32_C(0x03ffffff)) == step);
    }
    wr32(code, UINT32_C(0x94000000));
    ops[0].target = base - 4u;
    assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                          ops, 1u, error,
                                          sizeof(error)) == 0);
    assert((rd32(code) & UINT32_C(0x03ffffff)) == 0x03ffffffu);
    wr32(code, UINT32_C(0x94000000));
    ops[0].target = base + UINT32_C(134217724);
    assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                          ops, 1u, error,
                                          sizeof(error)) == 0);
    assert((rd32(code) & UINT32_C(0x03ffffff)) == 0x01ffffffu);

    memset(code, 0, sizeof(code));
    wr32(code, UINT32_C(0x94000000));
    wr32(code + 4u, UINT32_C(0x90000021)); /* ADRP x1 */
    wr32(code + 8u, UINT32_C(0x91000021)); /* ADD x1,x1,#0 */
    wr32(code + 12u, UINT32_C(0xf9400020)); /* LDR x0,[x1] */
    ops[0].target = base + 60u;
    ops[1] = (ios27_darwin_arm64_reloc){0};
    ops[1].offset = 4u;
    ops[1].length = 2u;
    ops[1].pcrel = 1u;
    ops[1].type = 3u;
    ops[1].target = base + UINT32_C(0x3028);
    ops[2] = (ios27_darwin_arm64_reloc){0};
    ops[2].offset = 8u;
    ops[2].length = 2u;
    ops[2].type = 4u;
    ops[2].target = ops[1].target;
    ops[3] = (ios27_darwin_arm64_reloc){0};
    ops[3].offset = 12u;
    ops[3].length = 2u;
    ops[3].type = 4u;
    ops[3].target = base + UINT32_C(0x3028);
    assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                          ops, 4u, error,
                                          sizeof(error)) == 0);
    assert(rd32(code) == UINT32_C(0x9400000f));
    assert((rd32(code + 4u) & UINT32_C(0x9f00001f)) ==
           UINT32_C(0x90000001));
    assert(((rd32(code + 8u) >> 10u) & 0xfffu) == 0x28u);
    assert(((rd32(code + 12u) >> 10u) & 0xfffu) == 5u);

    memcpy(saved, code, sizeof(code));
    ops[3].target = base + 0x3029u; /* unaligned 64-bit load */
    assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                          ops, 4u, error,
                                          sizeof(error)) == -1);
    assert(!memcmp(code, saved, sizeof(code)));
    ops[3].target = base + 0x3028u;
    ops[3].offset = 4u; /* overlapping ADRP */
    assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                          ops, 4u, error,
                                          sizeof(error)) == -1);
    assert(!memcmp(code, saved, sizeof(code)));

    memset(code, 0, sizeof(code));
    memset(ops, 0, sizeof(ops));
    ops[0].type = 1u; ops[0].length = 3u;
    ops[0].target = 0x2000u;
    ops[1].type = 0u; ops[1].length = 3u;
    ops[1].target = 0x5000u;
    assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                          ops, 2u, error,
                                          sizeof(error)) == 0);
    assert(rd32(code) == 0x3000u);
    ops[0].type = 11u; /* PAC relocation requires actual signing. */
    memcpy(saved, code, sizeof(code));
    assert(ios27_darwin_arm64_apply_batch(code, sizeof(code), base,
                                          ops, 2u, error,
                                          sizeof(error)) == -1);
    assert(!memcmp(code, saved, sizeof(code)));
}


static void phase19d_linked_branch(const uint8_t *consumer, size_t consumer_size,
                                   const uint8_t *provider, size_t provider_size) {
    ios27_darwin_link_input inputs[2] = {
        {consumer, consumer_size}, {provider, provider_size}
    };
    ios27_darwin_link_layout layout = {0};
    uint8_t *image = malloc(4u * 16384u);
    uint8_t *copy = malloc(consumer_size);
    char error[160] = {0};
    assert(image && copy);
    memset(image, 0xa5, 4u * 16384u);
    for (unsigned pass = 0u; pass < 64u; ++pass) {
        assert(ios27_darwin_link_objects(
                   inputs, 2u, "_fixture_consumer",
                   image, 4u * 16384u, UINT32_C(0x100000),
                   (pass & 1u) ? 16384u : 4096u,
                   &layout, error, sizeof(error)) == 0);
        assert(layout.entry_offset < layout.rx_bytes);
        assert(layout.rx_bytes && !(layout.rx_bytes & 4095u));
        assert(layout.image_bytes <= 4u * 16384u);
    }
    memcpy(copy, consumer, consumer_size);
    wr32(copy + 8u, 2u); /* unsupported ARM64e input subtype */
    inputs[0].bytes = copy;
    memset(image, 0xa5, 4u * 16384u);
    assert(ios27_darwin_link_objects(
               inputs, 2u, "_fixture_consumer",
               image, 4u * 16384u, UINT32_C(0x100000), 4096u,
               &layout, error, sizeof(error)) == -1);
    assert(layout.image_bytes == 0u);
    for (size_t i = 0u; i < 4u * 16384u; ++i)
        assert(image[i] == 0xa5u); /* whole-image rollback */
    inputs[0].bytes = consumer;
#if defined(__aarch64__) && defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *map;
        uintptr_t entry_addr;
        int (*entry)(void);
        assert(page == 4096L || page == 16384L);
        map = mmap(NULL, (size_t)page * 4u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(map != MAP_FAILED);
        assert(ios27_darwin_link_objects(
                   inputs, 2u, "_fixture_consumer",
                   map, (size_t)page * 4u, (uintptr_t)map,
                   (size_t)page, &layout, error, sizeof(error)) == 0);
        __builtin___clear_cache((char *)map,
                                (char *)map + layout.rx_bytes);
        assert(mprotect(map, layout.rx_bytes,
                        PROT_READ | PROT_EXEC) == 0);
        entry_addr = (uintptr_t)(map + layout.entry_offset);
        memcpy(&entry, &entry_addr, sizeof(entry));
        assert(entry() == 43);
        assert(munmap(map, (size_t)page * 4u) == 0);
    }
#endif
    free(copy);
    free(image);
}

int main(int argc, char **argv) {
    uint8_t *provider, *consumer, *bad_file;
    uint8_t *staged, *snapshot;
    size_t provider_size, consumer_size, seg, sym, reloff;
    ios27_darwin_object provider_info;
    ios27_darwin_branch26_plan plan;
    char error[160] = {0};

    assert(argc == 3);
    provider = load(argv[1], &provider_size);
    consumer = load(argv[2], &consumer_size);
    bad_file = malloc(consumer_size);
    assert(bad_file);
    assert(ios27_darwin_object_inspect(provider, provider_size,
                                      &provider_info, error,
                                      sizeof(error)) == 0);
    assert(ios27_darwin_branch26_inspect(consumer, consumer_size,
                                         &plan, error, sizeof(error)) == 0);
    assert(plan.text_file_offset <= consumer_size &&
           plan.text_size <= consumer_size - plan.text_file_offset);
    assert(plan.branch_offset < plan.text_size);
    assert(plan.entry_offset < plan.text_size);
    assert(rd32(consumer + (size_t)plan.text_file_offset +
                (size_t)plan.branch_offset) == UINT32_C(0x94000000));

    seg = command(consumer, consumer_size, 0x19u);
    sym = command(consumer, consumer_size, 0x02u);
    /* The Clang fixture places its one relocation in __TEXT,__text. */
    assert(seg + 152u <= consumer_size);
    assert(rd32(consumer + seg + 64u) >= 1u);
    assert(rd32(consumer + seg + 72u + 60u) == 1u);
    reloff = rd32(consumer + seg + 72u + 56u);
    assert(reloff <= consumer_size && consumer_size - reloff >= 8u);
    assert(rd32(consumer + reloff) == plan.branch_offset);

    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + reloff + 4u, rd32(bad_file + reloff + 4u) ^
                                    (UINT32_C(1) << 28u)); /* BRANCH26 type */
    rejected(bad_file, consumer_size);
    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + reloff + 4u, rd32(bad_file + reloff + 4u) ^
                                    (UINT32_C(1) << 24u)); /* pc-relative */
    rejected(bad_file, consumer_size);
    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + reloff + 4u, rd32(bad_file + reloff + 4u) ^
                                    (UINT32_C(1) << 27u)); /* external */
    rejected(bad_file, consumer_size);
    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + reloff, UINT32_MAX); /* scattered/negative offset */
    rejected(bad_file, consumer_size);
    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + seg + 72u + 60u, 2u); /* multiple relocations */
    rejected(bad_file, consumer_size);
    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + sym + 8u, UINT32_MAX); /* truncated symbol table */
    rejected(bad_file, consumer_size);
    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + (size_t)plan.text_file_offset +
         (size_t)plan.branch_offset, UINT32_C(0xd65f03c0));
    rejected(bad_file, consumer_size);
    memcpy(bad_file, consumer, consumer_size);
    wr32(bad_file + 8u, 2u); /* arm64e/unsupported subtype */
    rejected(bad_file, consumer_size);

    phase19d_batch_tests(consumer, &plan);
    phase19d_linked_branch(consumer, consumer_size, provider, provider_size);

    staged = malloc((size_t)plan.text_size);
    snapshot = malloc((size_t)plan.text_size);
    assert(staged && snapshot);
    memcpy(staged, consumer + (size_t)plan.text_file_offset,
           (size_t)plan.text_size);
    memcpy(snapshot, staged, (size_t)plan.text_size);
    assert(ios27_darwin_branch26_apply(staged, (size_t)plan.text_size,
                                         &plan, UINT32_C(0x00100000),
                                         UINT32_C(0x00100100),
                                         error, sizeof(error)) == 0);
    assert(rd32(staged + (size_t)plan.branch_offset) !=
           UINT32_C(0x94000000));
    memcpy(staged, snapshot, (size_t)plan.text_size);
    assert(ios27_darwin_branch26_apply(staged, (size_t)plan.text_size,
                                         &plan, UINT32_C(0x00100100),
                                         UINT32_C(0x00100000),
                                         error, sizeof(error)) == 0);
    assert((rd32(staged + (size_t)plan.branch_offset) & UINT32_C(0xfc000000)) ==
           UINT32_C(0x94000000));
    memcpy(staged, snapshot, (size_t)plan.text_size);
    assert(ios27_darwin_branch26_apply(staged, (size_t)plan.text_size,
                                         &plan, UINT32_C(0x00100000),
                                         UINT32_C(0xf0000000),
                                         error, sizeof(error)) == -1);
    assert(!memcmp(staged, snapshot, (size_t)plan.text_size));
    assert(ios27_darwin_branch26_apply(staged, (size_t)plan.text_size,
                                         &plan, UINT32_C(0x00100000),
                                         UINT32_C(0x00100003),
                                         error, sizeof(error)) == -1);
    assert(!memcmp(staged, snapshot, (size_t)plan.text_size));
    staged[(size_t)plan.branch_offset] ^= 1u;
    memcpy(snapshot, staged, (size_t)plan.text_size);
    assert(ios27_darwin_branch26_apply(staged, (size_t)plan.text_size,
                                         &plan, UINT32_C(0x00100000),
                                         UINT32_C(0x00100100),
                                         error, sizeof(error)) == -1);
    assert(!memcmp(staged, snapshot, (size_t)plan.text_size));

#if defined(__aarch64__) && defined(__linux__)
    {
        long page_size = sysconf(_SC_PAGESIZE);
        uint8_t *map;
        size_t provider_base = 512u;
        uintptr_t entry_addr;
        int (*entry)(void);
        assert(page_size >= 4096L);
        assert(plan.text_size <= provider_base);
        assert(provider_base + provider_info.text_size <= (uint64_t)page_size);
        map = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(map != MAP_FAILED);
        memcpy(map, consumer + (size_t)plan.text_file_offset,
               (size_t)plan.text_size);
        memcpy(map + provider_base,
               provider + (size_t)provider_info.text_file_offset,
               (size_t)provider_info.text_size);
        {
            ios27_darwin_arm64_reloc relocation = {0};
            relocation.offset = (uint32_t)plan.branch_offset;
            relocation.type = 2u;
            relocation.length = 2u;
            relocation.pcrel = 1u;
            relocation.external = 1u;
            relocation.target = (uintptr_t)(map + provider_base +
                                            (size_t)provider_info.entry_offset);
            assert(ios27_darwin_arm64_apply_batch(
                       map, (size_t)plan.text_size, (uintptr_t)map,
                       &relocation, 1u, error, sizeof(error)) == 0);
        }
        __builtin___clear_cache((char *)map, (char *)map + (size_t)page_size);
        assert(mprotect(map, (size_t)page_size,
                        PROT_READ | PROT_EXEC) == 0);
        entry_addr = (uintptr_t)(map + (size_t)plan.entry_offset);
        memcpy(&entry, &entry_addr, sizeof(entry));
        /* Two genuine Clang-produced Mach-O C objects, resolved with one
         * checked BRANCH26 relocation, execute natively on ARM64 Linux. */
        assert(entry() == 43);
        assert(munmap(map, (size_t)page_size) == 0);
    }
#else
    puts("cross-object code execution skipped: host is not ARM64 Linux");
#endif
    free(snapshot);
    free(staged);
    free(bad_file);
    free(consumer);
    free(provider);
    puts("real Darwin BRANCH26 external relocation fixtures passed");
    return 0;
}
