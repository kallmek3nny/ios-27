#ifndef IOS27_LIBSYSTEM20D_THREADS_H
#define IOS27_LIBSYSTEM20D_THREADS_H
#include <stddef.h>
#include <stdint.h>

/* Opaque, generation-checked bridge handles, NOT native Darwin pthread_t. */
#define IOS27_20D_MAX_THREADS 64u
#define IOS27_20D_MAX_SYNCS 128u
#define IOS27_20D_MAX_KEYS 64u
typedef struct ios27_20d_runtime ios27_20d_runtime;
typedef struct { uint32_t slot, generation; } ios27_20d_handle;
typedef void *(*ios27_20d_start)(void *);
typedef void (*ios27_20d_destructor)(void *);
typedef void (*ios27_20d_initializer)(void *);
enum ios27_20d_sync_kind {
    IOS27_20D_MUTEX = 1, IOS27_20D_RECURSIVE_MUTEX = 2,
    IOS27_20D_CONDITION = 3, IOS27_20D_RWLOCK = 4, IOS27_20D_ONCE = 5
};
int ios27_20d_create(ios27_20d_runtime **out, size_t max_threads);
int ios27_20d_destroy(ios27_20d_runtime *rt);
int ios27_20d_thread_create(ios27_20d_runtime *rt, ios27_20d_handle *out,
                            ios27_20d_start start, void *argument);
int ios27_20d_thread_join(ios27_20d_runtime *rt, ios27_20d_handle thread,
                          void **result);
int ios27_20d_thread_detach(ios27_20d_runtime *rt, ios27_20d_handle thread);
ios27_20d_handle ios27_20d_thread_self(ios27_20d_runtime *rt);
int ios27_20d_cancel_request(ios27_20d_runtime *rt, ios27_20d_handle thread);
int ios27_20d_cancel_point(ios27_20d_runtime *rt);
int ios27_20d_sync_create(ios27_20d_runtime *rt, enum ios27_20d_sync_kind kind,
                          ios27_20d_handle *out);
int ios27_20d_sync_destroy(ios27_20d_runtime *rt, ios27_20d_handle object);
int ios27_20d_mutex_lock(ios27_20d_runtime *rt, ios27_20d_handle object);
int ios27_20d_mutex_trylock(ios27_20d_runtime *rt, ios27_20d_handle object);
int ios27_20d_mutex_unlock(ios27_20d_runtime *rt, ios27_20d_handle object);
int ios27_20d_rwlock_rdlock(ios27_20d_runtime *rt, ios27_20d_handle object);
int ios27_20d_rwlock_wrlock(ios27_20d_runtime *rt, ios27_20d_handle object);
int ios27_20d_rwlock_unlock(ios27_20d_runtime *rt, ios27_20d_handle object);
int ios27_20d_cond_signal(ios27_20d_runtime *rt, ios27_20d_handle object,
                          int broadcast);
/* Absolute CLOCK_REALTIME nanoseconds; zero means wait indefinitely. */
int ios27_20d_cond_wait(ios27_20d_runtime *rt, ios27_20d_handle condition,
                         ios27_20d_handle mutex, int64_t deadline_ns);
int ios27_20d_once(ios27_20d_runtime *rt, ios27_20d_handle object,
                    ios27_20d_initializer init, void *context);
int ios27_20d_key_create(ios27_20d_runtime *rt, ios27_20d_handle *out,
                         ios27_20d_destructor destructor);
int ios27_20d_key_delete(ios27_20d_runtime *rt, ios27_20d_handle key);
int ios27_20d_setspecific(ios27_20d_runtime *rt, ios27_20d_handle key,
                           const void *value);
int ios27_20d_getspecific(ios27_20d_runtime *rt, ios27_20d_handle key,
                           void **out);
void ios27_20d_usage(ios27_20d_runtime *rt, size_t *threads,
                      size_t *syncs, size_t *keys);
#endif
