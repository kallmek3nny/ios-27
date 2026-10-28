#ifndef IOS27_CHAINED_H
#define IOS27_CHAINED_H

#include "ios27/binds.h"
#include <stddef.h>
#include <stdint.h>

#define IOS27_CHAINED_MAX_PATCHES 256u
#define IOS27_CHAINED_MAX_IMPORTS 256u

typedef struct {
    char symbol[IOS27_BIND_SYMBOL_MAX];
    int32_t ordinal;
    int32_t addend; /* signed DYLD_CHAINED_IMPORT_ADDEND, zero for basic imports */
} ios27_chained_import;

typedef struct {
    size_t segment_index;
    uint64_t segment_offset;
    uint64_t raw;
    uint64_t rebase_target;
    uint32_t import_index;
    int8_t addend;
    unsigned is_bind : 1;
} ios27_chained_patch;

typedef struct {
    size_t import_count;
    size_t count;
    ios27_chained_import imports[IOS27_CHAINED_MAX_IMPORTS];
    ios27_chained_patch patches[IOS27_CHAINED_MAX_PATCHES];
} ios27_chained_plan;

/*
 * Parse LC_DYLD_CHAINED_FIXUPS on a *synthetic*, thin, unencrypted ARM64
 * Mach-O file. Only DYLD_CHAINED_PTR_64_OFFSET (6), plain 32-bit imports
 * (1) and signed-addend imports (2), uncompressed symbols (0),
 * single-start pages and unauthenticated
 * bind/rebase pointers are supported. Every other layout fails closed.
 *
 * No host code or memory is modified by parsing.
 */
int ios27_chained_parse(const uint8_t *file, size_t size,
                        const ios27_macho_info *info,
                        ios27_chained_plan *out,
                        char *error, size_t error_size);

/*
 * Resolve all imports and relocations before writing a single byte. 'copy'
 * must be a caller-owned COPY of the original file bytes. 'load_base' is
 * the runtime mapping address of the image's lowest segment vmaddr (not a
 * kernel privilege or page mapping operation). No files, VM permissions,
 * executable memory, or device resources are changed by this function.
 */
int ios27_chained_apply(uint8_t *copy, size_t size,
                        const ios27_macho_info *info,
                        const ios27_chained_plan *plan,
                        uint64_t load_base,
                        ios27_bind_resolver resolver, void *context,
                        char *error, size_t error_size);

#endif
