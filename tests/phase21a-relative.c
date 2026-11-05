#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BASE UINT64_C(0xa00000000)
static unsigned char arena[2048];
static ios27_21a_runtime *rt;
static int invoke(const char *symbol, uint64_t addr, uint64_t mutex, uint64_t ts) {
    uint64_t args[6] = {addr, mutex, ts, 0u, 0u, 0u};
    uint64_t x0 = UINT64_MAX;
    int e = ios27_21a_call(rt, symbol, args, &x0);
    assert(x0 == (uint64_t)e);
    return e;
}
static void guest_timespec(uint64_t offset, int64_t sec, int64_t nsec) {
    memcpy(arena + offset, &sec, sizeof(sec));
    memcpy(arena + offset + 8u, &nsec, sizeof(nsec));
}
int main(void) {
    const uint64_t m = BASE + 64u, c = BASE + 256u, t = BASE + 512u;
    struct timespec before, after;
    int64_t elapsed_ns;
    assert(ios27_21a_create(&rt, arena, sizeof(arena), BASE) == 0);
    assert(invoke("pthread_mutex_init", m, 0, 0) == 0);
    assert(invoke("pthread_cond_init", c, 0, 0) == 0);
    guest_timespec(512u, 0, 10000000LL);
    assert(invoke("pthread_cond_timedwait_relative_np", c, m, t) == EPERM);
    assert(invoke("pthread_mutex_lock", m, 0, 0) == 0);
    assert(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    assert(invoke("pthread_cond_timedwait_relative_np", c, m, t) == ETIMEDOUT);
    assert(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    elapsed_ns = ((int64_t)(after.tv_sec - before.tv_sec) * 1000000000LL) +
                 (int64_t)after.tv_nsec - (int64_t)before.tv_nsec;
    assert(elapsed_ns >= 5000000LL && elapsed_ns < 500000000LL);
    guest_timespec(512u, -1LL, 0);
    assert(invoke("pthread_cond_timedwait_relative_np", c, m, t) == EINVAL);
    guest_timespec(512u, 0, 1000000000LL);
    assert(invoke("pthread_cond_timedwait_relative_np", c, m, t) == EINVAL);
    guest_timespec(512u, INT64_MAX, 0);
    assert(invoke("pthread_cond_timedwait_relative_np", c, m, t) == EOVERFLOW);
    guest_timespec(512u, 0, 0);
    assert(invoke("pthread_cond_timedwait_relative_np", c, m, t) == ETIMEDOUT);
    assert(invoke("pthread_mutex_unlock", m, 0, 0) == 0);
    assert(invoke("pthread_cond_destroy", c, 0, 0) == 0);
    assert(invoke("pthread_mutex_destroy", m, 0, 0) == 0);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: relative Darwin waits, monotonic duration and overflow passed");
    return 0;
}
