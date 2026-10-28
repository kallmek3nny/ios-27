#include "ios27/chained.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <sys/mman.h>
#include <unistd.h>
#endif

#define IMAGE_SIZE 32768u
#define FIXUPS_OFF 256u
#define DATA_OFF 4096u
#define CODE_OFF 512u
#define PTR_BIND (UINT64_C(0x8000000000000000) | (UINT64_C(2) << 51u))
#define PTR_REBASE UINT64_C(0x70)

static void le16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8u);
}
static void le32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(n >> (i * 8u));
}
static void le64(uint8_t *p, uint64_t n) {
    le32(p, (uint32_t)n);
    le32(p + 4u, (uint32_t)(n >> 32u));
}
static uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0u;
    for (unsigned i = 0u; i < 8u; ++i)
        v |= (uint64_t)p[i] << (8u * i);
    return v;
}
static void segment(uint8_t *p, const char *name, uint64_t vmaddr,
                    uint64_t vmsize, uint64_t fileoff,
                    uint64_t filesize, uint32_t prot) {
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
static void fixture(uint8_t image[IMAGE_SIZE], uint16_t pagesize) {
    uint8_t *payload;
    memset(image, 0, IMAGE_SIZE);
    le32(image, 0xfeedfacfu);
    le32(image + 4u, 0x0100000cu);
    le32(image + 12u, 2u);
    le32(image + 16u, 3u);
    le32(image + 20u, 160u);
    segment(image + 32u, "__TEXT", UINT64_C(0x100000000), 4096u,
            0u, 1024u, 5u);
    segment(image + 104u, "__DATA", UINT64_C(0x100001000), 16384u,
            DATA_OFF, 16384u, 3u);
    le32(image + 176u, 0x80000034u);
    le32(image + 180u, 16u);
    le32(image + 184u, FIXUPS_OFF);
    le32(image + 188u, 80u);
    payload = image + FIXUPS_OFF;
    le32(payload + 4u, 28u);  /* starts in image */
    le32(payload + 8u, 64u);  /* one 4-byte import */
    le32(payload + 12u, 68u); /* uncompressed symbols */
    le32(payload + 16u, 1u);
    le32(payload + 20u, 1u);  /* DYLD_CHAINED_IMPORT */
    le32(payload + 28u, 2u); /* two segments */
    le32(payload + 36u, 12u); /* starts segment offset */
    le32(payload + 40u, 24u); /* starts segment size */
    le16(payload + 44u, pagesize);
    le16(payload + 46u, 6u); /* DYLD_CHAINED_PTR_64_OFFSET */
    le64(payload + 48u, 4096u); /* __DATA offset from image base */
    le16(payload + 60u, 1u); /* one page */
    le16(payload + 62u, 0u); /* chain starts at first byte */
    le32(payload + 64u, 1u); /* dylib ordinal 1, name offset 0 */
    memcpy(payload + 68u, "_answer", 8u);
    le64(image + DATA_OFF, PTR_BIND);
    le64(image + DATA_OFF + 8u, PTR_REBASE);
    /* ADRP x16,+1 page; LDR x16,[x16]; BR x16 */
    le32(image + CODE_OFF, 0xb0000010u);
    le32(image + CODE_OFF + 4u, 0xf9400210u);
    le32(image + CODE_OFF + 8u, 0xd61f0200u);
}
typedef struct {
    uint64_t value;
    int permitted;
} resolve_context;
static int resolve(void *opaque, const char *symbol,
                   int32_t ordinal, uint64_t *value) {
    resolve_context *ctx = opaque;
    if (!ctx->permitted || ordinal != 1 ||
        strcmp(symbol, "_answer") != 0) return -1;
    *value = ctx->value;
    return 0;
}
static void parse_ok(const uint8_t *image,
                     ios27_macho_info *info,
                     ios27_chained_plan *plan) {
    char error[128];
    assert(ios27_macho_parse(image, IMAGE_SIZE, info, error, sizeof(error)) == 0);
    assert(ios27_chained_parse(image, IMAGE_SIZE, info, plan,
                              error, sizeof(error)) == 0);
    assert(plan->count == 2u && plan->import_count == 1u);
    assert(plan->patches[0].is_bind && !plan->patches[1].is_bind);
    assert(strcmp(plan->imports[0].symbol, "_answer") == 0);
}
static void parse_bad(uint8_t *image) {
    ios27_macho_info info;
    ios27_chained_plan plan;
    char error[128];
    assert(ios27_macho_parse(image, IMAGE_SIZE, &info, error, sizeof(error)) == 0);
    assert(ios27_chained_parse(image, IMAGE_SIZE, &info, &plan,
                              error, sizeof(error)) != 0);
    assert(error[0] != '\0');
}
int main(void) {
    static uint8_t image[IMAGE_SIZE], original[IMAGE_SIZE];
    ios27_macho_info info;
    ios27_chained_plan plan;
    resolve_context ctx = { UINT64_C(0x123450000), 1 };
    char error[128];

    for (unsigned p = 0u; p < 2u; ++p) {
        fixture(image, p == 0u ? 4096u : 16384u);
        memcpy(original, image, IMAGE_SIZE);
        parse_ok(image, &info, &plan);
        assert(memcmp(image, original, IMAGE_SIZE) == 0);
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) == 0);
        assert(rd64(image + DATA_OFF) == ctx.value);
        assert(rd64(image + DATA_OFF + 8u) == UINT64_C(0x500000070));
        assert(rd64(image + DATA_OFF + 16u) == 0u);
        memcpy(image, original, IMAGE_SIZE);
        ctx.permitted = 0;
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) != 0);
        assert(memcmp(image, original, IMAGE_SIZE) == 0);
        ctx.permitted = 1;
        ctx.value = UINT64_MAX;
        plan.patches[0].addend = 1;
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) != 0);
        assert(memcmp(image, original, IMAGE_SIZE) == 0);
        ctx.value = UINT64_C(0x123450000);
        plan.patches[0].addend = 0;
        plan.patches[1].rebase_target = UINT64_MAX;
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) != 0);
        assert(memcmp(image, original, IMAGE_SIZE) == 0);
        plan.patches[1].rebase_target = 0x70u;
        plan.patches[1].rebase_target = 0x300000u; /* outside mapped image */
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) != 0);
        assert(memcmp(image, original, IMAGE_SIZE) == 0);
        plan.patches[1].rebase_target = 0x70u;
        plan.patches[1].segment_offset = 0u; /* overlapping patch */
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) != 0);
        assert(memcmp(image, original, IMAGE_SIZE) == 0);
        plan.patches[1].segment_offset = 8u;
        image[DATA_OFF] ^= 1u; /* stale plan */
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) != 0);
    }

    /* Signed 32-bit import addends must be applied without overflow. */
    fixture(image, 4096u);
    le32(image + 188u, 84u);
    le32(image + FIXUPS_OFF + 20u, 2u);
    le32(image + FIXUPS_OFF + 12u, 72u);
    le32(image + FIXUPS_OFF + 68u, 13u);
    memcpy(image + FIXUPS_OFF + 72u, "_answer", 8u);
    parse_ok(image, &info, &plan);
    assert(plan.imports[0].addend == 13);
    assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                               UINT64_C(0x500000000), resolve, &ctx,
                               error, sizeof(error)) == 0);
    assert(rd64(image + DATA_OFF) == ctx.value + 13u);
    fixture(image, 4096u);
    le32(image + 188u, 84u);
    le32(image + FIXUPS_OFF + 20u, 2u);
    le32(image + FIXUPS_OFF + 12u, 72u);
    le32(image + FIXUPS_OFF + 68u, UINT32_MAX - 16u);
    memcpy(image + FIXUPS_OFF + 72u, "_answer", 8u);
    parse_ok(image, &info, &plan);
    assert(plan.imports[0].addend == -17);
    assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                               UINT64_C(0x500000000), resolve, &ctx,
                               error, sizeof(error)) == 0);
    assert(rd64(image + DATA_OFF) == ctx.value - 17u);

    /* Genuine ld64.lld dylibs use format 2 (preferred VM address),
     * unlike the offset format 6 used by the original synthetic fixture. */
    fixture(image, 4096u);
    le16(image + FIXUPS_OFF + 46u, 2u); /* DYLD_CHAINED_PTR_64 */
    le64(image + DATA_OFF + 8u, UINT64_C(0x100000070));
    parse_ok(image, &info, &plan);
    assert(plan.patches[1].rebase_target == 0x70u);
    assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                               UINT64_C(0x500000000), resolve, &ctx,
                               error, sizeof(error)) == 0);
    assert(rd64(image + DATA_OFF + 8u) == UINT64_C(0x500000070));
    fixture(image, 4096u);
    le16(image + FIXUPS_OFF + 46u, 2u);
    le64(image + DATA_OFF + 8u, 0x70u); /* before preferred VM base */
    parse_bad(image);

    fixture(image, 4096u);
    le64(image + DATA_OFF + 8u, 0x300000u);
    parse_bad(image); /* rebase outside every mapped segment */
    fixture(image, 4096u);
    le16(image + FIXUPS_OFF + 60u, 5u); /* five 4K pages in 16K segment */
    le32(image + FIXUPS_OFF + 40u, 32u);
    parse_bad(image);
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF, 1u); /* version */
    parse_bad(image);
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 20u, 99u); /* unsupported import format */
    parse_bad(image);
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 24u, 1u); /* compressed symbols unsupported */
    parse_bad(image);
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 16u, 10000u); /* import count */
    parse_bad(image);
    /* Weak imports may resolve to zero, but strong imports still fail
     * transactionally. Nonzero addends must not turn null into an address. */
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 64u, 0x101u);
    parse_ok(image, &info, &plan);
    assert(plan.imports[0].weak_import);
    ctx.permitted = 0;
    plan.imports[0].addend = 27;
    assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                               UINT64_C(0x500000000), resolve, &ctx,
                               error, sizeof(error)) == 0);
    assert(rd64(image + DATA_OFF) == 0u);
    assert(rd64(image + DATA_OFF + 8u) == UINT64_C(0x500000070));
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 64u, 0x101u);
    parse_ok(image, &info, &plan);
    ctx.permitted = 1;
    assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                               UINT64_C(0x500000000), resolve, &ctx,
                               error, sizeof(error)) == 0);
    assert(rd64(image + DATA_OFF) == ctx.value);
    ctx.permitted = 1;

    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 64u, 0x7fu << 9u | 1u); /* symbol outside pool */
    parse_bad(image);
    fixture(image, 4096u);
    memset(image + FIXUPS_OFF + 68u, 'A', 12u); /* no terminator */
    parse_bad(image);
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 4u, UINT32_MAX); /* starts outside payload */
    parse_bad(image);
    fixture(image, 4096u);
    le32(image + FIXUPS_OFF + 28u, 3u); /* wrong segment count */
    parse_bad(image);
    fixture(image, 4096u);
    le16(image + FIXUPS_OFF + 44u, 8192u); /* reject nonstandard page */
    parse_bad(image);
    fixture(image, 4096u);
    le16(image + FIXUPS_OFF + 46u, 1u); /* arm64e pointer format, may use PAC */
    parse_bad(image);
    fixture(image, 4096u);
    le16(image + FIXUPS_OFF + 62u, 0x8000u); /* multistart unsupported */
    parse_bad(image);
    fixture(image, 4096u);
    le16(image + FIXUPS_OFF + 62u, 4092u); /* pointer crosses page */
    parse_bad(image);
    fixture(image, 4096u);
    le64(image + FIXUPS_OFF + 48u, 0u); /* incorrect segment offset */
    parse_bad(image);
    fixture(image, 4096u);
    le64(image + DATA_OFF, UINT64_C(0x8000000000000001) | (UINT64_C(1) << 32u));
    parse_bad(image); /* bind reserved bits */
    fixture(image, 4096u);
    le64(image + DATA_OFF, PTR_BIND | 1u); /* import index out of range */
    parse_bad(image);
    fixture(image, 4096u);
    le64(image + DATA_OFF + 8u, PTR_REBASE | (UINT64_C(1) << 36u));
    parse_bad(image); /* unsupported tagged rebase */
    fixture(image, 4096u);
    le64(image + DATA_OFF, UINT64_C(0x8000000000000000) |
         (UINT64_C(1023) << 51u)); /* chain escapes 4K page */
    parse_bad(image);

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        long ps = sysconf(_SC_PAGESIZE);
        void *provider_page, *consumer_pages;
        uintptr_t bits;
        int (*entry)(void);
        assert(ps >= 4096 && ps % 4096 == 0);
        provider_page = mmap(NULL, (size_t)ps, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        consumer_pages = mmap(NULL, (size_t)ps * 2u, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(provider_page != MAP_FAILED && consumer_pages != MAP_FAILED);
        /* Separate native provider Mach-O-style text: mov w0,#42; ret */
        le32((uint8_t *)provider_page, 0x52800540u);
        le32((uint8_t *)provider_page + 4u, 0xd65f03c0u);
        __builtin___clear_cache((char *)provider_page, (char *)provider_page + 8u);
        assert(mprotect(provider_page, (size_t)ps, PROT_READ | PROT_EXEC) == 0);
        fixture(image, 4096u);
        parse_ok(image, &info, &plan);
        ctx.value = (uint64_t)(uintptr_t)provider_page;
        assert(ios27_chained_apply(image, IMAGE_SIZE, &info, &plan,
                                   UINT64_C(0x500000000), resolve, &ctx,
                                   error, sizeof(error)) == 0);
        memcpy(consumer_pages, image + CODE_OFF, 12u);
        {
            uint32_t delta = (uint32_t)((size_t)ps / 4096u);
            le32((uint8_t *)consumer_pages, 0x90000010u |
                 ((delta & 3u) << 29u) | ((delta >> 2u) << 5u));
        }
        memcpy((uint8_t *)consumer_pages + (size_t)ps, image + DATA_OFF, 8u);
        __builtin___clear_cache((char *)consumer_pages, (char *)consumer_pages + 12u);
        assert(mprotect(consumer_pages, (size_t)ps, PROT_READ | PROT_EXEC) == 0);
        bits = (uintptr_t)consumer_pages;
        memcpy(&entry, &bits, sizeof(entry));
        assert(entry() == 42);
        assert(munmap(consumer_pages, (size_t)ps * 2u) == 0);
        assert(munmap(provider_page, (size_t)ps) == 0);
    }
#endif

    puts("bounded chained fixups tests passed");
    return 0;
}
