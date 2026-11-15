#include "ios27/initializers.h"
#include "ios27/macho.h"

#include <stdio.h>
#include <string.h>

/* Layout/section constants: Apple's open-source cctools include/mach-o/loader.h.
 * S_INIT_FUNC_OFFSETS (0x16) is also decoded by dyld MachOAnalyzer's
 * forEachInitializer. No Apple implementation code is embedded here. */
#define SECTION_LIMIT 256u
#define INSTRUCTIONS UINT32_C(0x80000400)

typedef struct { uint64_t begin, end; } code_range;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u |
           (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4u) << 32u;
}
static int fail(char *error, size_t size, const char *message) {
    if (error && size) (void)snprintf(error, size, "%s", message);
    return -1;
}
static int span(uint64_t start, uint64_t size, uint64_t base, uint64_t capacity) {
    return start >= base && start - base <= capacity && size <= capacity - (start - base);
}
static int is_code(uint64_t address, const code_range *ranges, size_t count) {
    if (address & 3u) return 0;
    for (size_t i = 0u; i < count; ++i)
        if (address >= ranges[i].begin && address < ranges[i].end &&
            ranges[i].end - address >= 4u) return 1;
    return 0;
}

int ios27_initializers_inspect(const uint8_t *data, size_t size,
                               ios27_initializer_plan *out,
                               char *error, size_t error_size) {
    ios27_macho_info info;
    ios27_initializer_plan staged = {0};
    code_range code[SECTION_LIMIT];
    size_t code_count = 0u, section_count = 0u, cursor = 32u;
    uint64_t text_vm = 0u, text_file = 0u, table_file = 0u, table_size = 0u;
    unsigned text_found = 0u, table_found = 0u;
    if (!out) return fail(error, error_size, "null initializer plan");
    memset(out, 0, sizeof(*out));
    if (ios27_macho_parse(data, size, &info, error, error_size)) return -1;
    if ((info.filetype != 2u && info.filetype != 6u) || info.cpusubtype != 0u || info.encrypted)
        return fail(error, error_size, "initializers require an unencrypted generic ARM64 linked image");
    for (size_t i = 0u; i < info.segment_count; ++i) {
        const ios27_segment *a = &info.segments[i];
        for (size_t j = 0u; j < i; ++j) {
            const ios27_segment *b = &info.segments[j];
            if ((a->vmsize && b->vmsize && a->vmaddr < b->vmaddr + b->vmsize &&
                 b->vmaddr < a->vmaddr + a->vmsize) ||
                (a->filesize && b->filesize && a->fileoff < b->fileoff + b->filesize &&
                 b->fileoff < a->fileoff + a->filesize))
                return fail(error, error_size, "overlapping initializer image segments");
        }
    }
    for (uint32_t i = 0u; i < rd32(data + 16u); ++i) {
        const uint8_t *command = data + cursor;
        if (rd32(command) == 0x1au) /* LC_ROUTINES_64 */
            staged.unsupported |= IOS27_INIT_LEGACY_POINTERS;
        if (rd32(command) == 0x19u) {
            uint64_t vm = rd64(command + 24u), capacity = rd64(command + 32u);
            uint64_t file = rd64(command + 40u), bytes = rd64(command + 48u);
            uint32_t maximum = rd32(command + 56u), protection = rd32(command + 60u);
            uint32_t count = rd32(command + 64u);
            if (maximum > 7u || (maximum & 6u) == 6u || (protection & ~maximum) ||
                rd32(command + 4u) != 72u + (uint64_t)count * 80u ||
                count > SECTION_LIMIT - section_count)
                return fail(error, error_size, "invalid initializer segment or section count");
            section_count += count;
            if (!memcmp(command + 8u, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16u)) {
                if (text_found++) return fail(error, error_size, "duplicate initializer text segment");
                text_vm = vm;
                text_file = file;
            }
            for (uint32_t j = 0u; j < count; ++j) {
                const uint8_t *section = command + 72u + (size_t)j * 80u;
                uint64_t addr = rd64(section + 32u), length = rd64(section + 40u);
                uint64_t offset = rd32(section + 48u);
                uint32_t flags = rd32(section + 64u), type = flags & 255u;
                int zerofill = type == 1u || type == 0xcu || type == 0x12u;
                if (memcmp(section + 16u, command + 8u, 16u) ||
                    !span(addr, length, vm, capacity) || rd32(section + 60u))
                    return fail(error, error_size, "invalid linked initializer section");
                if (!zerofill && (!span(offset, length, file, bytes) ||
                                 offset - file != addr - vm))
                    return fail(error, error_size, "initializer section file/VM mismatch");
                for (uint32_t k = 0u; k < j; ++k) {
                    const uint8_t *prior = command + 72u + (size_t)k * 80u;
                    uint64_t start = rd64(prior + 32u), prior_size = rd64(prior + 40u);
                    if (length && prior_size && addr < start + prior_size && start < addr + length)
                        return fail(error, error_size, "overlapping initializer sections");
                }
                if (type == 9u) staged.unsupported |= IOS27_INIT_LEGACY_POINTERS;
                if (type == 10u) staged.unsupported |= IOS27_INIT_TERMINATORS;
                if ((!memcmp(section, "__mod_init_func\0", 16u) && type != 9u) ||
                    (!memcmp(section, "__mod_term_func\0", 16u) && type != 10u))
                    return fail(error, error_size, "mismatched lifecycle section type");
                if (type >= 0x11u && type <= 0x15u) staged.unsupported |= IOS27_INIT_TLS;
                if (!memcmp(section, "__objc", 6u)) staged.unsupported |= IOS27_INIT_OBJC;
                if (!memcmp(section, "__swift", 7u)) staged.unsupported |= IOS27_INIT_SWIFT;
                if ((flags & INSTRUCTIONS) && length) {
                    if ((type != 0u && type != 8u) || protection != 5u || (addr & 3u) || (length & 3u) ||
                        offset < 32u + (uint64_t)rd32(data + 20u))
                        return fail(error, error_size, "invalid file-backed initializer code section");
                    if (type == 8u) {
                        /* Generic ARM64 LLD import stubs are ADRP/LDR/BR,
                         * twelve bytes each. They are valid executable image
                         * content, but are never initializer/main targets. */
                        if (rd32(section + 72u) != 12u || length % 12u)
                            return fail(error, error_size, "unsupported ARM64 symbol stub size");
                    } else {
                        code[code_count++] = (code_range){addr, addr + length};
                    }
                }
                if (type == 0x16u || !memcmp(section, "__init_offsets\0\0", 16u)) {
                    if (table_found++ || memcmp(section, "__init_offsets\0\0", 16u) ||
                        memcmp(section + 16u, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16u) ||
                        flags != 0x16u || protection != 5u ||
                        (addr & 3u) || (offset & 3u) || (length & 3u) ||
                        rd32(section + 52u) < 2u || rd32(section + 52u) > 14u ||
                        length / 4u > IOS27_INITIALIZERS_MAX ||
                        offset < 32u + (uint64_t)rd32(data + 20u))
                        return fail(error, error_size, "invalid or duplicate initializer offset table");
                    table_file = offset;
                    table_size = length;
                    uint64_t mask = (UINT64_C(1) << rd32(section + 52u)) - 1u;
                    if ((addr & mask) || (offset & mask))
                        return fail(error, error_size, "initializer table violates declared alignment");
                }
            }
        }
        cursor += rd32(command + 4u);
    }
    if (text_found != 1u) return fail(error, error_size, "missing initializer text segment");
    for (size_t i = 0u; i < table_size / 4u; ++i) {
        uint32_t offset = rd32(data + (size_t)table_file + i * 4u);
        if (offset > UINT64_MAX - text_vm || !is_code(text_vm + offset, code, code_count))
            return fail(error, error_size, "initializer target is outside image-owned code");
        /* Table order is meaningful; duplicate callbacks are not deduplicated. */
        staged.addresses[staged.count++] = text_vm + offset;
    }
    if (info.filetype == 2u) {
        if (!info.has_main || info.entryoff < text_file ||
            info.entryoff - text_file > UINT64_MAX - text_vm ||
            !is_code(text_vm + info.entryoff - text_file, code, code_count))
            return fail(error, error_size, "initialized LC_MAIN is outside image-owned code");
        staged.entry_vmaddr = text_vm + info.entryoff - text_file;
    }
    staged.stack_size = info.stacksize;
    *out = staged;
    if (error && error_size) error[0] = '\0';
    return 0;
}
