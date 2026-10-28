#ifndef IOS27_LIBSYSTEM21G_COND_H
#define IOS27_LIBSYSTEM21G_COND_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Host-backed synchronization model, NOT a Darwin pthread ABI structure.
 * External Darwin pthread symbols must be marshalled at a separate ABI layer. */
#define IOS27_21G_MAX_OBJECTS 64u

typedef struct ios27_21g_runtime ios27_21g_runtime;
typedef struct { uint32_t slot, generation; } ios27_21g_handle;
enum ios27_21g_kind { IOS27_21G_MUTEX = 1, IOS27_21G_COND = 2 };
enum ios27_21g_clock { IOS27_21G_REALTIME = 1, IOS27_21G_MONOTONIC = 2 };

typedef struct {
    size_t queued;
    size_t outstanding;
    uint64_t wakeups;
    uint64_t timeouts;
} ios27_21g_stats;

int ios27_21g_runtime_create(ios27_21g_runtime **out, size_t capacity);
int ios27_21g_runtime_destroy(ios27_21g_runtime *rt);
int ios27_21g_create(ios27_21g_runtime *rt, enum ios27_21g_kind kind,
                     ios27_21g_handle *out);
int ios27_21g_destroy(ios27_21g_runtime *rt, ios27_21g_handle h);
int ios27_21g_mutex_lock(ios27_21g_runtime *rt, ios27_21g_handle m);
int ios27_21g_mutex_trylock(ios27_21g_runtime *rt, ios27_21g_handle m);
int ios27_21g_mutex_unlock(ios27_21g_runtime *rt, ios27_21g_handle m);
int ios27_21g_cond_wait(ios27_21g_runtime *rt, ios27_21g_handle c,
                        ios27_21g_handle m);
int ios27_21g_cond_timedwait(ios27_21g_runtime *rt, ios27_21g_handle c,
                             ios27_21g_handle m, enum ios27_21g_clock clock,
                             const struct timespec *absolute);
int ios27_21g_cond_signal(ios27_21g_runtime *rt, ios27_21g_handle c);
int ios27_21g_cond_broadcast(ios27_21g_runtime *rt, ios27_21g_handle c);
int ios27_21g_cond_stats(ios27_21g_runtime *rt, ios27_21g_handle c,
                         ios27_21g_stats *out);
#endif
