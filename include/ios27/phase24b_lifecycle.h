#ifndef IOS27_PHASE24B_LIFECYCLE_H
#define IOS27_PHASE24B_LIFECYCLE_H

/* Standalone, bounded Darwin ARM64 image lifecycle contract.
 * Parsing and modeled dispatch never imply execution of guest instructions.
 * The caller must independently validate/maps/fixup/seal guest images before
 * passing an execution bridge; this API never casts guest PCs to host calls. */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define IOS27_24B_IMAGES 24u
#define IOS27_24B_DEPS 8u
#define IOS27_24B_CALLBACKS 64u
#define IOS27_24B_RANGES 32u
#define IOS27_24B_HANDLES 256u
#define IOS27_24B_REF_LIMIT 65535u

typedef struct { uint64_t start, end; } ios27_24b_range;
typedef struct {
    uint64_t init_table, term_table, offset_table, text_vmaddr;
    uint64_t init[IOS27_24B_CALLBACKS], term[IOS27_24B_CALLBACKS];
    ios27_24b_range executable[IOS27_24B_RANGES];
    size_t init_count, term_count, offset_count, executable_count;
    unsigned parsed, bound;
} ios27_24b_image;

/* read the *already fixed up* mapped guest image, never raw disk pointers.
 * size must be exactly 4 or 8. VM address is slid before invoking read. */
typedef int (*ios27_24b_read)(void *context, uint64_t vmaddr,
                              unsigned width, uint64_t *out);
typedef enum {
    IOS27_24B_INIT, IOS27_24B_TERM, IOS27_24B_COMPENSATE
} ios27_24b_action;
typedef enum {
    IOS27_24B_MODELED, IOS27_24B_NATIVE_VERIFIED
} ios27_24b_execution_kind;

/* The bridge must attest independent instruction/ABI validation if marked
 * NATIVE_VERIFIED; the lifecycle engine does not establish this itself.
 * COMPENSATE is only for explicitly reversible initialization side-effects. */
typedef int (*ios27_24b_dispatch)(void *context, unsigned image,
    ios27_24b_action action, uint64_t callback_pc, size_t ordinal);

typedef enum {
    IOS27_24B_NEW, IOS27_24B_STARTING, IOS27_24B_READY,
    IOS27_24B_FAILED, IOS27_24B_FINALIZING, IOS27_24B_QUARANTINED
} ios27_24b_state;
typedef enum {
    IOS27_24B_DIAG_NONE, IOS27_24B_DIAG_MALFORMED,
    IOS27_24B_DIAG_CYCLE, IOS27_24B_DIAG_REENTRANT,
    IOS27_24B_DIAG_CALLBACK_FAILED, IOS27_24B_DIAG_COMPENSATION_FAILED,
    IOS27_24B_DIAG_TERMINATOR_FAILED, IOS27_24B_DIAG_PINNED,
    IOS27_24B_DIAG_QUARANTINE, IOS27_24B_DIAG_TIMEOUT
} ios27_24b_diagnostic;

typedef struct {
    unsigned index;
    uint32_t generation;
    uint64_t ticket;
} ios27_24b_handle;
typedef struct {
    uint64_t ticket;
    unsigned index, active;
} ios27_24b_lease;
typedef struct {
    ios27_24b_image image;
    unsigned deps[IOS27_24B_DEPS];
    size_t dep_count;
    unsigned registered, refs, active, completed, poisoned;
    uint32_t generation;
    ios27_24b_state state;
    int failure;
} ios27_24b_slot;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    ios27_24b_slot slots[IOS27_24B_IMAGES];
    ios27_24b_lease leases[IOS27_24B_HANDLES];
    uint64_t next_ticket, callbacks, transitions, startups, shutdowns;
    size_t count;
    pthread_t gate_owner;
    unsigned gate, initialized;
    ios27_24b_diagnostic last_diagnostic;
    unsigned last_image;
    int last_error;
    ios27_24b_execution_kind kind;
} ios27_24b_runtime;

typedef struct {
    ios27_24b_state state;
    unsigned references, active_callbacks, completed_initializers, poisoned;
    uint32_t generation;
    int failure;
} ios27_24b_snapshot;

/* Parse genuinely linked thin arm64 Mach-O MH_DYLIB / MH_EXECUTE images.
 * Reject unusual section layouts/types rather than guessing an ABI. */
int ios27_24b_parse(const uint8_t *bytes, size_t size, ios27_24b_image *out);
/* Transactional bind after mapping and relocation; unsigned slide, bounded. */
int ios27_24b_bind(ios27_24b_image *image, uint64_t slide,
                   ios27_24b_read reader, void *context);
int ios27_24b_runtime_init(ios27_24b_runtime *runtime,
                           ios27_24b_execution_kind kind);
int ios27_24b_register(ios27_24b_runtime *runtime, unsigned index,
                       const ios27_24b_image *image, const unsigned *deps,
                       size_t dep_count);
int ios27_24b_open(ios27_24b_runtime *runtime, unsigned index,
                   ios27_24b_handle *out);
int ios27_24b_ensure(ios27_24b_runtime *runtime, ios27_24b_handle handle,
                     ios27_24b_dispatch execute,
                     ios27_24b_dispatch compensate, void *context);
int ios27_24b_close(ios27_24b_runtime *runtime, ios27_24b_handle handle,
                    ios27_24b_dispatch execute, void *context);
int ios27_24b_inspect(ios27_24b_runtime *runtime, unsigned index,
                      ios27_24b_snapshot *out);
int ios27_24b_runtime_destroy(ios27_24b_runtime *runtime);

#endif
