#ifndef IOS27_LIBSYSTEM21I_ABI_H
#define IOS27_LIBSYSTEM21I_ABI_H
#include <stddef.h>
#include <stdint.h>

/* The caller MUST supply independently verified Darwin opaque object extents
 * and alignment. No Linux pthread_t/object bytes are ever copied into guest
 * storage. A successful call does not establish Apple ABI layout equivalence. */
typedef struct ios27_21i_runtime ios27_21i_runtime;
typedef struct {
    uint8_t *memory;
    size_t length;
    uint64_t base;
    size_t mutex_extent, cond_extent;
    size_t mutex_alignment, cond_alignment;
} ios27_21i_guest;
typedef struct { uint32_t slot, generation; } ios27_21i_token;
enum { IOS27_21I_MUTEX = 1, IOS27_21I_CONDITION = 2 };

/* Host-only introspection for leak and lifecycle verification. */
typedef struct {
    size_t capacity, mutexes, conditions, in_flight, retired_slots;
} ios27_21i_stats;
int ios27_21i_get_stats(ios27_21i_runtime *runtime, ios27_21i_stats *out);

int ios27_21i_create(ios27_21i_runtime **out, const ios27_21i_guest *guest,
                     size_t capacity);
/* Opt-in Apple *published* LP64 pthreadattr profile (NOT a claim about any
 * future SDK): mutexattr has sig+ceiling+bitfields at 16 bytes; condattr has
 * sig+flags at 16 bytes. Only process-private, non-priority, documented
 * normal/errorcheck/recursive types are accepted. Unsupported policy
 * semantics remain explicitly limited. Guest memory must be stable during
 * attribute decoding. Requires mutex 64/8 and condition 48/8 profiles. */
int ios27_21i_create_apple_lp64_attrs(ios27_21i_runtime **out,
                                      const ios27_21i_guest *guest,
                                      size_t capacity);
int ios27_21i_destroy(ios27_21i_runtime *runtime);
/* Symbol names are the exact ARM64 Mach-O external names, with leading '_'.
 * Supported: init/destroy/lock/trylock/unlock for mutex; init/destroy/wait/
 * timedwait/signal/broadcast for conditions. Non-null attrs are unsupported.
 * The guest absolute deadline uses two little-endian signed 64-bit values. */
int ios27_21i_dispatch(ios27_21i_runtime *runtime, const char *symbol,
                       uint64_t arg0, uint64_t arg1, uint64_t arg2);
/* Host-side validation token, not part of Darwin's pthread API. */
int ios27_21i_lookup(ios27_21i_runtime *runtime, uint64_t guest_address,
                     int kind, ios27_21i_token *out);
int ios27_21i_verify(ios27_21i_runtime *runtime, uint64_t guest_address,
                     int kind, ios27_21i_token token);
#endif
