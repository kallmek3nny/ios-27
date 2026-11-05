#ifndef IOS27_PHASE24A_REGISTRY_H
#define IOS27_PHASE24A_REGISTRY_H

#include <stddef.h>
#include <stdint.h>
#include "ios27/images.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOS27_PHASE24A_MAX_HANDLES 64u
#define IOS27_PHASE24A_MAX_PINS 64u

typedef struct ios27_phase24a_registry ios27_phase24a_registry;
typedef uint64_t ios27_phase24a_handle;
typedef uint64_t ios27_phase24a_pin;

enum {
    IOS27_PHASE24A_OK = 0,
    IOS27_PHASE24A_NOT_FOUND = 1,
    IOS27_PHASE24A_INVALID = -1,
    IOS27_PHASE24A_LIMIT = -2,
    IOS27_PHASE24A_MAP_FAILED = -3
};

typedef struct {
    uint64_t opens;
    uint64_t closes;
    uint64_t lookups;
    uint64_t failures;
    uint64_t graph_maps;
    uint64_t graph_unmaps;
    uint64_t live_handles;
    uint64_t active_pins;
} ios27_phase24a_stats;

/*
 * Owned only by this phase. The supplied Mach-O bytes and ordered dependency
 * strings remain caller-owned and MUST outlive the registry. Paths are
 * canonical absolute guest paths, not host filenames. The catalog is capped
 * by the existing eight-image mapped-graph backend.
 *
 * The current implementation maps ONE common, fully validated catalog graph
 * upon the first open, then publishes that same mapping to all handles.
 * It does not incrementally map unrelated images or offer full dyld semantics.
 * All guest code must be TRUSTED. Destroy only after every worker has joined.
 */
int ios27_phase24a_create(const ios27_image_spec *specs,
                           const char *const *guest_paths, size_t count,
                           const char *main_executable,
                           ios27_phase24a_registry **out,
                           char *error, size_t error_size);
void ios27_phase24a_destroy(ios27_phase24a_registry *registry);

int ios27_phase24a_open(ios27_phase24a_registry *registry,
                        const char *guest_path, ios27_phase24a_handle *out,
                        char *error, size_t error_size);
int ios27_phase24a_close(ios27_phase24a_registry *registry,
                         ios27_phase24a_handle handle,
                         char *error, size_t error_size);

/* A successful symbol lookup returns a PIN protecting the entire mapping.
 * Keep the pin alive while executing guest code or reading guest data.
 * Release exactly once. Close may retire the handle while a pin remains.
 * No guest code or constructor is executed by these APIs.
 */
int ios27_phase24a_symbol(ios27_phase24a_registry *registry,
                          ios27_phase24a_handle handle, const char *symbol,
                          uintptr_t *address, ios27_phase24a_pin *pin,
                          char *error, size_t error_size);
int ios27_phase24a_flat_symbol(ios27_phase24a_registry *registry,
                               const char *symbol, uintptr_t *address,
                               ios27_phase24a_pin *pin,
                               char *error, size_t error_size);
int ios27_phase24a_release(ios27_phase24a_registry *registry,
                           ios27_phase24a_pin pin,
                           char *error, size_t error_size);

/* Logical root-closure reachability, not physical mapping counts. */
int ios27_phase24a_refs(ios27_phase24a_registry *registry,
                        const char *guest_path, uint64_t *out);
int ios27_phase24a_stats_read(ios27_phase24a_registry *registry,
                             ios27_phase24a_stats *out);
#ifdef __cplusplus
}
#endif
#endif
