#ifndef IOS27_DYLD_PATHS_H
#define IOS27_DYLD_PATHS_H
#include <stddef.h>
#define IOS27_DYLD_MAX_CANDIDATES 16u
#define IOS27_DYLD_PATH_MAX 512u

typedef struct {
    size_t count;
    char paths[IOS27_DYLD_MAX_CANDIDATES][IOS27_DYLD_PATH_MAX];
} ios27_dyld_candidates;

/* Pure string resolver for guest-virtual dyld install names. Never opens
 * or reads paths on Android; results do not mean libraries are loaded. */
int ios27_dyld_paths(const char *install_name, const char *loader_image,
                     const char *main_executable, const char *const *rpaths,
                     size_t rpath_count, ios27_dyld_candidates *out,
                     char *error, size_t error_size);
#endif
