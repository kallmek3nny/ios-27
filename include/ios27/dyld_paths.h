#ifndef IOS27_DYLD_PATHS_H
#define IOS27_DYLD_PATHS_H
#include <stddef.h>
#define IOS27_DYLD_MAX_CANDIDATES 16u
#define IOS27_DYLD_PATH_MAX 512u

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
#endif
