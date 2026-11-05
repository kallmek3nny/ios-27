#ifndef IOS27_PHASE23A_LOADER_H
#define IOS27_PHASE23A_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include "ios27/images.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit, caller-owned catalog: no host filesystem searches or arbitrary dlopen.
 * The catalog and its bytes/dependency arrays must outlive the manager.
 * Maximum IOS27_IMAGES_MAX entries. Concurrent operations are serialized.
 * Destruction requires all external workers and guest calls to have stopped.
 */
typedef struct ios27_phase23a_loader ios27_phase23a_loader;
typedef uint64_t ios27_phase23a_handle;

typedef enum {
    IOS27_PHASE23A_OK = 0,
    IOS27_PHASE23A_NOT_FOUND = 1,
    IOS27_PHASE23A_INVALID = -1,
    IOS27_PHASE23A_LIMIT = -2,
    IOS27_PHASE23A_LOAD_FAILED = -3
} ios27_phase23a_result;

/* A valid dlsym address belongs to the handle's mapping cohort. It stays
 * valid until its last corresponding handle is closed, not beyond that point.
 * Different root cohorts may contain independent mappings of shared dylibs.
 * This deliberately does not claim Darwin's global single-image semantics.
 */
int ios27_phase23a_create(const ios27_image_spec *catalog, size_t count,
                          ios27_phase23a_loader **out,
                          char *error, size_t error_cap);
void ios27_phase23a_destroy(ios27_phase23a_loader *loader);

int ios27_phase23a_open(ios27_phase23a_loader *loader, const char *name,
                        ios27_phase23a_handle *out,
                        char *error, size_t error_cap);
int ios27_phase23a_close(ios27_phase23a_loader *loader,
                         ios27_phase23a_handle handle,
                         char *error, size_t error_cap);

/* Two-level handle lookup: only directly exported/reexported names of the
 * selected image. Never infer a symbol from a private/unexported nlist. */
int ios27_phase23a_symbol(ios27_phase23a_loader *loader,
                          ios27_phase23a_handle handle,
                          const char *symbol, uintptr_t *out,
                          char *error, size_t error_cap);

/* Deterministic flat search over loaded cohorts, in catalog order. Strong
 * definitions take precedence over weak definitions, with stable tie breaks.
 * Addresses are borrowed; concurrent close must not race their use. */
int ios27_phase23a_flat_symbol(ios27_phase23a_loader *loader,
                               const char *symbol, uintptr_t *out,
                               char *error, size_t error_cap);

/* Reachability references, including active root handles protecting every
 * transitive dependency. Not a physical VM mapping count. */
int ios27_phase23a_reference_count(ios27_phase23a_loader *loader,
                                   const char *name, uint64_t *out);

/* Counters describe actual completed operations, never synthetic successes. */
typedef struct {
    uint64_t opens;
    uint64_t closes;
    uint64_t lookups;
    uint64_t failures;
    uint64_t mapping_loads;
    uint64_t mapping_unloads;
    size_t live_roots;
} ios27_phase23a_diagnostics;
int ios27_phase23a_diagnostics_read(ios27_phase23a_loader *loader,
                                    ios27_phase23a_diagnostics *out);
#ifdef __cplusplus
}
#endif
#endif
