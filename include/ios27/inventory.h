#ifndef IOS27_INVENTORY_H
#define IOS27_INVENTORY_H

#include <stddef.h>
#include <stdint.h>

#define IOS27_MAX_IMPORTS 128u
#define IOS27_IMPORT_NAME_MAX 256u

typedef struct {
    char path[IOS27_IMPORT_NAME_MAX];
    uint32_t command;
} ios27_import;

typedef struct {
    size_t slice_offset;
    size_t slice_size;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    size_t imports_count;
    size_t frameworks_count;
    size_t private_frameworks_count;
    unsigned is_fat : 1;
    unsigned is_arm64e : 1;
    unsigned needs_dyld : 1;
    unsigned encrypted : 1;
    unsigned has_fixups : 1;
    unsigned has_main : 1;
    ios27_import imports[IOS27_MAX_IMPORTS];
} ios27_inventory;

/* Select a generic ARM64 slice from a FAT/FAT64 or thin Mach-O and inspect
 * its linked libraries. Data is never executed, written, or decrypted. */
int ios27_inventory_parse(const uint8_t *input, size_t size,
                          ios27_inventory *out,
                          char *error, size_t error_size);

/* Apple frameworks, a native compositor and SpringBoard are not supported. */
int ios27_inventory_native_ui_ready(const ios27_inventory *inv);

#endif
