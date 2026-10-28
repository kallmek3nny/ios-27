#ifndef IOS27_LIBSYSTEM21C_SYNC_H
#define IOS27_LIBSYSTEM21C_SYNC_H
#include <stddef.h>
#include <stdint.h>
struct timespec;
/* Host-backed generation handles; NOT Darwin's opaque pthread ABI. */
#define IOS27_21C_MAX_OBJECTS 128u
typedef struct ios27_21c_runtime ios27_21c_runtime;
typedef struct { uint32_t slot, generation; } ios27_21c_handle;
enum ios27_21c_kind {
    IOS27_21C_NORMAL_MUTEX = 1, IOS27_21C_RECURSIVE_MUTEX = 2,
    IOS27_21C_ERRORCHECK_MUTEX = 3, IOS27_21C_CONDITION = 4
};
enum ios27_21c_clock { IOS27_21C_REALTIME = 1, IOS27_21C_MONOTONIC = 2 };
int ios27_21c_runtime_create(ios27_21c_runtime **out, size_t capacity);
int ios27_21c_runtime_destroy(ios27_21c_runtime *rt);
int ios27_21c_sync_create(ios27_21c_runtime *rt, enum ios27_21c_kind kind,
                          ios27_21c_handle *out);
int ios27_21c_sync_destroy(ios27_21c_runtime *rt, ios27_21c_handle h);
int ios27_21c_mutex_lock(ios27_21c_runtime *rt, ios27_21c_handle h);
int ios27_21c_mutex_trylock(ios27_21c_runtime *rt, ios27_21c_handle h);
int ios27_21c_mutex_unlock(ios27_21c_runtime *rt, ios27_21c_handle h);
int ios27_21c_cond_signal(ios27_21c_runtime *rt, ios27_21c_handle c);
int ios27_21c_cond_broadcast(ios27_21c_runtime *rt, ios27_21c_handle c);
int ios27_21c_cond_wait(ios27_21c_runtime *rt, ios27_21c_handle c,
                        ios27_21c_handle m);
int ios27_21c_cond_timedwait(ios27_21c_runtime *rt, ios27_21c_handle c,
                             ios27_21c_handle m, enum ios27_21c_clock clock,
                             const struct timespec *absolute);
void ios27_21c_usage(ios27_21c_runtime *rt, size_t *used);
#endif
