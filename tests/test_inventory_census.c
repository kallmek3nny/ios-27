/*
 * Z3 t-2064a3: deterministic mutation tests for the dylib load-command
 * census in src/inventory.c.
 *
 * ios27_inventory_parse records each LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB,
 * LC_REEXPORT_DYLIB, LC_LOAD_UPWARD_DYLIB and LC_LAZY_LOAD_DYLIB of the
 * selected slice in load-command order, with its command and install name.
 * It counts names under /System/Library/Frameworks/ and
 * /System/Library/PrivateFrameworks/ and does not merge duplicate install
 * names. Every fixture is a synthetic thin ARM64 MH_EXECUTE built in this
 * file; no Apple bytes are used. FAT/FAT64 slice selection (K t-63fd91) and
 * the 228/512/513 capacity cases in tests/test_inventory.c are out of scope.
 *
 * Each call runs on an exactly sized heap copy, so an ASan build reports any
 * read past the input, at error capacities 256, 8 and 1. For each call:
 *   - the return code is 0 or -1, the same at every error capacity, and the
 *     const input bytes are never modified;
 *   - the error is NUL-terminated within its capacity; a rejection leaves it
 *     nonempty when the capacity is above 1, and a success leaves it empty;
 *   - a success matches model(), an independent walk with the load command
 *     rules of src/macho.c and the census rules of src/inventory.c: the same
 *     imports in the same order, with the same commands, names and
 *     framework counts.
 * Named edits state their expected code, and model() must agree with it.
 * The name offset sweep keeps every command code, so there the parser and
 * model() must agree in both directions.
 *
 * include/ios27/inventory.h does not say what a rejection leaves in the
 * inventory, and src fills imports as it walks, so no test asserts a
 * cleared inventory after a rejection.
 */
#include "ios27/inventory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_MAX 1024u
#define RANDOM_BYTES 2048u
#define RANDOM_STACKED 512u
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define MH_MAGIC_64 0xfeedfacfu
#define CPU_TYPE_ARM64 0x0100000cu
#define MH_EXECUTE 2u

#define LC_LOAD_DYLIB 0x0cu
#define LC_ID_DYLIB 0x0du
#define LC_LOAD_DYLINKER 0x0eu
#define LC_UUID 0x1bu
#define LC_LAZY_LOAD_DYLIB 0x20u
#define LC_LOAD_WEAK_DYLIB 0x80000018u
#define LC_RPATH 0x8000001cu
#define LC_REEXPORT_DYLIB 0x8000001fu
#define LC_LOAD_UPWARD_DYLIB 0x80000023u

#define FRAMEWORKS "/System/Library/Frameworks/"
#define PRIVATE_FRAMEWORKS "/System/Library/PrivateFrameworks/"
#define NAME_ALPHA FRAMEWORKS "Alpha.framework/Alpha"
#define NAME_BETA PRIVATE_FRAMEWORKS "Beta.framework/Beta"
#define NAME_GAMMA "/usr/lib/libgamma.dylib"
#define NAME_DELTA FRAMEWORKS "Delta.framework/Delta"
#define NAME_EPSILON "/usr/lib/libepsilon.dylib"
#define NAME_SELF FRAMEWORKS "Self.framework/Self"
#define NAME_ZETA FRAMEWORKS "Zeta.framework/Zeta"

typedef struct {
    const char *name;
    uint64_t seed;
    size_t size;
    uint32_t ncmds;
    uint8_t bytes[IMAGE_MAX];
} image;

/* The census that model() expects; paths point into the input. */
typedef struct {
    size_t count;
    size_t frameworks;
    size_t private_frameworks;
    uint32_t command[IOS27_MAX_IMPORTS];
    const char *path[IOS27_MAX_IMPORTS];
} census;

/* Commands of the mixed fixture: a dylinker, an LC_ID_DYLIB with a
 * framework name and an LC_UUID, then seven imports that use all five
 * import commands and repeat two install names. */
enum {
    MIX_DYLINKER,
    MIX_ID,
    MIX_UUID,
    MIX_ALPHA,
    MIX_BETA,
    MIX_GAMMA,
    MIX_DELTA,
    MIX_EPSILON,
    MIX_ALPHA_AGAIN,
    MIX_GAMMA_AGAIN,
    MIX_COUNT
};

static const struct {
    uint32_t command;
    const char *path;
} mixed_imports[] = {
    {LC_LOAD_DYLIB, NAME_ALPHA},
    {LC_LOAD_WEAK_DYLIB, NAME_BETA},
    {LC_REEXPORT_DYLIB, NAME_GAMMA},
    {LC_LOAD_UPWARD_DYLIB, NAME_DELTA},
    {LC_LAZY_LOAD_DYLIB, NAME_EPSILON},
    {LC_LOAD_DYLIB, NAME_ALPHA},
    {LC_LOAD_WEAK_DYLIB, NAME_GAMMA}
};

/* One single-command fixture per import command. */
static const struct {
    const char *name;
    uint32_t command;
    uint64_t seed;
} kinds[] = {
    {"load", LC_LOAD_DYLIB, UINT64_C(0xd1b54a32d192ed03)},
    {"weak", LC_LOAD_WEAK_DYLIB, UINT64_C(0x8cb92ba72f3d8dd7)},
    {"reexport", LC_REEXPORT_DYLIB, UINT64_C(0xbf58476d1ce4e5b9)},
    {"upward", LC_LOAD_UPWARD_DYLIB, UINT64_C(0x94d049bb133111eb)},
    {"lazy", LC_LAZY_LOAD_DYLIB, UINT64_C(0x2545f4914f6cdd1d)}
};

static const size_t caps[] = {256u, 8u, 1u};

static ios27_inventory inv; /* over 130 KiB, so it stays off the stack */
static const char *ctx_fixture = "setup";
static const char *ctx_kind = "none";
static size_t ctx_pos;
static unsigned long long ctx_value;
static size_t ctx_cap;
static unsigned long long calls;
static uint64_t rng_state;

static void die(const char *what) {
    fprintf(stderr, "FAIL setup: %s\n", what);
    exit(1);
}

static void fail(const char *what) {
    fprintf(stderr,
            "FAIL inventory_parse on %s mutant %s at %zu value 0x%llx "
            "error_cap %zu: %s\n",
            ctx_fixture, ctx_kind, ctx_pos, ctx_value, ctx_cap, what);
    exit(1);
}

static void le32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* xorshift64*: fixed seeds make every random mutant reproducible. */
static uint64_t next_random(void) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * UINT64_C(0x2545f4914f6cdd1d);
}

static int is_import(uint32_t cmd) {
    return cmd == LC_LOAD_DYLIB || cmd == LC_LOAD_WEAK_DYLIB ||
           cmd == LC_REEXPORT_DYLIB || cmd == LC_LOAD_UPWARD_DYLIB ||
           cmd == LC_LAZY_LOAD_DYLIB;
}

/* Independent census of a thin image. It applies the load command table
 * rules of src/macho.c and the census rules of src/inventory.c, so every
 * thin image the parser accepts also passes here. Reads stay below size. */
static int model(const uint8_t *p, size_t size, census *c) {
    uint32_t ncmds, walked = 0u;
    size_t cursor = 32u, end;
    memset(c, 0, sizeof(*c));
    if (size < 32u || rd_le32(p) != MH_MAGIC_64 ||
        rd_le32(p + 4) != CPU_TYPE_ARM64)
        return -1;
    ncmds = rd_le32(p + 16);
    if (ncmds > 4096u || rd_le32(p + 20) > size - 32u) return -1;
    end = cursor + (size_t)rd_le32(p + 20);
    while (cursor < end) {
        const uint8_t *at = p + cursor;
        uint32_t cmd, cmdsize, off;
        if (end - cursor < 8u) return -1;
        cmd = rd_le32(at);
        cmdsize = rd_le32(at + 4);
        if (cmdsize < 8u || (cmdsize & 7u) != 0u || cmdsize > end - cursor)
            return -1;
        if (is_import(cmd)) {
            const uint8_t *name, *nul;
            size_t length;
            if (cmdsize < 24u) return -1;
            off = rd_le32(at + 8);
            if (off < 24u || off >= cmdsize) return -1;
            name = at + off;
            nul = memchr(name, 0, cmdsize - off);
            if (!nul) return -1;
            length = (size_t)(nul - name);
            if (length == 0u || length >= IOS27_IMPORT_NAME_MAX ||
                c->count == IOS27_MAX_IMPORTS)
                return -1;
            c->command[c->count] = cmd;
            c->path[c->count] = (const char *)name;
            c->count++;
            if (strncmp((const char *)name, PRIVATE_FRAMEWORKS,
                        sizeof(PRIVATE_FRAMEWORKS) - 1u) == 0)
                c->private_frameworks++;
            else if (strncmp((const char *)name, FRAMEWORKS,
                             sizeof(FRAMEWORKS) - 1u) == 0)
                c->frameworks++;
        } else if (cmd == LC_RPATH || cmd == LC_LOAD_DYLINKER ||
                   cmd == LC_ID_DYLIB) {
            const uint32_t min = cmd == LC_ID_DYLIB ? 24u : 12u;
            if (cmdsize < min) return -1;
            off = rd_le32(at + 8);
            if (off < min || off >= cmdsize ||
                !memchr(at + off, 0, cmdsize - off))
                return -1;
        }
        cursor += cmdsize;
        ++walked;
    }
    return walked == ncmds ? 0 : -1;
}

/* Poisons the fields the checks read, so a stale inventory cannot pass. */
static void poison(void) {
    inv.slice_offset = SIZE_MAX;
    inv.slice_size = 0u;
    inv.imports_count = SIZE_MAX;
    inv.frameworks_count = SIZE_MAX;
    inv.private_frameworks_count = SIZE_MAX;
    inv.is_fat = 1u;
    inv.imports[0].command = 0xa5a5a5a5u;
    memset(inv.imports[0].path, 'Z', sizeof(inv.imports[0].path));
}

static void check_result(int rc, const uint8_t *input, size_t size,
                         const char *error, size_t cap) {
    static census want;
    const int model_rc = model(input, size, &want);
    if (rc != 0 && rc != -1) fail("return code is neither 0 nor -1");
    if (!memchr(error, '\0', cap))
        fail("error is not NUL-terminated within error_cap");
    if (rc != 0) {
        if (cap > 1u && error[0] == '\0') fail("rejection left an empty error");
        return;
    }
    if (error[0] != '\0') fail("success left a nonempty error");
    if (model_rc != 0) fail("accepted an image that model() rejects");
    if (inv.is_fat || inv.slice_offset != 0u || inv.slice_size != size)
        fail("thin image did not select the whole input");
    if (inv.imports_count != want.count)
        fail("imports_count does not match the import commands");
    if (inv.frameworks_count != want.frameworks ||
        inv.private_frameworks_count != want.private_frameworks)
        fail("framework counts do not match the install names");
    for (size_t i = 0; i < want.count; ++i) {
        const ios27_import *item = &inv.imports[i];
        if (!memchr(item->path, '\0', sizeof(item->path)))
            fail("import path is not NUL-terminated");
        if (item->command != want.command[i])
            fail("import command does not match its load command");
        if (strcmp(item->path, want.path[i]) != 0)
            fail("import path does not match its install name");
    }
}

/* Runs one mutant from an exactly sized heap copy; returns its code. */
static int run_cap(const uint8_t *mutant, size_t size, size_t cap) {
    uint8_t *input = malloc(size ? size : 1u);
    char *error = malloc(cap);
    int rc;
    if (!input || !error) die("out of memory");
    if (size) memcpy(input, mutant, size);
    memset(error, 0x5a, cap); /* no stale NUL can hide a missing terminator */
    poison();
    ctx_cap = cap;
    rc = ios27_inventory_parse(input, size, &inv, error, cap);
    check_result(rc, input, size, error, cap);
    if (size && memcmp(input, mutant, size) != 0)
        fail("const input bytes were modified");
    ++calls;
    free(error);
    free(input);
    return rc;
}

/* Runs a mutant at every error capacity; the code must not change. */
static int run(const uint8_t *mutant, size_t size) {
    const int rc = run_cap(mutant, size, caps[0]);
    for (size_t i = 1; i < COUNT(caps); ++i) {
        if (run_cap(mutant, size, caps[i]) != rc)
            fail("return code depends on error_cap");
    }
    return rc;
}

/* Runs a named edit; model() and the parser must both give want_rc. */
static void expect(const uint8_t *m, size_t size, const char *kind,
                   unsigned long long value, int want_rc) {
    static census want;
    ctx_kind = kind;
    ctx_pos = 0u;
    ctx_value = value;
    if (model(m, size, &want) != want_rc)
        fail("model() disagrees with the named expectation");
    if (run(m, size) != want_rc)
        fail(want_rc ? "named mutant was accepted" : "named mutant was rejected");
}

static void image_init(image *im, const char *name, uint64_t seed) {
    memset(im, 0, sizeof(*im));
    im->name = name;
    im->seed = seed;
    le32(im->bytes, MH_MAGIC_64);
    le32(im->bytes + 4, CPU_TYPE_ARM64);
    le32(im->bytes + 12, MH_EXECUTE);
    im->size = 32u;
}

/* Appends a zeroed command and updates ncmds and sizeofcmds. */
static uint8_t *image_add(image *im, uint32_t cmd, size_t cmdsize) {
    uint8_t *c;
    if (cmdsize < 8u || (cmdsize & 7u) != 0u ||
        cmdsize > sizeof(im->bytes) - im->size)
        die("image capacity");
    c = im->bytes + im->size;
    le32(c, cmd);
    le32(c + 4, (uint32_t)cmdsize);
    im->size += cmdsize;
    im->ncmds++;
    le32(im->bytes + 16, im->ncmds);
    le32(im->bytes + 20, (uint32_t)(im->size - 32u));
    return c;
}

/* Appends a path command whose string starts at offset, padded to 8. */
static uint8_t *image_path(image *im, uint32_t cmd, uint32_t offset,
                           const char *path) {
    const size_t n = strlen(path) + 1u;
    uint8_t *c = image_add(im, cmd, ((size_t)offset + n + 7u) & ~(size_t)7u);
    le32(c + 8, offset);
    memcpy(c + offset, path, n);
    return c;
}

/* Offset of a load command in a fixture built with image_add. */
static size_t command_at(const image *im, uint32_t index) {
    size_t at = 32u;
    if (index >= im->ncmds) die("command index");
    for (uint32_t i = 0; i < index; ++i) at += rd_le32(im->bytes + at + 4u);
    return at;
}

static void make_mixed(image *im) {
    image_init(im, "mixed", UINT64_C(0x6a09e667f3bcc909));
    (void)image_path(im, LC_LOAD_DYLINKER, 12u, "/usr/lib/dyld");
    (void)image_path(im, LC_ID_DYLIB, 24u, NAME_SELF);
    (void)image_add(im, LC_UUID, 24u);
    for (size_t i = 0; i < COUNT(mixed_imports); ++i)
        (void)image_path(im, mixed_imports[i].command, 24u,
                         mixed_imports[i].path);
    if (im->ncmds != (uint32_t)MIX_COUNT) die("mixed fixture layout");
}

static void check_mixed_baseline(const image *im) {
    ctx_fixture = im->name;
    expect(im->bytes, im->size, "baseline", 0u, 0);
    if (inv.imports_count != COUNT(mixed_imports))
        fail("baseline imports_count does not match the fixture");
    for (size_t i = 0; i < COUNT(mixed_imports); ++i) {
        if (inv.imports[i].command != mixed_imports[i].command ||
            strcmp(inv.imports[i].path, mixed_imports[i].path) != 0)
            fail("baseline import does not match the fixture");
    }
    /* Alpha twice and Delta; Self is an LC_ID_DYLIB name, not an import. */
    if (inv.frameworks_count != 3u || inv.private_frameworks_count != 1u)
        fail("baseline framework counts do not match the fixture");
    if (!inv.needs_dyld) fail("baseline LC_LOAD_DYLINKER was not recorded");
}

static void named_mixed(const image *im) {
    const size_t dylinker = command_at(im, MIX_DYLINKER);
    const size_t id = command_at(im, MIX_ID);
    const size_t alpha = command_at(im, MIX_ALPHA);
    const size_t beta = command_at(im, MIX_BETA);
    const size_t last = command_at(im, MIX_GAMMA_AGAIN);
    const uint32_t alpha_size = rd_le32(im->bytes + alpha + 4u);
    uint8_t m[IMAGE_MAX];

    ctx_fixture = im->name;

    /* Alpha's name offset points at Beta's install name in the next command. */
    memcpy(m, im->bytes, im->size);
    le32(m + alpha + 8u, alpha_size + 24u);
    expect(m, im->size, "offset-into-next-command", alpha_size + 24u, -1);

    /* Alpha's name fills its command. The low byte of Beta's cmd word is
     * 0x18 and the next byte is 0, so a read past cmdsize finds a NUL. */
    memcpy(m, im->bytes, im->size);
    memset(m + alpha + 24u, 'x', alpha_size - 24u);
    expect(m, im->size, "unterminated-before-next-command", 0u, -1);

    /* Six imports are recorded before these rejections. src/macho.c accepts
     * an empty name, so only the census rejects the first one. */
    memcpy(m, im->bytes, im->size);
    m[last + 24u] = 0u;
    expect(m, im->size, "empty-name-after-imports", 0u, -1);
    memcpy(m, im->bytes, im->size);
    le32(m + last + 8u, 0u);
    expect(m, im->size, "offset-zero-after-imports", 0u, -1);

    /* src/macho.c does not check LC_ID_DYLIB; the census needs offset 24. */
    memcpy(m, im->bytes, im->size);
    le32(m + id + 8u, 12u);
    expect(m, im->size, "id-dylib-offset-12", 12u, -1);

    memcpy(m, im->bytes, im->size);
    memset(m + dylinker + 12u, 'x', rd_le32(im->bytes + dylinker + 4u) - 12u);
    expect(m, im->size, "dylinker-unterminated", 0u, -1);

    /* 0x18 without LC_REQ_DYLD is not an import command. */
    memcpy(m, im->bytes, im->size);
    le32(m + beta, 0x18u);
    expect(m, im->size, "weak-without-req-bit", 0x18u, 0);
    if (inv.imports_count != COUNT(mixed_imports) - 1u ||
        inv.private_frameworks_count != 0u)
        fail("a non-import command was counted");

    /* ncmds one short leaves the last command outside the counted table. */
    memcpy(m, im->bytes, im->size);
    le32(m + 16u, (uint32_t)MIX_COUNT - 1u);
    expect(m, im->size, "ncmds-one-short", (uint32_t)MIX_COUNT - 1u, -1);

    /* Bytes past sizeofcmds are not load commands, even if they look like one. */
    memcpy(m, im->bytes, im->size);
    le32(m + 16u, (uint32_t)MIX_COUNT - 1u);
    le32(m + 20u, (uint32_t)(last - 32u));
    expect(m, im->size, "last-command-past-sizeofcmds", last - 32u, 0);
    if (inv.imports_count != COUNT(mixed_imports) - 1u)
        fail("a command past sizeofcmds was counted");
}

/* Named edits of one single-command fixture whose import uses kind. */
static void named_single(const image *im, uint32_t kind) {
    static const uint32_t inside_header[] = {0u, 8u, 12u, 23u};
    const uint32_t cmdsize = rd_le32(im->bytes + 36u);
    uint8_t m[IMAGE_MAX];
    char path[IOS27_IMPORT_NAME_MAX + 1u];
    image s;

    ctx_fixture = im->name;
    if (im->bytes[32u + cmdsize - 1u] != 0u) die("single fixture padding");
    expect(im->bytes, im->size, "baseline", 0u, 0);
    if (inv.imports_count != 1u || inv.imports[0].command != kind ||
        strcmp(inv.imports[0].path, NAME_ZETA) != 0 ||
        inv.frameworks_count != 1u || inv.private_frameworks_count != 0u)
        fail("baseline import does not match the fixture");

    for (size_t i = 0; i < COUNT(inside_header); ++i) {
        memcpy(m, im->bytes, im->size);
        le32(m + 40u, inside_header[i]);
        expect(m, im->size, "offset-inside-command-header", inside_header[i],
               -1);
    }
    memcpy(m, im->bytes, im->size);
    le32(m + 40u, cmdsize);
    expect(m, im->size, "offset-at-cmdsize", cmdsize, -1);
    memcpy(m, im->bytes, im->size);
    le32(m + 40u, cmdsize - 1u); /* a padding NUL: an empty name */
    expect(m, im->size, "offset-at-last-byte", cmdsize - 1u, -1);
    memcpy(m, im->bytes, im->size);
    le32(m + 40u, UINT32_MAX);
    expect(m, im->size, "offset-max", UINT32_MAX, -1);

    memcpy(m, im->bytes, im->size);
    m[56] = 0u;
    expect(m, im->size, "empty-name", 0u, -1);
    /* The command ends the input, so a read past cmdsize leaves the copy. */
    memcpy(m, im->bytes, im->size);
    memset(m + 56, 'x', cmdsize - 24u);
    expect(m, im->size, "unterminated-name", 0u, -1);

    for (size_t short_size = 8u; short_size < 24u; short_size += 8u) {
        image_init(&s, im->name, 0u);
        (void)image_add(&s, kind, short_size);
        expect(s.bytes, s.size, "cmdsize-below-24", short_size, -1);
    }

    /* A 255-byte install name is kept whole; 256 bytes is rejected. */
    for (size_t len = IOS27_IMPORT_NAME_MAX - 1u; len <= IOS27_IMPORT_NAME_MAX;
         ++len) {
        const int keep = len < IOS27_IMPORT_NAME_MAX;
        memset(path, 'n', len);
        memcpy(path, "/usr/lib/", 9u);
        path[len] = '\0';
        image_init(&s, im->name, 0u);
        (void)image_path(&s, kind, 24u, path);
        expect(s.bytes, s.size, keep ? "name-at-limit" : "name-past-limit", len,
               keep ? 0 : -1);
        if (keep && (inv.imports_count != 1u ||
                     strcmp(inv.imports[0].path, path) != 0))
            fail("longest install name was not kept whole");
    }
}

/* Sets the name offset word of every command to each value from 0 to 16
 * past cmdsize, then to boundary words. Command codes are unchanged, so
 * the parser and model() must agree on every mutant. */
static void offset_sweep(const image *im) {
    static const uint32_t words[] = {
        0x7fffffffu, 0x80000000u, 0xfffffff8u, 0xffffffffu
    };
    static census want;
    uint8_t m[IMAGE_MAX];
    size_t at = 32u;

    ctx_fixture = im->name;
    ctx_kind = "name-offset";
    for (uint32_t i = 0; i < im->ncmds; ++i) {
        const uint32_t cmdsize = rd_le32(im->bytes + at + 4u);
        const size_t total = (size_t)cmdsize + 16u + COUNT(words);
        for (size_t v = 0; v < total; ++v) {
            const uint32_t off = v < (size_t)cmdsize + 16u
                                     ? (uint32_t)v
                                     : words[v - cmdsize - 16u];
            int want_rc;
            memcpy(m, im->bytes, im->size);
            le32(m + at + 8u, off);
            ctx_pos = at;
            ctx_value = off;
            want_rc = model(m, im->size, &want);
            if (run(m, im->size) != want_rc)
                fail(want_rc ? "accepted a name offset that model() rejects"
                             : "rejected a name offset that model() accepts");
        }
        at += cmdsize;
    }
}

static void sweep(const image *im) {
    uint8_t m[IMAGE_MAX];

    if (im->size <= 16u) die("sweep fixture size");
    ctx_fixture = im->name;
    ctx_kind = "truncate";
    ctx_value = 0u;
    for (size_t n = 0; n < im->size; ++n) {
        ctx_pos = n;
        (void)run(im->bytes, n);
    }
    ctx_kind = "bit-flip";
    for (size_t i = 0; i < im->size; ++i) {
        for (unsigned bit = 0; bit < 8u; ++bit) {
            memcpy(m, im->bytes, im->size);
            m[i] ^= (uint8_t)(1u << bit);
            ctx_pos = i;
            ctx_value = 1ull << bit;
            (void)run(m, im->size);
        }
    }
    rng_state = im->seed;
    ctx_kind = "random-byte";
    for (unsigned r = 0; r < RANDOM_BYTES; ++r) {
        const size_t at = (size_t)(next_random() % im->size);
        const uint8_t value = (uint8_t)next_random();
        memcpy(m, im->bytes, im->size);
        m[at] = value;
        ctx_pos = at;
        ctx_value = value;
        (void)run(m, im->size);
    }
    ctx_kind = "random-stacked";
    for (unsigned r = 0; r < RANDOM_STACKED; ++r) {
        const unsigned edits = 2u + (unsigned)(next_random() % 7u);
        const size_t keep = im->size - (size_t)(next_random() % 16u);
        memcpy(m, im->bytes, im->size);
        for (unsigned e = 0; e < edits; ++e) {
            const size_t at = (size_t)(next_random() % im->size);
            m[at] = (uint8_t)next_random();
        }
        ctx_pos = r;
        ctx_value = ((unsigned long long)edits << 32) | keep;
        (void)run(m, keep);
    }
}

int main(void) {
    static image mixed;
    static image singles[COUNT(kinds)];

    make_mixed(&mixed);
    check_mixed_baseline(&mixed);
    named_mixed(&mixed);
    for (size_t i = 0; i < COUNT(kinds); ++i) {
        image_init(&singles[i], kinds[i].name, kinds[i].seed);
        (void)image_path(&singles[i], kinds[i].command, 24u, NAME_ZETA);
        named_single(&singles[i], kinds[i].command);
    }
    offset_sweep(&mixed);
    sweep(&mixed);
    for (size_t i = 0; i < COUNT(kinds); ++i) {
        offset_sweep(&singles[i]);
        sweep(&singles[i]);
    }
    printf("inventory census tests passed: %llu calls\n", calls);
    return 0;
}
