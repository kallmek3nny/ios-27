#include "ios27/darwin_branch26.h"
#include "ios27/macho.h"

#include <stdio.h>
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
