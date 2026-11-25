/*
 * R5b t-609093: read-only code-signature census. For each Mach-O named on
 * the command line, find LC_CODE_SIGNATURE, run the R5a parser
 * (ios27_codesign_parse) and print presence, SuperBlob slot types and
 * CodeDirectory facts. Entitlement contents are never read, no hash is
 * verified, nothing is executed and no file is written.
 */
#include "ios27/codesign.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Mach-O constants from Apple's <mach-o/loader.h>. */
#define MH_MAGIC_64 0xfeedfacfu
#define LC_CODE_SIGNATURE 0x1du
#define HEADER_64 32u

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int reject(const char *reason, uint64_t offset) {
    printf("  reject=%s offset=0x%" PRIx64 "\n", reason, offset);
    return -1;
}

static void report(const uint8_t *image, uint32_t dataoff,
                   const ios27_codesign *cs) {
    printf("  superblob_length=%" PRIu32 " index_count=%" PRIu32
           "\n  slot_types=", cs->length, cs->count);
    /* The parser bounded 12 + count * 8 inside the SuperBlob. Only the type
     * field of each index entry is read here. */
    for (uint32_t i = 0u; i < cs->count; ++i)
        printf("%s0x%" PRIx32, i ? "," : "",
               be32(image + dataoff + 12u + (size_t)i * 8u));
    printf("\n  slot2=%u slot5=%u slot7=%u slot0x10000=%u directories=%zu\n",
           (unsigned)cs->has_requirements, (unsigned)cs->has_entitlements,
           (unsigned)cs->has_der_entitlements, (unsigned)cs->has_signature,
           cs->directory_count);
    for (size_t i = 0u; i < cs->directory_count; ++i) {
        const ios27_code_directory *cd = &cs->directories[i];
        printf("  cd slot=0x%" PRIx32 " version=0x%" PRIx32
               " flags=0x%" PRIx32 " hash_type=%u hash_size=%u"
               " page_size_log2=%u platform=%u special_slots=%" PRIu32
               " code_slots=%" PRIu32,
               cd->slot, cd->version, cd->flags, (unsigned)cd->hash_type,
               (unsigned)cd->hash_size, (unsigned)cd->page_size_log2,
               (unsigned)cd->platform, cd->special_slots, cd->code_slots);
        if (cd->has_exec_seg)
            printf(" exec_seg_flags=0x%" PRIx64 "\n", cd->exec_seg_flags);
        else
            printf(" exec_seg_flags=none\n");
    }
}

/* One thin 64-bit image. base is its file offset, printed in rejects. */
static int census_image(const uint8_t *image, size_t size, uint64_t base) {
    uint32_t ncmds, dataoff = 0u, datasize = 0u, found = 0u;
    size_t cursor = HEADER_64, end;
    ios27_codesign cs;
    char error[128];

    if (size < HEADER_64 || le32(image) != MH_MAGIC_64)
        return reject("not a 64-bit Mach-O", base);
    ncmds = le32(image + 16u);
    if ((uint64_t)HEADER_64 + le32(image + 20u) > size)
        return reject("load commands past end of slice", base + 20u);
    end = HEADER_64 + (size_t)le32(image + 20u);
    for (uint32_t i = 0u; i < ncmds; ++i) {
        uint32_t cmd, cmdsize;

        if (end - cursor < 8u)
            return reject("truncated load command", base + cursor);
        cmd = le32(image + cursor);
        cmdsize = le32(image + cursor + 4u);
        if (cmdsize < 8u || cmdsize > end - cursor)
            return reject("invalid load command size", base + cursor);
        if (cmd == LC_CODE_SIGNATURE) {
            if (cmdsize != 16u || found)
                return reject("invalid or duplicate LC_CODE_SIGNATURE",
                              base + cursor);
            dataoff = le32(image + cursor + 8u);
            datasize = le32(image + cursor + 12u);
            found = 1u;
        }
        cursor += cmdsize;
    }
    printf("  ncmds=%" PRIu32 " lc_code_signature=%s\n", ncmds,
           found ? "present" : "absent");
    if (!found) return 0;
    printf("  dataoff=0x%" PRIx32 " datasize=%" PRIu32 "\n", dataoff,
           datasize);
    if (ios27_codesign_parse(image, size, dataoff, datasize, &cs, error,
                             sizeof(error)) != 0)
        return reject(error, base + dataoff);
    report(image, dataoff, &cs);
    return 0;
}

int main(int argc, char **argv) {
    int status = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s MACHO...\n", argv[0]);
        return 2;
    }
    for (int i = 1; i < argc; ++i) {
        FILE *file = fopen(argv[i], "rb");
        uint8_t *data = NULL;
        long length = -1;

        if (file && fseek(file, 0, SEEK_END) == 0) length = ftell(file);
        if (length > 0 && fseek(file, 0, SEEK_SET) == 0)
            data = malloc((size_t)length);
        printf("file=%s size=%ld\n", argv[i], length);
        if (!data || fread(data, 1u, (size_t)length, file) != (size_t)length) {
            fprintf(stderr, "%s: cannot read\n", argv[i]);
            status = 1;
        } else if (census_image(data, (size_t)length, 0u) != 0) {
            status = 1;
        }
        free(data);
        if (file) fclose(file);
    }
    return status;
}
