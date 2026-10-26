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
        }
        cursor += (size_t)cmdsize;
    }
    if (cursor != end)
        return bad(error, error_size, "load command byte count mismatch");
    if (error != NULL && error_size != 0u)
        error[0] = '\0';
    return 0;
}
