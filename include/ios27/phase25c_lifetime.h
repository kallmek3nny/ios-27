#ifndef IOS27_PHASE25C_LIFETIME_H
#define IOS27_PHASE25C_LIFETIME_H

#include <stddef.h>
#include <stdint.h>

/* Independent ARC-compatible ownership substrate, not a full Apple libobjc.
 * Callers must keep weak/strong storage valid until unregistered/cleared and
 * join participating threads before destruction. No tagged/arm64e pointers. */
typedef struct ios27_phase25c_runtime ios27_phase25c_runtime;
typedef void (*ios27_phase25c_dealloc)(ios27_phase25c_runtime *, void *, void *);

typedef struct {
    const void *isa;          /* validated loader-owned class identity */
    uint64_t image_generation; /* prevents stale class/image substitutions */
    size_t instance_size;
    size_t alignment;
    size_t ivar_offset;
    size_t ivar_bytes;
} ios27_phase25c_class;

typedef struct {
    uint64_t allocations, deallocations, transitions, weak_loads, weak_zeroed;
    size_t live_objects, live_bytes, weak_slots, thread_contexts;
} ios27_phase25c_stats;

int ios27_phase25c_create(ios27_phase25c_runtime **out);
/* Returns -1 if objects, weak slots, thread contexts, or finalizers remain. */
int ios27_phase25c_destroy(ios27_phase25c_runtime **runtime);
/* -1 rejects a class with live objects, 0 allows unload. */
int ios27_phase25c_can_unload(ios27_phase25c_runtime *, const void *isa,
                              uint64_t image_generation);
/* Fail the next allocation when countdown == 0; -1 disables injection. */
void ios27_phase25c_fail_alloc_after(ios27_phase25c_runtime *, int64_t countdown);
int ios27_phase25c_alloc(ios27_phase25c_runtime *,
                         const ios27_phase25c_class *, ios27_phase25c_dealloc,
                         void *context, void **out);
int ios27_phase25c_identity(ios27_phase25c_runtime *, const void *object,
                            const void *isa, uint64_t image_generation);
int ios27_phase25c_retain(ios27_phase25c_runtime *, const void *object);
int ios27_phase25c_release(ios27_phase25c_runtime *, const void *object);
int ios27_phase25c_retain_count(ios27_phase25c_runtime *, const void *,
                                uint32_t *count);
/* Pins hold executable receiver storage until the matching dispatch exits.
 * Last release zeroes weak slots immediately; dealloc waits for last unpin. */
int ios27_phase25c_pin(ios27_phase25c_runtime *, const void *);
int ios27_phase25c_unpin(ios27_phase25c_runtime *, const void *);
/* Each strong slot owns exactly one reference. All slot accesses must use
 * this API or be otherwise externally synchronized, never raw racing loads. */
int ios27_phase25c_store_strong(ios27_phase25c_runtime *, void **slot, void *next);
int ios27_phase25c_store_weak(ios27_phase25c_runtime *, void **slot, void *next);
int ios27_phase25c_load_weak_retained(ios27_phase25c_runtime *, void **slot,
                                     void **out);
int ios27_phase25c_destroy_weak(ios27_phase25c_runtime *, void **slot);
/* Tokens are thread-affine and are never reused until runtime destruction. */
int ios27_phase25c_pool_push(ios27_phase25c_runtime *, void **token);
int ios27_phase25c_pool_pop(ios27_phase25c_runtime *, void *token);
int ios27_phase25c_autorelease(ios27_phase25c_runtime *, const void *);
int ios27_phase25c_thread_detach(ios27_phase25c_runtime *);
void ios27_phase25c_get_stats(ios27_phase25c_runtime *,
                             ios27_phase25c_stats *out);
int ios27_phase25c_validate(ios27_phase25c_runtime *);

/* Compiler-emitted ABI entrypoints are only legal inside a bound runtime
 * scope. A missing scope, unknown pointer, or unsupported operation aborts
 * instead of silently invoking the host Objective-C runtime. These symbols
 * must be bound in an isolated guest image, not interposed on system libobjc.
 * No optimized autorelease-return handshake or exception unwinding. */
ios27_phase25c_runtime *ios27_phase25c_scope_enter(ios27_phase25c_runtime *);
void ios27_phase25c_scope_leave(ios27_phase25c_runtime *previous);
void *objc_retain(void *object);
void objc_release(void *object);
void objc_storeStrong(void **slot, void *object);
void *objc_storeWeak(void **slot, void *object);
void *objc_initWeak(void **slot, void *object);
void *objc_loadWeakRetained(void **slot);
void objc_destroyWeak(void **slot);
void *objc_autorelease(void *object);
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *token);

#endif
