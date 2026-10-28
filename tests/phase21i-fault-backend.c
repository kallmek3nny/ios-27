#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include "ios27/libsystem21c_sync.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define EXPECT(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i injection %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); } \
} while (0)
#define OK(x) EXPECT((x) == 0)
#define BASE UINT64_C(0xa00000000)
#define M (BASE + 64u)
#define C (BASE + 192u)
#define CALL(rt, name, addr) \
    ios27_21i_dispatch(rt, "_pthread_" name, addr, 0, 0)

/* Linked INSTEAD OF phase21c to inject deterministic backend failures.
 * This test does not stand in for the real multithreaded phase21c tests. */
struct ios27_21c_runtime {
    int live;
    uint32_t generation;
};
static int fail_runtime_create, fail_sync_create, fail_sync_destroy;
static unsigned runtime_live, handles_live;
int ios27_21c_runtime_create(ios27_21c_runtime **out, size_t capacity) {
    ios27_21c_runtime *rt;
    if (!out || !capacity) return EINVAL;
    *out = NULL;
    if (fail_runtime_create) return ENOMEM;
    rt = calloc(1, sizeof(*rt));
    if (!rt) return ENOMEM;
    rt->generation = 1u;
    ++runtime_live;
    *out = rt;
    return 0;
}
int ios27_21c_runtime_destroy(ios27_21c_runtime *rt) {
    if (!rt) return EINVAL;
    if (rt->live) return EBUSY;
    EXPECT(runtime_live == 1);
    --runtime_live;
    free(rt);
    return 0;
}
int ios27_21c_sync_create(ios27_21c_runtime *rt, enum ios27_21c_kind kind,
                          ios27_21c_handle *out) {
    if (!rt || !out || kind < IOS27_21C_NORMAL_MUTEX ||
        kind > IOS27_21C_CONDITION) return EINVAL;
    *out = (ios27_21c_handle){0, 0};
    if (fail_sync_create) return ENOMEM;
    if (rt->live) return EAGAIN;
    rt->live = 1;
    ++handles_live;
    *out = (ios27_21c_handle){1, rt->generation};
    return 0;
}
int ios27_21c_sync_destroy(ios27_21c_runtime *rt, ios27_21c_handle handle) {
    if (!rt || !rt->live || handle.slot != 1 ||
        handle.generation != rt->generation) return EINVAL;
    if (fail_sync_destroy) return EIO;
    EXPECT(handles_live == 1);
    rt->live = 0;
    --handles_live;
    ++rt->generation;
    return 0;
}
int ios27_21c_mutex_lock(ios27_21c_runtime *rt, ios27_21c_handle h) {
    (void)rt; (void)h; return ENOSYS;
}
int ios27_21c_mutex_trylock(ios27_21c_runtime *rt, ios27_21c_handle h) {
    (void)rt; (void)h; return ENOSYS;
}
int ios27_21c_mutex_unlock(ios27_21c_runtime *rt, ios27_21c_handle h) {
    (void)rt; (void)h; return ENOSYS;
}
int ios27_21c_cond_wait(ios27_21c_runtime *rt, ios27_21c_handle c,
                        ios27_21c_handle m) {
    (void)rt; (void)c; (void)m; return ENOSYS;
}
int ios27_21c_cond_timedwait(ios27_21c_runtime *rt, ios27_21c_handle c,
                             ios27_21c_handle m,
                             enum ios27_21c_clock clock,
                             const struct timespec *absolute) {
    (void)rt; (void)c; (void)m; (void)clock; (void)absolute;
    return ENOSYS;
}
int ios27_21c_cond_signal(ios27_21c_runtime *rt, ios27_21c_handle c) {
    (void)rt; (void)c; return ENOSYS;
}
int ios27_21c_cond_broadcast(ios27_21c_runtime *rt, ios27_21c_handle c) {
    (void)rt; (void)c; return ENOSYS;
}
void ios27_21c_usage(ios27_21c_runtime *rt, size_t *used) {
    if (used) *used = rt && rt->live ? 1u : 0u;
}

int main(void) {
    uint8_t bytes[512] = {0};
    ios27_21i_guest guest = {bytes, sizeof(bytes), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *rt = (ios27_21i_runtime *)(uintptr_t)1;
    ios27_21i_token old, fresh;
    ios27_21i_stats stats;
    fail_runtime_create = 1;
    EXPECT(ios27_21i_create(&rt, &guest, 1) == ENOMEM);
    EXPECT(rt == NULL && runtime_live == 0);
    fail_runtime_create = 0;
    OK(ios27_21i_create(&rt, &guest, 1));
    EXPECT(runtime_live == 1);
    fail_sync_create = 1;
    EXPECT(CALL(rt, "mutex_init", M) == ENOMEM);
    EXPECT(CALL(rt, "mutex_init", M) == ENOMEM);
    OK(ios27_21i_get_stats(rt, &stats));
    EXPECT(stats.mutexes == 0 && stats.conditions == 0 &&
           stats.in_flight == 0 && handles_live == 0);
    fail_sync_create = 0;
    OK(CALL(rt, "mutex_init", M));
    OK(ios27_21i_lookup(rt, M, IOS27_21I_MUTEX, &old));
    EXPECT(CALL(rt, "cond_init", C) == EAGAIN);
    EXPECT(handles_live == 1);
    fail_sync_destroy = 1;
    EXPECT(CALL(rt, "mutex_destroy", M) == EIO);
    OK(ios27_21i_get_stats(rt, &stats));
    EXPECT(stats.mutexes == 1 && stats.in_flight == 0);
    EXPECT(ios27_21i_destroy(rt) == EBUSY);
    fail_sync_destroy = 0;
    OK(CALL(rt, "mutex_destroy", M));
    EXPECT(ios27_21i_verify(rt, M, IOS27_21I_MUTEX, old) == EINVAL);
    OK(CALL(rt, "cond_init", C));
    OK(CALL(rt, "cond_destroy", C));
    OK(CALL(rt, "mutex_init", M));
    OK(ios27_21i_lookup(rt, M, IOS27_21I_MUTEX, &fresh));
    EXPECT(fresh.generation > old.generation);
    OK(CALL(rt, "mutex_destroy", M));
    OK(ios27_21i_get_stats(rt, &stats));
    EXPECT(!stats.mutexes && !stats.conditions && !stats.in_flight &&
           !stats.retired_slots && !handles_live);
    OK(ios27_21i_destroy(rt));
    EXPECT(runtime_live == 0);
    puts("phase21i: backend constructor ENOMEM rollback, sync-create failure, sync-destroy EIO retention, recovery and zero leaks passed");
    return 0;
}
