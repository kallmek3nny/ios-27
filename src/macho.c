#include "ios27/macho.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MH_MAGIC_64 0xfeedfacfu
#define CPU_TYPE_ARM64 0x0100000cu
#define LC_SEGMENT_64 0x19u
#define LC_MAIN 0x80000028u
#define LC_LOAD_DYLINKER 0x0eu
#define LC_LOAD_DYLIB 0x0cu
#define LC_LOAD_WEAK_DYLIB 0x80000018u
#define LC_REEXPORT_DYLIB 0x8000001fu
#define LC_LOAD_UPWARD_DYLIB 0x80000023u
#define LC_LAZY_LOAD_DYLIB 0x20u
#define LC_DYLD_INFO 0x22u
#define LC_DYLD_INFO_ONLY 0x80000022u
#define LC_DYLD_CHAINED_FIXUPS 0x80000034u
#define LC_DYLD_EXPORTS_TRIE 0x80000033u
#define LC_ENCRYPTION_INFO_64 0x2cu
#define LC_BUILD_VERSION 0x32u
#define LC_SYMTAB 0x02u
#define LC_DYSYMTAB 0x0bu
#define LC_UUID 0x1bu
#define LC_CODE_SIGNATURE 0x1du
#define LC_VERSION_MIN_MACOSX 0x24u
#define LC_FUNCTION_STARTS 0x26u
#define LC_DATA_IN_CODE 0x29u
#define LC_SOURCE_VERSION 0x2au

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);
}

static int bad(char *error, size_t size, const char *format, ...) {
    if (error != NULL && size != 0u) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(error, size, format, args);
        va_end(args);
    }
    return -1;
}

static int within(uint64_t offset, uint64_t length, size_t size) {
    return offset <= (uint64_t)size &&
           length <= (uint64_t)size - offset;
}

int ios27_macho_parse(const uint8_t *data, size_t size,
                     ios27_macho_info *out, char *error, size_t error_size) {
    uint32_t command_count, command_bytes;
    size_t cursor, end;
    if (out == NULL || data == NULL)
        return bad(error, error_size, "null input");
    memset(out, 0, sizeof(*out));
    if (size < 32u)
        return bad(error, error_size, "truncated mach_header_64");
    if (u32(data) != MH_MAGIC_64)
        return bad(error, error_size, "not a little-endian 64-bit Mach-O");
    out->cputype = u32(data + 4);
    out->cpusubtype = u32(data + 8);
    out->filetype = u32(data + 12);
    out->flags = u32(data + 24);
    if (out->cputype != CPU_TYPE_ARM64)
        return bad(error, error_size, "not an ARM64 Mach-O");
    command_count = u32(data + 16);
    command_bytes = u32(data + 20);
    if (command_count > 4096u || !within(32u, command_bytes, size))
        return bad(error, error_size, "invalid load command table");
    cursor = 32u;
    end = cursor + (size_t)command_bytes;

    for (uint32_t i = 0; i < command_count; ++i) {
        uint32_t cmd, cmdsize;
        const uint8_t *p;
        if (cursor > end || end - cursor < 8u)
            return bad(error, error_size, "truncated load command %u", i);
        p = data + cursor;
        cmd = u32(p);
        cmdsize = u32(p + 4);
        if (cmdsize < 8u || (cmdsize & 7u) != 0u ||
            (size_t)cmdsize > end - cursor)
            return bad(error, error_size, "invalid size of load command %u", i);

        if (cmd == LC_SEGMENT_64) {
            ios27_segment *s;
            uint32_t sections;
            if (cmdsize < 72u)
                return bad(error, error_size, "truncated LC_SEGMENT_64");
            sections = u32(p + 64);
            if (sections > (cmdsize - 72u) / 80u)
                return bad(error, error_size, "invalid segment section count");
            if (out->segment_count >= IOS27_MAX_SEGMENTS)
                return bad(error, error_size, "too many segments");
            s = &out->segments[out->segment_count++];
            memcpy(s->name, p + 8, 16u);
            s->name[16] = '\0';
            s->vmaddr = u64(p + 24);
            s->vmsize = u64(p + 32);
            s->fileoff = u64(p + 40);
            s->filesize = u64(p + 48);
            s->maxprot = u32(p + 56);
            s->initprot = u32(p + 60);
            if (!within(s->fileoff, s->filesize, size) ||
                s->filesize > s->vmsize ||
                s->vmsize > UINT64_MAX - s->vmaddr)
                return bad(error, error_size, "invalid segment %s bounds", s->name);
        } else if (cmd == LC_MAIN) {
            if (cmdsize < 24u || out->has_main)
                return bad(error, error_size, "invalid or duplicate LC_MAIN");
            out->has_main = 1u;
            out->entryoff = u64(p + 8);
            out->stacksize = u64(p + 16);
        } else if (cmd == LC_LOAD_DYLINKER) {
            if (cmdsize < 12u || u32(p + 8) < 12u ||
                u32(p + 8) >= cmdsize ||
                memchr(p + u32(p + 8), '\0', cmdsize - u32(p + 8)) == NULL)
                return bad(error, error_size, "invalid LC_LOAD_DYLINKER");
            out->needs_dyld = 1u;
        } else if (cmd == LC_LOAD_DYLIB || cmd == LC_LOAD_WEAK_DYLIB ||
                   cmd == LC_REEXPORT_DYLIB || cmd == LC_LOAD_UPWARD_DYLIB ||
                   cmd == LC_LAZY_LOAD_DYLIB) {
            if (cmdsize < 24u || u32(p + 8) < 24u ||
                u32(p + 8) >= cmdsize ||
                memchr(p + u32(p + 8), '\0', cmdsize - u32(p + 8)) == NULL)
                return bad(error, error_size, "invalid LC_LOAD_DYLIB");
            out->has_dylibs = 1u;
        } else if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY ||
                   cmd == LC_DYLD_CHAINED_FIXUPS ||
                   cmd == LC_DYLD_EXPORTS_TRIE) {
            out->has_fixups = 1u;
        } else if (cmd == LC_ENCRYPTION_INFO_64) {
            if (cmdsize < 24u)
                return bad(error, error_size, "truncated encryption info");
            if (u32(p + 16) != 0u)
                out->encrypted = 1u;
        } else if (cmd == LC_BUILD_VERSION) {
            if (cmdsize < 24u || u32(p + 20) > (cmdsize - 24u) / 8u)
                return bad(error, error_size, "invalid build version");
            out->platform = u32(p + 8);
            out->minos = u32(p + 12);
            out->sdk = u32(p + 16);
        } else if ((cmd & 0x80000000u) != 0u) {
            /* An unrecognized LC_REQ_DYLD is not safely executable. */
            out->unsupported_required = 1u;
        }
        cursor += (size_t)cmdsize;
    }
    if (cursor != end)
        return bad(error, error_size, "load command byte count mismatch");
    if (error != NULL && error_size != 0u)
        error[0] = '\0';
    return 0;
}


/*
 * A linked executable may declare /usr/lib/dyld and carry empty chained
 * fixup metadata even though its original-source code has no imports or
 * rebases. Treat those commands as inert only after inspecting their payloads.
 * No dynamic linker, import resolver, or relocation engine is invoked.
 */
static int freestanding_linkedit_safe(const uint8_t *data, size_t size,
                                     char *error, size_t capacity) {
    size_t cursor = 32u;
    uint32_t count = u32(data + 16u);
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *p = data + cursor;
        uint32_t cmd = u32(p), bytes = u32(p + 4u);
        if (cmd == LC_SEGMENT_64) {
            uint32_t sections = u32(p + 64u);
            uint64_t vmaddr = u64(p + 24u), vmsize = u64(p + 32u);
            uint64_t fileoff = u64(p + 40u), filesize = u64(p + 48u);
            uint32_t initprot = u32(p + 60u);
            /*
             * A linked image's section records must describe the bytes we
             * actually stage, not merely fit inside the load command.
             * S_ZEROFILL has a VM range but must not consume file bytes.
             */
            if (memcmp(p + 8u, "__PAGEZERO", 10u) == 0 && sections)
                return bad(error, capacity, "sections in __PAGEZERO");
            for (uint32_t j = 0u; j < sections; ++j) {
                const uint8_t *section = p + 72u + (size_t)j * 80u;
                uint64_t addr = u64(section + 32u);
                uint64_t length = u64(section + 40u);
                uint64_t delta, expected;
                uint32_t offset = u32(section + 48u);
                uint32_t flags = u32(section + 64u);
                uint32_t type = flags & 0xffu;
                int zerofill = type == 1u;
                if (memcmp(section + 16u, p + 8u, 16u) != 0 ||
                    u32(section + 60u) != 0u ||
                    addr < vmaddr || length > vmsize ||
                    addr - vmaddr > vmsize - length)
                    return bad(error, capacity, "unlinked or out-of-range section");
                delta = addr - vmaddr;
                /* Reject indirect pointers, TLV and other dyld-managed types. */
                if (type != 0u && type != 1u && type != 2u &&
                    type != 4u && type != 5u && type != 14u)
                    return bad(error, capacity, "section needs dyld/runtime fixups");
                if ((flags & UINT32_C(0xc0000000)) != 0u &&
                    (initprot & 4u) == 0u)
                    return bad(error, capacity, "instructions in non-executable segment");
                if (zerofill) {
                    if ((initprot & 4u) != 0u)
                        return bad(error, capacity, "invalid zero-fill section");
                } else {
                    if (delta > filesize || length > filesize - delta ||
                        fileoff > UINT64_MAX - delta)
                        return bad(error, capacity, "section outside segment file");
                    expected = fileoff + delta;
                    if (expected != (uint64_t)offset ||
                        !within(expected, length, size))
                        return bad(error, capacity, "section/file mapping mismatch");
                }
                for (uint32_t k = 0u; k < j; ++k) {
                    const uint8_t *previous = p + 72u + (size_t)k * 80u;
                    uint64_t start = u64(previous + 32u);
                    uint64_t count_bytes = u64(previous + 40u);
                    if (length && count_bytes &&
                        start < addr + length && addr < start + count_bytes)
                        return bad(error, capacity, "overlapping linked sections");
                }
            }
        } else if (cmd == LC_MAIN) {
            if (bytes != 24u)
                return bad(error, capacity, "unsupported LC_MAIN size");
        } else if (cmd == LC_LOAD_DYLINKER) {
            static const char dyld[] = "/usr/lib/dyld";
            uint32_t path = u32(p + 8u);
            if (bytes < 12u || path > bytes ||
                (size_t)(bytes - path) < sizeof(dyld) ||
                memcmp(p + path, dyld, sizeof(dyld)) != 0)
                return bad(error, capacity, "unsupported dynamic linker");
        } else if (cmd == LC_DYLD_CHAINED_FIXUPS) {
            uint32_t offset, length, starts, imports, symbols;
            if (bytes != 16u)
                return bad(error, capacity, "invalid chained fixup command");
            offset = u32(p + 8u);
            length = u32(p + 12u);
            if (length < 28u || !within(offset, length, size))
                return bad(error, capacity, "truncated chained fixup payload");
            /* dyld_chained_fixups_header: version, three offsets, count,
             * imports format, symbols format. Reject all actual fixup starts. */
            starts = u32(data + offset + 4u);
            imports = u32(data + offset + 8u);
            symbols = u32(data + offset + 12u);
            if (u32(data + offset) != 0u ||
                starts < 28u || starts > length ||
                imports < 28u || imports > length ||
                symbols < 28u || symbols > length ||
                u32(data + offset + 16u) != 0u ||
                u32(data + offset + 20u) > 3u ||
                u32(data + offset + 24u) != 0u)
                return bad(error, capacity, "unsupported chained fixup metadata");
            /*
             * LLD may encode an empty starts_in_image table containing
             * one zero offset per segment, instead of a bare zero count.
             * Accept only those provably empty chains. imports_count=0
             * makes the (possibly nonzero) imports format inert.
             */
            if (starts < length) {
                uint32_t count;
                if (length - starts < 4u)
                    return bad(error, capacity, "truncated chained starts");
                count = u32(data + offset + starts);
                if (count > IOS27_MAX_SEGMENTS ||
                    (uint64_t)count * 4u > length - starts - 4u)
                    return bad(error, capacity, "invalid chained starts count");
                for (uint32_t j = 0u; j < count; ++j) {
                    if (u32(data + offset + starts + 4u + (size_t)j * 4u))
                        return bad(error, capacity, "chained rebases require dyld");
                }
            }
        } else if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) {
            if (bytes != 48u)
                return bad(error, capacity, "invalid dyld info command");
            for (unsigned j = 0u; j < 4u; ++j) {
                if (u32(p + 12u + j * 8u) != 0u)
                    return bad(error, capacity, "dyld bind/rebase not supported");
            }
            if (!within(u32(p + 40u), u32(p + 44u), size))
                return bad(error, capacity, "invalid dyld export bounds");
        } else if (cmd == LC_SYMTAB) {
            uint32_t offset, count_symbols, strings, string_bytes;
            if (bytes != 24u)
                return bad(error, capacity, "invalid linked symbol table");
            offset = u32(p + 8u);
            count_symbols = u32(p + 12u);
            strings = u32(p + 16u);
            string_bytes = u32(p + 20u);
            if (count_symbols > 4096u ||
                !within(offset, (uint64_t)count_symbols * 16u, size) ||
                !within(strings, string_bytes, size))
                return bad(error, capacity, "invalid linked symbol bounds");
            for (uint32_t j = 0u; j < count_symbols; ++j) {
                const uint8_t *symbol = data + offset + (size_t)j * 16u;
                uint8_t kind = symbol[4u];
                if (u32(symbol) >= string_bytes ||
                    !memchr(data + strings + u32(symbol), '\0',
                           string_bytes - u32(symbol)) ||
                    ((kind & 0xe0u) == 0u &&
                     (kind & 0x0eu) != 0x0eu &&
                     (kind & 0x0eu) != 0x02u))
                    return bad(error, capacity, "unresolved or malformed symbol");
            }
        } else if (cmd == LC_DYSYMTAB) {
            if (bytes != 80u || u32(p + 28u) != 0u ||
                u32(p + 60u) != 0u)
                return bad(error, capacity, "unresolved dynamic symbols");
        } else if (cmd == LC_DYLD_EXPORTS_TRIE ||
                   cmd == LC_CODE_SIGNATURE || cmd == LC_FUNCTION_STARTS ||
                   cmd == LC_DATA_IN_CODE) {
            if (bytes != 16u ||
                !within(u32(p + 8u), u32(p + 12u), size))
                return bad(error, capacity, "invalid linkedit command");
        } else if (cmd == LC_UUID) {
            if (bytes != 24u)
                return bad(error, capacity, "invalid uuid command");
        } else if (cmd == LC_SOURCE_VERSION) {
            if (bytes != 16u)
                return bad(error, capacity, "invalid source version command");
        } else if (cmd == LC_BUILD_VERSION ||
                   cmd == LC_VERSION_MIN_MACOSX) {
            /* Parsed and bounded by the Mach-O load-command walker. */
        } else {
            return bad(error, capacity, "unsupported executable load command");
        }
        cursor += (size_t)bytes;
    }
    return 0;
}

/*
 * Preflight a freestanding executable against the *actual mapping granule*.
 * The same check runs on x86_64, without ever making guest pages executable.
 * LC_MAIN.entryoff is a file offset relative to __TEXT's fileoff.
 * No syscall/dyld/ObjC execution is supported by this path.
 */
int ios27_macho_executable_layout(const uint8_t *data, size_t size,
                                  const ios27_macho_info *info,
                                  size_t page_size, ios27_exec_layout *layout,
                                  char *error, size_t error_size) {
    ios27_macho_info parsed;
    uint64_t low = UINT64_MAX, high = 0u, text_vm = 0u;
    uint64_t text_fileoff = 0u, entry, pagezero_end = 0u;
    int text_found = 0, entry_found = 0, pagezero_found = 0;

    if (layout) memset(layout, 0, sizeof(*layout));
    if (!data || !info || !layout ||
        (page_size != 4096u && page_size != 16384u))
        return bad(error, error_size, "invalid executable layout input/page size");
    if (ios27_macho_parse(data, size, &parsed, error, error_size))
        return -1;
    /* Reject stale or hand-edited metadata, including protection overrides. */
    if (memcmp(info, &parsed, sizeof(parsed)) != 0)
        return bad(error, error_size, "image metadata differs from source");
    if (parsed.filetype != IOS27_MH_EXECUTE ||
        (parsed.cpusubtype & 0xffu) != 0u || !parsed.has_main ||
        parsed.has_dylibs ||
        parsed.encrypted || parsed.unsupported_required ||
        parsed.segment_count == 0u)
        return bad(error, error_size, "not a freestanding LC_MAIN executable");
    /* Do not accept a process stack request the native runner cannot honor.
     * Keep the architecture-independent inspector and runtime in sync. */
    if (parsed.stacksize > UINT64_C(8) * 1024u * 1024u)
        return bad(error, error_size, "LC_MAIN stacksize exceeds trusted limit");
    if (freestanding_linkedit_safe(data, size, error, error_size) != 0)
        return -1;

    for (size_t i = 0u; i < parsed.segment_count; ++i) {
        const ios27_segment *s = &parsed.segments[i];
        uint64_t last, aligned_last;
        if (strcmp(s->name, "__TEXT") == 0) {
            if (text_found++)
                return bad(error, error_size, "duplicate __TEXT segment");
            text_vm = s->vmaddr;
            text_fileoff = s->fileoff;
        }
        if ((s->initprot | s->maxprot) & ~7u ||
            (s->initprot & ~s->maxprot) != 0u ||
            (s->initprot & 6u) == 6u ||
            (s->maxprot & 6u) == 6u)
            return bad(error, error_size, "invalid or W+X protections");
        /*
         * Enforce segment roles before mapping, including maxprot. Even
         * when initially NX, data/linkedit can never become executable;
         * text and linkedit cannot acquire writable permissions.
         */
        if (((strcmp(s->name, "__TEXT") == 0 ||
              strcmp(s->name, "__TEXT_EXEC") == 0) &&
             ((s->initprot | s->maxprot) & 2u) != 0u) ||
            (strcmp(s->name, "__TEXT") != 0 &&
             strcmp(s->name, "__TEXT_EXEC") != 0 &&
             ((s->initprot | s->maxprot) & 4u) != 0u) ||
            (strcmp(s->name, "__TEXT_EXEC") == 0 && s->vmsize &&
             (s->initprot != 5u || s->maxprot != 5u)) ||
            (strcmp(s->name, "__LINKEDIT") == 0 &&
             (s->initprot != 1u || s->maxprot != 1u)))
            return bad(error, error_size, "unsupported executable segment permissions");
        if (strcmp(s->name, "__PAGEZERO") == 0) {
            if (pagezero_found++ || s->vmaddr != 0u ||
                s->fileoff != 0u || s->filesize || s->initprot || s->maxprot)
                return bad(error, error_size, "invalid __PAGEZERO range/protections");
            pagezero_end = s->vmsize;
            continue;
        }
        if (!s->vmsize) continue;
        /* Reject malformed 4K Mach-O VM/file page phase relationships. */
        if (s->filesize && ((s->vmaddr ^ s->fileoff) & 4095u) != 0u)
            return bad(error, error_size, "misaligned file/VM segment offset");
        last = s->vmaddr + s->vmsize; /* parser checked overflow */
        if (last > UINT64_MAX - (uint64_t)(page_size - 1u))
            return bad(error, error_size, "segment page rounding overflow");
        aligned_last = (last + (uint64_t)(page_size - 1u)) &
                       ~((uint64_t)page_size - 1u);
        if ((s->vmaddr & ~((uint64_t)page_size - 1u)) < low)
            low = s->vmaddr & ~((uint64_t)page_size - 1u);
        if (aligned_last > high) high = aligned_last;

        /* Page-level isolation is required even if byte ranges do not overlap. */
        for (size_t j = 0u; j < i; ++j) {
            const ios27_segment *p = &parsed.segments[j];
            uint64_t p_end;
            if (!p->vmsize || strcmp(p->name, "__PAGEZERO") == 0) continue;
            p_end = (p->vmaddr + p->vmsize + (uint64_t)(page_size - 1u)) &
                    ~((uint64_t)page_size - 1u);
            /* Disjoint VM mappings may not alias the same file-backed bytes. */
            if (s->filesize && p->filesize &&
                s->fileoff < p->fileoff + p->filesize &&
                p->fileoff < s->fileoff + s->filesize)
                return bad(error, error_size, "overlapping segment file ranges");
            if ((p->vmaddr & ~((uint64_t)page_size - 1u)) < aligned_last &&
                (s->vmaddr & ~((uint64_t)page_size - 1u)) < p_end)
                return bad(error, error_size, "overlapping host mapping pages");
        }
    }
    /* LC_MAIN is a file offset; map it relative to __TEXT's file offset. */
    if (!text_found || parsed.entryoff < text_fileoff ||
        parsed.entryoff - text_fileoff > UINT64_MAX - text_vm)
        return bad(error, error_size, "missing __TEXT or overflowing LC_MAIN");
    entry = text_vm + (parsed.entryoff - text_fileoff);
    if ((entry & 3u) != 0u)
        return bad(error, error_size, "unaligned ARM64 LC_MAIN entry");
    for (size_t i = 0u; i < parsed.segment_count; ++i) {
        const ios27_segment *s = &parsed.segments[i];
        if ((s->initprot & 4u) && entry >= s->vmaddr &&
            entry - s->vmaddr <= s->filesize &&
            s->filesize - (entry - s->vmaddr) >= 4u) {
            /*
             * Even if mapped RX, Mach-O load commands are not ARM64 code.
             * Reject LC_MAIN offsets which point into the file header.
             */
            if (s->fileoff + (entry - s->vmaddr) <
                32u + (uint64_t)u32(data + 20u))
                return bad(error, error_size, "entry in Mach-O load commands");
            if (entry_found++)
                return bad(error, error_size, "ambiguous LC_MAIN entry");
        }
    }
    if (!entry_found || low == UINT64_MAX || high <= low ||
        low < pagezero_end ||
        high - low > (256ull * 1024ull * 1024ull) ||
        high - low > (uint64_t)SIZE_MAX)
        return bad(error, error_size, "unmapped LC_MAIN or invalid span");
    layout->min_vmaddr = low;
    layout->entry_vmaddr = entry;
    layout->span = (size_t)(high - low);
    layout->page_size = page_size;
    if (error && error_size) error[0] = '\0';
    return 0;
}

/* An LC_MAIN offset is relative to the __TEXT VM base, not the file start.
 * Deliberately separate from freestanding-layout preflight: the graph loader
 * can resolve explicitly supplied dependent images before exposing this RX
 * entry, without claiming support for dyld or Darwin process initialization. */
int ios27_macho_entry_vm(const ios27_macho_info *info, uint64_t *address) {
    uint64_t text_base = 0u, text_fileoff = 0u, entry;
    unsigned text_found = 0u, code_found = 0u;
    if (!info || !address || info->filetype != IOS27_MH_EXECUTE ||
        !info->has_main || info->unsupported_required ||
        info->segment_count > IOS27_MAX_SEGMENTS)
        return -1;
    for (size_t i = 0u; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        if (strcmp(s->name, "__TEXT") == 0) {
            if (text_found++) return -1;
            text_base = s->vmaddr;
            text_fileoff = s->fileoff;
        }
    }
    if (text_found != 1u || info->entryoff < text_fileoff ||
        info->entryoff - text_fileoff > UINT64_MAX - text_base)
        return -1;
    entry = text_base + (info->entryoff - text_fileoff);
    if ((entry & 3u) != 0u) return -1;
    for (size_t i = 0u; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        uint64_t delta;
        if (entry < s->vmaddr) continue;
        delta = entry - s->vmaddr;
        if (delta >= s->vmsize) continue;
        if (!(s->initprot & 4u) || s->filesize < 4u ||
            delta > s->filesize - 4u || code_found++)
            return -1;
    }
    if (code_found != 1u) return -1;
    *address = entry;
    return 0;
}
