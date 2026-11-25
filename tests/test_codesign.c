/*
 * R5a t-3c92c3: tests for the read-only embedded code signature parser in
 * src/codesign.c. Every signature here is synthetic and built from the
 * layouts in xnu osfmk/kern/cs_blobs.h (cited in ios27/codesign.h), with
 * no Apple bytes. Every reject must return -1 with a NUL-terminated error
 * and a zeroed output. Each parse runs over an exactly sized heap copy, so
 * ASan reports any read past the input, and the copy is compared afterwards
 * to show that the parser never writes to it.
 */
#include "ios27/codesign.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG_CAP 4096u
#define BLOB_CAP 1024u
#define MAX_TEST_BLOBS 40u
#define SWEEP_SEED 0x2c3f5a17u
#define SWEEP_ROUNDS 20000u

/* CS_CodeDirectory field offsets from cs_blobs.h. */
#define CD_LENGTH 4u
#define CD_VERSION 8u
#define CD_FLAGS 12u
#define CD_HASH_OFFSET 16u
#define CD_SPECIAL_SLOTS 24u
#define CD_CODE_SLOTS 28u
#define CD_CODE_LIMIT 32u
#define CD_HASH_SIZE 36u
#define CD_HASH_TYPE 37u
#define CD_PLATFORM 38u
#define CD_PAGE_SIZE 39u
#define CD_EXEC_SEG_FLAGS 80u

/* Index entries of the signature that build_base() makes. */
#define E_CD 0u
#define E_REQ 1u
#define E_ENT 2u
#define E_DER 3u
#define E_ALT 4u
#define E_WRAP 5u
#define BASE_COUNT 6u

static int failures;

static void check_at(int ok, int line, const char *text) {
    if (ok) return;
    fprintf(stderr, "test_codesign.c:%d: check failed: %s\n", line, text);
    ++failures;
}

#define CHECK(cond) check_at((cond) ? 1 : 0, __LINE__, #cond)

static void die(const char *why) {
    fprintf(stderr, "test_codesign.c: %s\n", why);
    exit(2);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)(v >> 32));
    put32(p + 4, (uint32_t)v);
}

/* A SuperBlob under construction: the 12-byte header, count index
 * entries, then the blobs back to back in index order. */
typedef struct {
    uint8_t data[SIG_CAP];
    uint32_t size;
    uint32_t count;
    uint32_t offset[MAX_TEST_BLOBS];
} sigbuf;

static void sig_begin(sigbuf *s, uint32_t count) {
    if (count > MAX_TEST_BLOBS) die("too many test blobs");
    memset(s, 0, sizeof(*s));
    s->count = count;
    s->size = 12u + count * 8u;
}

static void sig_add(sigbuf *s, uint32_t index, uint32_t type,
                    const uint8_t *blob, uint32_t length) {
    uint8_t *entry;

    if (index >= s->count || length > SIG_CAP - s->size)
        die("test signature does not fit");
    entry = s->data + 12u + index * 8u;
    put32(entry, type);
    put32(entry + 4, s->size);
    s->offset[index] = s->size;
    memcpy(s->data + s->size, blob, length);
    s->size += length;
}

static void sig_end(sigbuf *s) {
    put32(s->data, IOS27_CSMAGIC_EMBEDDED_SIGNATURE);
    put32(s->data + 4, s->size);
    put32(s->data + 8, s->count);
}

/* CS_BlobIndex entry i: type at 12 + 8i, offset at 16 + 8i. */
static uint32_t entry_type_at(uint32_t i) { return 12u + 8u * i; }
static uint32_t entry_offset_at(uint32_t i) { return 16u + 8u * i; }

/* A blob the parser may only bound: magic, length, synthetic filler. */
static uint32_t make_generic(uint8_t *blob, uint32_t magic,
                             uint32_t length) {
    if (length < 8u || length > BLOB_CAP) die("bad generic blob length");
    put32(blob, magic);
    put32(blob + 4, length);
    for (uint32_t i = 8u; i < length; ++i)
        blob[i] = (uint8_t)(0x41u + i % 26u);
    return length;
}

typedef struct {
    uint32_t version;
    uint32_t flags;
    uint8_t hash_type;
    uint8_t hash_size;
    uint8_t platform;
    uint8_t page_size_log2;
    uint32_t special_slots;
    uint32_t code_slots;
    uint64_t exec_seg_flags;
} cd_spec;

static const cd_spec primary_spec = {
    .version = IOS27_CS_SUPPORTSEXECSEG,
    .flags = 0x00010002u,
    .hash_type = IOS27_CS_HASHTYPE_SHA256,
    .hash_size = 32u,
    .platform = 0u,
    .page_size_log2 = 12u,
    .special_slots = 7u,
    .code_slots = 3u,
    .exec_seg_flags = 0x1u,
};

static const cd_spec alternate_spec = {
    .version = IOS27_CS_SUPPORTSSCATTER,
    .flags = 0x2u,
    .hash_type = IOS27_CS_HASHTYPE_SHA1,
    .hash_size = 20u,
    .platform = 0u,
    .page_size_log2 = 14u,
    .special_slots = 2u,
    .code_slots = 3u,
    .exec_seg_flags = 0x1u, /* not encoded below version 0x20400 */
};

static const cd_spec small_spec = {
    .version = IOS27_CS_VERSION_EARLIEST,
    .flags = 0u,
    .hash_type = IOS27_CS_HASHTYPE_SHA256,
    .hash_size = 32u,
    .platform = 0u,
    .page_size_log2 = 12u,
    .special_slots = 0u,
    .code_slots = 1u,
    .exec_seg_flags = 0u,
};

/* Fixed CS_CodeDirectory size at each cs_blobs.h version gate: through
 * spare2, scatterOffset, teamOffset, codeLimit64, execSegFlags, the
 * runtime fields and the linkage fields. */
static uint32_t cd_header(uint32_t version) {
    if (version >= IOS27_CS_SUPPORTSLINKAGE) return 108u;
    if (version >= IOS27_CS_SUPPORTSRUNTIME) return 96u;
    if (version >= IOS27_CS_SUPPORTSEXECSEG) return 88u;
    if (version >= IOS27_CS_SUPPORTSCODELIMIT64) return 64u;
    if (version >= IOS27_CS_SUPPORTSTEAMID) return 52u;
    if (version >= IOS27_CS_SUPPORTSSCATTER) return 48u;
    return 44u;
}

/* The special slots start right after the fixed header, the code slots
 * right after them, and the blob ends at the last code slot. */
static uint32_t make_cd(uint8_t *blob, const cd_spec *spec) {
    uint32_t header = cd_header(spec->version);
    uint64_t hash_offset =
        header + (uint64_t)spec->special_slots * spec->hash_size;
    uint64_t length =
        hash_offset + (uint64_t)spec->code_slots * spec->hash_size;

    if (length > BLOB_CAP) die("test CodeDirectory does not fit");
    memset(blob, 0, (size_t)length);
    put32(blob, IOS27_CSMAGIC_CODEDIRECTORY);
    put32(blob + CD_LENGTH, (uint32_t)length);
    put32(blob + CD_VERSION, spec->version);
    put32(blob + CD_FLAGS, spec->flags);
    put32(blob + CD_HASH_OFFSET, (uint32_t)hash_offset);
    put32(blob + CD_SPECIAL_SLOTS, spec->special_slots);
    put32(blob + CD_CODE_SLOTS, spec->code_slots);
    put32(blob + CD_CODE_LIMIT, 0x4000u);
    blob[CD_HASH_SIZE] = spec->hash_size;
    blob[CD_HASH_TYPE] = spec->hash_type;
    blob[CD_PLATFORM] = spec->platform;
    blob[CD_PAGE_SIZE] = spec->page_size_log2;
    if (spec->version >= IOS27_CS_SUPPORTSEXECSEG)
        put64(blob + CD_EXEC_SEG_FLAGS, spec->exec_seg_flags);
    for (uint64_t i = header; i < length; ++i)
        blob[i] = (uint8_t)(i * 7u + 3u);
    return (uint32_t)length;
}

static void build_base(sigbuf *s) {
    uint8_t blob[BLOB_CAP];
    uint32_t len;

    sig_begin(s, BASE_COUNT);
    len = make_cd(blob, &primary_spec);
    sig_add(s, E_CD, IOS27_CSSLOT_CODEDIRECTORY, blob, len);
    len = make_generic(blob, IOS27_CSMAGIC_REQUIREMENTS, 12u);
    sig_add(s, E_REQ, IOS27_CSSLOT_REQUIREMENTS, blob, len);
    len = make_generic(blob, IOS27_CSMAGIC_EMBEDDED_ENTITLEMENTS, 24u);
    sig_add(s, E_ENT, IOS27_CSSLOT_ENTITLEMENTS, blob, len);
    len = make_generic(blob, IOS27_CSMAGIC_EMBEDDED_DER_ENTITLEMENTS, 16u);
    sig_add(s, E_DER, IOS27_CSSLOT_DER_ENTITLEMENTS, blob, len);
    len = make_cd(blob, &alternate_spec);
    sig_add(s, E_ALT, IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES, blob, len);
    len = make_generic(blob, IOS27_CSMAGIC_BLOBWRAPPER, 8u);
    sig_add(s, E_WRAP, IOS27_CSSLOT_SIGNATURESLOT, blob, len);
    sig_end(s);
}

static void build_single(sigbuf *s, const cd_spec *spec) {
    uint8_t blob[BLOB_CAP];
    uint32_t len = make_cd(blob, spec);

    sig_begin(s, 1u);
    sig_add(s, 0u, IOS27_CSSLOT_CODEDIRECTORY, blob, len);
    sig_end(s);
}

/* Slot 0 plus count - 1 bounded-only filler blobs. */
static void build_many(sigbuf *s, uint32_t count) {
    uint8_t blob[BLOB_CAP];
    uint32_t len;

    sig_begin(s, count);
    len = make_cd(blob, &small_spec);
    sig_add(s, 0u, IOS27_CSSLOT_CODEDIRECTORY, blob, len);
    for (uint32_t i = 1u; i < count; ++i) {
        len = make_generic(blob, 0x0badf00du, 8u);
        sig_add(s, i, 0x100u + i, blob, len);
    }
    sig_end(s);
}

static unsigned hash_len(uint8_t type) {
    switch (type) {
    case IOS27_CS_HASHTYPE_SHA1: return 20u;
    case IOS27_CS_HASHTYPE_SHA256: return 32u;
    case IOS27_CS_HASHTYPE_SHA256_TRUNCATED: return 20u;
    case IOS27_CS_HASHTYPE_SHA384: return 48u;
    default: return 0u;
    }
}

static int is_zero(const ios27_codesign *out) {
    static const ios27_codesign zero;
    return memcmp(out, &zero, sizeof(zero)) == 0;
}

/* Parse an exactly sized heap copy of image[0, size), then check that the
 * parser left the copy unchanged. */
static int parse_copy(const uint8_t *image, size_t size, uint32_t dataoff,
                      uint32_t datasize, ios27_codesign *out, char *error,
                      size_t error_capacity) {
    uint8_t *copy = malloc(size ? size : 1u);
    int rc;

    if (!copy) die("out of memory");
    if (size) memcpy(copy, image, size);
    rc = ios27_codesign_parse(copy, size, dataoff, datasize, out, error,
                              error_capacity);
    if (size && memcmp(copy, image, size) != 0) {
        fprintf(stderr, "test_codesign.c: the parser wrote to its input\n");
        ++failures;
    }
    free(copy);
    return rc;
}

static void expect_reject_at(int line, const uint8_t *image, size_t size,
                             uint32_t dataoff, uint32_t datasize,
                             const char *want) {
    ios27_codesign out;
    char error[128];
    int rc, terminated;

    memset(&out, 0xa5, sizeof(out));
    memset(error, 'X', sizeof(error));
    rc = parse_copy(image, size, dataoff, datasize, &out, error,
                    sizeof(error));
    terminated = memchr(error, 0, sizeof(error)) != NULL;
    if (rc != -1 || !terminated || error[0] == 0 || !is_zero(&out) ||
        strstr(error, want) == NULL) {
        fprintf(stderr,
                "test_codesign.c:%d: want reject '%s', got rc=%d "
                "error='%.*s'%s\n",
                line, want, rc, (int)sizeof(error), error,
                is_zero(&out) ? "" : " and a nonzero output");
        ++failures;
    }
}

#define REJECT(img, n, off, len, want) expect_reject_at(__LINE__, (img), (n), (off), (len), (want))
#define REJECT_SIG(s, want) expect_reject_at(__LINE__, (s)->data, (s)->size, 0u, (s)->size, (want))

static int accept_at(int line, const sigbuf *s, ios27_codesign *out) {
    char error[128];
    int rc;

    memset(out, 0xa5, sizeof(*out));
    memset(error, 'X', sizeof(error));
    rc = parse_copy(s->data, s->size, 0u, s->size, out, error,
                    sizeof(error));
    if (rc != 0 || error[0] != 0) {
        fprintf(stderr,
                "test_codesign.c:%d: want accept, got rc=%d error='%.*s'\n",
                line, rc, (int)sizeof(error), error);
        ++failures;
        return 0;
    }
    return 1;
}

#define ACCEPT(s, out) accept_at(__LINE__, (s), (out))

static void test_base(void) {
    sigbuf s;
    ios27_codesign out;
    const ios27_code_directory *cd;

    build_base(&s);
    if (!ACCEPT(&s, &out)) return;
    CHECK(out.length == s.size);
    CHECK(out.count == BASE_COUNT);
    CHECK(out.directory_count == 2u);
    CHECK(out.has_requirements == 1u);
    CHECK(out.has_entitlements == 1u);
    CHECK(out.has_der_entitlements == 1u);
    CHECK(out.has_signature == 1u);

    cd = &out.directories[0];
    CHECK(cd->slot == IOS27_CSSLOT_CODEDIRECTORY);
    CHECK(cd->version == primary_spec.version);
    CHECK(cd->flags == primary_spec.flags);
    CHECK(cd->special_slots == primary_spec.special_slots);
    CHECK(cd->code_slots == primary_spec.code_slots);
    CHECK(cd->hash_type == primary_spec.hash_type);
    CHECK(cd->hash_size == primary_spec.hash_size);
    CHECK(cd->page_size_log2 == primary_spec.page_size_log2);
    CHECK(cd->platform == primary_spec.platform);
    CHECK(cd->has_exec_seg == 1u);
    CHECK(cd->exec_seg_flags == primary_spec.exec_seg_flags);

    cd = &out.directories[1];
    CHECK(cd->slot == IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES);
    CHECK(cd->version == alternate_spec.version);
    CHECK(cd->flags == alternate_spec.flags);
    CHECK(cd->special_slots == alternate_spec.special_slots);
    CHECK(cd->code_slots == alternate_spec.code_slots);
    CHECK(cd->hash_type == alternate_spec.hash_type);
    CHECK(cd->hash_size == alternate_spec.hash_size);
    CHECK(cd->page_size_log2 == alternate_spec.page_size_log2);
    /* 0x20100 has no exec-segment fields; offset 80 holds hash bytes. */
    CHECK(cd->has_exec_seg == 0u);
    CHECK(cd->exec_seg_flags == 0u);
}

static void test_presence_only(void) {
    sigbuf s;
    ios27_codesign out;

    build_single(&s, &primary_spec);
    if (!ACCEPT(&s, &out)) return;
    CHECK(out.count == 1u);
    CHECK(out.directory_count == 1u);
    CHECK(out.has_requirements == 0u);
    CHECK(out.has_entitlements == 0u);
    CHECK(out.has_der_entitlements == 0u);
    CHECK(out.has_signature == 0u);
}

static void test_placement(void) {
    static uint8_t image[32u + SIG_CAP + 16u];
    sigbuf s;
    ios27_codesign out;
    char error[64];
    size_t size;
    int rc;

    build_base(&s);
    memset(image, 0xcc, sizeof(image));
    memcpy(image + 32, s.data, s.size);
    size = 32u + s.size + 16u;

    /* At an offset, with padding after the SuperBlob as linkers emit. */
    memset(error, 'X', sizeof(error));
    rc = parse_copy(image, size, 32u, s.size + 16u, &out, error,
                    sizeof(error));
    CHECK(rc == 0);
    CHECK(error[0] == 0);
    CHECK(out.length == s.size);
    CHECK(out.directory_count == 2u);

    REJECT(image, size, 32u, s.size + 17u, "outside the image");
    REJECT(image, size, (uint32_t)size, 12u, "outside the image");
    REJECT(image, size, (uint32_t)size + 1u, 0u, "outside the image");
    REJECT(image, size, UINT32_MAX, 1u, "outside the image");
    REJECT(image, size, 1u, UINT32_MAX, "outside the image");
    REJECT(image, size, UINT32_MAX, UINT32_MAX, "outside the image");
    REJECT(image, size, 32u, 0u, "truncated SuperBlob");
    REJECT(image, size, 32u, 11u, "truncated SuperBlob");
    REJECT(image, size, 32u, s.size - 1u, "past the signature data");
    /* The SuperBlob magic must sit at dataoff itself. */
    REJECT(image, size, 28u, s.size + 4u, "bad SuperBlob magic");
    REJECT(image, size, 33u, s.size, "bad SuperBlob magic");
}

static void test_arguments_and_error_capacity(void) {
    sigbuf s;
    ios27_codesign out;
    char error[16];
    int rc;

    build_base(&s);
    memset(&out, 0xa5, sizeof(out));
    memset(error, 'X', sizeof(error));
    rc = ios27_codesign_parse(NULL, s.size, 0u, s.size, &out, error,
                              sizeof(error));
    CHECK(rc == -1);
    CHECK(is_zero(&out));
    CHECK(memchr(error, 0, sizeof(error)) != NULL &&
          strcmp(error, "missing image") == 0);
    memset(error, 'X', sizeof(error));
    rc = ios27_codesign_parse(s.data, s.size, 0u, s.size, NULL, error,
                              sizeof(error));
    CHECK(rc == -1);
    CHECK(memchr(error, 0, sizeof(error)) != NULL &&
          strcmp(error, "missing output") == 0);

    /* The error for a bad magic is truncated to fit and NUL-terminated. */
    put32(s.data, 0xdeadbeefu);
    memset(error, 'X', sizeof(error));
    rc = parse_copy(s.data, s.size, 0u, s.size, &out, error, 1u);
    CHECK(rc == -1);
    CHECK(error[0] == 0);
    CHECK(error[1] == 'X');
    memset(error, 'X', sizeof(error));
    rc = parse_copy(s.data, s.size, 0u, s.size, &out, error, 6u);
    CHECK(rc == -1);
    CHECK(error[5] == 0);
    CHECK(strcmp(error, "bad S") == 0);
    CHECK(error[6] == 'X');
    memset(error, 'X', sizeof(error));
    rc = parse_copy(s.data, s.size, 0u, s.size, &out, error, 0u);
    CHECK(rc == -1);
    CHECK(error[0] == 'X');
    memset(&out, 0xa5, sizeof(out));
    rc = parse_copy(s.data, s.size, 0u, s.size, &out, NULL, 0u);
    CHECK(rc == -1);
    CHECK(is_zero(&out));

    /* A success clears a stale error. */
    build_base(&s);
    memset(error, 'X', sizeof(error));
    rc = parse_copy(s.data, s.size, 0u, s.size, &out, error, sizeof(error));
    CHECK(rc == 0);
    CHECK(error[0] == 0);
}

static void test_superblob_rejects(void) {
    sigbuf s;
    uint32_t whole_index;

    build_base(&s);
    put32(s.data, IOS27_CSMAGIC_CODEDIRECTORY);
    REJECT_SIG(&s, "bad SuperBlob magic");
    build_base(&s);
    put32(s.data, 0xc00cdefau); /* the magic stored little-endian */
    REJECT_SIG(&s, "bad SuperBlob magic");

    build_base(&s);
    put32(s.data + 4, 0u);
    REJECT_SIG(&s, "SuperBlob length is past the signature data");
    build_base(&s);
    put32(s.data + 4, 11u);
    REJECT_SIG(&s, "SuperBlob length is past the signature data");
    build_base(&s);
    put32(s.data + 4, s.size + 1u);
    REJECT_SIG(&s, "SuperBlob length is past the signature data");
    build_base(&s);
    put32(s.data + 4, UINT32_MAX);
    REJECT_SIG(&s, "SuperBlob length is past the signature data");

    /* An index that fits inside the SuperBlob passes the bound and then
     * hits the entry limit; one entry more overflows it. */
    build_base(&s);
    whole_index = (s.size - 12u) / 8u;
    put32(s.data + 8, whole_index);
    REJECT_SIG(&s, "SuperBlob has too many entries");
    build_base(&s);
    put32(s.data + 8, whole_index + 1u);
    REJECT_SIG(&s, "SuperBlob count overflows its index");
    build_base(&s);
    put32(s.data + 8, 0x20000000u); /* 8 * count wraps to 0 in 32 bits */
    REJECT_SIG(&s, "SuperBlob count overflows its index");
    build_base(&s);
    put32(s.data + 8, UINT32_MAX);
    REJECT_SIG(&s, "SuperBlob count overflows its index");

    /* One entry more than the blobs: the index now covers the first blob. */
    build_base(&s);
    put32(s.data + 8, BASE_COUNT + 1u);
    REJECT_SIG(&s, "blob overlaps the SuperBlob header or index");

    build_base(&s);
    put32(s.data + 8, 0u);
    REJECT_SIG(&s, "no CodeDirectory in slot 0");
}

static void test_entry_limits(void) {
    sigbuf s;
    ios27_codesign out;

    build_many(&s, IOS27_CODESIGN_MAX_BLOBS);
    if (ACCEPT(&s, &out)) {
        CHECK(out.count == IOS27_CODESIGN_MAX_BLOBS);
        CHECK(out.directory_count == 1u);
    }
    build_many(&s, IOS27_CODESIGN_MAX_BLOBS + 1u);
    REJECT_SIG(&s, "SuperBlob has too many entries");
}

static void test_entry_rejects(void) {
    sigbuf s;
    const uint32_t index_end = 12u + BASE_COUNT * 8u;

    build_base(&s);
    put32(s.data + entry_offset_at(E_CD), index_end - 1u);
    REJECT_SIG(&s, "blob overlaps the SuperBlob header or index");
    build_base(&s);
    put32(s.data + entry_offset_at(E_WRAP), 0u);
    REJECT_SIG(&s, "blob overlaps the SuperBlob header or index");
    build_base(&s);
    put32(s.data + entry_offset_at(E_WRAP), 8u);
    REJECT_SIG(&s, "blob overlaps the SuperBlob header or index");

    build_base(&s);
    put32(s.data + entry_offset_at(E_WRAP), s.size - 7u);
    REJECT_SIG(&s, "blob header is past the SuperBlob");
    build_base(&s);
    put32(s.data + entry_offset_at(E_WRAP), UINT32_MAX - 3u);
    REJECT_SIG(&s, "blob header is past the SuperBlob");

    build_base(&s);
    put32(s.data + s.offset[E_WRAP] + 4u, 0u);
    REJECT_SIG(&s, "blob length is past the SuperBlob");
    build_base(&s);
    put32(s.data + s.offset[E_WRAP] + 4u, 7u);
    REJECT_SIG(&s, "blob length is past the SuperBlob");
    build_base(&s);
    put32(s.data + s.offset[E_WRAP] + 4u, 9u);
    REJECT_SIG(&s, "blob length is past the SuperBlob");
    /* offset + length is exactly 2^32, which wraps to 0 in 32 bits. */
    build_base(&s);
    put32(s.data + s.offset[E_REQ] + 4u, 0u - s.offset[E_REQ]);
    REJECT_SIG(&s, "blob length is past the SuperBlob");

    build_base(&s);
    put32(s.data + entry_type_at(E_ENT), IOS27_CSSLOT_REQUIREMENTS);
    REJECT_SIG(&s, "duplicate SuperBlob slot");
    build_base(&s);
    put32(s.data + entry_type_at(E_ALT), IOS27_CSSLOT_CODEDIRECTORY);
    REJECT_SIG(&s, "duplicate SuperBlob slot");

    build_base(&s);
    put32(s.data + entry_offset_at(E_REQ), s.offset[E_CD]);
    REJECT_SIG(&s, "SuperBlob blobs overlap");
    /* One byte: the requirements blob grows into the entitlements blob. */
    build_base(&s);
    put32(s.data + s.offset[E_REQ] + 4u,
          s.offset[E_ENT] - s.offset[E_REQ] + 1u);
    REJECT_SIG(&s, "SuperBlob blobs overlap");
}

static void test_slot_magic(void) {
    static const struct { uint32_t entry, magic; } cases[] = {
        {E_CD, IOS27_CSMAGIC_REQUIREMENTS},
        {E_CD, IOS27_CSMAGIC_EMBEDDED_SIGNATURE},
        {E_REQ, IOS27_CSMAGIC_EMBEDDED_ENTITLEMENTS},
        {E_ENT, IOS27_CSMAGIC_EMBEDDED_DER_ENTITLEMENTS},
        {E_DER, IOS27_CSMAGIC_EMBEDDED_ENTITLEMENTS},
        {E_ALT, IOS27_CSMAGIC_BLOBWRAPPER},
        {E_WRAP, IOS27_CSMAGIC_CODEDIRECTORY},
        {E_WRAP, 0u},
    };
    sigbuf s;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        build_base(&s);
        put32(s.data + s.offset[cases[i].entry], cases[i].magic);
        REJECT_SIG(&s, "blob magic does not match its slot");
    }
}

/* Slots the parser does not know are bounded only. That includes a
 * CodeDirectory magic one slot below and one past the alternates. */
static void test_unknown_slots(void) {
    sigbuf s;
    uint8_t blob[BLOB_CAP];
    ios27_codesign out;
    uint32_t len;

    sig_begin(&s, 4u);
    len = make_cd(blob, &small_spec);
    sig_add(&s, 0u, IOS27_CSSLOT_CODEDIRECTORY, blob, len);
    len = make_generic(blob, 0x12345678u, 16u);
    sig_add(&s, 1u, 1u, blob, len); /* CSSLOT_INFOSLOT */
    len = make_generic(blob, IOS27_CSMAGIC_CODEDIRECTORY, 12u);
    sig_add(&s, 2u, IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES - 1u, blob, len);
    sig_add(&s, 3u,
            IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES +
                IOS27_CSSLOT_ALTERNATE_CODEDIRECTORY_MAX,
            blob, len);
    sig_end(&s);
    if (!ACCEPT(&s, &out)) return;
    CHECK(out.count == 4u);
    CHECK(out.directory_count == 1u);
    CHECK(out.has_requirements == 0u && out.has_entitlements == 0u);
    CHECK(out.has_der_entitlements == 0u && out.has_signature == 0u);
}

static void test_alternates(void) {
    static const uint32_t order[] = {0x1004u, 0x1000u, 0x1003u, 0x1001u,
                                     0x1002u};
    sigbuf s;
    uint8_t blob[BLOB_CAP];
    ios27_codesign out;
    cd_spec spec = small_spec;
    uint32_t len;

    /* Every alternate slot, out of order: reported by slot after slot 0. */
    sig_begin(&s, 6u);
    len = make_cd(blob, &primary_spec);
    sig_add(&s, 0u, IOS27_CSSLOT_CODEDIRECTORY, blob, len);
    for (uint32_t i = 0; i < 5u; ++i) {
        spec.code_slots =
            1u + (order[i] - IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES);
        len = make_cd(blob, &spec);
        sig_add(&s, 1u + i, order[i], blob, len);
    }
    sig_end(&s);
    if (ACCEPT(&s, &out)) {
        CHECK(out.directory_count == IOS27_CODESIGN_MAX_DIRECTORIES);
        for (uint32_t i = 1u; i < IOS27_CODESIGN_MAX_DIRECTORIES; ++i) {
            CHECK(out.directories[i].slot ==
                  IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES + i - 1u);
            CHECK(out.directories[i].code_slots == i);
        }
    }

    sig_begin(&s, 1u);
    len = make_cd(blob, &alternate_spec);
    sig_add(&s, 0u, IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES, blob, len);
    sig_end(&s);
    REJECT_SIG(&s, "no CodeDirectory in slot 0");

    /* A bad alternate rejects the whole signature and clears slot 0. */
    build_base(&s);
    s.data[s.offset[E_ALT] + CD_HASH_TYPE] = 9u;
    REJECT_SIG(&s, "unknown CodeDirectory hash type");
    build_base(&s);
    put32(s.data + s.offset[E_ALT] + CD_VERSION, 0x20000u);
    REJECT_SIG(&s, "unsupported CodeDirectory version");
}

static void test_versions(void) {
    static const uint32_t accepted[] = {
        IOS27_CS_VERSION_EARLIEST,   IOS27_CS_SUPPORTSSCATTER,
        IOS27_CS_SUPPORTSTEAMID,     IOS27_CS_SUPPORTSCODELIMIT64,
        IOS27_CS_SUPPORTSEXECSEG,    IOS27_CS_SUPPORTSEXECSEG + 1u,
        IOS27_CS_SUPPORTSRUNTIME,    IOS27_CS_SUPPORTSLINKAGE,
    };
    static const uint32_t rejected[] = {
        0u, 0x10000u, IOS27_CS_VERSION_EARLIEST - 1u,
        IOS27_CS_SUPPORTSLINKAGE + 1u, 0x30000u, UINT32_MAX,
    };
    sigbuf s;
    uint8_t blob[BLOB_CAP];
    ios27_codesign out;
    uint32_t len;

    for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); ++i) {
        cd_spec spec = primary_spec;
        const char *shorter;

        spec.version = accepted[i];
        spec.platform = 2u;
        spec.page_size_log2 = 0u; /* infinite page size */
        build_single(&s, &spec);
        if (ACCEPT(&s, &out)) {
            const ios27_code_directory *cd = &out.directories[0];

            CHECK(cd->version == accepted[i]);
            CHECK(cd->platform == 2u);
            CHECK(cd->page_size_log2 == 0u);
            if (accepted[i] >= IOS27_CS_SUPPORTSEXECSEG) {
                CHECK(cd->has_exec_seg == 1u);
                CHECK(cd->exec_seg_flags == spec.exec_seg_flags);
            } else {
                CHECK(cd->has_exec_seg == 0u);
                CHECK(cd->exec_seg_flags == 0u);
            }
        }

        /* Exactly the fixed header is enough and one byte less is not. */
        spec.special_slots = 0u;
        spec.code_slots = 0u;
        build_single(&s, &spec);
        if (ACCEPT(&s, &out)) CHECK(out.directories[0].code_slots == 0u);
        put32(s.data + s.offset[0] + CD_LENGTH, cd_header(accepted[i]) - 1u);
        shorter = accepted[i] == IOS27_CS_VERSION_EARLIEST
                      ? "truncated CodeDirectory"
                      : "CodeDirectory is shorter than its version";
        REJECT_SIG(&s, shorter);
    }

    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        build_single(&s, &primary_spec);
        put32(s.data + s.offset[0] + CD_VERSION, rejected[i]);
        REJECT_SIG(&s, "unsupported CodeDirectory version");
    }

    sig_begin(&s, 1u);
    len = make_generic(blob, IOS27_CSMAGIC_CODEDIRECTORY, 43u);
    sig_add(&s, 0u, IOS27_CSSLOT_CODEDIRECTORY, blob, len);
    sig_end(&s);
    REJECT_SIG(&s, "truncated CodeDirectory");
    sig_begin(&s, 1u);
    len = make_generic(blob, IOS27_CSMAGIC_CODEDIRECTORY, 8u);
    sig_add(&s, 0u, IOS27_CSSLOT_CODEDIRECTORY, blob, len);
    sig_end(&s);
    REJECT_SIG(&s, "truncated CodeDirectory");
}

static void test_hash_fields(void) {
    static const struct { uint8_t type, size; } good[] = {
        {IOS27_CS_HASHTYPE_SHA1, 20u},
        {IOS27_CS_HASHTYPE_SHA256, 32u},
        {IOS27_CS_HASHTYPE_SHA256_TRUNCATED, 20u},
        {IOS27_CS_HASHTYPE_SHA384, 48u},
    };
    static const struct { uint8_t type, size; } mismatched[] = {
        {IOS27_CS_HASHTYPE_SHA1, 32u},
        {IOS27_CS_HASHTYPE_SHA256, 20u},
        {IOS27_CS_HASHTYPE_SHA256, 0u},
        {IOS27_CS_HASHTYPE_SHA256_TRUNCATED, 32u},
        {IOS27_CS_HASHTYPE_SHA384, 32u},
    };
    static const uint8_t unknown[] = {0u, 5u, 0xffu};
    const uint32_t hash_offset =
        cd_header(primary_spec.version) +
        primary_spec.special_slots * primary_spec.hash_size;
    const struct { uint32_t field, value; } bounds[] = {
        {CD_HASH_OFFSET, hash_offset - 1u},
        {CD_HASH_OFFSET, hash_offset + 1u},
        {CD_HASH_OFFSET, 0u},
        {CD_HASH_OFFSET, UINT32_MAX},
        {CD_SPECIAL_SLOTS, primary_spec.special_slots + 1u},
        {CD_SPECIAL_SLOTS, 0x08000000u}, /* * 32 wraps to 0 in 32 bits */
        {CD_SPECIAL_SLOTS, UINT32_MAX},
        {CD_CODE_SLOTS, primary_spec.code_slots + 1u},
        {CD_CODE_SLOTS, 0x08000000u},
        {CD_CODE_SLOTS, UINT32_MAX},
    };
    sigbuf s;
    ios27_codesign out;

    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
        cd_spec spec = primary_spec;

        spec.hash_type = good[i].type;
        spec.hash_size = good[i].size;
        build_single(&s, &spec);
        if (ACCEPT(&s, &out)) {
            CHECK(out.directories[0].hash_type == good[i].type);
            CHECK(out.directories[0].hash_size == good[i].size);
        }
    }
    for (size_t i = 0; i < sizeof(mismatched) / sizeof(mismatched[0]); ++i) {
        build_single(&s, &primary_spec);
        s.data[s.offset[0] + CD_HASH_TYPE] = mismatched[i].type;
        s.data[s.offset[0] + CD_HASH_SIZE] = mismatched[i].size;
        REJECT_SIG(&s, "CodeDirectory hash size does not match its hash type");
    }
    for (size_t i = 0; i < sizeof(unknown); ++i) {
        build_single(&s, &primary_spec);
        s.data[s.offset[0] + CD_HASH_TYPE] = unknown[i];
        REJECT_SIG(&s, "unknown CodeDirectory hash type");
    }
    for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i) {
        build_single(&s, &primary_spec);
        put32(s.data + s.offset[0] + bounds[i].field, bounds[i].value);
        REJECT_SIG(&s, "CodeDirectory hash slots are out of bounds");
    }
}

/* The parser never reads blob bodies or hash bytes, so changing them must
 * leave the output byte for byte the same. */
static void test_contents_not_interpreted(void) {
    static const uint32_t bodies[] = {E_REQ, E_ENT, E_DER};
    sigbuf base, s;
    ios27_codesign want, got;
    uint32_t hashes;

    build_base(&base);
    if (!ACCEPT(&base, &want)) return;
    for (size_t b = 0; b < sizeof(bodies) / sizeof(bodies[0]); ++b) {
        uint32_t e = bodies[b];

        for (uint32_t at = base.offset[e] + 8u; at < base.offset[e + 1u];
             ++at) {
            s = base;
            s.data[at] ^= 0xffu;
            if (ACCEPT(&s, &got))
                CHECK(memcmp(&got, &want, sizeof(got)) == 0);
        }
    }
    hashes = base.offset[E_CD] + cd_header(primary_spec.version);
    for (uint32_t at = hashes; at < base.offset[E_REQ]; ++at) {
        s = base;
        s.data[at] ^= 0xffu;
        if (ACCEPT(&s, &got)) CHECK(memcmp(&got, &want, sizeof(got)) == 0);
    }
}

static uint32_t rng_state;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* What every accepted parse must satisfy, whatever the input. */
static int success_holds(const ios27_codesign *out, uint32_t datasize) {
    static const ios27_code_directory empty;

    if (out->length < 12u || out->length > datasize) return 0;
    if (out->count == 0u || out->count > IOS27_CODESIGN_MAX_BLOBS) return 0;
    if (out->directory_count < 1u ||
        out->directory_count > IOS27_CODESIGN_MAX_DIRECTORIES ||
        out->directory_count > out->count)
        return 0;
    for (size_t i = 0; i < IOS27_CODESIGN_MAX_DIRECTORIES; ++i) {
        const ios27_code_directory *cd = &out->directories[i];
        unsigned want;

        if (i >= out->directory_count) {
            if (memcmp(cd, &empty, sizeof(empty)) != 0) return 0;
            continue;
        }
        if (i == 0u && cd->slot != IOS27_CSSLOT_CODEDIRECTORY) return 0;
        if (i > 0u &&
            (cd->slot < IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES ||
             cd->slot >= IOS27_CSSLOT_ALTERNATE_CODEDIRECTORIES +
                             IOS27_CSSLOT_ALTERNATE_CODEDIRECTORY_MAX ||
             cd->slot <= out->directories[i - 1u].slot))
            return 0;
        if (cd->version < IOS27_CS_VERSION_EARLIEST ||
            cd->version > IOS27_CS_SUPPORTSLINKAGE)
            return 0;
        want = hash_len(cd->hash_type);
        if (want == 0u || (unsigned)cd->hash_size != want) return 0;
        if (cd->has_exec_seg != (cd->version >= IOS27_CS_SUPPORTSEXECSEG))
            return 0;
        if (!cd->has_exec_seg && cd->exec_seg_flags != 0u) return 0;
    }
    return 1;
}

/* Run one mutant and hold the parser to the fail-closed contract: a
 * reject has a NUL-terminated error and a zeroed output, and an accept has
 * an empty error and an output that success_holds(). */
static void sweep_case(const uint8_t *image, size_t size, uint32_t datasize,
                       const char *what, unsigned a, unsigned b,
                       int must_reject, unsigned counts[2]) {
    ios27_codesign out;
    char error[128];
    int rc, ok;

    memset(&out, 0xa5, sizeof(out));
    memset(error, 'X', sizeof(error));
    rc = parse_copy(image, size, 0u, datasize, &out, error, sizeof(error));
    if (memchr(error, 0, sizeof(error)) == NULL)
        ok = 0;
    else if (rc == -1)
        ok = error[0] != 0 && is_zero(&out);
    else
        ok = rc == 0 && !must_reject && error[0] == 0 &&
             success_holds(&out, datasize);
    if (!ok) {
        if (failures < 20)
            fprintf(stderr,
                    "test_codesign.c: %s case %u/%u: rc=%d error='%.*s' "
                    "breaks the fail-closed contract\n",
                    what, a, b, rc, (int)sizeof(error), error);
        ++failures;
    }
    ++counts[rc == 0 ? 0 : 1];
}

static void test_truncation_sweep(void) {
    static uint8_t mutant[SIG_CAP];
    sigbuf s;
    unsigned counts[2] = {0u, 0u};

    build_base(&s);
    for (uint32_t n = 0; n < s.size; ++n) {
        /* The image itself ends early. */
        sweep_case(s.data, n, n, "truncated image", n, 0u, 1, counts);
        /* The whole image, but a short LC_CODE_SIGNATURE datasize. */
        sweep_case(s.data, s.size, n, "short datasize", n, 1u, 1, counts);
        /* A SuperBlob length that ends before the last blob does. */
        memcpy(mutant, s.data, s.size);
        put32(mutant + 4, n);
        sweep_case(mutant, s.size, s.size, "short length", n, 2u, 1, counts);
    }
    CHECK(counts[0] == 0u);
    printf("truncation sweeps: %u of %u mutants rejected\n", counts[1],
           counts[0] + counts[1]);
}

static void test_bit_flip_sweep(void) {
    static uint8_t mutant[SIG_CAP];
    sigbuf s;
    unsigned single[2] = {0u, 0u};
    unsigned multi[2] = {0u, 0u};

    build_base(&s);
    for (uint32_t at = 0; at < s.size; ++at) {
        for (unsigned bit = 0; bit < 8u; ++bit) {
            memcpy(mutant, s.data, s.size);
            mutant[at] ^= (uint8_t)(1u << bit);
            sweep_case(mutant, s.size, s.size, "bit flip", at, bit, 0,
                       single);
        }
    }
    rng_state = SWEEP_SEED;
    for (unsigned iter = 0; iter < SWEEP_ROUNDS; ++iter) {
        unsigned flips = 1u + rng_next() % 8u;

        memcpy(mutant, s.data, s.size);
        for (unsigned k = 0; k < flips; ++k) {
            uint32_t r = rng_next();
            mutant[r % s.size] ^= (uint8_t)(1u << (r >> 29));
        }
        sweep_case(mutant, s.size, s.size, "random flips", iter, flips, 0,
                   multi);
    }
    /* Flips in filler and hash bytes still parse; flips in magics do not. */
    CHECK(single[0] > 0u && single[1] > 0u);
    CHECK(multi[1] > 0u);
    printf("bit flips: single %u accepted, %u rejected; seed 0x%08x "
           "%u accepted, %u rejected\n",
           single[0], single[1], (unsigned)SWEEP_SEED, multi[0], multi[1]);
}

/* Every aligned 32-bit word set to each boundary value in turn. */
static void test_field_sweep(void) {
    static uint8_t mutant[SIG_CAP];
    sigbuf s;
    unsigned counts[2] = {0u, 0u};

    build_base(&s);
    for (uint32_t at = 0; at + 4u <= s.size; at += 4u) {
        const uint32_t values[] = {
            0u, 1u, 7u, 8u, 12u, s.size - 1u, s.size, s.size + 1u,
            0x7fffffffu, 0x80000000u, UINT32_MAX - 7u, UINT32_MAX,
        };

        for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
            memcpy(mutant, s.data, s.size);
            put32(mutant + at, values[v]);
            sweep_case(mutant, s.size, s.size, "boundary word", at, v, 0,
                       counts);
        }
    }
    CHECK(counts[1] > 0u);
    printf("boundary words: %u accepted, %u rejected\n", counts[0],
           counts[1]);
}

int main(void) {
    test_base();
    test_presence_only();
    test_placement();
    test_arguments_and_error_capacity();
    test_superblob_rejects();
    test_entry_limits();
    test_entry_rejects();
    test_slot_magic();
    test_unknown_slots();
    test_alternates();
    test_versions();
    test_hash_fields();
    test_contents_not_interpreted();
    test_truncation_sweep();
    test_bit_flip_sweep();
    test_field_sweep();
    if (failures) {
        fprintf(stderr, "test_codesign: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_codesign: all checks passed\n");
    return 0;
}
