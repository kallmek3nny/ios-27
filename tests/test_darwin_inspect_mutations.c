/*
 * N t-ffb060: deterministic, bounded mutation sweep over the fail-closed
 * Darwin MH_OBJECT inspectors ios27_darwin_object_inspect,
 * ios27_darwin_branch26_inspect, ios27_darwin_got_inspect and
 * ios27_darwin_const_inspect, driven by the four genuine SDK-free Clang
 * fixtures that CMake builds.
 *
 * Every mutant is copied into an exactly sized heap buffer, so an ASan
 * build reports any read past the end of the input, and every mutant of
 * every fixture goes to all four inspectors. Invariants for each call:
 *   - the return code is 0 or -1;
 *   - -1 leaves an error NUL-terminated within error_cap, and nonempty at
 *     the normal 256-byte capacity;
 *   - 0 yields a plan whose offsets all lie inside the mutant buffer;
 *   - the const input bytes are never modified.
 * Mutations: every prefix truncation; exhaustive single-bit flips and
 * boundary word and quad overwrites across the header and load commands;
 * named edits to magic, cputype, ncmds, sizeofcmds, cmdsize, segment and
 * section offsets, sizes and counts, symtab, nlist, string table and
 * relocation bitfields; and fixed-seed random bit, byte and stacked edits.
 * Nothing is mapped or executed, so the sweep runs on every host.
 */
#include "ios27/darwin_branch26.h"
#include "ios27/darwin_got.h"
#include "ios27/darwin_object.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_CAP 256u
#define SMALL_ERROR_CAP 8u
#define MAX_FIXTURE_BYTES (1L << 20)
#define RANDOM_FLIPS 2048u
#define RANDOM_BYTES 2048u
#define RANDOM_STACKED 512u
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

enum {
    INSPECT_OBJECT,
    INSPECT_BRANCH26,
    INSPECT_GOT,
    INSPECT_CONST,
    INSPECT_COUNT
};

static const char *const inspector_names[INSPECT_COUNT] = {
    "object_inspect", "branch26_inspect", "got_inspect", "const_inspect"
};

typedef struct {
    const char *name;
    uint8_t *bytes;
    size_t size;
    int own_inspector;
} fixture;

/* File offsets of the structures inside one genuine fixture. */
typedef struct {
    uint32_t sizeofcmds;
    uint32_t lc_count;
    size_t lc_off[64];
    uint32_t sect_count;
    size_t sect_off[64];
    uint32_t reloc_count;
    size_t reloc_off[256];
    int has_symtab;
    size_t symtab_off;
    uint32_t symoff, nsyms, stroff, strsize;
} layout;

static uint8_t *scratch;
static unsigned long long mutant_count;
static unsigned long long accepted[INSPECT_COUNT], rejected[INSPECT_COUNT];
static const char *ctx_fixture = "";
static const char *ctx_kind = "";
static size_t ctx_pos;
static unsigned long long ctx_value;
static uint64_t rng_state;

static void die(const char *what) {
    fprintf(stderr, "FAIL setup: %s\n", what);
    exit(1);
}

static void fail(int which, const char *what) {
    fprintf(stderr, "FAIL %s on %s mutant %s at %zu value 0x%llx: %s\n",
            inspector_names[which], ctx_fixture, ctx_kind, ctx_pos, ctx_value,
            what);
    exit(1);
}

static void set_ctx(const fixture *f, const char *kind, size_t pos,
                    unsigned long long value) {
    ctx_fixture = f->name;
    ctx_kind = kind;
    ctx_pos = pos;
    ctx_value = value;
}

static int range_ok(uint64_t offset, uint64_t length, uint64_t limit) {
    return offset <= limit && length <= limit - offset;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

/* xorshift64*: fixed seeds make every random mutant reproducible. */
static uint64_t next_random(void) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * UINT64_C(0x2545f4914f6cdd1d);
}

static void load(fixture *f, const char *name, const char *path, int own) {
    FILE *fp = fopen(path, "rb");
    long n;

    if (!fp) {
        perror(path);
        exit(1);
    }
    if (fseek(fp, 0, SEEK_END) != 0) die("cannot seek fixture");
    n = ftell(fp);
    if (n <= 0 || n > MAX_FIXTURE_BYTES || fseek(fp, 0, SEEK_SET) != 0)
        die("fixture size out of range");
    f->bytes = malloc((size_t)n);
    if (!f->bytes || fread(f->bytes, 1, (size_t)n, fp) != (size_t)n)
        die("cannot read fixture");
    fclose(fp);
    f->name = name;
    f->size = (size_t)n;
    f->own_inspector = own;
}

static void check_error(int which, int rc, const char *error, size_t cap) {
    if (rc != 0 && rc != -1) fail(which, "return code is neither 0 nor -1");
    if (rc == 0) return;
    if (!memchr(error, 0, cap))
        fail(which, "error is not NUL-terminated within error_cap");
    if (cap == ERROR_CAP && error[0] == 0)
        fail(which, "rejection left an empty error");
}

static void check_text(int which, uint64_t text_off, uint64_t text_size,
                       uint64_t entry, size_t size) {
    if (!range_ok(text_off, text_size, size))
        fail(which, "accepted text range lies outside the input");
    if (entry >= text_size)
        fail(which, "accepted entry offset lies outside the text");
}

static void check_insn(int which, uint64_t offset, uint64_t text_size,
                       const char *what) {
    if (!range_ok(offset, 4u, text_size)) fail(which, what);
}

/* One inspector call plus its plan invariants. Returns the inspector rc. */
static int inspect_one(int which, const uint8_t *input, size_t size,
                       char *error, size_t cap) {
    int rc = -1;

    memset(error, 'Z', cap);
    switch (which) {
    case INSPECT_OBJECT: {
        ios27_darwin_object out;
        memset(&out, 0, sizeof out);
        rc = ios27_darwin_object_inspect(input, size, &out, error, cap);
        check_error(which, rc, error, cap);
        if (rc == 0)
            check_text(which, out.text_file_offset, out.text_size,
                       out.entry_offset, size);
        break;
    }
    case INSPECT_BRANCH26: {
        ios27_darwin_branch26_plan out;
        memset(&out, 0, sizeof out);
        rc = ios27_darwin_branch26_inspect(input, size, &out, error, cap);
        check_error(which, rc, error, cap);
        if (rc == 0) {
            check_text(which, out.text_file_offset, out.text_size,
                       out.entry_offset, size);
            check_insn(which, out.branch_offset, out.text_size,
                       "accepted BL offset lies outside the text");
        }
        break;
    }
    case INSPECT_GOT: {
        ios27_darwin_got_plan out;
        memset(&out, 0, sizeof out);
        rc = ios27_darwin_got_inspect(input, size, &out, error, cap);
        check_error(which, rc, error, cap);
        if (rc == 0) {
            check_text(which, out.text_file_offset, out.text_size,
                       out.entry_offset, size);
            check_insn(which, out.adrp_offset, out.text_size,
                       "accepted ADRP offset lies outside the text");
            check_insn(which, out.ldr_offset, out.text_size,
                       "accepted LDR offset lies outside the text");
        }
        break;
    }
    default: {
        ios27_darwin_const_plan out;
        memset(&out, 0, sizeof out);
        rc = ios27_darwin_const_inspect(input, size, &out, error, cap);
        check_error(which, rc, error, cap);
        if (rc == 0 && !range_ok(out.data_file_offset, out.data_size, size))
            fail(which, "accepted data range lies outside the input");
        break;
    }
    }
    return rc;
}

/* Feed one mutant, as an exactly sized heap copy, to all four inspectors. */
static void run_all(const uint8_t *mutant, size_t size, size_t cap) {
    uint8_t *input = malloc(size);
    char *error = malloc(cap);
    int which;

    if (!input) input = malloc(1);
    if (!input || !error) die("out of memory");
    if (size) memcpy(input, mutant, size);
    for (which = 0; which < INSPECT_COUNT; ++which) {
        const int rc = inspect_one(which, input, size, error, cap);
        if (size && memcmp(input, mutant, size) != 0)
            fail(which, "inspector modified its const input");
        if (rc == 0)
            ++accepted[which];
        else
            ++rejected[which];
    }
    ++mutant_count;
    free(error);
    free(input);
}

static void check_baseline(const fixture *f) {
    char error[ERROR_CAP];
    int rc;

    set_ctx(f, "baseline", 0u, 0u);
    rc = inspect_one(f->own_inspector, f->bytes, f->size, error, sizeof error);
    if (rc != 0) {
        fprintf(stderr, "FAIL baseline: %s rejected genuine %s: %s\n",
                inspector_names[f->own_inspector], f->name, error);
        exit(1);
    }
}

/* Walk a genuine build output strictly; any surprise aborts the test. */
static void parse_layout(const fixture *f, layout *l) {
    const uint8_t *b = f->bytes;
    const size_t size = f->size;
    size_t at = 32u, end;
    uint32_t ncmds, i, j, k;

    memset(l, 0, sizeof *l);
    if (size < 32u || rd32(b) != 0xfeedfacfu) die("fixture is not MH_MAGIC_64");
    ncmds = rd32(b + 16);
    l->sizeofcmds = rd32(b + 20);
    if (!range_ok(32u, l->sizeofcmds, size) || ncmds == 0u || ncmds > 64u)
        die("fixture load commands out of range");
    end = 32u + (size_t)l->sizeofcmds;
    for (i = 0; i < ncmds; ++i) {
        uint32_t cmd, cmdsize;

        if (!range_ok(at, 8u, end)) die("fixture load command truncated");
        cmd = rd32(b + at);
        cmdsize = rd32(b + at + 4);
        if (cmdsize < 8u || !range_ok(at, cmdsize, end))
            die("fixture cmdsize out of range");
        l->lc_off[l->lc_count++] = at;
        if (cmd == 0x19u) { /* LC_SEGMENT_64 */
            uint32_t nsects;

            if (cmdsize < 72u) die("fixture segment truncated");
            nsects = rd32(b + at + 64);
            if (nsects > (cmdsize - 72u) / 80u || nsects > 64u - l->sect_count)
                die("fixture section count out of range");
            for (j = 0; j < nsects; ++j) {
                const size_t s = at + 72u + (size_t)j * 80u;
                const uint32_t reloff = rd32(b + s + 56);
                const uint32_t nreloc = rd32(b + s + 60);

                l->sect_off[l->sect_count++] = s;
                if (nreloc > 256u - l->reloc_count ||
                    !range_ok(reloff, (uint64_t)nreloc * 8u, size))
                    die("fixture relocations out of range");
                for (k = 0; k < nreloc; ++k)
                    l->reloc_off[l->reloc_count++] =
                        (size_t)reloff + (size_t)k * 8u;
            }
        } else if (cmd == 0x2u) { /* LC_SYMTAB */
            if (cmdsize < 24u || l->has_symtab) die("fixture symtab malformed");
            l->has_symtab = 1;
            l->symtab_off = at;
            l->symoff = rd32(b + at + 8);
            l->nsyms = rd32(b + at + 12);
            l->stroff = rd32(b + at + 16);
            l->strsize = rd32(b + at + 20);
            if (!range_ok(l->symoff, (uint64_t)l->nsyms * 16u, size) ||
                !range_ok(l->stroff, l->strsize, size))
                die("fixture symbol table out of range");
        }
        at += cmdsize;
    }
}

static void run_scratch(const fixture *f, const char *kind, size_t pos,
                        unsigned long long value) {
    set_ctx(f, kind, pos, value);
    run_all(scratch, f->size, ERROR_CAP);
}

static void put8(const fixture *f, const char *kind, size_t off,
                 uint8_t value) {
    if (off >= f->size) return;
    memcpy(scratch, f->bytes, f->size);
    scratch[off] = value;
    run_scratch(f, kind, off, value);
}

static void put32(const fixture *f, const char *kind, size_t off,
                  uint32_t value) {
    if (!range_ok(off, 4u, f->size)) return;
    memcpy(scratch, f->bytes, f->size);
    wr32(scratch + off, value);
    run_scratch(f, kind, off, value);
}

static void put64(const fixture *f, const char *kind, size_t off,
                  uint64_t value) {
    if (!range_ok(off, 8u, f->size)) return;
    memcpy(scratch, f->bytes, f->size);
    wr64(scratch + off, value);
    run_scratch(f, kind, off, value);
}

static void fill(const fixture *f, const char *kind, size_t off, size_t len,
                 uint8_t value) {
    if (!range_ok(off, len, f->size)) return;
    memcpy(scratch, f->bytes, f->size);
    memset(scratch + off, value, len);
    run_scratch(f, kind, off, value);
}

static void put32_list(const fixture *f, const char *kind, size_t off,
                       const uint32_t *values, size_t count) {
    size_t i;

    for (i = 0; i < count; ++i) put32(f, kind, off, values[i]);
}

static void put64_list(const fixture *f, const char *kind, size_t off,
                       const uint64_t *values, size_t count) {
    size_t i;

    for (i = 0; i < count; ++i) put64(f, kind, off, values[i]);
}

/* relocation_info word: r_symbolnum:24 at bit 0, r_pcrel:1 at 24,
 * r_length:2 at 25, r_extern:1 at 27 and r_type:4 at 28. */
static void put_bits(const fixture *f, const char *kind, size_t off,
                     unsigned shift, uint32_t mask, uint32_t value) {
    uint32_t word = rd32(f->bytes + off);

    word = (word & ~(mask << shift)) | ((value & mask) << shift);
    put32(f, kind, off, word);
}

static void sweep_prefixes(const fixture *f) {
    size_t len;

    for (len = 0; len <= f->size; ++len) {
        set_ctx(f, "prefix", len, len);
        run_all(f->bytes, len, ERROR_CAP);
        run_all(f->bytes, len, SMALL_ERROR_CAP);
    }
}

static void sweep_command_bits(const fixture *f, const layout *l) {
    const size_t end = 32u + (size_t)l->sizeofcmds;
    size_t i;
    unsigned bit;

    for (i = 0; i < end; ++i) {
        for (bit = 0; bit < 8u; ++bit) {
            memcpy(scratch, f->bytes, f->size);
            scratch[i] ^= (uint8_t)(1u << bit);
            run_scratch(f, "command-bit", i, bit);
        }
    }
}

static void sweep_command_words(const fixture *f, const layout *l) {
    const uint32_t size32 = (uint32_t)f->size;
    const uint32_t words[] = {0u, 1u, 3u, 8u, 0x7fffffffu, 0x80000000u,
                              0xffffffffu, size32 - 1u, size32, size32 + 1u,
                              size32 / 2u};
    const uint64_t quads[] = {UINT64_MAX, UINT64_C(1) << 63,
                              (uint64_t)f->size + 1u};
    const size_t end = 32u + (size_t)l->sizeofcmds;
    size_t off;

    for (off = 0; off + 4u <= end; off += 4u)
        put32_list(f, "command-word", off, words, COUNT(words));
    for (off = 0; off + 8u <= end; off += 8u)
        put64_list(f, "command-quad", off, quads, COUNT(quads));
}

static void sweep_header(const fixture *f, const layout *l) {
    static const uint32_t magics[] = {
        0xfeedfaceu, /* MH_MAGIC: 32-bit */
        0xcefaedfeu, /* big-endian 32-bit */
        0xcffaedfeu, /* big-endian 64-bit */
        0xbebafecau, /* on-disk fat header */
        0xbfbafecau, /* on-disk 64-bit fat header */
        0xcafebabeu, /* byte-swapped fat header */
        0u
    };
    static const uint32_t cputypes[] = {
        0x01000007u, /* x86_64 */
        0x0000000cu, /* 32-bit ARM */
        0x0200000cu, /* arm64_32 */
        0u, 0xffffffffu
    };
    static const uint32_t subtypes[] = {1u, 2u /* arm64e */, 0x80000002u,
                                        0xffffffffu};
    static const uint32_t filetypes[] = {0u, 2u /* MH_EXECUTE */,
                                         6u /* MH_DYLIB */, 0xffffffffu};
    const uint32_t size32 = (uint32_t)f->size, cmds = l->sizeofcmds;
    const uint32_t ncmds[] = {0u, l->lc_count - 1u, l->lc_count + 1u,
                              0x80000000u, 0xffffffffu};
    const uint32_t sizes[] = {0u, cmds - 8u, cmds - 4u, cmds - 1u,
                              cmds + 1u, cmds + 4u, cmds + 8u, size32 - 32u,
                              size32 - 31u, size32, 0x80000000u, 0xffffffffu};

    put32_list(f, "magic", 0u, magics, COUNT(magics));
    put32_list(f, "cputype", 4u, cputypes, COUNT(cputypes));
    put32_list(f, "cpusubtype", 8u, subtypes, COUNT(subtypes));
    put32_list(f, "filetype", 12u, filetypes, COUNT(filetypes));
    put32_list(f, "ncmds", 16u, ncmds, COUNT(ncmds));
    put32_list(f, "sizeofcmds", 20u, sizes, COUNT(sizes));
}

static void sweep_commands(const fixture *f, const layout *l) {
    const uint8_t *b = f->bytes;
    const uint32_t cmds = l->sizeofcmds;
    uint32_t i;

    for (i = 0; i < l->lc_count; ++i) {
        const size_t lc = l->lc_off[i];
        const uint32_t cmd = rd32(b + lc), cmdsize = rd32(b + lc + 4);
        const uint32_t cmd_values[] = {0u, 0x1u /* LC_SEGMENT */,
                                       0x80000028u /* LC_MAIN */,
                                       cmd ^ 0x80000000u, 0xffffffffu};
        const uint32_t size_values[] = {0u, 1u, 4u, 7u, cmdsize - 8u,
                                        cmdsize - 1u, cmdsize + 1u,
                                        cmdsize + 4u, cmdsize + 8u, cmds,
                                        cmds + 8u, 0xffffffffu};

        put32_list(f, "lc.cmd", lc, cmd_values, COUNT(cmd_values));
        put32_list(f, "lc.cmdsize", lc + 4u, size_values, COUNT(size_values));
        if (cmd == 0x19u) { /* LC_SEGMENT_64 */
            const uint32_t nsects = rd32(b + lc + 64);
            const uint32_t nsect_values[] = {0u, nsects + 1u, nsects + 2u,
                                             0x10000000u, 0xffffffffu};
            const uint64_t spans[] = {(uint64_t)f->size,
                                      (uint64_t)f->size + 1u, UINT64_MAX};

            put32_list(f, "segment.nsects", lc + 64u, nsect_values,
                       COUNT(nsect_values));
            put64_list(f, "segment.vmsize", lc + 32u, spans, COUNT(spans));
            put64_list(f, "segment.fileoff", lc + 40u, spans, COUNT(spans));
            put64_list(f, "segment.filesize", lc + 48u, spans, COUNT(spans));
        } else if (cmd == 0x32u && cmdsize >= 24u) { /* LC_BUILD_VERSION */
            const uint32_t platforms[] = {0u, 2u /* iOS */,
                                          7u /* iOS simulator */, 0xffffffffu};
            const uint32_t ntools[] = {rd32(b + lc + 20) + 1u, 0x10000000u,
                                       0xffffffffu};

            put32_list(f, "build.platform", lc + 8u, platforms,
                       COUNT(platforms));
            put32_list(f, "build.ntools", lc + 20u, ntools, COUNT(ntools));
        }
    }
}

static void sweep_sections(const fixture *f, const layout *l) {
    const uint8_t *b = f->bytes;
    const uint32_t size32 = (uint32_t)f->size;
    uint32_t i;

    for (i = 0; i < l->sect_count; ++i) {
        const size_t s = l->sect_off[i];
        const uint32_t offset = rd32(b + s + 48);
        const uint32_t reloff = rd32(b + s + 56);
        const uint32_t nreloc = rd32(b + s + 60);
        const uint64_t past_eof =
            offset <= f->size ? (uint64_t)(f->size - offset) + 1u : 1u;
        const uint64_t addrs[] = {UINT64_MAX, UINT64_MAX - 3u};
        const uint64_t sizes[] = {0u, past_eof, (uint64_t)f->size,
                                  UINT64_C(1) << 63, UINT64_MAX};
        const uint32_t offsets[] = {0u, offset + 1u, offset + 4u,
                                    offset + 4096u, size32 - 1u, size32,
                                    0xffffffffu};
        const uint32_t aligns[] = {31u, 32u, 64u, 0xffffffffu};
        const uint32_t reloffs[] = {0u, offset, reloff + 1u, reloff + 4u,
                                    size32 - 4u, size32, 0xffffffffu};
        const uint32_t nrelocs[] = {0u, nreloc + 1u, nreloc + 2u,
                                    0x10000000u, 0x20000000u, 0xffffffffu};
        const uint32_t flags[] = {0u, 0xffffffffu};

        put64_list(f, "section.addr", s + 32u, addrs, COUNT(addrs));
        put64_list(f, "section.size", s + 40u, sizes, COUNT(sizes));
        put32_list(f, "section.offset", s + 48u, offsets, COUNT(offsets));
        put32_list(f, "section.align", s + 52u, aligns, COUNT(aligns));
        put32_list(f, "section.reloff", s + 56u, reloffs, COUNT(reloffs));
        put32_list(f, "section.nreloc", s + 60u, nrelocs, COUNT(nrelocs));
        put32_list(f, "section.flags", s + 64u, flags, COUNT(flags));
        fill(f, "section.sectname", s, 16u, 0xffu);
        fill(f, "section.segname", s + 16u, 16u, 0x41u);
    }
}

static void sweep_symbols(const fixture *f, const layout *l) {
    static const uint8_t types[] = {0x00u, 0x01u, 0x0eu, 0x0fu, 0x1eu,
                                    0xe0u, 0xffu};
    const uint32_t size32 = (uint32_t)f->size;
    const size_t st = l->symtab_off;
    uint32_t i;
    size_t k;

    if (!l->has_symtab) return;
    {
        const uint32_t symoffs[] = {0u, l->symoff + 1u, l->symoff + 8u,
                                    l->stroff, size32 - 8u, size32,
                                    0xffffffffu};
        const uint32_t nsyms[] = {0u, l->nsyms + 1u, 0x10000000u,
                                  0x10000001u, 0xffffffffu};
        const uint32_t stroffs[] = {0u, l->stroff + l->strsize, size32,
                                    0xffffffffu};
        const uint32_t strsizes[] = {0u, 1u, l->strsize + 1u, size32,
                                     0xffffffffu};

        put32_list(f, "symtab.symoff", st + 8u, symoffs, COUNT(symoffs));
        put32_list(f, "symtab.nsyms", st + 12u, nsyms, COUNT(nsyms));
        put32_list(f, "symtab.stroff", st + 16u, stroffs, COUNT(stroffs));
        put32_list(f, "symtab.strsize", st + 20u, strsizes, COUNT(strsizes));
    }
    for (i = 0; i < l->nsyms; ++i) {
        const size_t e = (size_t)l->symoff + (size_t)i * 16u;
        const uint32_t strx[] = {l->strsize - 1u, l->strsize, l->strsize + 1u,
                                 0xffffffffu};
        const uint8_t sects[] = {0u, (uint8_t)(l->sect_count + 1u), 0xffu};
        const uint64_t values[] = {UINT64_MAX, UINT64_C(1) << 63};

        put32_list(f, "nlist.n_strx", e, strx, COUNT(strx));
        for (k = 0; k < COUNT(types); ++k)
            put8(f, "nlist.n_type", e + 4u, types[k]);
        for (k = 0; k < COUNT(sects); ++k)
            put8(f, "nlist.n_sect", e + 5u, sects[k]);
        put64_list(f, "nlist.n_value", e + 8u, values, COUNT(values));
    }
    if (l->strsize) {
        fill(f, "strtab.unterminated", l->stroff, l->strsize, 0x41u);
        put8(f, "strtab.last-byte", (size_t)l->stroff + l->strsize - 1u,
             0x41u);
    }
}

static void sweep_relocations(const fixture *f, const layout *l) {
    const uint8_t *b = f->bytes;
    uint32_t i, v;

    for (i = 0; i < l->reloc_count; ++i) {
        const size_t r = l->reloc_off[i];
        const uint32_t address = rd32(b + r);
        const uint32_t info = rd32(b + r + 4u);
        const uint32_t addresses[] = {0u, address + 1u, address + 2u,
                                      address + 4u, 0x7fffffffu, 0x80000000u,
                                      0xffffffffu};
        const uint32_t symbols[] = {l->nsyms, l->nsyms + 1u, 0xffffffu};

        put32_list(f, "reloc.r_address", r, addresses, COUNT(addresses));
        for (v = 0; v < COUNT(symbols); ++v)
            put_bits(f, "reloc.r_symbolnum", r + 4u, 0u, 0xffffffu, symbols[v]);
        put_bits(f, "reloc.r_pcrel", r + 4u, 24u, 1u, ((info >> 24) & 1u) ^ 1u);
        for (v = 0; v < 4u; ++v)
            put_bits(f, "reloc.r_length", r + 4u, 25u, 3u, v);
        put_bits(f, "reloc.r_extern", r + 4u, 27u, 1u, ((info >> 27) & 1u) ^ 1u);
        for (v = 0; v < 16u; ++v)
            put_bits(f, "reloc.r_type", r + 4u, 28u, 0xfu, v);
    }
}

static void sweep_random(const fixture *f, uint64_t seed) {
    static const uint8_t interesting[] = {0x00u, 0x01u, 0x02u, 0x7fu, 0x80u,
                                          0xfeu, 0xffu};
    unsigned i, j;

    rng_state = seed;
    for (i = 0; i < RANDOM_FLIPS; ++i) {
        const uint64_t r = next_random();
        const size_t pos = (size_t)(r % f->size);
        const unsigned bit = (unsigned)((r >> 40) & 7u);

        memcpy(scratch, f->bytes, f->size);
        scratch[pos] ^= (uint8_t)(1u << bit);
        run_scratch(f, "random-bit", pos, bit);
    }
    for (i = 0; i < RANDOM_BYTES; ++i) {
        const uint64_t r = next_random();
        const size_t pos = (size_t)(r % f->size);
        const size_t pick = (size_t)((r >> 32) % (COUNT(interesting) + 1u));
        const uint8_t value =
            pick < COUNT(interesting) ? interesting[pick] : (uint8_t)(r >> 56);

        memcpy(scratch, f->bytes, f->size);
        scratch[pos] = value;
        run_scratch(f, "random-byte", pos, value);
    }
    for (i = 0; i < RANDOM_STACKED; ++i) {
        const unsigned edits = 2u + (unsigned)(next_random() % 7u);
        size_t first = SIZE_MAX;

        memcpy(scratch, f->bytes, f->size);
        for (j = 0; j < edits; ++j) {
            const uint64_t e = next_random();
            const size_t pos = (size_t)(e % f->size);

            if (first == SIZE_MAX) first = pos;
            scratch[pos] = (uint8_t)(e >> 56);
        }
        run_scratch(f, "random-stacked", first, i);
    }
}

int main(int argc, char **argv) {
    static const char *const names[INSPECT_COUNT] = {
        "arm64_darwin_fixture.o", "arm64_darwin_consumer.o",
        "arm64_darwin_data_consumer.o", "arm64_darwin_data_provider.o"
    };
    fixture fixtures[INSPECT_COUNT];
    size_t max_size = 0;
    int i;

    if (argc != 1 + INSPECT_COUNT) {
        fprintf(stderr,
                "usage: %s fixture.o consumer.o data_consumer.o data_provider.o\n",
                argc ? argv[0] : "test_darwin_inspect_mutations");
        return 2;
    }
    for (i = 0; i < INSPECT_COUNT; ++i) {
        load(&fixtures[i], names[i], argv[i + 1], i);
        if (fixtures[i].size > max_size) max_size = fixtures[i].size;
    }
    scratch = malloc(max_size);
    if (!scratch) die("out of memory");
    /* Each genuine fixture must pass the inspector it was compiled for. */
    for (i = 0; i < INSPECT_COUNT; ++i) check_baseline(&fixtures[i]);
    for (i = 0; i < INSPECT_COUNT; ++i) {
        const fixture *f = &fixtures[i];
        layout l;

        parse_layout(f, &l);
        sweep_prefixes(f);
        sweep_command_bits(f, &l);
        sweep_command_words(f, &l);
        sweep_header(f, &l);
        sweep_commands(f, &l);
        sweep_sections(f, &l);
        sweep_symbols(f, &l);
        sweep_relocations(f, &l);
        sweep_random(f, UINT64_C(0x6e5eedf060000001) +
                            (uint64_t)i * UINT64_C(0x9e3779b97f4a7c15));
    }
    printf("darwin inspector mutations: %llu mutants, each through %d inspectors\n",
           mutant_count, INSPECT_COUNT);
    for (i = 0; i < INSPECT_COUNT; ++i)
        printf("  %s: %llu accepted, %llu rejected\n", inspector_names[i],
               accepted[i], rejected[i]);
    puts("bounds, error and const-input invariants held; PASS");
    for (i = 0; i < INSPECT_COUNT; ++i) free(fixtures[i].bytes);
    free(scratch);
    return 0;
}
