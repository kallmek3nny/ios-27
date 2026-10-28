#include "ios27/darwin_branch26.h"
#include "ios27/macho.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MH_OBJECT 1u
#define CPU_SUBTYPE_ARM64_ALL 0u
#define LC_SEGMENT_64 0x19u
#define LC_SYMTAB 0x02u
#define LC_BUILD_VERSION 0x32u
#define PLATFORM_MACOS 1u
#define N_STAB 0xe0u
#define N_TYPE 0x0eu
#define N_EXT 0x01u
#define N_UNDF 0u
#define N_SECT 0x0eu
#define ARM64_RELOC_BRANCH26 2u
#define MAX_SYMBOLS 512u
#define MAX_TEXT 16384u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32u);
}
static void wr32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(v >> (8u * i));
}
static int fits(uint64_t offset, uint64_t bytes, size_t cap) {
    return offset <= (uint64_t)cap && bytes <= (uint64_t)cap - offset;
}
static int bad(char *error, size_t cap, const char *reason) {
    if (error && cap) (void)snprintf(error, cap, "%s", reason);
    return -1;
}

int ios27_darwin_branch26_inspect(const uint8_t *file, size_t size,
                                  ios27_darwin_branch26_plan *out,
                                  char *error, size_t error_cap) {
    ios27_macho_info info;
    size_t cursor = 32u;
    uint32_t sections_seen = 0u, text_section = 0u;
    uint32_t symoff = 0u, nsyms = 0u, stroff = 0u, strsize = 0u;
    uint32_t relocation_address = 0u, relocation_symbol = 0u;
    uint64_t text_addr = 0u, text_off = 0u, text_len = 0u;
    uint64_t entry_address = 0u;
    unsigned saw_symtab = 0u, saw_build = 0u, saw_text = 0u;
    unsigned saw_reloc = 0u, saw_entry = 0u, saw_import = 0u;
    uint32_t symbol_count, command_count;
    if (!file || !out || size < 32u || size > 1024u * 1024u)
        return bad(error, error_cap, "invalid or oversized Mach-O object");
    memset(out, 0, sizeof(*out));
    if (ios27_macho_parse(file, size, &info, error, error_cap))
        return -1;
    if (info.filetype != MH_OBJECT || (info.cpusubtype & 0xffu) !=
        CPU_SUBTYPE_ARM64_ALL || info.has_main || info.encrypted ||
        info.has_dylibs || info.needs_dyld || info.has_fixups)
        return bad(error, error_cap, "requires SDK-free ARM64-all MH_OBJECT");

    command_count = rd32(file + 16u);
    for (uint32_t i = 0u; i < command_count; ++i) {
        const uint8_t *cmd = file + cursor;
        uint32_t op = rd32(cmd);
        uint32_t cmdsize = rd32(cmd + 4u);
        /* The base Mach-O parser already verified each command bound. */
        if (op == LC_SEGMENT_64) {
            uint32_t nsects = rd32(cmd + 64u);
            if (cmdsize < 72u || nsects > (cmdsize - 72u) / 80u ||
                nsects > UINT32_MAX - sections_seen)
                return bad(error, error_cap, "invalid Mach-O section count");
            for (uint32_t j = 0u; j < nsects; ++j) {
                const uint8_t *s = cmd + 72u + (size_t)j * 80u;
                uint64_t section_addr = rd64(s + 32u);
                uint64_t section_len = rd64(s + 40u);
                uint32_t section_off = rd32(s + 48u);
                uint32_t reloc_off = rd32(s + 56u);
                uint32_t nreloc = rd32(s + 60u);
                ++sections_seen;
                if (memcmp(s, "__text\0", 7u) == 0 &&
                    memcmp(s + 16u, "__TEXT\0", 7u) == 0) {
                    const uint8_t *r;
                    uint32_t bits;
                    if (saw_text++ || !section_len ||
                        section_len > MAX_TEXT ||
                        section_len > UINT64_MAX - section_addr ||
                        !fits(section_off, section_len, size) || nreloc != 1u ||
                        !fits(reloc_off, 8u, size) ||
                        (rd32(s + 64u) & 0xffu) != 0u)
                        return bad(error, error_cap, "unsupported __text relocations");
                    text_section = sections_seen;
                    text_addr = section_addr;
                    text_off = section_off;
                    text_len = section_len;
                    r = file + reloc_off;
                    relocation_address = rd32(r);
                    bits = rd32(r + 4u);
                    /* r_address is a signed 32-bit offset. Disallow scattered
                     * or negative offsets; r_symbolnum is 24 bits. */
                    if ((relocation_address & 0x80000000u) ||
                        (bits >> 24u & 1u) != 1u ||
                        ((bits >> 25u) & 3u) != 2u ||
                        ((bits >> 27u) & 1u) != 1u ||
                        (bits >> 28u) != ARM64_RELOC_BRANCH26 ||
                        (relocation_address & 3u) ||
                        relocation_address > text_len ||
                        text_len - relocation_address < 4u ||
                        rd32(file + (size_t)text_off + relocation_address) !=
                        UINT32_C(0x94000000))
                        return bad(error, error_cap, "unsupported ARM64 branch relocation");
                    relocation_symbol = bits & UINT32_C(0x00ffffff);
                    saw_reloc = 1u;
                } else if (nreloc != 0u) {
                    return bad(error, error_cap, "other section relocations unsupported");
                }
            }
        } else if (op == LC_SYMTAB) {
            if (cmdsize < 24u || saw_symtab++)
                return bad(error, error_cap, "invalid or duplicate LC_SYMTAB");
            symoff = rd32(cmd + 8u);
            nsyms = rd32(cmd + 12u);
            stroff = rd32(cmd + 16u);
            strsize = rd32(cmd + 20u);
        } else if (op == LC_BUILD_VERSION) {
            if (cmdsize < 24u || saw_build++)
                return bad(error, error_cap, "invalid LC_BUILD_VERSION");
        }
        cursor += cmdsize;
    }
    if (!saw_reloc || !saw_symtab || !saw_build || info.platform != PLATFORM_MACOS ||
        !nsyms || nsyms > MAX_SYMBOLS || relocation_symbol >= nsyms ||
        !fits(symoff, (uint64_t)nsyms * 16u, size) ||
        !fits(stroff, strsize, size) || !strsize)
        return bad(error, error_cap, "missing valid Darwin object metadata");
    symbol_count = nsyms;
    for (uint32_t i = 0u; i < symbol_count; ++i) {
        const uint8_t *sym = file + (size_t)symoff + (size_t)i * 16u;
        uint32_t strx = rd32(sym);
        uint8_t kind = sym[4u], section = sym[5u];
        uint64_t value = rd64(sym + 8u);
        const char *name;
        if (strx >= strsize ||
            !memchr(file + (size_t)stroff + strx, '\0', strsize - strx))
            return bad(error, error_cap, "invalid string table symbol");
        name = (const char *)file + (size_t)stroff + strx;
        if ((kind & N_STAB) != 0u) continue;
        if (((kind & N_TYPE) == N_UNDF && section == 0u &&
             (kind & N_EXT) && value == 0u)) {
            if (i != relocation_symbol || saw_import++ ||
                strcmp(name, "_fixture_entry") != 0)
                return bad(error, error_cap, "unexpected undefined symbol");
        } else if ((kind & N_TYPE) == N_SECT && section > 0u &&
                   section <= sections_seen) {
            if (section == text_section && (kind & N_EXT) &&
                strcmp(name, "_fixture_consumer") == 0) {
                if (saw_entry++ || value < text_addr ||
                    value - text_addr > text_len ||
                    text_len - (value - text_addr) < 4u ||
                    ((value - text_addr) & 3u))
                    return bad(error, error_cap, "bad consumer entry");
                entry_address = value - text_addr;
            }
        } else {
            return bad(error, error_cap, "unsupported symbol definition");
        }
    }
    if (saw_entry != 1u || saw_import != 1u)
        return bad(error, error_cap, "missing consumer or external provider symbol");

    out->text_file_offset = text_off;
    out->text_size = text_len;
    out->entry_offset = entry_address;
    out->branch_offset = relocation_address;
    if (error && error_cap) error[0] = '\0';
    return 0;
}

int ios27_darwin_branch26_apply(uint8_t *text, size_t size,
                                const ios27_darwin_branch26_plan *plan,
                                uintptr_t loaded_address,
                                uintptr_t destination_address,
                                char *error, size_t cap) {
    uintptr_t pc;
    uint64_t displacement;
    uint32_t immediate;
    if (!text || !plan || !loaded_address || !destination_address ||
        !plan->text_size || plan->text_size > size ||
        plan->branch_offset > plan->text_size ||
        plan->text_size - plan->branch_offset < 4u ||
        (plan->branch_offset & 3u) ||
        plan->branch_offset > UINTPTR_MAX - loaded_address ||
        rd32(text + (size_t)plan->branch_offset) != UINT32_C(0x94000000))
        return bad(error, cap, "invalid or mutated BL relocation target");
    pc = loaded_address + (uintptr_t)plan->branch_offset;
    if ((pc & 3u) || (destination_address & 3u))
        return bad(error, cap, "unaligned BL address");
    if (destination_address >= pc) {
        displacement = destination_address - pc;
        if (displacement > UINT64_C(134217724))
            return bad(error, cap, "BL target out of 128MiB range");
        immediate = (uint32_t)(displacement / 4u);
    } else {
        displacement = pc - destination_address;
        if (displacement > UINT64_C(134217728))
            return bad(error, cap, "BL target out of 128MiB range");
        immediate = (UINT32_C(0x04000000) -
                     (uint32_t)(displacement / 4u)) &
                    UINT32_C(0x03ffffff);
    }
    /* Commit after all checks; no partial patch on failure. */
    wr32(text + (size_t)plan->branch_offset,
         UINT32_C(0x94000000) | immediate);
    if (error && cap) error[0] = '\0';
    return 0;
}


/* Phase 19d: a bounded ARM64 relocation transaction. This operates only on
 * caller-owned staging pages, never directly on executable mappings. */
#define RELOC_UNSIGNED 0u
#define RELOC_SUBTRACTOR 1u
#define RELOC_BRANCH26 2u
#define RELOC_PAGE21 3u
#define RELOC_PAGEOFF12 4u
#define RELOC_GOT_PAGE21 5u
#define RELOC_GOT_PAGEOFF12 6u
#define RELOC_POINTER_GOT 7u
#define RELOC_ADDEND 10u
#define RELOC_AUTH_POINTER 11u

static void write64(uint8_t *p, uint64_t value) {
    wr32(p, (uint32_t)value);
    wr32(p + 4u, (uint32_t)(value >> 32u));
}
static int shift_address(uint64_t base, int64_t delta, uint64_t *out) {
    if (delta >= 0) {
        if ((uint64_t)delta > UINT64_MAX - base) return -1;
        *out = base + (uint64_t)delta;
    } else {
        uint64_t magnitude = (uint64_t)(-(delta + 1)) + 1u;
        if (base < magnitude) return -1;
        *out = base - magnitude;
    }
    return 0;
}
static int sum_signed(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) ||
        (b < 0 && a < INT64_MIN - b)) return -1;
    *out = a + b;
    return 0;
}
static int64_t sext32(uint32_t n, unsigned bits) {
    uint32_t sign = UINT32_C(1) << (bits - 1u);
    return (int64_t)(n & (sign - 1u)) -
           (int64_t)(n & sign);
}
static int patch_instruction(uint8_t *p, unsigned type, uint64_t pc,
                             uint64_t target, char *error, size_t cap) {
    uint32_t w = rd32(p);
    uint64_t value, delta, imm;
    unsigned backwards, scale;
    if (type == RELOC_BRANCH26) {
        int64_t addend = sext32(w & 0x03ffffffu, 26u) * 4;
        if ((w & 0x7c000000u) != 0x14000000u ||
            shift_address(target, addend, &value) || ((value | pc) & 3u))
            return bad(error, cap, "invalid ARM64 branch opcode/addend");
        backwards = value < pc;
        delta = backwards ? pc - value : value - pc;
        if (delta > (backwards ? UINT64_C(134217728) :
                                   UINT64_C(134217724)))
            return bad(error, cap, "BRANCH26 signed displacement overflow");
        imm = delta >> 2u;
        if (backwards) imm = (UINT64_C(0x4000000) - imm) & 0x3ffffffu;
        wr32(p, (w & 0xfc000000u) | (uint32_t)imm);
    } else if (type == RELOC_PAGE21 || type == RELOC_GOT_PAGE21) {
        uint32_t old = ((w >> 29u) & 3u) |
                       (((w >> 5u) & 0x7ffffu) << 2u);
        if ((w & 0x9f000000u) != 0x90000000u ||
            (type == RELOC_GOT_PAGE21 && (target & 7u)) ||
            shift_address(target, sext32(old, 21u) * 4096, &value))
            return bad(error, cap, "invalid ADRP/GOT relocation");
        backwards = (value >> 12u) < (pc >> 12u);
        delta = backwards ? (pc >> 12u) - (value >> 12u) :
                            (value >> 12u) - (pc >> 12u);
        if (delta > (backwards ? UINT64_C(1048576) :
                                   UINT64_C(1048575)))
            return bad(error, cap, "ADRP signed 21-bit page overflow");
        imm = backwards ? (UINT64_C(0x200000) - delta) & 0x1fffffu : delta;
        wr32(p, (w & 0x9f00001fu) |
                (((uint32_t)imm & 3u) << 29u) |
                ((((uint32_t)imm >> 2u) & 0x7ffffu) << 5u));
    } else if (type == RELOC_PAGEOFF12 || type == RELOC_GOT_PAGEOFF12) {
        if (type == RELOC_GOT_PAGEOFF12) {
            if ((w & 0xffc00000u) != 0xf9400000u || (target & 7u))
                return bad(error, cap, "GOT PAGEOFF12 requires LDR X");
            scale = 3u;
        } else if ((w & 0xffc00000u) == 0x91000000u) {
            scale = 0u;
        } else if ((w & 0x3b000000u) == 0x39000000u &&
                   !(w & 0x04000000u)) {
            scale = (w >> 30u) & 3u;
        } else {
            return bad(error, cap, "unsupported PAGEOFF12 instruction");
        }
        delta = (target & 4095u) +
                ((uint64_t)((w >> 10u) & 0xfffu) << scale);
        if (delta > 4095u || (delta & ((1u << scale) - 1u)))
            return bad(error, cap, "PAGEOFF12 alignment or page overflow");
        wr32(p, (w & ~UINT32_C(0x003ffc00)) |
                ((uint32_t)(delta >> scale) << 10u));
    } else if (type == RELOC_POINTER_GOT) {
        backwards = target < pc;
        delta = backwards ? pc - target : target - pc;
        if (rd32(p) || (target & 7u) ||
            delta > (backwards ? UINT64_C(2147483648) :
                                   UINT64_C(2147483647)))
            return bad(error, cap, "POINTER_TO_GOT signed32 overflow");
        wr32(p, (uint32_t)(backwards ? 0u - delta : delta));
    } else {
        return bad(error, cap, "unsupported instruction relocation");
    }
    return 0;
}

int ios27_darwin_arm64_apply_batch(uint8_t *section, size_t size,
                                  uintptr_t loaded,
                                  const ios27_darwin_arm64_reloc *ops,
                                  size_t count, char *error, size_t cap) {
    uint8_t *staged, *used;
    int status = -1;
    if (!section || !loaded || !size || size > 16u * 1024u * 1024u ||
        count > 16384u || (count && !ops) ||
        (uint64_t)loaded > UINT64_MAX - (uint64_t)size)
        return bad(error, cap, "invalid relocation transaction");
    staged = malloc(size);
    used = calloc(size, 1u);
    if (!staged || !used) {
        free(staged); free(used);
        return bad(error, cap, "relocation staging allocation failed");
    }
    memcpy(staged, section, size);
    for (size_t i = 0u; i < count; ++i) {
        const ios27_darwin_arm64_reloc *r = &ops[i];
        uint64_t target, value;
        int64_t addend = r->addend;
        size_t width;
        if (r->type == RELOC_AUTH_POINTER) {
            bad(error, cap, "ARM64e authenticated pointer requires PAC signing");
            goto done;
        }
        if (r->type == RELOC_ADDEND) {
            if (r->length != 2u || addend < -8388608 ||
                addend > 8388607 || i + 1u >= count ||
                ops[i + 1u].type == RELOC_ADDEND ||
                ops[i + 1u].type == RELOC_SUBTRACTOR) {
                bad(error, cap, "malformed ARM64 addend prefix");
                goto done;
            }
            continue;
        }
        if (i && ops[i - 1u].type == RELOC_ADDEND &&
            sum_signed(addend, ops[i - 1u].addend, &addend)) {
            bad(error, cap, "relocation addend overflow");
            goto done;
        }
        width = (r->type == RELOC_UNSIGNED ||
                 r->type == RELOC_SUBTRACTOR) ? 8u : 4u;
        if (r->length != (width == 8u ? 3u : 2u) ||
            r->offset > size || size - r->offset < width ||
            (r->offset & (width - 1u)) ||
            (uint64_t)loaded > UINT64_MAX - r->offset) {
            bad(error, cap, "relocation width/alignment/bounds invalid");
            goto done;
        }
        if ((r->type == RELOC_BRANCH26 || r->type == RELOC_PAGE21 ||
             r->type == RELOC_GOT_PAGE21 ||
             r->type == RELOC_POINTER_GOT) != (r->pcrel != 0u) ||
            (r->type != RELOC_UNSIGNED &&
             r->type != RELOC_SUBTRACTOR &&
             r->type != RELOC_BRANCH26 &&
             r->type != RELOC_PAGE21 &&
             r->type != RELOC_PAGEOFF12 &&
             r->type != RELOC_GOT_PAGE21 &&
             r->type != RELOC_GOT_PAGEOFF12 &&
             r->type != RELOC_POINTER_GOT)) {
            bad(error, cap, "unsupported ARM64 relocation type/flags");
            goto done;
        }
        for (size_t j = 0u; j < width; ++j) {
            if (used[(size_t)r->offset + j]) {
                bad(error, cap, "overlapping ARM64 relocations");
                goto done;
            }
        }
        if (shift_address((uint64_t)r->target, addend, &target)) {
            bad(error, cap, "resolved relocation target overflow");
            goto done;
        }
        if (r->type == RELOC_SUBTRACTOR) {
            const ios27_darwin_arm64_reloc *plus;
            uint64_t minuend;
            if (i + 1u == count) {
                bad(error, cap, "unpaired SUBTRACTOR");
                goto done;
            }
            plus = &ops[++i];
            if (plus->type != RELOC_UNSIGNED || plus->length != 3u ||
                plus->offset != r->offset || plus->pcrel ||
                shift_address((uint64_t)plus->target, plus->addend,
                              &minuend) || minuend < target ||
                rd64(staged + r->offset) > INT64_MAX ||
                shift_address(minuend - target,
                              (int64_t)rd64(staged + r->offset), &value)) {
                bad(error, cap, "invalid SUBTRACTOR/UNSIGNED pair");
                goto done;
            }
            write64(staged + r->offset, value);
        } else if (r->type == RELOC_UNSIGNED) {
            if (rd64(staged + r->offset) > INT64_MAX ||
                shift_address(target, (int64_t)rd64(staged + r->offset),
                              &value)) {
                bad(error, cap, "UNSIGNED addend overflow");
                goto done;
            }
            write64(staged + r->offset, value);
        } else if (patch_instruction(staged + r->offset, r->type,
                                     (uint64_t)loaded + r->offset,
                                     target, error, cap)) {
            goto done;
        }
        for (size_t j = 0u; j < width; ++j)
            used[(size_t)r->offset + j] = 1u;
    }
    memcpy(section, staged, size);
    if (error && cap) error[0] = '\0';
    status = 0;
done:
    free(used);
    free(staged);
    return status;
}


/* Phase 19d: multi-object Darwin section linker. All lengths are bounded;
 * the caller-supplied mapping is not modified until ALL symbols, GOT slots,
 * relocations and section writes have been verified in a private buffer. */
#define LINK_MAX_OBJECTS 12u
#define LINK_MAX_SECTIONS 256u
#define LINK_MAX_SLOTS 8192u
#define LINK_MAX_SYMS 4096u
#define LINK_MAX_RELOCS 4096u
#define LINK_CAP (16u * 1024u * 1024u)
#define LINK_S_ZEROFILL 1u

typedef struct {
    uint32_t symoff, nsyms, stroff, strsize;
    size_t first_section, sections;
} link_object;
typedef struct {
    uint32_t obj, ordinal, reloff, nreloc, align;
    uint64_t address, bytes, fileoff;
    size_t output_offset;
    unsigned region, zero;
} link_section;
typedef struct {
    uint32_t obj, symbol;
    unsigned external;
    size_t output_offset;
} link_slot;

static int link_named(const uint8_t *s, const char *name) {
    size_t n = strlen(name);
    return n < 16u && memcmp(s, name, n) == 0 && s[n] == 0u;
}
static int link_align(size_t *cursor, size_t alignment) {
    if (!alignment || (alignment & (alignment - 1u)) ||
        *cursor > LINK_CAP || alignment > LINK_CAP ||
        *cursor > LINK_CAP - (alignment - 1u))
        return -1;
    *cursor = (*cursor + alignment - 1u) & ~(alignment - 1u);
    return 0;
}
static const uint8_t *link_symbol(const ios27_darwin_link_input *inputs,
                                  const link_object *objects,
                                  size_t object, uint32_t symbol) {
    return inputs[object].bytes + objects[object].symoff +
           (size_t)symbol * 16u;
}
static const char *link_symbol_name(const ios27_darwin_link_input *inputs,
                                    const link_object *objects,
                                    size_t object, uint32_t symbol) {
    const uint8_t *p = link_symbol(inputs, objects, object, symbol);
    return (const char *)inputs[object].bytes + objects[object].stroff +
           rd32(p);
}
static const link_section *link_section_by_ordinal(
    const link_object *objects, const link_section *sections,
    size_t object, uint32_t ordinal) {
    if (!ordinal || ordinal > objects[object].sections) return NULL;
    return &sections[objects[object].first_section + ordinal - 1u];
}
static int link_defined_address(const ios27_darwin_link_input *inputs,
                                const link_object *objects,
                                const link_section *sections,
                                size_t object, uint32_t symbol,
                                uintptr_t base, uint64_t *address) {
    const uint8_t *p = link_symbol(inputs, objects, object, symbol);
    const link_section *s;
    uint64_t value = rd64(p + 8u);
    if ((p[4u] & N_TYPE) != N_SECT || (p[4u] & N_STAB))
        return -1;
    s = link_section_by_ordinal(objects, sections, object, p[5u]);
    if (!s || value < s->address || value - s->address > s->bytes ||
        (uint64_t)base > UINT64_MAX - s->output_offset ||
        (uint64_t)base + s->output_offset >
            UINT64_MAX - (value - s->address))
        return -1;
    *address = (uint64_t)base + s->output_offset +
               (value - s->address);
    return 0;
}
static int link_resolve(const ios27_darwin_link_input *inputs,
                        const link_object *objects,
                        const link_section *sections, size_t object_count,
                        size_t object, uint32_t symbol, unsigned external,
                        uintptr_t base, uint64_t *address) {
    const uint8_t *p;
    const char *name;
    size_t definitions = 0u;
    if (!external) {
        const link_section *s = link_section_by_ordinal(
            objects, sections, object, symbol);
        if (!s || (uint64_t)base > UINT64_MAX - s->output_offset)
            return -1;
        *address = (uint64_t)base + s->output_offset;
        return 0;
    }
    if (symbol >= objects[object].nsyms) return -1;
    p = link_symbol(inputs, objects, object, symbol);
    if ((p[4u] & N_STAB) || !(p[4u] & N_EXT)) return -1;
    if ((p[4u] & N_TYPE) == N_SECT)
        return link_defined_address(inputs, objects, sections, object,
                                    symbol, base, address);
    if ((p[4u] & N_TYPE) != N_UNDF || p[5u] || rd64(p + 8u))
        return -1; /* No commons, dylib imports or absolute host symbols. */
    name = link_symbol_name(inputs, objects, object, symbol);
    for (size_t j = 0u; j < object_count; ++j) {
        for (uint32_t k = 0u; k < objects[j].nsyms; ++k) {
            const uint8_t *q = link_symbol(inputs, objects, j, k);
            if ((q[4u] & (N_TYPE | N_EXT | N_STAB)) !=
                (N_SECT | N_EXT) ||
                strcmp(name, link_symbol_name(inputs, objects, j, k)))
                continue;
            if (++definitions > 1u ||
                link_defined_address(inputs, objects, sections, j, k,
                                     base, address))
                return -1;
        }
    }
    return definitions == 1u ? 0 : -1;
}

int ios27_darwin_link_objects(const ios27_darwin_link_input *inputs,
                              size_t count, const char *entry_name,
                              uint8_t *output, size_t capacity,
                              uintptr_t base, size_t page,
                              ios27_darwin_link_layout *layout,
                              char *error, size_t cap) {
    link_object objs[LINK_MAX_OBJECTS] = {{0}};
    link_section *sections = NULL;
    link_slot *slots = NULL;
    size_t nsections = 0u, nslots = 0u, used = 0u, rx = 0u, ro = 0u;
    size_t total = 0u, entry = 0u, found_entry = 0u;
    uint8_t *stage = NULL;
    int status = -1;
    if (!layout || !inputs || !output || !entry_name || !*entry_name ||
        count < 2u || count > LINK_MAX_OBJECTS ||
        (page != 4096u && page != 16384u) ||
        !base || (base & (page - 1u)) || capacity > LINK_CAP * 2u)
        return bad(error, cap, "invalid Darwin multi-object link request");
    memset(layout, 0, sizeof(*layout));
    sections = calloc(LINK_MAX_SECTIONS, sizeof(*sections));
    slots = calloc(LINK_MAX_SLOTS, sizeof(*slots));
    if (!sections || !slots) {
        bad(error, cap, "link metadata allocation failed");
        goto done;
    }
    for (size_t i = 0u; i < count; ++i) {
        const uint8_t *file = inputs[i].bytes;
        ios27_macho_info info;
        size_t cursor = 32u;
        unsigned seen_sym = 0u, seen_build = 0u;
        if (!file || inputs[i].size < 32u ||
            inputs[i].size > 4u * 1024u * 1024u ||
            ios27_macho_parse(file, inputs[i].size,
                              &info, error, cap))
            goto done;
        if (info.filetype != MH_OBJECT ||
            (info.cpusubtype & 0xffu) != 0u ||
            info.platform != PLATFORM_MACOS ||
            info.has_dylibs || info.needs_dyld || info.has_main ||
            info.encrypted || info.has_fixups || info.unsupported_required) {
            bad(error, cap, "unsafe or unsupported Darwin object ABI");
            goto done;
        }
        objs[i].first_section = nsections;
        for (uint32_t j = 0u; j < rd32(file + 16u); ++j) {
            uint32_t cmd = rd32(file + cursor), cmdsize = rd32(file + cursor + 4u);
            const uint8_t *p = file + cursor;
            if (cmd == LC_SEGMENT_64) {
                uint32_t nsect = rd32(p + 64u);
                if (cmdsize < 72u || nsect > (cmdsize - 72u) / 80u ||
                    nsect > LINK_MAX_SECTIONS - nsections ||
                    nsect > 255u - objs[i].sections) {
                    bad(error, cap, "excessive Darwin sections");
                    goto done;
                }
                for (uint32_t k = 0u; k < nsect; ++k) {
                    const uint8_t *s = p + 72u + (size_t)k * 80u;
                    link_section *v = &sections[nsections++];
                    uint32_t flags = rd32(s + 64u);
                    v->obj = (uint32_t)i;
                    v->ordinal = (uint32_t)++objs[i].sections;
                    v->address = rd64(s + 32u);
                    v->bytes = rd64(s + 40u);
                    v->fileoff = rd32(s + 48u);
                    v->reloff = rd32(s + 56u);
                    v->nreloc = rd32(s + 60u);
                    v->align = rd32(s + 52u);
                    v->zero = (flags & 0xffu) == LINK_S_ZEROFILL;
                    if (link_named(s, "__text") &&
                        link_named(s + 16u, "__TEXT") && !v->zero) {
                        v->region = 0u;
                        /* Clang emits zero-length __text stubs in
                         * data-only objects with alignment 0. */
                        if (v->bytes && ((flags & 0x80000000u) == 0u ||
                                         v->align < 2u)) {
                            bad(error, cap, "invalid executable section");
                            goto done;
                        }
                    } else if ((link_named(s, "__const") ||
                                link_named(s, "__cstring")) &&
                               link_named(s + 16u, "__TEXT") && !v->zero) {
                        v->region = 1u;
                    } else if ((link_named(s, "__data") && !v->zero) ||
                               (link_named(s, "__bss") && v->zero)) {
                        if (!link_named(s + 16u, "__DATA")) {
                            bad(error, cap, "unsupported data segment");
                            goto done;
                        }
                        v->region = 2u;
                    } else if (!v->bytes && !v->nreloc) {
                        v->region = 1u;
                    } else {
                        bad(error, cap, "unsupported Darwin section or flags");
                        goto done;
                    }
                    if (v->align > 14u || v->bytes > LINK_CAP ||
                        (v->bytes && v->address > UINT64_MAX - v->bytes) ||
                        (!v->zero && !fits(v->fileoff, v->bytes,
                                           inputs[i].size)) ||
                        v->nreloc > LINK_MAX_RELOCS ||
                        (v->nreloc && (!v->bytes ||
                         !fits(v->reloff, (uint64_t)v->nreloc * 8u,
                               inputs[i].size)))) {
                        bad(error, cap, "Darwin section bounds invalid");
                        goto done;
                    }
                }
            } else if (cmd == LC_SYMTAB) {
                if (seen_sym++ || cmdsize < 24u) {
                    bad(error, cap, "invalid symbol table command");
                    goto done;
                }
                objs[i].symoff = rd32(p + 8u);
                objs[i].nsyms = rd32(p + 12u);
                objs[i].stroff = rd32(p + 16u);
                objs[i].strsize = rd32(p + 20u);
            } else if (cmd == LC_BUILD_VERSION) {
                if (seen_build++ || cmdsize < 24u) {
                    bad(error, cap, "invalid build version");
                    goto done;
                }
            }
            cursor += cmdsize; /* base parser verified command bounds */
        }
        if (!seen_build || !seen_sym || !objs[i].nsyms ||
            objs[i].nsyms > LINK_MAX_SYMS || !objs[i].strsize ||
            !fits(objs[i].symoff, (uint64_t)objs[i].nsyms * 16u,
                  inputs[i].size) ||
            !fits(objs[i].stroff, objs[i].strsize, inputs[i].size)) {
            bad(error, cap, "invalid linker symbol/string table");
            goto done;
        }
        for (uint32_t k = 0u; k < objs[i].nsyms; ++k) {
            const uint8_t *sym = link_symbol(inputs, objs, i, k);
            uint32_t strx = rd32(sym);
            if (strx >= objs[i].strsize ||
                !memchr(inputs[i].bytes + objs[i].stroff + strx, '\0',
                        objs[i].strsize - strx)) {
                bad(error, cap, "invalid Mach-O symbol string");
                goto done;
            }
            if ((sym[4u] & N_STAB) == 0u &&
                (sym[4u] & N_TYPE) == N_SECT &&
                !link_section_by_ordinal(objs, sections, i, sym[5u])) {
                bad(error, cap, "symbol references invalid section");
                goto done;
            }
        }
    }

    /* Gather distinct per-object GOT references before section layout. */
    for (size_t i = 0u; i < nsections; ++i) {
        const link_section *s = &sections[i];
        const uint8_t *file = inputs[s->obj].bytes;
        for (uint32_t j = 0u; j < s->nreloc; ++j) {
            const uint8_t *r = file + s->reloff + (size_t)j * 8u;
            uint32_t bits = rd32(r + 4u), type = bits >> 28u;
            if (type == RELOC_GOT_PAGE21 || type == RELOC_GOT_PAGEOFF12) {
                uint32_t symbol = bits & 0xffffffu;
                unsigned ext = (bits >> 27u) & 1u;
                size_t k;
                for (k = 0u; k < nslots; ++k)
                    if (slots[k].obj == s->obj &&
                        slots[k].symbol == symbol &&
                        slots[k].external == ext) break;
                if (k == nslots) {
                    if (nslots == LINK_MAX_SLOTS) {
                        bad(error, cap, "too many Darwin GOT slots");
                        goto done;
                    }
                    slots[nslots].obj = s->obj;
                    slots[nslots].symbol = symbol;
                    slots[nslots].external = ext;
                    ++nslots;
                }
            }
        }
    }
    /* All RX sections first; then a page-aligned const/GOT region; finally
     * RW data/BSS. Never share an RX page with writable storage. */
    for (unsigned region = 0u; region < 3u; ++region) {
        if (region == 1u && link_align(&used, page)) goto layout_error;
        if (region == 2u) {
            if (link_align(&used, 8u)) goto layout_error;
            for (size_t i = 0u; i < nslots; ++i) {
                if (used > LINK_CAP - 8u) goto layout_error;
                slots[i].output_offset = used;
                used += 8u;
            }
            if (link_align(&used, page)) goto layout_error;
        }
        if (region == 1u) rx = used;
        if (region == 2u) ro = used - rx;
        for (size_t i = 0u; i < nsections; ++i) {
            link_section *s = &sections[i];
            if (s->region != region) continue;
            if (link_align(&used, (size_t)1u << s->align) ||
                s->bytes > LINK_CAP - used) goto layout_error;
            s->output_offset = used;
            used += (size_t)s->bytes;
        }
    }
    if (link_align(&used, page)) goto layout_error;
    total = used;
    if (total > capacity || (uint64_t)base > UINTPTR_MAX - total) {
        bad(error, cap, "Darwin linked image exceeds caller mapping");
        goto done;
    }
    stage = calloc(total ? total : 1u, 1u);
    if (!stage) {
        bad(error, cap, "Darwin image staging allocation failed");
        goto done;
    }
    for (size_t i = 0u; i < nsections; ++i) {
        const link_section *s = &sections[i];
        if (!s->zero && s->bytes)
            memcpy(stage + s->output_offset,
                   inputs[s->obj].bytes + s->fileoff, (size_t)s->bytes);
    }
    for (size_t i = 0u; i < nslots; ++i) {
        uint64_t target;
        if (link_resolve(inputs, objs, sections, count, slots[i].obj,
                         slots[i].symbol, slots[i].external,
                         base, &target)) {
            bad(error, cap, "unresolved or ambiguous GOT symbol");
            goto done;
        }
        write64(stage + slots[i].output_offset, target);
    }
    /* Relocate into the private staged image, never caller memory. */
    for (size_t i = 0u; i < nsections; ++i) {
        const link_section *s = &sections[i];
        const uint8_t *file = inputs[s->obj].bytes;
        ios27_darwin_arm64_reloc *ops;
        if (!s->nreloc) continue;
        ops = calloc(s->nreloc, sizeof(*ops));
        if (!ops) {
            bad(error, cap, "relocation allocation failed");
            goto done;
        }
        for (uint32_t j = 0u; j < s->nreloc; ++j) {
            const uint8_t *r = file + s->reloff + (size_t)j * 8u;
            uint32_t bits = rd32(r + 4u), symbol = bits & 0xffffffu;
            unsigned type = bits >> 28u;
            uint64_t address = 0u;
            ios27_darwin_arm64_reloc *op = &ops[j];
            op->offset = rd32(r);
            op->type = (uint8_t)type;
            op->length = (uint8_t)((bits >> 25u) & 3u);
            op->pcrel = (uint8_t)((bits >> 24u) & 1u);
            op->external = (uint8_t)((bits >> 27u) & 1u);
            if (op->offset & 0x80000000u) {
                bad(error, cap, "scattered/negative ARM64 relocation");
                free(ops);
                goto done;
            }
            if (type == RELOC_ADDEND) {
                op->length = 2u;
                op->addend = sext32(symbol, 24u);
                continue;
            }
            if (type == RELOC_GOT_PAGE21 || type == RELOC_GOT_PAGEOFF12) {
                size_t k;
                for (k = 0u; k < nslots; ++k)
                    if (slots[k].obj == s->obj &&
                        slots[k].symbol == symbol &&
                        slots[k].external == op->external) break;
                if (k == nslots) {
                    bad(error, cap, "missing GOT allocation");
                    free(ops);
                    goto done;
                }
                address = (uint64_t)base + slots[k].output_offset;
            } else if (link_resolve(inputs, objs, sections, count, s->obj,
                                    symbol, op->external, base, &address)) {
                bad(error, cap, "unresolved or ambiguous ARM64 symbol");
                free(ops);
                goto done;
            }
            op->target = (uintptr_t)address;
        }
        if (ios27_darwin_arm64_apply_batch(
                stage + s->output_offset, (size_t)s->bytes,
                base + s->output_offset, ops, s->nreloc, error, cap)) {
            free(ops);
            goto done;
        }
        free(ops);
    }
    for (size_t i = 0u; i < count; ++i) {
        for (uint32_t j = 0u; j < objs[i].nsyms; ++j) {
            const uint8_t *sym = link_symbol(inputs, objs, i, j);
            uint64_t address;
            if ((sym[4u] & (N_TYPE | N_EXT | N_STAB)) !=
                (N_SECT | N_EXT) ||
                strcmp(entry_name, link_symbol_name(inputs, objs, i, j)))
                continue;
            if (++found_entry != 1u ||
                link_defined_address(inputs, objs, sections,
                                     i, j, base, &address) ||
                address < base || address - base >= rx ||
                ((address - base) & 3u)) {
                bad(error, cap, "invalid or duplicate entry definition");
                goto done;
            }
            entry = (size_t)(address - base);
        }
    }
    if (found_entry != 1u) {
        bad(error, cap, "requested Darwin entry symbol not exported");
        goto done;
    }
    memcpy(output, stage, total);
    layout->image_bytes = total;
    layout->rx_bytes = rx;
    layout->ro_bytes = ro;
    layout->rw_bytes = total - rx - ro;
    layout->entry_offset = entry;
    if (error && cap) error[0] = '\0';
    status = 0;
    goto done;
layout_error:
    bad(error, cap, "Darwin section layout overflow");
done:
    free(stage);
    free(slots);
    free(sections);
    return status;
}
