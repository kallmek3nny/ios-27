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
