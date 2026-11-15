#ifndef IOS27_IMAGES_H
#define IOS27_IMAGES_H

#include <stddef.h>
#include <stdint.h>

#include "ios27/process.h"

#define IOS27_IMAGES_MAX 8u
#define IOS27_IMAGE_NAME_MAX 64u

/*
 * Every Mach-O and dependency must be supplied explicitly by the caller.
 * No host paths, Apple binaries, kernel APIs, root, or implicit lookups.
 * The loader is for trusted, locally constructed ARM64 Linux test fixtures.
 */
typedef struct {
    const char *name;
    const uint8_t *file;
    size_t size;
    const char *const *dependencies; /* ordered LC_LOAD_DYLIB names */
    size_t dependency_count;
} ios27_image_spec;

typedef enum {
    IOS27_IMAGE_PARSED = 1,
    IOS27_IMAGE_MAPPED = 2,
    IOS27_IMAGE_RELOCATED = 3,
    IOS27_IMAGE_READY = 4,
    IOS27_IMAGE_RETIRED = 5
} ios27_image_state;

typedef struct ios27_images ios27_images;

/*
 * Validates the complete dependency DAG, maps into anonymous private Linux
 * userspace pages, resolves imports via existing classic/chained fixups,
 * and seals segment pages with W^X protections. On any error the graph is
 * destroyed and *out stays NULL. Never execute untrusted/Apple binaries.
 */
int ios27_images_load(const ios27_image_spec *specs, size_t count,
                      ios27_images **out, char *error, size_t cap);

/* Resolve Mach-O install names against explicitly supplied canonical guest
 * paths. No filesystem access. main_executable is a canonical absolute guest
 * path; LC_RPATH from a matching main image is inherited by dependent dylibs.
 * Exact ordinal names remain in specs[].dependencies for validation.
 * All failures leave *out == NULL and roll back the entire graph. */
int ios27_images_load_with_paths(const ios27_image_spec *specs, size_t count,
                                 const char *const *guest_paths,
                                 const char *main_executable,
                                 ios27_images **out, char *error, size_t cap);

/* Dependency-first initialization plan, without running guest constructors.
 * The returned name remains owned by the live image graph. */
int ios27_images_initialization_image(const ios27_images *images,
                                      size_t position, const char **name);

/* Reverse dependency order for caller-managed finalizers. This is an
 * observation API only: no untrusted guest callbacks run on unload. */
int ios27_images_finalization_image(const ios27_images *images,
                                    size_t position, const char **name);

/*
 * Return an RX entry pointer for a loaded freestanding MH_EXECUTE with
 * validated LC_MAIN. Caller must keep the graph alive while using it.
 * The loader does not provide Darwin ABI, dyld or untrusted-code isolation.
 */
int ios27_images_entrypoint(const ios27_images *images, const char *name,
                            uintptr_t *address);

/* Finds an export address of one explicitly loaded image; does not execute. */
int ios27_images_symbol(const ios27_images *images, const char *image,
                        const char *symbol, uintptr_t *address);

/* Observe a live image's final state; no transition after unload is exposed. */
int ios27_images_status(const ios27_images *images, const char *image,
                        ios27_image_state *state);

/* Execute a trusted original-source C graph in a fresh supervised fork.
 * The whole root dependency closure's modern __init_offsets tables are
 * validated first. Dependency-first constructors run before LC_MAIN on the
 * private Darwin startup stack. Each invocation gets independent COW state;
 * constructors never modify the parent graph. No constructors run on load.
 *
 * Legacy pointer constructors, termination hooks, ObjC, Swift and TLS setup
 * are refused. This is not an Apple-framework bootstrap. The existing native
 * syscall filter/deadline apply during constructors as well as main.
 * Requires native ARM64 Linux/Android and a single-threaded launcher with
 * default SIGCHLD. Keep graph and immutable input buffers alive throughout.
 * On failure result is cleared. Normal return is the full-width main result.
 */
int ios27_images_run_contained(const ios27_images *images, const char *root,
                               const ios27_startup_context *context,
                               const ios27_process_options *options,
                               ios27_process_result *result,
                               char *error, size_t capacity);

/* Unmaps all VM and frees all staged buffers, safe when images == NULL. */
void ios27_images_unload(ios27_images *images);

#endif
