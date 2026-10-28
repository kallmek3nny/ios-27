#ifndef IOS27_IMAGES_H
#define IOS27_IMAGES_H

#include <stddef.h>
#include <stdint.h>

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

/* Unmaps all VM and frees all staged buffers, safe when images == NULL. */
void ios27_images_unload(ios27_images *images);

#endif
