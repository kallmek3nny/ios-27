#ifndef IOS27_EXPORTS_H
#define IOS27_EXPORTS_H

#include <stddef.h>
#include <stdint.h>

#define IOS27_MAX_EXPORTS 256u
#define IOS27_EXPORT_NAME_MAX 256u

/* Mach-O dyld export-trie flags (Apple mach-o/loader.h). */
#define IOS27_EXPORT_REEXPORT 0x08u
#define IOS27_EXPORT_STUB_RESOLVER 0x10u

typedef struct {
    char name[IOS27_EXPORT_NAME_MAX];
    char imported_name[IOS27_EXPORT_NAME_MAX];
    uint64_t flags;
    uint64_t address;      /* image-relative offset, not a host pointer */
    uint64_t resolver;     /* image-relative, for STUB_AND_RESOLVER */
    uint64_t ordinal;      /* library ordinal, for REEXPORT */
} ios27_export;

typedef struct {
    size_t count;
    unsigned has_trie : 1;
    ios27_export entries[IOS27_MAX_EXPORTS];
} ios27_exports;

/* Read-only, bounded decoder for a *thin* ARM64 Mach-O.
 * Reads LC_DYLD_EXPORTS_TRIE or LC_DYLD_INFO(_ONLY). Does not bind,
 * execute, decrypt, map Apple frameworks, or access the filesystem. */
int ios27_exports_parse(const uint8_t *image, size_t size,
                        ios27_exports *out,
                        char *error, size_t error_capacity);

/* Returns NULL when not found. Pointers belong to 'exports'. */
const ios27_export *ios27_exports_find(const ios27_exports *exports,
                                       const char *symbol_name);
#endif
