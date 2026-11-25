/*
 * K t-63fd91: deterministic, bounded mutation sweeps over two parsers.
 *
 * ios27_inventory_parse selects a FAT/FAT64 ARM64 slice and then walks that
 * slice's load commands. Its fixtures are a thin ARM64 MH_EXECUTE with one
 * LC_LOAD_DYLIB, and FAT_MAGIC and FAT_MAGIC_64 files that each hold an
 * arm64e slice followed by a generic arm64 slice. Invariants for each call:
 *   - the return code is 0 or -1;
 *   - the error is NUL-terminated within error_cap, and a rejection leaves
 *     it nonempty at the normal 256-byte capacity;
 *   - 0 leaves an empty error and an ARM64 slice inside the input; a
 *     universal slice starts past the arch table and matches the
 *     best-ranked ARM64 arch entry; imports are bounded, nonempty and
 *     NUL-terminated;
 *   - the const input bytes are never modified.
 * Named edits cover nfat_arch, the arch table, offset plus size overflow,
 * overlap, alignment, cputype and byte-swapped (FAT_CIGAM) magic.
 *
 * ios27_exports_parse decodes the dyld export trie. Its fixtures are an
 * MH_DYLIB whose trie sits behind LC_DYLD_EXPORTS_TRIE or the legacy
 * LC_DYLD_INFO_ONLY. The trie holds a regular, a reexport and a
 * stub-and-resolver export under a shared "_" edge, and it ends the file.
 * The same return-code, error and const-input invariants hold, and a
 * rejection leaves no partial output. A success lists bounded, unique,
 * NUL-terminated names whose flags and metadata follow the rules in walk()
 * in src/exports.c. Named edits pin the IOS27_EXPORT_NAME_MAX boundary for
 * one-edge and split symbol names and for reexport imported names.
 *
 * Every fixture gets every prefix truncation, every single-bit flip, big-
 * and little-endian boundary word overwrites at every offset, and
 * fixed-seed random byte and stacked edits. Each mutant is copied into an
 * exactly sized heap buffer, so an ASan build reports any read past the
 * end of the input. Nothing is mapped or executed, so the sweep runs on
 * every host.
 */
#include "ios27/exports.h"
#include "ios27/inventory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_CAP 256u
#define SMALL_ERROR_CAP 8u
#define FIXTURE_MAX 512u
#define RANDOM_BYTES 2048u
#define RANDOM_STACKED 512u
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define FAT_MAGIC 0xcafebabeu
#define FAT_MAGIC_64 0xcafebabfu
#define FAT_CIGAM 0xbebafecau
#define FAT_CIGAM_64 0xbfbafecau
#define CPU_TYPE_ARM64 0x0100000cu
#define CPU_TYPE_X86_64 0x01000007u
#define CPU_SUBTYPE_ARM64E 2u
#define THIN_DYLIB "/System/Library/Frameworks/UIKit.framework/UIKit"

/* Slices are 16-byte aligned (align field 4) and start past either table. */
#define SLICE_ALIGN 4u
#define FAT32_FIRST 64u
#define FAT32_SECOND 192u
#define FAT64_FIRST 128u
#define FAT64_SECOND 256u

typedef struct {
    const char *name;
    size_t size;
    size_t first;  /* arm64e slice offset; 0 for the thin fixture */
    size_t second; /* generic arm64 slice offset; 0 for the thin fixture */
    uint64_t seed;
    int (*run)(const uint8_t *mutant, size_t size, size_t cap);
    uint8_t bytes[FIXTURE_MAX];
} fixture;

static ios27_inventory inv; /* over 130 KiB, so it stays off the stack */
static ios27_exports exps;  /* about 136 KiB, for the same reason */
static const char *ctx_parser = "setup";
static const char *ctx_fixture = "setup";
static const char *ctx_kind = "none";
static size_t ctx_pos;
static unsigned long long ctx_value;
static size_t ctx_cap = ERROR_CAP;
static unsigned long long calls;
static unsigned long long export_calls;
static uint64_t rng_state;

static void die(const char *what) {
    fprintf(stderr, "FAIL setup: %s\n", what);
    exit(1);
}

static void fail(const char *what) {
    fprintf(stderr,
            "FAIL %s on %s mutant %s at %zu value 0x%llx "
            "error_cap %zu: %s\n",
            ctx_parser, ctx_fixture, ctx_kind, ctx_pos, ctx_value, ctx_cap,
            what);
    exit(1);
}

static void le32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}

static void be32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (24u - 8u * i));
}

static void be64(uint8_t *p, uint64_t v) {
    be32(p, (uint32_t)(v >> 32));
    be32(p + 4, (uint32_t)v);
}

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t rd_be64(const uint8_t *p) {
    return ((uint64_t)rd_be32(p) << 32) | rd_be32(p + 4);
}

/* Arch entry offset is at +8 in both forms; size and align move in FAT64. */
static size_t size_field(int wide) { return wide ? 16u : 12u; }
static size_t align_field(int wide) { return wide ? 24u : 16u; }

/* Writes an arch entry offset or size at its FAT or FAT64 width. */
static void put_field(uint8_t *p, int wide, uint64_t v) {
    if (wide)
        be64(p, v);
    else
        be32(p, (uint32_t)v);
}

/* xorshift64*: fixed seeds make every random mutant reproducible. */
static uint64_t next_random(void) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * UINT64_C(0x2545f4914f6cdd1d);
}

/* Same layout as make_thin in tests/test_inventory.c. */
static size_t make_thin(uint8_t *p, size_t capacity) {
    static const char name[] = THIN_DYLIB;
    const size_t cmdsize = (24u + sizeof(name) + 7u) & ~(size_t)7u;
    if (capacity < 32u + cmdsize) die("thin fixture capacity");
    memset(p, 0, capacity);
    le32(p, 0xfeedfacfu);
    le32(p + 4, CPU_TYPE_ARM64);
    le32(p + 12, 2u);                /* MH_EXECUTE */
    le32(p + 16, 1u);                /* ncmds */
    le32(p + 20, (uint32_t)cmdsize); /* sizeofcmds */
    le32(p + 32, 0x0cu);             /* LC_LOAD_DYLIB */
    le32(p + 36, (uint32_t)cmdsize);
    le32(p + 40, 24u);               /* dylib name offset */
    memcpy(p + 56, name, sizeof(name));
    return 32u + cmdsize;
}

/* An arm64e entry, then a generic arm64 entry, each over a thin copy. */
static void make_fat(fixture *f, int wide, const uint8_t *thin,
                     size_t thin_len) {
    const size_t stride = wide ? 32u : 20u;
    f->first = wide ? FAT64_FIRST : FAT32_FIRST;
    f->second = wide ? FAT64_SECOND : FAT32_SECOND;
    if (thin_len > f->second - f->first || f->second + thin_len > FIXTURE_MAX)
        die("fat fixture capacity");
    memset(f->bytes, 0, sizeof(f->bytes));
    be32(f->bytes, wide ? FAT_MAGIC_64 : FAT_MAGIC);
    be32(f->bytes + 4, 2u);
    for (unsigned i = 0; i < 2u; ++i) {
        uint8_t *entry = f->bytes + 8u + i * stride;
        const size_t offset = i ? f->second : f->first;
        be32(entry, CPU_TYPE_ARM64);
        be32(entry + 4, i ? 0u : CPU_SUBTYPE_ARM64E);
        put_field(entry + 8, wide, offset);
        put_field(entry + size_field(wide), wide, thin_len);
        be32(entry + align_field(wide), SLICE_ALIGN);
        memcpy(f->bytes + offset, thin, thin_len);
    }
    f->size = f->second + thin_len;
}

/* Mirrors select_slice in src/inventory.c: generic arm64 first, then
 * arm64e, then any other ARM64 subtype. */
static int arch_rank(uint32_t subtype) {
    subtype &= 0x00ffffffu;
    return subtype == 0u ? 2 : (subtype == CPU_SUBTYPE_ARM64E ? 1 : 0);
}

/* A universal success must pick the best-ranked ARM64 entry past the table. */
static void check_fat_selection(const uint8_t *input, size_t size) {
    uint32_t magic, count;
    size_t stride;
    uint64_t table_end;
    int best = -1, matched = -1;
    if (size < 8u) fail("is_fat set on an input shorter than a fat header");
    magic = rd_be32(input);
    if (magic != FAT_MAGIC && magic != FAT_MAGIC_64)
        fail("is_fat set without a universal magic");
    stride = magic == FAT_MAGIC ? 20u : 32u;
    count = rd_be32(input + 4);
    table_end = 8u + (uint64_t)count * stride;
    if (count == 0u || table_end > size)
        fail("accepted an empty or out-of-bounds arch table");
    if (inv.slice_offset < table_end)
        fail("selected slice overlaps the universal arch table");
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry = input + 8u + (size_t)i * stride;
        const int wide = stride == 32u;
        const uint64_t offset = wide ? rd_be64(entry + 8) : rd_be32(entry + 8);
        const uint64_t bytes = wide ? rd_be64(entry + 16) : rd_be32(entry + 12);
        int rank;
        if (rd_be32(entry) != CPU_TYPE_ARM64) continue;
        rank = arch_rank(rd_be32(entry + 4));
        if (rank > best) best = rank;
        if (offset == inv.slice_offset && bytes == inv.slice_size &&
            rank > matched)
            matched = rank;
    }
    if (matched < 0) fail("selected slice matches no ARM64 arch entry");
    if (matched != best) fail("a better-ranked ARM64 slice was not selected");
}

static void check_result(int rc, const uint8_t *input, size_t size,
                         const char *error, size_t cap) {
    if (rc != 0 && rc != -1) fail("return code is neither 0 nor -1");
    if (!memchr(error, '\0', cap))
        fail("error is not NUL-terminated within error_cap");
    if (rc != 0) {
        if (cap == ERROR_CAP && error[0] == '\0')
            fail("rejection left an empty error");
        return;
    }
    if (error[0] != '\0') fail("success left a nonempty error");
    if (inv.slice_offset > size || inv.slice_size > size - inv.slice_offset)
        fail("selected slice lies outside the input");
    if (inv.slice_size < 32u)
        fail("selected slice is shorter than a Mach-O header");
    if (inv.cputype != CPU_TYPE_ARM64) fail("selected image is not ARM64");
    if ((inv.is_arm64e != 0) !=
        ((inv.cpusubtype & 0x00ffffffu) == CPU_SUBTYPE_ARM64E))
        fail("is_arm64e disagrees with the slice cpusubtype");
    if (inv.is_fat) {
        check_fat_selection(input, size);
    } else {
        if (inv.slice_offset != 0u || inv.slice_size != size)
            fail("thin image did not select the whole input");
        if (size >= 4u && (rd_be32(input) == FAT_MAGIC ||
                           rd_be32(input) == FAT_MAGIC_64))
            fail("universal magic was parsed as a thin image");
    }
    if (inv.imports_count > IOS27_MAX_IMPORTS)
        fail("imports_count exceeds IOS27_MAX_IMPORTS");
    if (inv.frameworks_count + inv.private_frameworks_count >
        inv.imports_count)
        fail("framework counts exceed imports_count");
    for (size_t i = 0; i < inv.imports_count; ++i) {
        const char *path = inv.imports[i].path;
        if (!memchr(path, '\0', IOS27_IMPORT_NAME_MAX))
            fail("import path is not NUL-terminated");
        if (path[0] == '\0') fail("import path is empty");
    }
}

/* Runs one mutant from an exactly sized heap copy; returns its code. */
static int run_mutant(const uint8_t *mutant, size_t size, size_t cap) {
    uint8_t *input = malloc(size ? size : 1u);
    char *error = malloc(cap);
    int rc;
    if (!input || !error) die("out of memory");
    if (size) memcpy(input, mutant, size);
    memset(error, 0x5a, cap); /* no stale NUL can hide a missing terminator */
    ctx_parser = "inventory_parse";
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

/* Export trie fixtures use the header and load command of fixture() in
 * tests/test_exports.c, except that the trie ends the file. A read past
 * the trie then also leaves the exactly sized heap copy, and ASan reports
 * it. */
#define CMD_EXPORTS_TRIE 0x80000033u   /* LC_DYLD_EXPORTS_TRIE */
#define CMD_DYLD_INFO_ONLY 0x80000022u /* LC_DYLD_INFO_ONLY */
#define TRIE_OFF 128u

/* "_" splits into _alpha (regular, 0x1234), _beta (reexport of _original
 * from ordinal 1) and _stub (stub 0x11 with resolver 0x22). */
static const uint8_t base_trie[] = {
    0x00, 0x01, '_', 0x00, 0x05, /* 0: root */
    0x00, 0x03,                  /* 5: "_" */
    'a', 'l', 'p', 'h', 'a', 0x00, 0x1a,
    'b', 'e', 't', 'a', 0x00, 0x1f,
    's', 't', 'u', 'b', 0x00, 0x2d,
    0x03, 0x00, 0xb4, 0x24, 0x00, /* 26: _alpha */
    0x0c, 0x08, 0x01,             /* 31: _beta */
    '_', 'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l', 0x00, 0x00,
    0x03, 0x10, 0x11, 0x22, 0x00  /* 45: _stub */
};

static size_t make_exports(uint8_t *p, uint32_t command, const uint8_t *trie,
                           size_t trie_len) {
    const int modern = command == CMD_EXPORTS_TRIE;
    const uint32_t cmdsize = modern ? 16u : 48u;
    const size_t field = modern ? 40u : 72u; /* trie offset, then size */
    if (trie_len > FIXTURE_MAX - TRIE_OFF) die("exports fixture capacity");
    memset(p, 0, FIXTURE_MAX);
    le32(p, 0xfeedfacfu);
    le32(p + 4, CPU_TYPE_ARM64);
    le32(p + 12, 6u);      /* MH_DYLIB */
    le32(p + 16, 1u);      /* ncmds */
    le32(p + 20, cmdsize); /* sizeofcmds */
    le32(p + 32, command);
    le32(p + 36, cmdsize);
    le32(p + field, TRIE_OFF);
    le32(p + field + 4, (uint32_t)trie_len);
    memcpy(p + TRIE_OFF, trie, trie_len);
    return TRIE_OFF + trie_len;
}

/* Mirrors the flag rules in walk() in src/exports.c. */
static int export_flags_ok(uint64_t flags) {
    const uint64_t kind = flags & IOS27_EXPORT_KIND_MASK;
    const uint64_t special =
        flags & (IOS27_EXPORT_REEXPORT | IOS27_EXPORT_STUB_RESOLVER);
    if ((flags & ~UINT64_C(0x1f)) != 0u || kind == 3u) return 0;
    if (special != 0u && kind != IOS27_EXPORT_KIND_REGULAR) return 0;
    return special != (IOS27_EXPORT_REEXPORT | IOS27_EXPORT_STUB_RESOLVER);
}

static void check_exports(int rc, const char *error, size_t cap) {
    static const ios27_export zero;
    if (rc != 0 && rc != -1) fail("return code is neither 0 nor -1");
    if (!memchr(error, '\0', cap))
        fail("error is not NUL-terminated within error_cap");
    if (rc != 0) {
        if (cap == ERROR_CAP && error[0] == '\0')
            fail("rejection left an empty error");
        if (exps.count != 0u || exps.has_trie ||
            memcmp(&exps.entries[0], &zero, sizeof(zero)) != 0)
            fail("rejection left partial exports output");
        return;
    }
    if (error[0] != '\0') fail("success left a nonempty error");
    if (exps.count > IOS27_MAX_EXPORTS)
        fail("count exceeds IOS27_MAX_EXPORTS");
    if (exps.count != 0u && !exps.has_trie)
        fail("exports listed without has_trie");
    for (size_t i = 0; i < exps.count; ++i) {
        const ios27_export *e = &exps.entries[i];
        if (!memchr(e->name, '\0', IOS27_EXPORT_NAME_MAX) || !e->name[0])
            fail("export name is empty or not NUL-terminated");
        if (!memchr(e->imported_name, '\0', IOS27_EXPORT_NAME_MAX))
            fail("imported name is not NUL-terminated");
        if (!export_flags_ok(e->flags))
            fail("accepted reserved or conflicting export flags");
        if (e->flags & IOS27_EXPORT_REEXPORT) {
            if (e->ordinal == 0u || e->address != 0u || e->resolver != 0u)
                fail("reexport metadata is inconsistent");
        } else if (e->ordinal != 0u || e->imported_name[0] != '\0' ||
                   (!(e->flags & IOS27_EXPORT_STUB_RESOLVER) &&
                    e->resolver != 0u)) {
            fail("non-reexport carries reexport or resolver metadata");
        }
        if (ios27_exports_find(&exps, e->name) != e)
            fail("export name is duplicated or cannot be found");
    }
}

/* Runs one export trie mutant from an exactly sized heap copy. */
static int run_exports(const uint8_t *mutant, size_t size, size_t cap) {
    uint8_t *input = malloc(size ? size : 1u);
    char *error = malloc(cap);
    int rc;
    if (!input || !error) die("out of memory");
    if (size) memcpy(input, mutant, size);
    memset(error, 0x5a, cap);
    /* Poison every field the checks read, so a skipped clear cannot pass. */
    exps.count = SIZE_MAX;
    exps.has_trie = 1u;
    memset(&exps.entries[0], 0xa5, sizeof(exps.entries[0]));
    ctx_parser = "exports_parse";
    ctx_cap = cap;
    rc = ios27_exports_parse(input, size, &exps, error, cap);
    check_exports(rc, error, cap);
    if (size && memcmp(input, mutant, size) != 0)
        fail("const input bytes were modified");
    ++export_calls;
    free(error);
    free(input);
    return rc;
}

static void check_exports_baseline(const fixture *f) {
    const ios27_export *e;
    ctx_fixture = f->name;
    ctx_kind = "baseline";
    ctx_pos = 0u;
    ctx_value = 0u;
    if (run_exports(f->bytes, f->size, ERROR_CAP) != 0)
        fail("baseline fixture was rejected");
    if (exps.count != 3u || !exps.has_trie)
        fail("baseline export count does not match the fixture");
    e = ios27_exports_find(&exps, "_alpha");
    if (!e || e->flags != 0u || e->address != 0x1234u)
        fail("baseline _alpha does not match the fixture");
    e = ios27_exports_find(&exps, "_beta");
    if (!e || e->flags != IOS27_EXPORT_REEXPORT || e->ordinal != 1u ||
        strcmp(e->imported_name, "_original") != 0)
        fail("baseline _beta does not match the fixture");
    e = ios27_exports_find(&exps, "_stub");
    if (!e || e->flags != IOS27_EXPORT_STUB_RESOLVER || e->address != 0x11u ||
        e->resolver != 0x22u)
        fail("baseline _stub does not match the fixture");
}

/* Writes v as ULEB128 at t + at; returns the offset just past it. */
static size_t put_uleb(uint8_t *t, size_t at, uint64_t v) {
    do {
        const uint8_t low = (uint8_t)(v & 0x7fu);
        v >>= 7;
        t[at++] = (uint8_t)(low | (v != 0u ? 0x80u : 0u));
    } while (v != 0u);
    return at;
}

/* A node with no terminal and one n-byte edge of fill bytes, whose child
 * starts right after it; returns the child offset. */
static size_t edge_node(uint8_t *t, size_t at, int fill, size_t n) {
    const size_t child = at + 4u + n + (at + 4u + n >= 0x80u ? 1u : 0u);
    t[at] = 0u;      /* no terminal */
    t[at + 1u] = 1u; /* one child */
    memset(t + at + 2u, fill, n);
    t[at + 2u + n] = 0u;
    if (put_uleb(t, at + 3u + n, child) != child) die("edge node layout");
    return child;
}

/* One regular export named by an n1-byte edge plus an optional n2-byte
 * edge, so the full name can sit on either side of IOS27_EXPORT_NAME_MAX. */
static size_t name_trie(uint8_t *t, size_t n1, size_t n2) {
    size_t at = edge_node(t, 0u, 'a', n1);
    if (n2 != 0u) at = edge_node(t, at, 'b', n2);
    t[at] = 2u; /* terminal: flags 0 and address 1 */
    t[at + 1u] = 0u;
    t[at + 2u] = 1u;
    t[at + 3u] = 0u; /* no children */
    return at + 4u;
}

/* One reexport "r" from ordinal 1 whose imported name is n bytes long. */
static size_t alias_trie(uint8_t *t, size_t n) {
    size_t at = edge_node(t, 0u, 'r', 1u);
    at = put_uleb(t, at, 3u + n); /* flags, ordinal, name and its NUL */
    t[at++] = IOS27_EXPORT_REEXPORT;
    t[at++] = 1u;
    memset(t + at, 'i', n);
    at += n;
    t[at++] = 0u;
    t[at++] = 0u; /* no children */
    return at;
}

/* Runs a named export trie at both error capacities and checks its code. */
static void expect_exports(const uint8_t *trie, size_t trie_len,
                           const char *kind, int want_rc) {
    uint8_t m[FIXTURE_MAX];
    const size_t size = make_exports(m, CMD_EXPORTS_TRIE, trie, trie_len);
    ctx_fixture = "exports-named";
    ctx_kind = kind;
    ctx_pos = 0u;
    ctx_value = (unsigned long long)trie_len;
    if (run_exports(m, size, SMALL_ERROR_CAP) != want_rc ||
        run_exports(m, size, ERROR_CAP) != want_rc)
        fail(want_rc ? "named mutant was accepted" : "named mutant was rejected");
}

static void named_exports(void) {
    const size_t max = IOS27_EXPORT_NAME_MAX;
    uint8_t t[FIXTURE_MAX];
    size_t n;

    n = name_trie(t, max - 1u, 0u);
    expect_exports(t, n, "name-at-limit", 0);
    if (exps.count != 1u || strlen(exps.entries[0].name) != max - 1u)
        fail("longest symbol name was not kept whole");
    n = name_trie(t, max, 0u);
    expect_exports(t, n, "name-past-limit", -1);
    n = name_trie(t, 200u, max - 201u);
    expect_exports(t, n, "split-name-at-limit", 0);
    if (exps.count != 1u || strlen(exps.entries[0].name) != max - 1u)
        fail("longest split symbol name was not kept whole");
    n = name_trie(t, 200u, max - 200u);
    expect_exports(t, n, "split-name-past-limit", -1);
    n = alias_trie(t, max - 1u);
    expect_exports(t, n, "alias-at-limit", 0);
    if (exps.count != 1u || strlen(exps.entries[0].imported_name) != max - 1u)
        fail("longest imported name was not kept whole");
    n = alias_trie(t, max);
    expect_exports(t, n, "alias-past-limit", -1);
}

static void check_baseline(const fixture *f, size_t thin_len) {
    const int fat = f->second != 0u;
    ctx_fixture = f->name;
    ctx_kind = "baseline";
    ctx_pos = 0u;
    ctx_value = 0u;
    if (run_mutant(f->bytes, f->size, ERROR_CAP) != 0)
        fail("baseline fixture was rejected");
    if ((inv.is_fat != 0) != fat || inv.slice_offset != f->second ||
        inv.slice_size != thin_len || inv.is_arm64e ||
        inv.imports_count != 1u || inv.frameworks_count != 1u ||
        strcmp(inv.imports[0].path, THIN_DYLIB) != 0)
        fail("baseline inventory does not match the fixture");
}

/* Runs a named edit at both error capacities and checks its outcome. */
static void expect(const uint8_t *m, size_t size, const char *kind,
                   int want_rc, size_t want_offset) {
    ctx_kind = kind;
    ctx_pos = 0u;
    ctx_value = (unsigned long long)size;
    if (run_mutant(m, size, SMALL_ERROR_CAP) != want_rc ||
        run_mutant(m, size, ERROR_CAP) != want_rc)
        fail(want_rc ? "named mutant was accepted" : "named mutant was rejected");
    if (want_rc == 0 && (!inv.is_fat || inv.slice_offset != want_offset))
        fail("named mutant selected the wrong slice");
}

static void named_fat(const fixture *f) {
    const int wide = rd_be32(f->bytes) == FAT_MAGIC_64;
    const size_t stride = wide ? 32u : 20u;
    const size_t table_end = 8u + 2u * stride;
    const uint64_t field_max = wide ? UINT64_MAX : UINT32_MAX;
    uint8_t m[FIXTURE_MAX];
    uint8_t *const e0 = m + 8u;
    uint8_t *const e1 = m + 8u + stride;

    ctx_fixture = f->name;

    memcpy(m, f->bytes, f->size);
    be32(m + 4, 0u);
    expect(m, f->size, "nfat_arch-zero", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(m + 4, 65u); /* MAX_SLICES in src/inventory.c is 64 */
    expect(m, f->size, "nfat_arch-over-cap", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(m + 4, (uint32_t)((f->size - 8u) / stride + 1u));
    expect(m, f->size, "nfat_arch-past-input", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(m + 4, UINT32_MAX);
    expect(m, f->size, "nfat_arch-max", -1, 0u);

    expect(f->bytes, table_end - 1u, "arch-table-truncated", -1, 0u);
    expect(f->bytes, 8u + stride, "arch-table-one-entry", -1, 0u);

    /* offset + size wraps the field width; the range check must catch it. */
    memcpy(m, f->bytes, f->size);
    put_field(e1 + size_field(wide), wide, field_max - f->second + 1u);
    expect(m, f->size, "offset-plus-size-wraps", -1, 0u);

    memcpy(m, f->bytes, f->size);
    put_field(e1 + 8, wide, f->size);
    put_field(e1 + size_field(wide), wide, 32u);
    expect(m, f->size, "slice-at-end-of-input", -1, 0u);

    memcpy(m, f->bytes, f->size);
    put_field(e0 + 8, wide, 0u);
    expect(m, f->size, "slice-overlaps-fat-header", -1, 0u);

    memcpy(m, f->bytes, f->size);
    put_field(e0 + 8, wide, table_end - 4u);
    be32(e0 + align_field(wide), 0u);
    expect(m, f->size, "slice-overlaps-arch-table", -1, 0u);

    memcpy(m, f->bytes, f->size);
    put_field(e0 + 8, wide, table_end);
    be32(e0 + align_field(wide), 0u);
    expect(m, f->size, "unused-slice-at-table-end", 0, f->second);

    memcpy(m, f->bytes, f->size);
    put_field(e1 + size_field(wide), wide, 31u);
    expect(m, f->size, "slice-shorter-than-header", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(e1 + align_field(wide), 63u);
    expect(m, f->size, "align-63", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(e1 + align_field(wide), 15u);
    expect(m, f->size, "slice-misaligned", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(e0 + align_field(wide), 15u);
    expect(m, f->size, "unused-slice-misaligned", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(e0, CPU_TYPE_X86_64);
    be32(e1, CPU_TYPE_X86_64);
    expect(m, f->size, "no-arm64-slice", -1, 0u);

    memcpy(m, f->bytes, f->size);
    be32(e1, CPU_TYPE_X86_64);
    expect(m, f->size, "arm64e-only", 0, f->first);

    memcpy(m, f->bytes, f->size);
    be32(e1 + 4, 0x80000000u); /* capability bits are masked off */
    expect(m, f->size, "generic-with-capability-bits", 0, f->second);

    memcpy(m, f->bytes, f->size);
    be32(m, wide ? FAT_CIGAM_64 : FAT_CIGAM);
    expect(m, f->size, "byte-swapped-magic", -1, 0u);
}

static void sweep(const fixture *f) {
    const uint32_t words[] = {
        0u, 1u, 4u, 8u, 0x20u, 0x40u, 0x7fffffffu, 0x80000000u,
        0xfffffff8u, 0xffffffffu, (uint32_t)f->size - 1u,
        (uint32_t)f->size, (uint32_t)f->size + 1u
    };
    uint8_t m[FIXTURE_MAX];

    ctx_fixture = f->name;
    ctx_kind = "truncate";
    ctx_value = 0u;
    for (size_t n = 0; n < f->size; ++n) {
        ctx_pos = n;
        (void)f->run(f->bytes, n, ERROR_CAP);
        (void)f->run(f->bytes, n, SMALL_ERROR_CAP);
    }
    ctx_kind = "bit-flip";
    for (size_t i = 0; i < f->size; ++i) {
        for (unsigned bit = 0; bit < 8u; ++bit) {
            memcpy(m, f->bytes, f->size);
            m[i] ^= (uint8_t)(1u << bit);
            ctx_pos = i;
            ctx_value = 1ull << bit;
            (void)f->run(m, f->size, ERROR_CAP);
            (void)f->run(m, f->size, SMALL_ERROR_CAP);
        }
    }
    for (size_t i = 0; i + 4u <= f->size; ++i) {
        for (size_t w = 0; w < COUNT(words); ++w) {
            for (unsigned big = 0; big < 2u; ++big) {
                memcpy(m, f->bytes, f->size);
                if (big)
                    be32(m + i, words[w]);
                else
                    le32(m + i, words[w]);
                ctx_kind = big ? "word-be" : "word-le";
                ctx_pos = i;
                ctx_value = words[w];
                (void)f->run(m, f->size, ERROR_CAP);
            }
        }
    }
    rng_state = f->seed;
    ctx_kind = "random-byte";
    for (unsigned r = 0; r < RANDOM_BYTES; ++r) {
        const size_t at = (size_t)(next_random() % f->size);
        const uint8_t value = (uint8_t)next_random();
        memcpy(m, f->bytes, f->size);
        m[at] = value;
        ctx_pos = at;
        ctx_value = value;
        (void)f->run(m, f->size, ERROR_CAP);
    }
    ctx_kind = "random-stacked";
    for (unsigned r = 0; r < RANDOM_STACKED; ++r) {
        const unsigned edits = 2u + (unsigned)(next_random() % 7u);
        const size_t keep = f->size - (size_t)(next_random() % 16u);
        memcpy(m, f->bytes, f->size);
        for (unsigned e = 0; e < edits; ++e) {
            const size_t at = (size_t)(next_random() % f->size);
            m[at] = (uint8_t)next_random();
        }
        ctx_pos = r;
        ctx_value = ((unsigned long long)edits << 32) | keep;
        (void)f->run(m, keep, ERROR_CAP);
    }
}

int main(void) {
    static fixture fixtures[5];
    uint8_t thin[FIXTURE_MAX];
    const size_t thin_len = make_thin(thin, sizeof(thin));

    fixtures[0].name = "thin";
    fixtures[0].seed = UINT64_C(0x9e3779b97f4a7c15);
    fixtures[0].run = run_mutant;
    fixtures[0].size = thin_len;
    memcpy(fixtures[0].bytes, thin, thin_len);
    fixtures[1].name = "fat32";
    fixtures[1].seed = UINT64_C(0xd1b54a32d192ed03);
    fixtures[1].run = run_mutant;
    make_fat(&fixtures[1], 0, thin, thin_len);
    fixtures[2].name = "fat64";
    fixtures[2].seed = UINT64_C(0x8cb92ba72f3d8dd7);
    fixtures[2].run = run_mutant;
    make_fat(&fixtures[2], 1, thin, thin_len);
    fixtures[3].name = "exports-trie";
    fixtures[3].seed = UINT64_C(0xbf58476d1ce4e5b9);
    fixtures[3].run = run_exports;
    fixtures[3].size = make_exports(fixtures[3].bytes, CMD_EXPORTS_TRIE,
                                    base_trie, sizeof(base_trie));
    fixtures[4].name = "exports-dyld-info";
    fixtures[4].seed = UINT64_C(0x94d049bb133111eb);
    fixtures[4].run = run_exports;
    fixtures[4].size = make_exports(fixtures[4].bytes, CMD_DYLD_INFO_ONLY,
                                    base_trie, sizeof(base_trie));

    for (size_t i = 0; i < 3u; ++i)
        check_baseline(&fixtures[i], thin_len);
    check_exports_baseline(&fixtures[3]);
    check_exports_baseline(&fixtures[4]);
    named_fat(&fixtures[1]);
    named_fat(&fixtures[2]);
    named_exports();
    for (size_t i = 0; i < COUNT(fixtures); ++i)
        sweep(&fixtures[i]);
    printf("parser mutation tests passed: %llu inventory calls, "
           "%llu exports calls\n",
           calls, export_calls);
    return 0;
}
