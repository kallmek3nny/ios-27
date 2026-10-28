#include "ios27/darwin_object.h"
#include "ios27/macho.h"

#include <stdio.h>
#include <string.h>

#define MH_OBJECT 1u
#define ARM64_ALL 0u
#define LC_SEGMENT_64 0x19u
#define LC_SYMTAB 0x02u
#define LC_BUILD_VERSION 0x32u
#define PLATFORM_MACOS 1u
#define S_REGULAR 0u
#define S_ATTR_PURE_INSTRUCTIONS 0x80000000u
#define N_STAB 0xe0u
#define N_TYPE 0x0eu
#define N_SECT 0x0eu
#define N_EXT 0x01u
#define MAX_SYMBOLS 512u
#define MAX_TEXT_BYTES 16384u

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4u) << 32u);
}
static int within(uint64_t offset, uint64_t length, size_t size) {
    return offset <= (uint64_t)size && length <= (uint64_t)size - offset;
}
static int fail(char *error, size_t capacity, const char *why) {
    if (error && capacity) (void)snprintf(error, capacity, "%s", why);
    return -1;
}

int ios27_darwin_object_inspect(const uint8_t *file, size_t file_size,
                                ios27_darwin_object *out,
                                char *error, size_t cap) {
    ios27_macho_info info;
    size_t cursor = 32u;
    uint64_t text_addr = 0u, text_size = 0u, text_off = 0u;
    uint32_t text_section_number = 0u;
    uint32_t symoff = 0u, nsyms = 0u, stroff = 0u, strsize = 0u;
    uint32_t sections_seen = 0u;
    unsigned saw_text = 0u, saw_symtab = 0u, saw_build = 0u;
    unsigned saw_entry = 0u;

    if (!file || !out || file_size > 1024u * 1024u)
        return fail(error, cap, "invalid or oversized Mach-O object input");
    memset(out, 0, sizeof(*out));
    if (ios27_macho_parse(file, file_size, &info, error, cap))
        return -1;
    if (info.filetype != MH_OBJECT || (info.cpusubtype & 0xffu) != ARM64_ALL ||
        info.encrypted || info.has_main || info.has_dylibs ||
        info.needs_dyld || info.has_fixups)
        return fail(error, cap, "requires plain ARM64-all unlinked MH_OBJECT");
    /* MH_OBJECT segment maxprot may be RWX metadata, but is NEVER mapped
     * for execution by the image loader. Only the __text bytes are read. */
    for (uint32_t i = 0u; i < u32(file + 16u); ++i) {
        uint32_t cmd = u32(file + cursor);
        uint32_t cmdsize = u32(file + cursor + 4u);
        const uint8_t *p = file + cursor;
        if (cmd == LC_SEGMENT_64) {
            uint32_t sections = u32(p + 64u);
            if (cmdsize < 72u ||
                sections > (cmdsize - 72u) / 80u ||
                sections > UINT32_MAX - sections_seen)
                return fail(error, cap, "invalid object section count");
            for (uint32_t j = 0u; j < sections; ++j) {
                const uint8_t *sec = p + 72u + j * 80u;
                uint32_t off = u32(sec + 48u);
                uint32_t relocations = u32(sec + 60u);
                uint32_t flags = u32(sec + 64u);
                uint64_t addr = u64(sec + 32u);
                uint64_t bytes = u64(sec + 40u);
                ++sections_seen;
                if (relocations)
                    return fail(error, cap, "relocations require a linker");
                if (memcmp(sec, "__text\0", 7u) == 0 &&
                    memcmp(sec + 16u, "__TEXT\0", 7u) == 0) {
                    if (saw_text++ || (flags & 0xffu) != S_REGULAR ||
                        (flags & S_ATTR_PURE_INSTRUCTIONS) == 0u ||
                        !bytes || bytes > MAX_TEXT_BYTES ||
                        !within(off, bytes, file_size) ||
                        bytes > UINT64_MAX - addr)
                        return fail(error, cap, "invalid __TEXT,__text section");
                    text_section_number = sections_seen;
                    text_off = off;
                    text_size = bytes;
                    text_addr = addr;
                } else if (bytes && (flags & S_ATTR_PURE_INSTRUCTIONS))
                    return fail(error, cap, "unknown executable section");
            }
        } else if (cmd == LC_SYMTAB) {
            if (cmdsize < 24u || saw_symtab++)
                return fail(error, cap, "invalid or duplicate symbol table");
            symoff = u32(p + 8u);
            nsyms = u32(p + 12u);
            stroff = u32(p + 16u);
            strsize = u32(p + 20u);
        } else if (cmd == LC_BUILD_VERSION) {
            if (cmdsize < 24u || saw_build++)
                return fail(error, cap, "invalid build version command");
        }
        cursor += cmdsize;
    }
    if (!saw_text || !saw_symtab || !saw_build ||
        info.platform != PLATFORM_MACOS ||
        !nsyms || nsyms > MAX_SYMBOLS ||
        !within(symoff, (uint64_t)nsyms * 16u, file_size) ||
        !within(stroff, strsize, file_size) || !strsize)
        return fail(error, cap, "missing or invalid Darwin object metadata");

    for (uint32_t i = 0u; i < nsyms; ++i) {
        const uint8_t *sym = file + (size_t)symoff + (size_t)i * 16u;
        uint32_t strx = u32(sym);
        uint8_t type = sym[4u];
        uint8_t section = sym[5u];
        uint64_t value = u64(sym + 8u);
        const uint8_t *name;
        size_t remaining;
        if (strx >= strsize)
            return fail(error, cap, "symbol string index out of bounds");
        name = file + (size_t)stroff + strx;
        remaining = (size_t)(strsize - strx);
        if (!memchr(name, '\0', remaining))
            return fail(error, cap, "unterminated Mach-O symbol name");
        if (!(type & N_STAB) && ((type & N_TYPE) != N_SECT ||
                                  !section || section > sections_seen))
            return fail(error, cap, "undefined or unsupported symbol");
        if ((type & N_STAB) == 0u && section == text_section_number &&
            (type & N_EXT) &&
            strcmp((const char *)name, "_fixture_entry") == 0) {
            if (saw_entry++ || value < text_addr ||
                value - text_addr > text_size ||
                text_size - (value - text_addr) < 4u ||
                ((value - text_addr) & 3u))
                return fail(error, cap, "invalid fixture entry symbol");
            out->entry_offset = value - text_addr;
        }
    }
    if (!saw_entry)
        return fail(error, cap, "no externally defined _fixture_entry");
    out->platform = info.platform;
    out->minos = info.minos;
    out->text_file_offset = text_off;
    out->text_size = text_size;
    out->symbol_count = nsyms;
    if (error && cap) error[0] = '\0';
    return 0;
}

/* Phase 15a does not reuse the MH_OBJECT interpretation for linked files.
 * The API only accepts the narrow, link-complete, SDK-free fixture profile.
 */
int ios27_darwin_executable_inspect(const uint8_t *file, size_t size,
                                    ios27_darwin_executable *out,
                                    char *error, size_t cap) {
    ios27_macho_info info;
    size_t cursor = 32u;
    uint64_t textoff = 0u, textlen = 0u;
    uint32_t symbols = 0u;
    ios27_exec_layout executable_layout;
    unsigned saw_text = 0u, saw_symtab = 0u, saw_dysymtab = 0u;
    unsigned saw_fixups = 0u, saw_legacy_dyld = 0u, saw_entry_cmd = 0u;
    unsigned saw_pagezero = 0u, saw_linkedit = 0u, saw_text_seg = 0u;
    if (!file || !out || size < 32u || size > 2u * 1024u * 1024u)
        return fail(error, cap, "invalid or oversized linked executable");
    memset(out, 0, sizeof(*out));
    if (ios27_macho_parse(file, size, &info, error, cap))
        return -1;
    if (info.filetype != IOS27_MH_EXECUTE ||
        (info.cpusubtype & 0xffu) != 0u || info.platform != PLATFORM_MACOS ||
        !info.has_main || info.encrypted || info.has_dylibs ||
        !info.segment_count || !info.needs_dyld)
        return fail(error, cap, "requires linked ARM64-all Darwin MH_EXECUTE");
    /*
     * The detailed inspector must never approve a linked image that the
     * actual native loader would reject. This also checks the complete
     * contents of legacy dyld-info and chained fixup commands first.
     */
    if (ios27_macho_executable_layout(file, size, &info, 4096u,
                                      &executable_layout, error, cap) != 0)
        return -1;

    for (size_t i = 0u; i < info.segment_count; ++i) {
        const ios27_segment *s = &info.segments[i];
        int is_pagezero = strcmp(s->name, "__PAGEZERO") == 0;
        int is_text = strcmp(s->name, "__TEXT") == 0;
        int is_linkedit = strcmp(s->name, "__LINKEDIT") == 0;
        int is_data = strcmp(s->name, "__DATA") == 0 ||
                      strcmp(s->name, "__DATA_CONST") == 0;
        if ((!is_pagezero && !is_text && !is_linkedit && !is_data) ||
            (s->initprot & ~s->maxprot) || (s->maxprot & ~7u) ||
            (s->initprot & ~7u) ||
            ((s->maxprot & 6u) == 6u) ||
            ((s->initprot & 6u) == 6u) ||
            (!s->vmsize && !is_pagezero))
            return fail(error, cap, "invalid segment permissions or name");
        if (is_pagezero) {
            if (saw_pagezero++ || s->filesize || s->initprot || s->maxprot)
                return fail(error, cap, "invalid __PAGEZERO");
        } else if (is_text) {
            if (saw_text_seg++ || s->fileoff != 0u ||
                s->initprot != 5u || s->maxprot != 5u || !s->filesize)
                return fail(error, cap, "invalid __TEXT protections");
        } else if (is_linkedit) {
            if (saw_linkedit++ || s->initprot != 1u ||
                s->maxprot != 1u || !s->filesize)
                return fail(error, cap, "invalid __LINKEDIT protections");
        } else if (s->initprot & 4u) {
            return fail(error, cap, "executable data segment");
        }
        for (size_t j = 0u; j < i; ++j) {
            const ios27_segment *t = &info.segments[j];
            if (s->vmsize && t->vmsize &&
                s->vmaddr < t->vmaddr + t->vmsize &&
                t->vmaddr < s->vmaddr + s->vmsize)
                return fail(error, cap, "overlapping segment virtual ranges");
            if (s->filesize && t->filesize &&
                s->fileoff < t->fileoff + t->filesize &&
                t->fileoff < s->fileoff + s->filesize)
                return fail(error, cap, "overlapping segment file ranges");
        }
    }
    if (saw_pagezero != 1u || saw_text_seg != 1u || saw_linkedit != 1u)
        return fail(error, cap, "missing required bounded segments");

    for (uint32_t i = 0u; i < u32(file + 16u); ++i) {
        const uint8_t *cmd = file + cursor;
        uint32_t op = u32(cmd), len = u32(cmd + 4u);
        if (op == LC_SEGMENT_64) {
            const ios27_segment *segment = NULL;
            uint32_t sections = u32(cmd + 64u);
            char name[17];
            if (len < 72u || sections > (len - 72u) / 80u)
                return fail(error, cap, "truncated linked segment");
            memcpy(name, cmd + 8u, 16u);
            name[16] = '\0';
            for (size_t j = 0u; j < info.segment_count; ++j) {
                if (strcmp(name, info.segments[j].name) == 0) {
                    segment = &info.segments[j];
                    break;
                }
            }
            if (!segment)
                return fail(error, cap, "unrecognized linked segment");
            for (uint32_t j = 0u; j < sections; ++j) {
                const uint8_t *sec = cmd + 72u + (size_t)j * 80u;
                uint64_t addr = u64(sec + 32u);
                uint64_t bytes = u64(sec + 40u);
                uint32_t offset = u32(sec + 48u);
                uint32_t nreloc = u32(sec + 60u);
                uint32_t type = u32(sec + 64u) & 0xffu;
                uint64_t delta = addr >= segment->vmaddr ?
                                 addr - segment->vmaddr : UINT64_MAX;
                if (memcmp(sec + 16u, cmd + 8u, 16u) ||
                    nreloc || !bytes || delta > segment->vmsize ||
                    bytes > segment->vmsize - delta)
                    return fail(error, cap, "unresolved relocation or invalid section");
                if (type == 1u) {
                    /* S_ZEROFILL describes VM only; section.offset is
                     * conventionally zero even when filesize is page-rounded. */
                    if (segment->initprot & 4u)
                        return fail(error, cap, "executable BSS section");
                } else if (type != 0u && type != 2u &&
                           type != 4u && type != 5u && type != 14u) {
                    return fail(error, cap, "dyld-managed linked section");
                } else if (delta > segment->filesize ||
                           bytes > segment->filesize - delta ||
                           segment->fileoff > UINT64_MAX - delta ||
                           (uint64_t)offset != segment->fileoff + delta ||
                           !within(offset, bytes, size)) {
                    return fail(error, cap, "invalid file-backed linked section");
                }
                if (memcmp(sec, "__text\0", 7u) == 0 &&
                    strcmp(name, "__TEXT") == 0) {
                    if (saw_text++ || (u32(sec + 64u) & 0xffu) != 0u ||
                        (u32(sec + 64u) & UINT32_C(0x80000000)) == 0u ||
                        bytes > 65536u)
                        return fail(error, cap, "invalid linked __text section");
                    textoff = offset;
                    textlen = bytes;
                }
            }
        } else if (op == LC_SYMTAB) {
            uint32_t symoff, stroff, strsize;
            if (saw_symtab++ || len < 24u)
                return fail(error, cap, "invalid linked LC_SYMTAB");
            symoff = u32(cmd + 8u);
            symbols = u32(cmd + 12u);
            stroff = u32(cmd + 16u);
            strsize = u32(cmd + 20u);
            if (symbols > 512u || !within(symoff, (uint64_t)symbols * 16u, size) ||
                !within(stroff, strsize, size) || !strsize)
                return fail(error, cap, "invalid linked symbol-table bounds");
            for (uint32_t j = 0u; j < symbols; ++j) {
                const uint8_t *sym = file + symoff + (size_t)j * 16u;
                uint32_t strx = u32(sym);
                if (strx >= strsize ||
                    !memchr(file + stroff + strx, '\0', strsize - strx) ||
                    ((sym[4u] & N_STAB) == 0u &&
                     (sym[4u] & N_TYPE) == 0u))
                    return fail(error, cap, "undefined or invalid linked symbol");
            }
        } else if (op == 0x0bu) { /* LC_DYSYMTAB */
            if (saw_dysymtab++ || len < 80u || u32(cmd + 28u) != 0u)
                return fail(error, cap, "undefined symbols in linked image");
        } else if (op == UINT32_C(0x80000034)) { /* LC_DYLD_CHAINED_FIXUPS */
            uint32_t offset, bytes;
            if (saw_fixups++ || len < 16u)
                return fail(error, cap, "invalid chained-fixup load command");
            offset = u32(cmd + 8u);
            bytes = u32(cmd + 12u);
            if (bytes < 28u || !within(offset, bytes, size) ||
                u32(file + offset + 16u) != 0u)
                return fail(error, cap, "unresolved chained-fixup imports");
        } else if (op == UINT32_C(0x80000033) || op == 0x26u ||
                   op == 0x29u || op == 0x1du) {
            if (len < 16u ||
                !within(u32(cmd + 8u), u32(cmd + 12u), size))
                return fail(error, cap, "out of bounds linkedit data command");
        } else if (op == UINT32_C(0x80000022) || op == 0x22u) {
            /* Some LLVM linkers emit empty LC_DYLD_INFO_ONLY instead of
             * LC_DYLD_CHAINED_FIXUPS. The mandatory layout preflight has
             * checked that all rebase and bind streams have zero length.
             */
            if (saw_legacy_dyld++ || len != 48u)
                return fail(error, cap, "invalid or duplicate inert dyld info");
        } else if (op == UINT32_C(0x80000028)) {
            if (saw_entry_cmd++ || len != 24u)
                return fail(error, cap, "invalid LC_MAIN");
        } else if (op == LC_BUILD_VERSION || op == 0x0eu || op == 0x1bu) {
            /* Already parsed by ios27_macho_parse, or fixed-size LC_UUID. */
            if (op == 0x1bu && len != 24u)
                return fail(error, cap, "invalid LC_UUID");
        } else {
            return fail(error, cap, "unexpected linked load command");
        }
        cursor += len;
    }
    if (saw_text != 1u || saw_symtab != 1u || saw_dysymtab != 1u ||
        saw_entry_cmd != 1u || (saw_fixups + saw_legacy_dyld) != 1u ||
        info.entryoff < textoff || (uint64_t)info.entryoff - textoff > textlen ||
        textlen - ((uint64_t)info.entryoff - textoff) < 4u ||
        ((info.entryoff - textoff) & 3u))
        return fail(error, cap, "invalid or missing linked entrypoint metadata");
    out->entry_file_offset = info.entryoff;
    out->text_file_offset = textoff;
    out->text_size = textlen;
    out->platform = info.platform;
    out->minos = info.minos;
    out->segment_count = (uint32_t)info.segment_count;
    out->symbol_count = symbols;
    if (error && cap) error[0] = '\0';
    return 0;
}
