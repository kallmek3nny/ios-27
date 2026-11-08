/*
 * R5a t-3c92c3: bounded, read-only parser for the embedded code signature
 * that LC_CODE_SIGNATURE locates. Field offsets, magics, slot numbers and
 * CodeDirectory version gates follow Apple xnu osfmk/kern/cs_blobs.h at
 * commit f6217f891ac0bb64f3d375211650a4c1ff8ca1ea (cited in
 * ios27/codesign.h). The parser reports facts and slot presence only. It
 * never reads entitlement contents, never hashes or verifies anything and
 * never executes code.
 */
#include "ios27/codesign.h"

#include <stdio.h>
#include <string.h>

/* CS_SuperBlob, CS_BlobIndex and CS_GenericBlob header sizes. */
#define SUPERBLOB_HEADER 12u /* magic, length, count */
#define INDEX_ENTRY 8u       /* type, offset */
#define BLOB_HEADER 8u       /* magic, length */

/* CS_CodeDirectory field offsets. */
#define CD_VERSION 8u
#define CD_FLAGS 12u
#define CD_HASH_OFFSET 16u
#define CD_SPECIAL_SLOTS 24u
#define CD_CODE_SLOTS 28u
#define CD_HASH_SIZE 36u
#define CD_HASH_TYPE 37u
#define CD_PLATFORM 38u
#define CD_PAGE_SIZE 39u
#define CD_EXEC_SEG_FLAGS 80u /* version 0x20400 */

/* Fixed CodeDirectory size at each version (the end_* markers). */
#define CD_END_EARLIEST 44u
#define CD_END_SCATTER 48u     /* + scatterOffset */
#define CD_END_TEAM 52u        /* + teamOffset */
#define CD_END_CODELIMIT64 64u /* + spare3, codeLimit64 */
#define CD_END_EXECSEG 88u     /* + execSegBase, execSegLimit, execSegFlags */
#define CD_END_RUNTIME 96u     /* + runtime, preEncryptOffset */
#define CD_END_LINKAGE 108u    /* + the five linkage fields */

#define NO_ENTRY UINT32_MAX

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | (uint64_t)be32(p + 4u);
}

static int fail(ios27_codesign *out, char *error, size_t cap,
                const char *reason) {
    if (out) memset(out, 0, sizeof(*out));
    if (error && cap)
        (void)snprintf(error, cap, "%s", reason);
    return -1;
}

static uint32_t directory_header(uint32_t version) {
    if (version >= IOS27_CS_SUPPORTSLINKAGE) return CD_END_LINKAGE;
    if (version >= IOS27_CS_SUPPORTSRUNTIME) return CD_END_RUNTIME;
    if (version >= IOS27_CS_SUPPORTSEXECSEG) return CD_END_EXECSEG;
    if (version >= IOS27_CS_SUPPORTSCODELIMIT64) return CD_END_CODELIMIT64;
    if (version >= IOS27_CS_SUPPORTSTEAMID) return CD_END_TEAM;
    if (version >= IOS27_CS_SUPPORTSSCATTER) return CD_END_SCATTER;
    return CD_END_EARLIEST;
}

/* CS_SHA1_LEN, CS_SHA256_LEN and CS_SHA256_TRUNCATED_LEN (cs_blobs.h).
 * cs_blobs.h has no SHA-384 length constant; 48 is the SHA-384 digest
 * size (FIPS 180-4). 0 for a hash type this parser does not know. */
static unsigned hash_length(uint8_t type) {
    switch (type) {
    case IOS27_CS_HASHTYPE_SHA1: return 20u;
    case IOS27_CS_HASHTYPE_SHA256: return 32u;
    case IOS27_CS_HASHTYPE_SHA256_TRUNCATED: return 20u;
    case IOS27_CS_HASHTYPE_SHA384: return 48u;
    default: return 0u;
    }
}

static int is_directory_slot(uint32_t type) {
    return type == IOS27_CSSLOT_CODEDIRECTORY ||
           (type >= IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES &&
            type < IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES +
                       IOS27_CSSLOT_ALTERNATE_CODEDIRECTORY_MAX);
}

/* The magic a known slot must carry, or 0 for a slot that is only bounded.
 * Only the 8-byte blob header is read, never the blob contents. */
static uint32_t slot_magic(uint32_t type) {
    if (is_directory_slot(type)) return IOS27_CSMAGIC_CODEDIRECTORY;
    switch (type) {
    case IOS27_CSSLOT_REQUIREMENTS: return IOS27_CSMAGIC_REQUIREMENTS;
    case IOS27_CSSLOT_ENTITLEMENTS: return IOS27_CSMAGIC_EMBEDDED_ENTITLEMENTS;
    case IOS27_CSSLOT_DER_ENTITLEMENTS:
        return IOS27_CSMAGIC_EMBEDDED_DER_ENTITLEMENTS;
    case IOS27_CSSLOT_SIGNATURESLOT: return IOS27_CSMAGIC_BLOBWRAPPER;
    default: return 0u;
    }
}

/* blob is a CodeDirectory whose length already lies inside the SuperBlob.
 * Returns NULL on success or the rejection reason. */
static const char *parse_directory(const uint8_t *blob, uint32_t length,
                                   uint32_t slot, ios27_code_directory *cd) {
    uint32_t version, header, hash_offset;
    uint64_t special_bytes, code_bytes;
    unsigned expected;

    if (length < CD_END_EARLIEST) return "truncated CodeDirectory";
    version = be32(blob + CD_VERSION);
    if (version < IOS27_CS_VERSION_EARLIEST ||
        version > IOS27_CS_SUPPORTSLINKAGE)
        return "unsupported CodeDirectory version";
    header = directory_header(version);
    if (length < header) return "CodeDirectory is shorter than its version";
    cd->slot = slot;
    cd->version = version;
    cd->flags = be32(blob + CD_FLAGS);
    cd->special_slots = be32(blob + CD_SPECIAL_SLOTS);
    cd->code_slots = be32(blob + CD_CODE_SLOTS);
    cd->hash_size = blob[CD_HASH_SIZE];
    cd->hash_type = blob[CD_HASH_TYPE];
    cd->platform = blob[CD_PLATFORM];
    cd->page_size_log2 = blob[CD_PAGE_SIZE];
    expected = hash_length(cd->hash_type);
    if (expected == 0u) return "unknown CodeDirectory hash type";
    if ((unsigned)cd->hash_size != expected)
        return "CodeDirectory hash size does not match its hash type";
    /* Special slots sit below hashOffset and code slots from it upward;
     * both ranges must stay past the fixed header and inside the blob. */
    hash_offset = be32(blob + CD_HASH_OFFSET);
    special_bytes = (uint64_t)cd->special_slots * cd->hash_size;
    code_bytes = (uint64_t)cd->code_slots * cd->hash_size;
    if ((uint64_t)hash_offset < (uint64_t)header + special_bytes ||
        (uint64_t)hash_offset + code_bytes > length)
        return "CodeDirectory hash slots are out of bounds";
    if (version >= IOS27_CS_SUPPORTSEXECSEG) {
        cd->has_exec_seg = 1u;
        cd->exec_seg_flags = be64(blob + CD_EXEC_SEG_FLAGS);
    }
    return NULL;
}

int ios27_codesign_parse(const uint8_t *image, size_t size,
                         uint32_t dataoff, uint32_t datasize,
                         ios27_codesign *out,
                         char *error, size_t error_capacity) {
    uint32_t types[IOS27_CODESIGN_MAX_BLOBS] = {0};
    uint32_t starts[IOS27_CODESIGN_MAX_BLOBS] = {0};
    uint32_t ends[IOS27_CODESIGN_MAX_BLOBS] = {0};
    const uint8_t *sig;
    uint32_t length, count, primary = NO_ENTRY;
    uint64_t index_end;
    const char *why;

    if (error && error_capacity) error[0] = '\0';
    if (!out) return fail(NULL, error, error_capacity, "missing output");
    memset(out, 0, sizeof(*out));
    if (!image) return fail(out, error, error_capacity, "missing image");
    /* src/macho.c already bounds LC_CODE_SIGNATURE; check it again here. */
    if ((uint64_t)dataoff + datasize > (uint64_t)size)
        return fail(out, error, error_capacity,
                    "signature data lies outside the image");
    if (datasize < SUPERBLOB_HEADER)
        return fail(out, error, error_capacity, "truncated SuperBlob");
    sig = image + dataoff;
    if (be32(sig) != IOS27_CSMAGIC_EMBEDDED_SIGNATURE)
        return fail(out, error, error_capacity, "bad SuperBlob magic");
    length = be32(sig + 4u);
    count = be32(sig + 8u);
    if (length < SUPERBLOB_HEADER || length > datasize)
        return fail(out, error, error_capacity,
                    "SuperBlob length is past the signature data");
    index_end = SUPERBLOB_HEADER + (uint64_t)count * INDEX_ENTRY;
    if (index_end > length)
        return fail(out, error, error_capacity,
                    "SuperBlob count overflows its index");
    if (count > IOS27_CODESIGN_MAX_BLOBS)
        return fail(out, error, error_capacity,
                    "SuperBlob has too many entries");

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry =
            sig + SUPERBLOB_HEADER + (size_t)i * INDEX_ENTRY;
        uint32_t type = be32(entry);
        uint32_t offset = be32(entry + 4u);
        uint32_t magic, blob_length, end, expected;

        if (offset < index_end)
            return fail(out, error, error_capacity,
                        "blob overlaps the SuperBlob header or index");
        if ((uint64_t)offset + BLOB_HEADER > length)
            return fail(out, error, error_capacity,
                        "blob header is past the SuperBlob");
        magic = be32(sig + offset);
        blob_length = be32(sig + offset + 4u);
        if (blob_length < BLOB_HEADER ||
            (uint64_t)offset + blob_length > length)
            return fail(out, error, error_capacity,
                        "blob length is past the SuperBlob");
        end = offset + blob_length;
        for (uint32_t j = 0; j < i; ++j) {
            if (types[j] == type)
                return fail(out, error, error_capacity,
                            "duplicate SuperBlob slot");
            if (offset < ends[j] && starts[j] < end)
                return fail(out, error, error_capacity,
                            "SuperBlob blobs overlap");
        }
        types[i] = type;
        starts[i] = offset;
        ends[i] = end;
        expected = slot_magic(type);
        if (expected != 0u && magic != expected)
            return fail(out, error, error_capacity,
                        "blob magic does not match its slot");
        if (type == IOS27_CSSLOT_CODEDIRECTORY)
            primary = i;
        else if (type == IOS27_CSSLOT_REQUIREMENTS)
            out->has_requirements = 1u;
        else if (type == IOS27_CSSLOT_ENTITLEMENTS)
            out->has_entitlements = 1u;
        else if (type == IOS27_CSSLOT_DER_ENTITLEMENTS)
            out->has_der_entitlements = 1u;
        else if (type == IOS27_CSSLOT_SIGNATURESLOT)
            out->has_signature = 1u;
    }

    if (primary == NO_ENTRY)
        return fail(out, error, error_capacity, "no CodeDirectory in slot 0");
    why = parse_directory(sig + starts[primary],
                          ends[primary] - starts[primary],
                          IOS27_CSSLOT_CODEDIRECTORY, &out->directories[0]);
    if (why) return fail(out, error, error_capacity, why);
    out->directory_count = 1u;
    for (uint32_t slot = IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES;
         slot < IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES +
                    IOS27_CSSLOT_ALTERNATE_CODEDIRECTORY_MAX;
         ++slot) {
        for (uint32_t i = 0; i < count; ++i) {
            if (types[i] != slot) continue;
            why = parse_directory(sig + starts[i], ends[i] - starts[i], slot,
                                  &out->directories[out->directory_count]);
            if (why) return fail(out, error, error_capacity, why);
            ++out->directory_count;
        }
    }
    out->length = length;
    out->count = count;
    return 0;
}
