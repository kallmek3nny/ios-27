#ifndef IOS27_LIBSYSTEM21F_MUTEX_H
#define IOS27_LIBSYSTEM21F_MUTEX_H
#include <stddef.h>
#include <stdint.h>

/* A bounded native host bridge, NOT a Darwin pthread_mutex_t ABI layout. */
#define IOS27_21F_MAX_MUTEXES 128u
#define IOS27_21F_MAX_WAITERS 1024u
typedef struct ios27_21f_runtime ios27_21f_runtime;
typedef struct { uint32_t slot, generation; } ios27_21f_handle;
typedef enum {
    IOS27_21F_NORMAL = 1,
    IOS27_21F_RECURSIVE = 2,
    IOS27_21F_ERRORCHECK = 3
} ios27_21f_kind;
typedef struct {
    int owned;
    uint32_t recursion;
    size_t queued;
    uint64_t handoffs;
} ios27_21f_info;
typedef struct {
    uint64_t grants;
    uint64_t handoffs;
    uint64_t deadlocks;
    uint64_t canceled;
    size_t pending_waiters;
    size_t peak_waiters;
} ios27_21f_stats;

int ios27_21f_runtime_create(ios27_21f_runtime **out, size_t capacity);
/* All created mutexes must be destroyed and all callers quiescent. */
int ios27_21f_runtime_destroy(ios27_21f_runtime *rt);
int ios27_21f_mutex_create(ios27_21f_runtime *rt, ios27_21f_kind kind,
                           ios27_21f_handle *out);
int ios27_21f_mutex_destroy(ios27_21f_runtime *rt, ios27_21f_handle handle);
int ios27_21f_mutex_lock(ios27_21f_runtime *rt, ios27_21f_handle handle);
int ios27_21f_mutex_trylock(ios27_21f_runtime *rt, ios27_21f_handle handle);
int ios27_21f_mutex_unlock(ios27_21f_runtime *rt, ios27_21f_handle handle);
int ios27_21f_mutex_info(ios27_21f_runtime *rt, ios27_21f_handle handle,
                          ios27_21f_info *out);
int ios27_21f_runtime_stats(ios27_21f_runtime *rt, ios27_21f_stats *out);
#endif
