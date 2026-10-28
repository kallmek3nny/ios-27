#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21f_mutex.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"phase21f wait faults %s:%d: %s\n", __FILE__,__LINE__,#x); \
    abort(); } } while (0)
static atomic_uint fail_next = ATOMIC_VAR_INIT(0);
static atomic_uint spurious_next = ATOMIC_VAR_INIT(0);
extern int __real_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
static int consume(atomic_uint *count) {
    unsigned n = atomic_load_explicit(count, memory_order_relaxed);
    while (n) {
        if (atomic_compare_exchange_weak_explicit(count, &n, n - 1u,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) return 1;
    }
    return 0;
}
int __wrap_pthread_cond_wait(pthread_cond_t *cv, pthread_mutex_t *guard) {
    if (consume(&fail_next)) return EINVAL;
    /* Simulate a permitted spurious wakeup while rt->guard stays locked. */
    if (consume(&spurious_next)) return 0;
    return __real_pthread_cond_wait(cv, guard);
}
typedef struct { ios27_21f_runtime *rt; ios27_21f_handle h; int status; } task;
static void *worker(void *p) {
    task *t = p;
    t->status = ios27_21f_mutex_lock(t->rt, t->h);
    if (!t->status) CHECK(ios27_21f_mutex_unlock(t->rt, t->h) == 0);
    return NULL;
}
static uint64_t now_ms(void) {
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle h;
    ios27_21f_info info;
    ios27_21f_stats stats;
    CHECK(ios27_21f_runtime_create(&rt, 1) == 0);
    CHECK(ios27_21f_mutex_create(rt, IOS27_21F_ERRORCHECK, &h) == 0);
    CHECK(ios27_21f_mutex_lock(rt, h) == 0);
    atomic_store_explicit(&fail_next, 128u, memory_order_relaxed);
    for (unsigned i = 0; i < 128; ++i) {
        pthread_t t;
        task j = {rt, h, -1};
        CHECK(pthread_create(&t, NULL, worker, &j) == 0);
        CHECK(pthread_join(t, NULL) == 0);
        CHECK(j.status == EINVAL);
        CHECK(ios27_21f_mutex_info(rt, h, &info) == 0);
        CHECK(info.owned && info.queued == 0u);
    }
    CHECK(atomic_load_explicit(&fail_next, memory_order_relaxed) == 0);
    CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
    CHECK(stats.pending_waiters == 0 && stats.peak_waiters == 1);
    atomic_store_explicit(&spurious_next, 1024u, memory_order_relaxed);
    {
        pthread_t t;
        task j = {rt, h, -1};
        CHECK(pthread_create(&t, NULL, worker, &j) == 0);
        uint64_t limit = now_ms() + 10000;
        for (;;) {
            CHECK(ios27_21f_mutex_info(rt, h, &info) == 0);
            if (info.queued == 1) break;
            CHECK(now_ms() < limit);
            sched_yield();
        }
        CHECK(atomic_load_explicit(&spurious_next, memory_order_relaxed) == 0);
        CHECK(ios27_21f_mutex_unlock(rt, h) == 0);
        CHECK(pthread_join(t, NULL) == 0);
        CHECK(j.status == 0);
    }
    CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
    CHECK(!stats.pending_waiters);
    CHECK(ios27_21f_mutex_destroy(rt, h) == 0);
    CHECK(ios27_21f_runtime_destroy(rt) == 0);
    puts("phase21f wait fault: 128 host errors, 1024 spurious returns, FIFO recovery passed");
    return 0;
}
