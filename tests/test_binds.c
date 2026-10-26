#include "ios27/binds.h"
#include "ios27/exports.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <sys/mman.h>
#include <unistd.h>
#endif

#define IMAGE_LEN 512u
#define STREAM_OFF 232u
#define CONSUMER_TEXT_OFF 256u
#define CONSUMER_DATA_OFF 320u
#define PROVIDER_TEXT_OFF 256u

static void le32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
static void le64(uint8_t *p, uint64_t v) {
    le32(p, (uint32_t)v);
    le32(p + 4u, (uint32_t)(v >> 32u));
}
static uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0u;
    for (unsigned i = 0u; i < 8u; ++i)
        v |= (uint64_t)p[i] << (8u * i);
    return v;
}
static void segment(uint8_t *p, const char *name,
                    uint64_t vmaddr, uint64_t vmsize,
                    uint64_t fileoff, uint64_t filesize, uint32_t prot) {
    le32(p, 0x19u);
    le32(p + 4u, 72u);
    memcpy(p + 8u, name, strlen(name));
    le64(p + 24u, vmaddr);
    le64(p + 32u, vmsize);
    le64(p + 40u, fileoff);
    le64(p + 48u, filesize);
    le32(p + 56u, prot);
    le32(p + 60u, prot);
}

static const uint8_t binding[] = {
    0x11u,                     /* library ordinal 1 */
    0x40u, '_','a','n','s','w','e','r',0,
    0x51u,                     /* pointer bind */
    0x71u, 0x00u,              /* __DATA, offset zero */
    0x90u, 0x00u               /* bind and finish */
};

static void consumer(uint8_t image[IMAGE_LEN]) {
    memset(image, 0, IMAGE_LEN);
    le32(image, 0xfeedfacfu);
    le32(image + 4u, 0x0100000cu);
    le32(image + 12u, 0x02u);
    le32(image + 16u, 3u);
    le32(image + 20u, 192u);
    segment(image + 32u, "__TEXT", 0x100000000ull, 4096u,
            CONSUMER_TEXT_OFF, 12u, 5u);
    segment(image + 104u, "__DATA", 0x100001000ull, 4096u,
            CONSUMER_DATA_OFF, 64u, 3u);
    le32(image + 176u, 0x80000022u);
    le32(image + 180u, 48u);
    le32(image + 192u, STREAM_OFF); /* bind_off */
    le32(image + 196u, (uint32_t)sizeof(binding)); /* bind_size */
    memcpy(image + STREAM_OFF, binding, sizeof(binding));
    /* ADRP x16,+1page; LDR x16,[x16]; BR x16 */
    le32(image + CONSUMER_TEXT_OFF, 0xb0000010u);
    le32(image + CONSUMER_TEXT_OFF + 4u, 0xf9400210u);
    le32(image + CONSUMER_TEXT_OFF + 8u, 0xd61f0200u);
}

static void provider(uint8_t image[IMAGE_LEN]) {
    static const uint8_t trie[] = {
        0x00u, 0x01u, '_','a','n','s','w','e','r', 0x00u, 0x0bu,
        0x02u, 0x00u, 0x00u, 0x00u
    };
    memset(image, 0, IMAGE_LEN);
    le32(image, 0xfeedfacfu);
    le32(image + 4u, 0x0100000cu);
    le32(image + 12u, 0x06u);
    le32(image + 16u, 2u);
    le32(image + 20u, 88u);
    segment(image + 32u, "__TEXT", 0x100000000ull, 4096u,
            PROVIDER_TEXT_OFF, 8u, 5u);
    le32(image + 104u, 0x80000033u);
    le32(image + 108u, 16u);
    le32(image + 112u, 192u);
    le32(image + 116u, (uint32_t)sizeof(trie));
    memcpy(image + 192u, trie, sizeof(trie));
    /* mov w0,#42; ret */
    le32(image + PROVIDER_TEXT_OFF, 0x52800540u);
    le32(image + PROVIDER_TEXT_OFF + 4u, 0xd65f03c0u);
}

typedef struct {
    const ios27_exports *exports;
    uint64_t base;
    int32_t permitted_ordinal;
} provider_context;

static int resolve(void *opaque, const char *symbol,
                   int32_t ordinal, uint64_t *address) {
    provider_context *ctx = opaque;
    const ios27_export *e = ios27_exports_find(ctx->exports, symbol);
    if (ordinal != ctx->permitted_ordinal || !e ||
        (e->flags & (IOS27_EXPORT_REEXPORT | IOS27_EXPORT_STUB_RESOLVER)) ||
        e->address > UINT64_MAX - ctx->base)
        return -1;
    *address = ctx->base + e->address;
    return 0;
}

int main(void) {
    uint8_t importer[IMAGE_LEN], exporter[IMAGE_LEN], original[IMAGE_LEN];
    ios27_macho_info import_info;
    ios27_bind_plan plan;
    ios27_exports exports;
    provider_context ctx;
    char error[128];
    consumer(importer);
    provider(exporter);
    assert(ios27_macho_parse(importer, sizeof(importer), &import_info,
                             error, sizeof(error)) == 0);
    assert(ios27_exports_parse(exporter, sizeof(exporter), &exports,
                               error, sizeof(error)) == 0);
    assert(exports.count == 1u);
    assert(ios27_binds_parse(importer, sizeof(importer), &import_info,
                             &plan, error, sizeof(error)) == 0);
    assert(plan.count == 1u && plan.entries[0].segment_index == 1u);
    assert(strcmp(plan.entries[0].symbol, "_answer") == 0);
    ctx.exports = &exports;
    ctx.base = 0x123450000ull;
    ctx.permitted_ordinal = 1;

    memcpy(original, importer, sizeof(original));
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) == 0);
    assert(rd64(importer + CONSUMER_DATA_OFF) == ctx.base);
    ctx.permitted_ordinal = 2;
    memcpy(importer, original, sizeof(importer));
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) != 0);
    assert(memcmp(importer, original, sizeof(importer)) == 0);
    ctx.permitted_ordinal = 1;

    plan.entries[0].addend = -16;
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) == 0);
    assert(rd64(importer + CONSUMER_DATA_OFF) == ctx.base - 16u);
    memcpy(importer, original, sizeof(importer));
    plan.entries[0].addend = 0;
    ctx.base = 0u;
    plan.entries[0].addend = -1;
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) != 0);
    assert(memcmp(importer, original, sizeof(importer)) == 0);
    plan.entries[0].addend = 0;
    ctx.base = UINT64_MAX;
    plan.entries[0].addend = 1;
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) != 0);
    assert(memcmp(importer, original, sizeof(importer)) == 0);
    plan.entries[0].addend = 0;

    /* File-backed __DATA binds can be repeated with spacing constraints. */
    consumer(importer);
    importer[STREAM_OFF + 13u] = 0xc0u;
    importer[STREAM_OFF + 14u] = 2u;
    importer[STREAM_OFF + 15u] = 0u;
    importer[STREAM_OFF + 16u] = 0u;
    le32(importer + 196u, 17u);
    ctx.base = 0x123450000ull;
    assert(ios27_binds_parse(importer, sizeof(importer), &import_info,
                             &plan, error, sizeof(error)) == 0);
    assert(plan.count == 2u);
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) == 0);
    assert(rd64(importer + CONSUMER_DATA_OFF) == ctx.base);
    assert(rd64(importer + CONSUMER_DATA_OFF + 8u) == ctx.base);
    plan.entries[1].segment_offset = 0u;
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) != 0);

    consumer(importer);
    assert(ios27_binds_parse(importer, sizeof(importer), &import_info,
                             &plan, error, sizeof(error)) == 0);

    /* Reject relocation into the read-only executable text segment. */
    plan.entries[0].segment_index = 0u;
    assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                             &plan, resolve, &ctx, error, sizeof(error)) != 0);
    plan.entries[0].segment_index = 1u;

    consumer(importer);
    importer[STREAM_OFF] = 0xd0u; /* unsupported THREADED/chained bind */
    assert(ios27_binds_parse(importer, sizeof(importer), &import_info,
                             &plan, error, sizeof(error)) != 0);
    consumer(importer);
    importer[STREAM_OFF + 12u] = 0x7fu; /* segment offset beyond file */
    assert(ios27_binds_parse(importer, sizeof(importer), &import_info,
                             &plan, error, sizeof(error)) != 0);
    consumer(importer);
    le32(importer + 192u, UINT32_MAX); /* out-of-range bind bytes */
    assert(ios27_binds_parse(importer, sizeof(importer), &import_info,
                             &plan, error, sizeof(error)) != 0);

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        long ps = sysconf(_SC_PAGESIZE);
        void *function_page;
        void *caller_pages;
        int (*entry)(void);
        uintptr_t bits;
        assert(ps >= 4096);
        function_page = mmap(NULL, (size_t)ps, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        caller_pages = mmap(NULL, (size_t)ps * 2u,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(function_page != MAP_FAILED && caller_pages != MAP_FAILED);
        memcpy(function_page, exporter + PROVIDER_TEXT_OFF, 8u);
        assert(mprotect(function_page, (size_t)ps,
                        PROT_READ | PROT_EXEC) == 0);

        consumer(importer);
        assert(ios27_binds_parse(importer, sizeof(importer), &import_info,
                                 &plan, error, sizeof(error)) == 0);
        ctx.base = (uint64_t)(uintptr_t)function_page;
        assert(ios27_binds_apply(importer, sizeof(importer), &import_info,
                                 &plan, resolve, &ctx,
                                 error, sizeof(error)) == 0);
        memcpy(caller_pages, importer + CONSUMER_TEXT_OFF, 12u);
        /* ADRP uses 4K address pages even if Android's VM page is 16K. */
        assert((ps % 4096) == 0 && (ps / 4096) < 1048576);
        {
            uint32_t delta = (uint32_t)((size_t)ps / 4096u);
            le32((uint8_t *)caller_pages, 0x90000010u |
                 ((delta & 3u) << 29u) | ((delta >> 2u) << 5u));
        }
        memcpy((uint8_t *)caller_pages + (size_t)ps,
               importer + CONSUMER_DATA_OFF, 8u);
        __builtin___clear_cache((char *)caller_pages,
                                (char *)caller_pages + 12u);
        assert(mprotect(caller_pages, (size_t)ps, PROT_READ | PROT_EXEC) == 0);
        bits = (uintptr_t)caller_pages;
        memcpy(&entry, &bits, sizeof(entry));
        assert(entry() == 42);
        assert(munmap(caller_pages, (size_t)ps * 2u) == 0);
        assert(munmap(function_page, (size_t)ps) == 0);
    }
#endif

    puts("classic dyld bind tests passed");
    return 0;
}
