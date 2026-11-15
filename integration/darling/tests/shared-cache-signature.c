/* Original synthetic dyld shared-cache signature contracts. Format references:
 * Apple OSS mach-o/dyld_cache_format.h and kern/cs_blobs.h. No Apple binary is
 * included. Link with the patched Darling dc_codesign.c and host libcrypto. */
#include <darlingserver/dc_codesign.h>
#include <kern/cs_blobs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CODE_SIZE = 4096, CD_SIZE = 96, SIGNATURE_SIZE = 20 + CD_SIZE,
       FILE_SIZE = CODE_SIZE + SIGNATURE_SIZE };
static unsigned checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { fprintf(stderr, "line %d: %s\n", __LINE__, #condition); exit(1); } \
} while (0)

static void le32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static void le64(uint8_t *p, uint64_t value) {
    le32(p, (uint32_t)value); le32(p + 4, (uint32_t)(value >> 32));
}
static void be32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> ((3 - i) * 8));
}

static void fixture(uint8_t *data) {
    memset(data, 0, FILE_SIZE);
    memcpy(data, "dyld_v1   arm64", 16);
    le32(data + 16, 56); le32(data + 20, 1);
    le64(data + 40, CODE_SIZE); le64(data + 48, SIGNATURE_SIZE);
    le64(data + 56, UINT64_C(0x180000000));
    le64(data + 64, CODE_SIZE);
    le32(data + 80, 5); le32(data + 84, 5);
    uint8_t *signature = data + CODE_SIZE, *cd = signature + 20;
    be32(signature, CSMAGIC_EMBEDDED_SIGNATURE);
    be32(signature + 4, SIGNATURE_SIZE); be32(signature + 8, 1);
    be32(signature + 12, CSSLOT_CODEDIRECTORY); be32(signature + 16, 20);
    be32(cd, CSMAGIC_CODEDIRECTORY); be32(cd + 4, CD_SIZE);
    be32(cd + 8, 0x20001); be32(cd + 12, CS_ADHOC);
    be32(cd + 16, 64); be32(cd + 20, 44);
    be32(cd + 28, 1); be32(cd + 32, CODE_SIZE);
    cd[36] = 32; cd[37] = CS_HASHTYPE_SHA256; cd[39] = 12;
    memcpy(cd + 44, "ios27-cache-test", 17);
    size_t length = 0;
    CHECK(dc_cs_digest(CS_HASHTYPE_SHA256, data, CODE_SIZE, cd + 64, &length) == 0);
    CHECK(length == 32);
}

static int locate(const uint8_t *data, size_t header, uint64_t size, struct dc_cs_image *image) {
    char error[256] = "";
    int result = dc_cs_locate_shared_cache(data, header, size, image, error, sizeof(error));
    CHECK(result == 0 || error[0]);
    return result;
}

static int parse(const uint8_t *data, struct dc_cs_image *image) {
    if (locate(data, 56, FILE_SIZE, image)) return -1;
    char error[256] = "";
    return dc_cs_parse_signature(data + CODE_SIZE, SIGNATURE_SIZE, image, error, sizeof(error));
}

int main(int argc, char **argv) {
    uint8_t data[FILE_SIZE], changed[FILE_SIZE];
    struct dc_cs_image image;
    char error[256] = "";
    uint32_t bad_page = UINT32_MAX;
    fixture(data);
    CHECK(parse(data, &image) == 0);
    CHECK(image.is_64 && image.cputype == 0x0100000c && image.cpusubtype == 0);
    CHECK(image.size == FILE_SIZE && image.load_commands_end == 88);
    CHECK(image.signature_offset == CODE_SIZE && image.signature_size == SIGNATURE_SIZE);
    CHECK(image.code_limit == CODE_SIZE && image.code_slot_count == 1 && image.page_size_log2 == 12);
    CHECK(dc_cs_verify_pages(&image, data, FILE_SIZE, &bad_page, error, sizeof(error)) == 0);
    memcpy(changed, data, FILE_SIZE); changed[200] ^= 1;
    CHECK(dc_cs_verify_pages(&image, changed, FILE_SIZE, &bad_page, error, sizeof(error)) < 0 && bad_page == 0);
    /* The ordinary executable parser must continue rejecting cache containers. */
    uint64_t executable_size = 0;
    CHECK(dc_cs_image_at(data, 56, FILE_SIZE, 0, &executable_size, error, sizeof(error)) < 0);
    CHECK(dc_cs_locate(data, FILE_SIZE, FILE_SIZE, &image, error, sizeof(error)) < 0);

    for (size_t length = 0; length < 56; ++length) {
        uint8_t *short_header = malloc(length ? length : 1);
        CHECK(short_header != NULL);
        if (length) memcpy(short_header, data, length);
        CHECK(locate(short_header, length, FILE_SIZE, &image) < 0);
        free(short_header);
    }
    CHECK(locate(NULL, 56, FILE_SIZE, &image) < 0);
    CHECK(dc_cs_locate_shared_cache(data, 56, FILE_SIZE, NULL, error, sizeof(error)) < 0);
    CHECK(locate(data, 56, 55, &image) < 0);

    for (unsigned i = 0; i < 16; ++i) {
        memcpy(changed, data, FILE_SIZE); changed[i] ^= 0x80;
        CHECK(locate(changed, 56, FILE_SIZE, &image) < 0);
    }
    static const struct { unsigned offset; uint32_t value; } bad32[] = {
        {16, 0}, {16, 55}, {16, FILE_SIZE}, {16, UINT32_MAX},
        {20, 0}, {20, UINT32_MAX},
    };
    for (size_t i = 0; i < sizeof(bad32) / sizeof(bad32[0]); ++i) {
        memcpy(changed, data, FILE_SIZE); le32(changed + bad32[i].offset, bad32[i].value);
        CHECK(locate(changed, 56, FILE_SIZE, &image) < 0);
    }
    static const struct { unsigned offset; uint64_t value; } bad64[] = {
        {40, 0}, {40, 87}, {40, FILE_SIZE}, {40, UINT64_MAX},
        {48, 0}, {48, 11}, {48, SIGNATURE_SIZE - 1}, {48, SIGNATURE_SIZE + 1},
        {48, UINT64_C(0x100000000)}, {48, UINT64_MAX},
    };
    for (size_t i = 0; i < sizeof(bad64) / sizeof(bad64[0]); ++i) {
        memcpy(changed, data, FILE_SIZE); le64(changed + bad64[i].offset, bad64[i].value);
        CHECK(locate(changed, 56, FILE_SIZE, &image) < 0);
    }
    memcpy(changed, data, FILE_SIZE); changed[CODE_SIZE] ^= 1;
    CHECK(parse(changed, &image) < 0);
    memcpy(changed, data, FILE_SIZE); be32(changed + CODE_SIZE + 20 + 32, 87);
    CHECK(parse(changed, &image) < 0); /* signed range excludes mapping-table end */
    memcpy(changed, data, FILE_SIZE); be32(changed + CODE_SIZE + 20 + 32, CODE_SIZE + 1);
    CHECK(parse(changed, &image) < 0); /* signed range enters signature */
    memcpy(changed, data, FILE_SIZE); be32(changed + CODE_SIZE + 20 + 28, UINT32_MAX);
    CHECK(parse(changed, &image) < 0); /* impossible hash-table length */

    /* Unaligned input and a >4 GiB signature offset must not truncate. */
    uint8_t unaligned[57]; memcpy(unaligned + 1, data, 56);
    le64(unaligned + 1 + 40, UINT64_C(0x100001000));
    CHECK(locate(unaligned + 1, 56, UINT64_C(0x100001000) + SIGNATURE_SIZE, &image) == 0);
    CHECK(image.signature_offset == UINT64_C(0x100001000));

    if (argc == 2) {
        FILE *file = fopen(argv[1], "wb"); CHECK(file != NULL);
        CHECK(fwrite(data, 1, FILE_SIZE, file) == FILE_SIZE);
        CHECK(fclose(file) == 0);
    } else CHECK(argc == 1);
    printf("SHARED_CACHE_SIGNATURE_OK checks=%u\n", checks);
    return 0;
}
