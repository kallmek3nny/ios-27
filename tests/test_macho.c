#include "ios27/macho.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

/* Minimal synthetic Mach-O containing ARM64 "mov w0,#42; ret". */
static void fixture(uint8_t b[264]) {
    memset(b, 0, 264);
    put32(b, 0xfeedfacf);
    put32(b + 4, 0x0100000c);
    put32(b + 12, 2);
    put32(b + 16, 2);
    put32(b + 20, 96);
    put32(b + 32, 0x19);
    put32(b + 36, 72);
    memcpy(b + 40, "__TEXT", 6);
    put64(b + 56, 0x100000000ull);
    put64(b + 64, 4096);
    put64(b + 72, 0);
    put64(b + 80, 264);
    put32(b + 88, 5);
    put32(b + 92, 5);
    put32(b + 104, 0x80000028);
    put32(b + 108, 24);
    put64(b + 112, 256);
    put32(b + 256, 0x52800540);
    put32(b + 260, 0xd65f03c0);
}

int main(void) {
    uint8_t b[264], scratch[264];
    ios27_macho_info image;
    char err[128];
    int rc;
    fixture(b);
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(image.segment_count == 1u);
    assert(image.has_main && image.entryoff == 256u);
    assert(image.filetype == IOS27_MH_EXECUTE);
    assert(ios27_macho_parse(b, 30u, &image, err, sizeof(err)) != 0);
    assert(ios27_macho_parse(b, 263u, &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 36, 0xffffffffu);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 16, 3);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put64(scratch + 80, UINT64_MAX);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 4, 0x01000007);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 108, 16);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);

    fixture(b);
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    if (sizeof(void *) == 8u) {
        /* On an ARM64 Linux/Android host, this entry must return 42. */
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
        rc = 0;
        assert(ios27_run_freestanding(b, sizeof(b), &image, &rc,
                                      err, sizeof(err)) == 0);
        assert(rc == 42);
#endif
    }
    rc = 0;
    image.needs_dyld = 1u;
    assert(ios27_run_freestanding(b, sizeof(b), &image, &rc,
                                  err, sizeof(err)) != 0);
    puts("macho tests passed");
    return 0;
}
