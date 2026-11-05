#ifndef IOS27_LIBSYSTEM21D_RW_ONCE_H
#define IOS27_LIBSYSTEM21D_RW_ONCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An original-source, bounded synchronization bridge, not a pthread_t ABI alias. */
#define IOS27_21D_MAX_OBJECTS 256u

typedef struct ios27_21d_runtime ios27_21d_runtime;
typedef struct {
    uint32_t slot;
    uint32_t generation;
    uint64_t runtime_cookie;
} ios27_21d_handle;

enum ios27_21d_kind {
    IOS27_21D_RWLOCK = 1,
    IOS27_21D_ONCE = 2
};

/* Initializers return zero on success, otherwise a POSIX error number.
   A failed initializer rolls back to UNINITIALIZED so another caller retries. */
typedef int (*ios27_21d_initializer)(void *context);

int ios27_21d_runtime_create(ios27_21d_runtime **out, size_t capacity);
/* Requires all other callers to have stopped entering the runtime. */
int ios27_21d_runtime_destroy(ios27_21d_runtime *runtime);
int ios27_21d_create(ios27_21d_runtime *runtime, enum ios27_21d_kind kind,
                     ios27_21d_handle *out);
int ios27_21d_destroy(ios27_21d_runtime *runtime, ios27_21d_handle object);
int ios27_21d_rdlock(ios27_21d_runtime *runtime, ios27_21d_handle object);
int ios27_21d_tryrdlock(ios27_21d_runtime *runtime, ios27_21d_handle object);
int ios27_21d_wrlock(ios27_21d_runtime *runtime, ios27_21d_handle object);
int ios27_21d_trywrlock(ios27_21d_runtime *runtime, ios27_21d_handle object);
/* Absolute CLOCK_REALTIME deadline in nanoseconds since Unix epoch.
   ETIMEDOUT removes queued requests without granting ownership. */
int ios27_21d_timedrdlock(ios27_21d_runtime *runtime,
                          ios27_21d_handle object, int64_t deadline_ns);
int ios27_21d_timedwrlock(ios27_21d_runtime *runtime,
                          ios27_21d_handle object, int64_t deadline_ns);
int ios27_21d_unlock(ios27_21d_runtime *runtime, ios27_21d_handle object);
int ios27_21d_once(ios27_21d_runtime *runtime, ios27_21d_handle object,
                   ios27_21d_initializer initialize, void *context);
/* Snapshot statistics for testing and diagnostics. */
int ios27_21d_usage(ios27_21d_runtime *runtime, size_t *objects,
                    size_t *waiting, size_t *holders);

#ifdef __cplusplus
}
#endif

#endif
