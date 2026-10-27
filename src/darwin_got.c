#include "ios27/darwin_got.h"
#include "ios27/macho.h"

#include <stdio.h>
#include <string.h>

#define MH_OBJECT 1u
#define LC_SEGMENT_64 0x19u
#define LC_SYMTAB 2u
#define LC_BUILD_VERSION 0x32u
#define PLATFORM_MACOS 1u
#define N_STAB 0xe0u
#define N_TYPE 0x0eu
#define N_EXT 1u
#define N_UNDF 0u
#define N_SECT 0x0eu
#define ARM64_RELOC_GOT_LOAD_PAGE21 5u
#define ARM64_RELOC_GOT_LOAD_PAGEOFF12 6u
#define PURE_INSTRUCTIONS 0x80000000u
#define MAX_FILE (1024u * 1024u)
#define MAX_SECTION 16384u
#define MAX_SYMS 512u
#define ADRP_X8 UINT32_C(0x90000008)
#define LDR_X8_GOT UINT32_C(0xf9400108)

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32u);
}
static void wr32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(n >> (8u * i));
}
static int fits(uint64_t off, uint64_t len, size_t cap) {
    return off <= (uint64_t)cap && len <= (uint64_t)cap - off;
}
static int fail(char *error, size_t cap, const char *message) {
    if (error && cap) (void)snprintf(error, cap, "%s", message);
    return -1;
}
static int named(const uint8_t *p, const char *name) {
    size_t n = strlen(name);
    return n < 16u && memcmp(p, name, n) == 0 && p[n] == 0;
}
static int relocation(const uint8_t *p, uint32_t addr, unsigned type,
                      uint32_t symbol, unsigned pcrel) {
    uint32_t bits = rd32(p + 4u);
    return rd32(p) == addr && !(addr & 0x80000000u) &&
           (bits & UINT32_C(0x00ffffff)) == symbol &&
           ((bits >> 24u) & 1u) == pcrel &&
           ((bits >> 25u) & 3u) == 2u &&
           ((bits >> 27u) & 1u) == 1u &&
           (bits >> 28u) == type;
}
static int common(const uint8_t *file, size_t size,
                  ios27_macho_info *info, char *error, size_t cap) {
    if (!file || size < 32u || size > MAX_FILE)
        return fail(error, cap, "null, truncated or oversized Darwin object");
    if (ios27_macho_parse(file, size, info, error, cap)) return -1;
    if (info->filetype != MH_OBJECT || (info->cpusubtype & 0xffu) != 0u ||
        info->has_main || info->has_dylibs || info->needs_dyld ||
        info->encrypted || info->has_fixups || info->platform != PLATFORM_MACOS)
        return fail(error, cap, "requires SDK-free ARM64-all macOS MH_OBJECT");
    return 0;
}
/* All symbol records must be bounded and any referenced external must be
 * exactly the expected name, section and index. Reject all other imports. */
static int symbols(const uint8_t *file, size_t size,
                   uint32_t symoff, uint32_t nsyms,
                   uint32_t stroff, uint32_t strsize,
                   uint32_t sections, uint32_t wanted_section,
                   uint64_t wanted_addr, uint64_t wanted_size,
                   const char *definition, const char *undefined,
                   uint32_t undefined_index, uint64_t *entry,
                   char *error, size_t cap) {
    unsigned found_def = 0u, found_undef = 0u;
    if (!nsyms || nsyms > MAX_SYMS || !strsize ||
        !fits(symoff, (uint64_t)nsyms * 16u, size) ||
        !fits(stroff, strsize, size))
        return fail(error, cap, "invalid object symbol table");
    for (uint32_t i = 0u; i < nsyms; ++i) {
        const uint8_t *p = file + (size_t)symoff + (size_t)i * 16u;
        uint32_t strx = rd32(p);
        uint8_t type = p[4u], section = p[5u];
        uint64_t value = rd64(p + 8u);
        const char *name;
        if (strx >= strsize ||
            !memchr(file + (size_t)stroff + strx, 0, strsize - strx))
            return fail(error, cap, "unterminated symbol");
        name = (const char *)(file + (size_t)stroff + strx);
        if ((type & N_STAB) != 0u) continue;
        if ((type & N_TYPE) == N_UNDF && section == 0u &&
            (type & N_EXT) && value == 0u && undefined) {
            if (i != undefined_index || found_undef++ ||
                strcmp(name, undefined) != 0)
                return fail(error, cap, "unknown external import");
        } else if ((type & N_TYPE) == N_SECT &&
                   section > 0u && section <= sections) {
            if (strcmp(name, definition) == 0) {
                if (!(type & N_EXT) || section != wanted_section ||
                    found_def++ || value < wanted_addr ||
                    value - wanted_addr > wanted_size)
                    return fail(error, cap, "bad external definition");
                *entry = value - wanted_addr;
            } else if ((type & N_EXT) != 0u) {
                return fail(error, cap, "unexpected exported symbol");
            }
        } else {
            return fail(error, cap, "unsupported symbol");
        }
    }
    if (found_def != 1u || (undefined ? found_undef != 1u : found_undef != 0u))
        return fail(error, cap, "required symbol missing");
    return 0;
}

int ios27_darwin_got_inspect(const uint8_t *file, size_t size,
                            ios27_darwin_got_plan *out,
                            char *error, size_t cap) {
    ios27_macho_info info;
    uint32_t symoff = 0u, nsyms = 0u, stroff = 0u, strsize = 0u;
    uint32_t count = 0u, text_section = 0u, adrp = 0u, ldr = 0u;
    uint32_t symbol_index = UINT32_MAX;
    uint64_t text_addr = 0u, text_off = 0u, text_size = 0u, entry = 0u;
    unsigned seen_text = 0u, seen_sym = 0u, seen_build = 0u;
    size_t cursor = 32u;
    if (!out) return fail(error, cap, "null relocation plan");
    memset(out, 0, sizeof(*out));
    if (common(file, size, &info, error, cap)) return -1;
    for (uint32_t i = 0u; i < rd32(file + 16u); ++i) {
        const uint8_t *cmd = file + cursor;
        uint32_t op = rd32(cmd), cs = rd32(cmd + 4u);
        if (op == LC_SEGMENT_64) {
            uint32_t nsects = rd32(cmd + 64u);
            if (cs < 72u || nsects > (cs - 72u) / 80u ||
                nsects > UINT32_MAX - count)
                return fail(error, cap, "invalid section count");
            for (uint32_t j = 0u; j < nsects; ++j) {
                const uint8_t *s = cmd + 72u + (size_t)j * 80u;
                uint64_t len = rd64(s + 40u);
                uint32_t nreloc = rd32(s + 60u);
                ++count;
                if (named(s, "__text") && named(s + 16u, "__TEXT")) {
                    uint32_t roff = rd32(s + 56u);
                    uint32_t first_sym, second_sym;
                    const uint8_t *r;
                    if (seen_text++ || !len || len > MAX_SECTION ||
                        !fits(rd32(s + 48u), len, size) ||
                        !fits(roff, 16u, size) || nreloc != 2u ||
                        (rd32(s + 64u) & PURE_INSTRUCTIONS) == 0u ||
                        (rd32(s + 64u) & 0xffu) != 0u)
                        return fail(error, cap, "invalid text or GOT relocations");
                    r = file + roff;
                    /* Clang emits PAGEOFF12 first, then PAGE21. */
                    ldr = rd32(r);
                    adrp = rd32(r + 8u);
                    first_sym = rd32(r + 4u) & UINT32_C(0x00ffffff);
                    second_sym = rd32(r + 12u) & UINT32_C(0x00ffffff);
                    if (first_sym != second_sym ||
                        !relocation(r, ldr, ARM64_RELOC_GOT_LOAD_PAGEOFF12,
                                    first_sym, 0u) ||
                        !relocation(r + 8u, adrp, ARM64_RELOC_GOT_LOAD_PAGE21,
                                    first_sym, 1u) ||
                        (adrp & 3u) || ldr != adrp + 4u ||
                        ldr > len || len - ldr < 4u ||
                        adrp > len || len - adrp < 4u ||
                        rd32(file + rd32(s + 48u) + adrp) != ADRP_X8 ||
                        rd32(file + rd32(s + 48u) + ldr) != LDR_X8_GOT)
                        return fail(error, cap, "unknown GOT relocation pair");
                    symbol_index = first_sym;
                    text_section = count;
                    text_addr = rd64(s + 32u);
                    text_off = rd32(s + 48u);
                    text_size = len;
                } else if (nreloc || len) {
                    return fail(error, cap, "unsupported secondary section");
                }
            }
        } else if (op == LC_SYMTAB) {
            if (cs < 24u || seen_sym++)
                return fail(error, cap, "invalid LC_SYMTAB");
            symoff = rd32(cmd + 8u); nsyms = rd32(cmd + 12u);
            stroff = rd32(cmd + 16u); strsize = rd32(cmd + 20u);
        } else if (op == LC_BUILD_VERSION) {
            if (cs < 24u || seen_build++)
                return fail(error, cap, "invalid LC_BUILD_VERSION");
        }
        cursor += cs;
    }
    if (seen_text != 1u || seen_sym != 1u || seen_build != 1u ||
        symbol_index >= nsyms ||
        symbols(file, size, symoff, nsyms, stroff, strsize,
                count, text_section, text_addr, text_size,
                "_fixture_read_data", "_fixture_data", symbol_index, &entry,
                error, cap))
        return fail(error, cap, "invalid consumer symbol metadata");
    if ((entry & 3u) || entry >= text_size)
        return fail(error, cap, "misaligned consumer entry");
    out->text_file_offset = text_off;
    out->text_size = text_size;
    out->entry_offset = entry;
    out->adrp_offset = adrp;
    out->ldr_offset = ldr;
    if (error && cap) error[0] = 0;
    return 0;
}

int ios27_darwin_const_inspect(const uint8_t *file, size_t size,
                              ios27_darwin_const_plan *out,
                              char *error, size_t cap) {
    ios27_macho_info info;
    uint32_t symoff = 0u, nsyms = 0u, stroff = 0u, strsize = 0u;
    uint32_t count = 0u, const_section = 0u;
    uint64_t addr = 0u, off = 0u, entry = 0u;
    unsigned seen_const = 0u, seen_sym = 0u, seen_build = 0u;
    size_t cursor = 32u;
    if (!out) return fail(error, cap, "null const plan");
    memset(out, 0, sizeof(*out));
    if (common(file, size, &info, error, cap)) return -1;
    for (uint32_t i = 0u; i < rd32(file + 16u); ++i) {
        const uint8_t *cmd = file + cursor;
        uint32_t op = rd32(cmd), cs = rd32(cmd + 4u);
        if (op == LC_SEGMENT_64) {
            uint32_t nsects = rd32(cmd + 64u);
            if (cs < 72u || nsects > (cs - 72u) / 80u ||
                nsects > UINT32_MAX - count)
                return fail(error, cap, "invalid const sections");
            for (uint32_t j = 0u; j < nsects; ++j) {
                const uint8_t *s = cmd + 72u + (size_t)j * 80u;
                uint64_t len = rd64(s + 40u);
                ++count;
                if (rd32(s + 60u))
                    return fail(error, cap, "provider relocations unsupported");
                if (named(s, "__const") && named(s + 16u, "__TEXT")) {
                    if (seen_const++ || len != 4u ||
                        (rd32(s + 64u) & 0xffu) != 0u ||
                        !fits(rd32(s + 48u), len, size))
                        return fail(error, cap, "invalid const section");
                    const_section = count;
                    addr = rd64(s + 32u);
                    off = rd32(s + 48u);
                } else if (len || !named(s, "__text") ||
                           !named(s + 16u, "__TEXT")) {
                    return fail(error, cap, "unexpected provider section");
                }
            }
        } else if (op == LC_SYMTAB) {
            if (cs < 24u || seen_sym++)
                return fail(error, cap, "invalid provider symtab");
            symoff = rd32(cmd + 8u); nsyms = rd32(cmd + 12u);
            stroff = rd32(cmd + 16u); strsize = rd32(cmd + 20u);
        } else if (op == LC_BUILD_VERSION) {
            if (cs < 24u || seen_build++)
                return fail(error, cap, "invalid provider version");
        }
        cursor += cs;
    }
    if (seen_const != 1u || seen_sym != 1u || seen_build != 1u ||
        symbols(file, size, symoff, nsyms, stroff, strsize,
                count, const_section, addr, 4u,
                "_fixture_data", NULL, 0u, &entry, error, cap) ||
        entry != 0u)
        return fail(error, cap, "invalid data definition");
    out->data_file_offset = off;
    out->data_size = 4u;
    if (error && cap) error[0] = 0;
    return 0;
}

int ios27_darwin_got_apply(uint8_t *text, size_t size,
                           const ios27_darwin_got_plan *plan,
                           uintptr_t loaded, uintptr_t got,
                           char *error, size_t cap) {
    uint64_t pcpage, targetpage, distance;
    uint32_t signed21, adrp_word, ldr_word;
    uintptr_t pc;
    if (!text || !plan || !loaded || !got ||
        plan->text_size == 0u || plan->text_size > size ||
        (plan->adrp_offset & 3u) || (plan->ldr_offset & 3u) ||
        plan->adrp_offset > plan->text_size ||
        plan->text_size - plan->adrp_offset < 4u ||
        plan->ldr_offset != plan->adrp_offset + 4u ||
        plan->ldr_offset > plan->text_size ||
        plan->text_size - plan->ldr_offset < 4u ||
        (loaded & 3u) || (got & 7u) ||
        plan->adrp_offset > UINTPTR_MAX - loaded ||
        rd32(text + plan->adrp_offset) != ADRP_X8 ||
        rd32(text + plan->ldr_offset) != LDR_X8_GOT)
        return fail(error, cap, "invalid relocation request or mutated text");
    pc = loaded + plan->adrp_offset;
    pcpage = (uint64_t)(pc >> 12u);
    targetpage = (uint64_t)(got >> 12u);
    if (targetpage >= pcpage) {
        distance = targetpage - pcpage;
        if (distance > UINT64_C(1048575))
            return fail(error, cap, "GOT page beyond +4GiB ADRP reach");
        signed21 = (uint32_t)distance;
    } else {
        distance = pcpage - targetpage;
        if (distance > UINT64_C(1048576))
            return fail(error, cap, "GOT page beyond -4GiB ADRP reach");
        signed21 = (UINT32_C(0x200000) - (uint32_t)distance) &
                   UINT32_C(0x1fffff);
    }
    adrp_word = ADRP_X8 |
                ((signed21 & 3u) << 29u) |
                (((signed21 >> 2u) & UINT32_C(0x7ffff)) << 5u);
    ldr_word = LDR_X8_GOT | (((uint32_t)(got & 4095u) >> 3u) << 10u);
    /* Both stores occur only after every validation and range check. */
    wr32(text + plan->adrp_offset, adrp_word);
    wr32(text + plan->ldr_offset, ldr_word);
    if (error && cap) error[0] = 0;
    return 0;
}
