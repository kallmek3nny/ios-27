#include "ios27/inventory.h"
#include "ios27/macho.h"
#include <stdio.h>
#include <string.h>

#define FAT_MAGIC 0xcafebabeu
#define FAT_MAGIC_64 0xcafebabfu
#define CPU_TYPE_ARM64 0x0100000cu
#define CPU_SUBTYPE_ARM64E 2u
#define LC_LOAD_DYLIB 0x0cu
#define LC_LOAD_WEAK_DYLIB 0x80000018u
#define LC_REEXPORT_DYLIB 0x8000001fu
#define LC_LOAD_UPWARD_DYLIB 0x80000023u
#define LC_LAZY_LOAD_DYLIB 0x20u
#define LC_RPATH 0x8000001cu
#define LC_LOAD_DYLINKER 0x0eu
#define LC_ID_DYLIB 0x0du
#define MAX_SLICES 64u

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t rd_be64(const uint8_t *p) {
    return ((uint64_t)rd_be32(p) << 32) | rd_be32(p + 4);
}
static int range_ok(uint64_t off, uint64_t length, size_t size) {
    return off <= (uint64_t)size && length <= (uint64_t)size - off;
}
static int fail(char *error, size_t cap, const char *msg) {
    if (error && cap) (void)snprintf(error, cap, "%s", msg);
    return -1;
}

static int select_slice(const uint8_t *input, size_t size,
                        size_t *off, size_t *len, unsigned *is_fat,
                        char *error, size_t cap) {
    uint32_t magic, count;
    size_t stride, table_end;
    int best = -1;
    size_t best_off = 0, best_len = 0;
    if (size < 4) return fail(error, cap, "truncated header");
    magic = rd_be32(input);
    if (magic != FAT_MAGIC && magic != FAT_MAGIC_64) {
        *off = 0u; *len = size; *is_fat = 0u;
        return 0;
    }
    if (size < 8) return fail(error, cap, "truncated universal header");
    count = rd_be32(input + 4);
    stride = magic == FAT_MAGIC ? 20u : 32u;
    if (!count || count > MAX_SLICES || count > (size - 8u) / stride)
        return fail(error, cap, "invalid universal slice table");
    table_end = 8u + (size_t)count * stride;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = input + 8u + (size_t)i * stride;
        uint32_t cputype = rd_be32(p);
        uint32_t subtype = rd_be32(p + 4) & 0x00ffffffu;
        uint64_t offset = stride == 20u ? rd_be32(p + 8) : rd_be64(p + 8);
        uint64_t bytes = stride == 20u ? rd_be32(p + 12) : rd_be64(p + 16);
        uint32_t align = rd_be32(p + (stride == 20u ? 16 : 24));
        if (align >= 63u || offset < table_end ||
            !range_ok(offset, bytes, size) || bytes < 32u ||
            (offset & ((UINT64_C(1) << align) - 1u)) != 0u)
            return fail(error, cap, "invalid universal architecture bounds/alignment");
        if (cputype == CPU_TYPE_ARM64) {
            int rank = subtype == 0u ? 2 : (subtype == CPU_SUBTYPE_ARM64E ? 1 : 0);
            if (rank > best) {
                best = rank;
                best_off = (size_t)offset;
                best_len = (size_t)bytes;
            }
        }
    }
    if (best < 0)
        return fail(error, cap, "universal file contains no ARM64 slice");
    *off = best_off; *len = best_len; *is_fat = 1u;
    return 0;
}

static int is_import(uint32_t cmd) {
    return cmd == LC_LOAD_DYLIB || cmd == LC_LOAD_WEAK_DYLIB ||
           cmd == LC_REEXPORT_DYLIB || cmd == LC_LOAD_UPWARD_DYLIB ||
           cmd == LC_LAZY_LOAD_DYLIB;
}

int ios27_inventory_parse(const uint8_t *input, size_t size,
                          ios27_inventory *out,
                          char *error, size_t error_size) {
    ios27_macho_info info;
    size_t off, len, cursor, end;
    unsigned fat;
    const uint8_t *data;
    if (!input || !out) return fail(error, error_size, "null inventory input");
    memset(out, 0, sizeof(*out));
    if (select_slice(input, size, &off, &len, &fat, error, error_size))
        return -1;
    data = input + off;
    if (ios27_macho_parse(data, len, &info, error, error_size))
        return -1;
    out->slice_offset = off;
    out->slice_size = len;
    out->is_fat = fat;
    out->cputype = info.cputype;
    out->cpusubtype = info.cpusubtype;
    out->filetype = info.filetype;
    out->platform = info.platform;
    out->minos = info.minos;
    out->sdk = info.sdk;
    out->is_arm64e = ((info.cpusubtype & 0x00ffffffu) == CPU_SUBTYPE_ARM64E);
    out->needs_dyld = info.needs_dyld;
    out->encrypted = info.encrypted;
    out->has_fixups = info.has_fixups;
    out->has_main = info.has_main;
    cursor = 32u;
    end = cursor + (size_t)rd_le32(data + 20u);
    while (cursor < end) {
        uint32_t cmd = rd_le32(data + cursor);
        uint32_t cmdsize = rd_le32(data + cursor + 4u);
        if (cmdsize < 8u || (size_t)cmdsize > end - cursor)
            return fail(error, error_size, "corrupt load commands");
        if (is_import(cmd)) {
            ios27_import *item;
            uint32_t name_offset;
            const char *name;
            const void *terminator;
            size_t name_length;
            if (cmdsize < 24u)
                return fail(error, error_size, "short dylib command");
            name_offset = rd_le32(data + cursor + 8u);
            if (name_offset < 24u || name_offset >= cmdsize)
                return fail(error, error_size, "invalid dependency name offset");
            name = (const char *)(data + cursor + name_offset);
            terminator = memchr(name, 0, (size_t)cmdsize - name_offset);
            if (!terminator)
                return fail(error, error_size, "unterminated dependency path");
            name_length = (size_t)((const char *)terminator - name);
            if (!name_length || name_length >= IOS27_IMPORT_NAME_MAX ||
                out->imports_count == IOS27_MAX_IMPORTS)
                return fail(error, error_size, "dependency path empty, too long, or too many imports");
            item = &out->imports[out->imports_count++];
            memcpy(item->path, name, name_length + 1u);
            item->command = cmd;
            if (strncmp(name, "/System/Library/PrivateFrameworks/", 34u) == 0)
                out->private_frameworks_count++;
            else if (strncmp(name, "/System/Library/Frameworks/", 27u) == 0)
                out->frameworks_count++;
        }
        if (cmd == LC_RPATH || cmd == LC_LOAD_DYLINKER || cmd == LC_ID_DYLIB) {
            uint32_t str_offset;
            uint32_t min_offset = cmd == LC_ID_DYLIB ? 24u : 12u;
            if (cmdsize < min_offset)
                return fail(error, error_size, "short path load command");
            str_offset = rd_le32(data + cursor + 8u);
            if (str_offset < min_offset || str_offset >= cmdsize ||
                !memchr(data + cursor + str_offset, 0, (size_t)cmdsize - str_offset))
                return fail(error, error_size, "invalid path load command");
        }
        cursor += (size_t)cmdsize;
    }
    if (error && error_size) error[0] = '\0';
    return 0;
}

int ios27_inventory_native_ui_ready(const ios27_inventory *inv) {
    /* Parsing a real Apple binary does not mean we can run Apple frameworks,
     * the iOS compositor, or SpringBoard on Android. */
    (void)inv;
    return 0;
}
