#ifndef IOS27_DYLD_PATHS_H
#define IOS27_DYLD_PATHS_H
#include <stddef.h>
#include <stdint.h>
#define IOS27_DYLD_MAX_CANDIDATES 16u
#define IOS27_DYLD_PATH_MAX 512u
#define IOS27_DYLD_MAX_DEPENDENCIES 32u

typedef struct {
    size_t count;
    char paths[IOS27_DYLD_MAX_CANDIDATES][IOS27_DYLD_PATH_MAX];
} ios27_dyld_candidates;

/* Pure string resolver for bounded, NUL-terminated guest-virtual paths.
 * No host filesystem access, searching, or library loading. Results are
 * canonical absolute guest paths in stable rpath order, without duplicates.
 * Any error leaves out->count == 0 (transactional, no partial candidates).
 * Traversal above guest root and unsupported nested @rpath are rejected. */
int ios27_dyld_paths(const char *install_name, const char *loader_image,
                     const char *main_executable, const char *const *rpaths,
                     size_t rpath_count, ios27_dyld_candidates *out,
                     char *error, size_t error_size);

/* Thin ARM64 Mach-O dyld metadata; dyld load commands, not a loader.
 * The command number is retained so consumers can distinguish optional
 * weak imports, reexports, upward dependencies, and ordinary imports. */
#define IOS27_DYLD_LC_LOAD_DYLIB 0x0cu
#define IOS27_DYLD_LC_ID_DYLIB 0x0du
#define IOS27_DYLD_LC_LOAD_WEAK_DYLIB 0x80000018u
#define IOS27_DYLD_LC_RPATH 0x8000001cu
#define IOS27_DYLD_LC_REEXPORT_DYLIB 0x8000001fu
#define IOS27_DYLD_LC_LOAD_UPWARD_DYLIB 0x80000023u
#define IOS27_DYLD_LC_LAZY_LOAD_DYLIB 0x20u

typedef struct {
    uint32_t command; /* one of IOS27_DYLD_LC_* import commands */
    char install_name[IOS27_DYLD_PATH_MAX];
} ios27_dyld_dependency;

typedef struct {
    size_t rpath_count;
    char rpaths[IOS27_DYLD_MAX_CANDIDATES][IOS27_DYLD_PATH_MAX];
    size_t dependency_count; /* exact ordinal order, including weak imports */
    ios27_dyld_dependency dependencies[IOS27_DYLD_MAX_DEPENDENCIES];
    unsigned has_id : 1;
    char id[IOS27_DYLD_PATH_MAX];
} ios27_dyld_image_metadata;

/* Inspect bounded load-command string metadata. Requires a parseable thin
 * ARM64 Mach-O, rejects malformed string offsets, overlong/unterminated
 * names, duplicate LC_ID_DYLIB and excessive counts. Never maps/executes
 * binaries or expands dyld tokens. Output is cleared on *any* error. */
int ios27_dyld_inspect_image(const uint8_t *image, size_t size,
                             ios27_dyld_image_metadata *out,
                             char *error, size_t error_size);

typedef struct {
    size_t image_index;     /* index in explicit guest image catalog */
    size_t candidate_index; /* rpath precedence position */
    char guest_path[IOS27_DYLD_PATH_MAX];
} ios27_dyld_catalog_match;

/* Match decoded Mach-O dependency against a bounded, explicitly provided
 * catalog of guest paths. No host filesystem, dynamic linking or execution.
 * Returns 0 for a match, 1 absent, -1 malformed (including catalog aliases).
 * Output cleared on error or absence. */
int ios27_dyld_match_dependency(const ios27_dyld_image_metadata *metadata,
                                 size_t dependency_index,
                                 const char *loader_image,
                                 const char *main_executable,
                                 const char *const *guest_image_paths,
                                 size_t image_count,
                                 ios27_dyld_catalog_match *out,
                                 char *error, size_t error_size);
#endif
