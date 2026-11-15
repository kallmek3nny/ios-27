#ifndef IOS27_IMAGES_INTERNAL_H
#define IOS27_IMAGES_INTERNAL_H

#include "ios27/images.h"
#include "ios27/initializers.h"

#include <stddef.h>

/*
 * Internal page-size seam for the image graph loader (crew card E, t-921d7d).
 * This is not part of the public API. The graph loader, native runner and
 * in-tree tests share this header; include/ios27/images.h does not declare it.
 *
 * page_size == 0 keeps the public behaviour and uses the real host page from
 * sysconf(_SC_PAGESIZE). A nonzero page_size must be a power of two in
 * [4096, 65536] and a whole multiple of the real host page. So a 4 KiB host
 * can lay out, map and W^X-seal a graph on 16 KiB boundaries, but a 16 KiB
 * host is never asked to use 4 KiB boundaries. Mapping bases and protection
 * lengths use the selected granule; alignment slack stays inaccessible and
 * is released with the graph. guest_paths and
 * main_executable work as in ios27_images_load_with_paths. Passing NULL for
 * both selects the plain-name mode of ios27_images_load.
 *
 * A simulated 16384 on 4 KiB hardware checks only the loader's alignment and
 * protection arithmetic. It is not 16 KiB hardware validation.
 */
int ios27_images_load_with_page_size(const ios27_image_spec *specs,
                                     size_t count,
                                     const char *const *guest_paths,
                                     const char *main_executable,
                                     size_t page_size, ios27_images **out,
                                     char *error, size_t cap);

/* Fully validate the root's dependency closure before dispatching any code.
 * Addresses refer to the caller-owned live graph; no lifetime is extended.
 * C-only modern offset initializers are supported; other runtime setup is
 * refused. Failure clears the output. Unrelated catalog images are omitted. */
#define IOS27_GRAPH_INITIALIZERS_MAX (IOS27_IMAGES_MAX * IOS27_INITIALIZERS_MAX)
typedef struct {
    uintptr_t initializers[IOS27_GRAPH_INITIALIZERS_MAX];
    size_t count;
    uintptr_t entry;
    uintptr_t main_header;
    uint64_t stack_size;
} ios27_image_startup;

int ios27_images_prepare_startup(const ios27_images *images, const char *root,
                                 ios27_image_startup *out,
                                 char *error, size_t capacity);

#endif
