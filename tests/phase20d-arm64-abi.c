/* Genuine Clang ARM64 Darwin MH_OBJECT callsites, not executable on Linux.
 * No Apple SDK or proprietary binary required. */
#include "ios27/libsystem20d_threads.h"
static ios27_20d_runtime *runtime;
static ios27_20d_handle mutex, ready, key, once;
static unsigned long shared;
static void initialize(void *p) { *(unsigned long *)p = 7u; }
static void *child(void *argument) {
    unsigned long i;
    (void)ios27_20d_once(runtime, once, initialize, &shared);
    (void)ios27_20d_setspecific(runtime, key, argument);
    for (i = 0; i < 4096u; ++i) {
        (void)ios27_20d_mutex_lock(runtime, mutex);
        shared += 1u;
        (void)ios27_20d_cond_signal(runtime, ready, 1);
        (void)ios27_20d_mutex_unlock(runtime, mutex);
        if (ios27_20d_cancel_point(runtime)) break;
    }
    return argument;
}
int darwin_arm64_compiler_thread_graph(void) {
    ios27_20d_handle workers[4];
    unsigned int i;
    void *value;
    if (ios27_20d_create(&runtime, 4u)) return 1;
    if (ios27_20d_sync_create(runtime, IOS27_20D_MUTEX, &mutex)) return 2;
    if (ios27_20d_sync_create(runtime, IOS27_20D_CONDITION, &ready)) return 3;
    if (ios27_20d_sync_create(runtime, IOS27_20D_ONCE, &once)) return 4;
    if (ios27_20d_key_create(runtime, &key, 0)) return 5;
    for (i = 0; i < 4u; ++i)
        if (ios27_20d_thread_create(runtime, &workers[i], child,
                                   (void *)(__UINTPTR_TYPE__)(i + 1u))) return 6;
    for (i = 0; i < 4u; ++i)
        if (ios27_20d_thread_join(runtime, workers[i], &value)) return 7;
    if (ios27_20d_key_delete(runtime, key)) return 8;
    if (ios27_20d_sync_destroy(runtime, once)) return 9;
    if (ios27_20d_sync_destroy(runtime, ready)) return 10;
    if (ios27_20d_sync_destroy(runtime, mutex)) return 11;
    return ios27_20d_destroy(runtime);
}
