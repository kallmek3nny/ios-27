#ifndef IOS27_PHASE22C_GRAPH_H
#define IOS27_PHASE22C_GRAPH_H

#include "ios27/images.h"
#include "ios27/exports.h"
#include <stddef.h>
#include <stdint.h>

#define IOS27_22C_MAX_IMAGES IOS27_IMAGES_MAX
#define IOS27_22C_MAX_LEASES 64u
#define IOS27_22C_DIAG_SIZE 192u

typedef enum {
    IOS27_22C_OK = 0,
    IOS27_22C_INPUT,
    IOS27_22C_MACHO,
    IOS27_22C_DEPENDENCY,
    IOS27_22C_CYCLE,
    IOS27_22C_EXPORT,
    IOS27_22C_FIXUP,
    IOS27_22C_LIFECYCLE,
    IOS27_22C_BUSY
} ios27_22c_reason;

typedef struct {
    ios27_22c_reason reason;
    size_t image;
    size_t dependency;
    char message[IOS27_22C_DIAG_SIZE];
} ios27_22c_diagnostic;

typedef struct ios27_22c_graph ios27_22c_graph;
typedef struct { uint64_t serial; } ios27_22c_lease;

/* Borrowed immutable file bytes must remain alive until graph destruction.
 * No host filesystem search: inputs must be explicitly provided. */
int ios27_22c_create(const ios27_image_spec *specs, size_t count,
                      ios27_22c_graph **out, ios27_22c_diagnostic *diag);

/* Preflight uses Mach-O metadata and export trie; actual relocation and W^X
 * mapping are delegated, without editing them, to ios27_images_load. */
int ios27_22c_load(ios27_22c_graph *graph, ios27_22c_diagnostic *diag);

/* Returns dependency-first image names, or reverse dependency-first for fini.
 * These are plans only: guest constructors are never called implicitly. */
int ios27_22c_order(const ios27_22c_graph *graph, size_t position,
                     int reverse, const char **name);

/* Result is metadata, not an executable host address. For special ordinals:
 * 0=self, -1=main executable, -2=flat, -3=weak flat. Missing weak -> 1.
 * No unrestricted dlsym, host paths, dyld cache or arm64e PAC. */
int ios27_22c_resolve(const ios27_22c_graph *graph, size_t importer,
                       int32_t ordinal, const char *symbol,
                       ios27_export_resolution *out,
                       ios27_22c_diagnostic *diag);

/* Leases protect the owning image and its transitive dependency closure.
 * Guest entry addresses must not be saved beyond lease release. */
int ios27_22c_acquire(ios27_22c_graph *graph, size_t image,
                       ios27_22c_lease *lease, ios27_22c_diagnostic *diag);
int ios27_22c_release(ios27_22c_graph *graph, ios27_22c_lease lease);
int ios27_22c_symbol(const ios27_22c_graph *graph, ios27_22c_lease lease,
                      size_t provider, const char *symbol, uintptr_t *address);
size_t ios27_22c_pins(const ios27_22c_graph *graph, size_t image);

/* Callbacks are caller-owned/trusted instrumentation, NOT guest binary code.
 * All initializers are dependency first, rollback finalizers reverse order.
 * Failure to initialize leaves the graph loaded but no callbacks active. */
typedef int (*ios27_22c_init_fn)(void *context, size_t index);
typedef void (*ios27_22c_fini_fn)(void *context, size_t index);
int ios27_22c_initialize(ios27_22c_graph *graph,
                          ios27_22c_init_fn init, ios27_22c_fini_fn fini,
                          void *context, ios27_22c_diagnostic *diag);
int ios27_22c_unload(ios27_22c_graph *graph, ios27_22c_diagnostic *diag);
void ios27_22c_destroy(ios27_22c_graph *graph);

#endif
