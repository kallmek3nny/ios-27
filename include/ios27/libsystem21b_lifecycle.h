#ifndef IOS27_LIBSYSTEM21B_LIFECYCLE_H
#define IOS27_LIBSYSTEM21B_LIFECYCLE_H
#include <stddef.h>
#include <stdint.h>

/* Independent host-backed lifecycle model, NOT Darwin pthread_t or an ABI thunk. */
#define IOS27_21B_MAX_THREADS 64u
#define IOS27_21B_ATTR_VERSION 0x21b00001u
#define IOS27_21B_STACK_MAX (128u * 1024u * 1024u)
typedef struct ios27_21b_runtime ios27_21b_runtime;
typedef struct { uint32_t slot, generation; uint64_t owner; } ios27_21b_thread;
typedef void *(*ios27_21b_entry)(void *);
typedef struct {
    uint32_t version;
    uint32_t detached;
    uint64_t stack_bytes;
    uint64_t guard_bytes;
    uint32_t scheduling; /* 0 = inherit; explicit scheduling unsupported */
    int32_t priority;
    uint64_t stack_address; /* explicit guest stack unsupported */
} ios27_21b_attrs;
typedef struct {
    int running;
    int detached;
    int join_claimed;
} ios27_21b_info;

int ios27_21b_attr_init(ios27_21b_attrs *attr);
int ios27_21b_attr_validate(const ios27_21b_attrs *attr);
int ios27_21b_runtime_create(ios27_21b_runtime **out, size_t quota);
/* EBUSY while a worker or a joinable completion is still tracked. */
int ios27_21b_runtime_destroy(ios27_21b_runtime *rt);
int ios27_21b_thread_create(ios27_21b_runtime *rt, ios27_21b_thread *out,
                             const ios27_21b_attrs *attr,
                             ios27_21b_entry entry, void *argument);
/* Exactly one actor may own a join or detach. A stale handle returns ESRCH. */
int ios27_21b_thread_join(ios27_21b_runtime *rt, ios27_21b_thread id,
                           void **result);
int ios27_21b_thread_detach(ios27_21b_runtime *rt, ios27_21b_thread id);
int ios27_21b_thread_info(ios27_21b_runtime *rt, ios27_21b_thread id,
                           ios27_21b_info *out);
ios27_21b_thread ios27_21b_thread_self(ios27_21b_runtime *rt);
int ios27_21b_thread_equal(ios27_21b_thread a, ios27_21b_thread b);
int ios27_21b_runtime_usage(ios27_21b_runtime *rt, size_t *out);
/* Wait for every detached worker to be reaped and every join to finish.
   A still-joinable completed worker counts as live. */
int ios27_21b_runtime_wait_idle(ios27_21b_runtime *rt, uint64_t timeout_ms);
#endif
