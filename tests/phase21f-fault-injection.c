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

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "phase21f injected failure %s:%d: %s\n", \
            __FILE__, __LINE__, #expr); abort(); } } while (0)

static atomic_uint fail_next = ATOMIC_VAR_INIT(0);
extern int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    unsigned n = atomic_load_explicit(&fail_next, memory_order_relaxed);
    while (n) {
        if (atomic_compare_exchange_weak_explicit(&fail_next, &n, n - 1u,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed))
            return EAGAIN;
    }
    return __real_pthread_cond_init(cond, attr);
}
typedef struct { ios27_21f_runtime *rt; ios27_21f_handle h; int status; } task;
static void *worker(void *ctx) {
    task *t = ctx;
    t->status = ios27_21f_mutex_lock(t->rt, t->h);
    if (!t->status)
        CHECK(ios27_21f_mutex_unlock(t->rt, t->h) == 0);
    return NULL;
}
static uint64_t now_ms(void) {
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle h;
    ios27_21f_stats stats;
    ios27_21f_info info;
    CHECK(ios27_21f_runtime_create(&rt, 1) == 0);
    CHECK(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &h) == 0);
    CHECK(ios27_21f_mutex_lock(rt, h) == 0);
    atomic_store_explicit(&fail_next, 128u, memory_order_relaxed);
    for (unsigned i = 0; i < 128; ++i) {
        pthread_t thread;
        task t = {rt, h, -1};
        CHECK(pthread_create(&thread, NULL, worker, &t) == 0);
        CHECK(pthread_join(thread, NULL) == 0);
        CHECK(t.status == EAGAIN);
        CHECK(ios27_21f_mutex_info(rt, h, &info) == 0);
        CHECK(info.owned && info.queued == 0u);
    }
    CHECK(atomic_load_explicit(&fail_next, memory_order_relaxed) == 0u);
    CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
    CHECK(stats.pending_waiters == 0 && stats.peak_waiters == 0);
    {
        pthread_t thread;
        task t = {rt, h, -1};
        CHECK(pthread_create(&thread, NULL, worker, &t) == 0);
        uint64_t deadline = now_ms() + 10000;
        for (;;) {
            CHECK(ios27_21f_mutex_info(rt, h, &info) == 0);
            if (info.queued == 1u) break;
            CHECK(now_ms() < deadline);
            sched_yield();
        }
        CHECK(ios27_21f_mutex_unlock(rt, h) == 0);
        CHECK(pthread_join(thread, NULL) == 0);
        CHECK(t.status == 0);
    }
    CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
    CHECK(stats.pending_waiters == 0 && stats.peak_waiters == 1u);
    CHECK(ios27_21f_mutex_destroy(rt, h) == 0);
    CHECK(ios27_21f_runtime_destroy(rt) == 0);
    puts("phase21f fault injection: 128 injected host condition failures and recovery passed");
    return 0;
}
