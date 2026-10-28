#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21f_mutex.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define REQUIRE(expr) do { if (!(expr)) { \
    fprintf(stderr, "phase21f functional failure %s:%d: %s\n", \
            __FILE__, __LINE__, #expr); abort(); } } while (0)
typedef struct { ios27_21f_runtime *rt; ios27_21f_handle lock; } args;
static uint64_t now_ms(void) {
    struct timespec ts;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static void await_queue(ios27_21f_runtime *rt, ios27_21f_handle h, size_t count) {
    uint64_t limit = now_ms() + 7000;
    for (;;) {
        ios27_21f_info info;
        REQUIRE(ios27_21f_mutex_info(rt, h, &info) == 0);
        if (info.queued == count) return;
        REQUIRE(now_ms() < limit);
        sched_yield();
    }
}
static void *bad_unlock(void *p) {
    args *a = p;
    REQUIRE(ios27_21f_mutex_unlock(a->rt, a->lock) == EPERM);
    return NULL;
}
typedef struct {
    args a;
    unsigned index;
    unsigned *order;
    atomic_uint *next;
} fair_args;
static void *fair_worker(void *p) {
    fair_args *a = p;
    unsigned where;
    REQUIRE(ios27_21f_mutex_lock(a->a.rt, a->a.lock) == 0);
    where = atomic_fetch_add_explicit(a->next, 1u, memory_order_relaxed);
    a->order[where] = a->index;
    REQUIRE(ios27_21f_mutex_unlock(a->a.rt, a->a.lock) == 0);
    return NULL;
}
typedef struct { args a; ios27_21f_handle second; atomic_int held; } cycle_args;
static void *cycle_worker(void *p) {
    cycle_args *a = p;
    REQUIRE(ios27_21f_mutex_lock(a->a.rt, a->second) == 0);
    atomic_store_explicit(&a->held, 1, memory_order_release);
    REQUIRE(ios27_21f_mutex_lock(a->a.rt, a->a.lock) == 0);
    REQUIRE(ios27_21f_mutex_unlock(a->a.rt, a->a.lock) == 0);
    REQUIRE(ios27_21f_mutex_unlock(a->a.rt, a->second) == 0);
    return NULL;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle a, b, c, d, stale, exhausted;
    ios27_21f_stats stats;
    ios27_21f_info info;
    pthread_t thread;
    args wrong;
    size_t round;
    REQUIRE(ios27_21f_runtime_create(NULL, 4) == EINVAL);
    REQUIRE(ios27_21f_runtime_create(&rt, 0) == EINVAL);
    REQUIRE(ios27_21f_runtime_create(&rt, 4) == 0);
    REQUIRE(ios27_21f_mutex_create(rt, (ios27_21f_kind)77, &a) == EINVAL);
    REQUIRE(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &a) == 0);
    REQUIRE(ios27_21f_mutex_create(rt, IOS27_21F_RECURSIVE, &b) == 0);
    REQUIRE(ios27_21f_mutex_create(rt, IOS27_21F_ERRORCHECK, &c) == 0);
    REQUIRE(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &d) == 0);
    REQUIRE(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &exhausted) == EAGAIN);
    REQUIRE(ios27_21f_mutex_unlock(rt, a) == EPERM);
    REQUIRE(ios27_21f_mutex_lock(rt, a) == 0);
    REQUIRE(ios27_21f_mutex_lock(rt, a) == EDEADLK);
    REQUIRE(ios27_21f_mutex_trylock(rt, a) == EBUSY);
    REQUIRE(ios27_21f_mutex_destroy(rt, a) == EBUSY);
    REQUIRE(ios27_21f_runtime_destroy(rt) == EBUSY);
    wrong = (args){rt, a};
    REQUIRE(pthread_create(&thread, NULL, bad_unlock, &wrong) == 0);
    REQUIRE(pthread_join(thread, NULL) == 0);
    REQUIRE(ios27_21f_mutex_unlock(rt, a) == 0);
    REQUIRE(ios27_21f_mutex_lock(rt, b) == 0);
    REQUIRE(ios27_21f_mutex_lock(rt, b) == 0);
    REQUIRE(ios27_21f_mutex_trylock(rt, b) == 0);
    REQUIRE(ios27_21f_mutex_info(rt, b, &info) == 0 && info.recursion == 3);
    REQUIRE(ios27_21f_mutex_unlock(rt, b) == 0);
    REQUIRE(ios27_21f_mutex_unlock(rt, b) == 0);
    REQUIRE(ios27_21f_mutex_unlock(rt, b) == 0);
    REQUIRE(ios27_21f_mutex_lock(rt, c) == 0);
    REQUIRE(ios27_21f_mutex_lock(rt, c) == EDEADLK);
    REQUIRE(ios27_21f_mutex_unlock(rt, c) == 0);
    REQUIRE(ios27_21f_mutex_destroy(rt, d) == 0);
    stale = d;
    REQUIRE(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &d) == 0);
    REQUIRE(stale.slot == d.slot && stale.generation != d.generation);
    REQUIRE(ios27_21f_mutex_lock(rt, stale) == EINVAL);
    REQUIRE(ios27_21f_mutex_destroy(rt, stale) == EINVAL);
    for (round = 0; round < 12u; ++round) {
        enum { N = 12 };
        fair_args workers[N];
        pthread_t threads[N];
        unsigned order[N], i;
        atomic_uint next = ATOMIC_VAR_INIT(0);
        REQUIRE(ios27_21f_mutex_lock(rt, a) == 0);
        for (i = 0; i < N; ++i) {
            workers[i] = (fair_args){{rt, a}, i, order, &next};
            REQUIRE(pthread_create(&threads[i], NULL, fair_worker, &workers[i]) == 0);
            await_queue(rt, a, i + 1u);
        }
        REQUIRE(ios27_21f_mutex_info(rt, a, &info) == 0 && info.queued == N);
        REQUIRE(ios27_21f_mutex_trylock(rt, a) == EBUSY);
        REQUIRE(ios27_21f_mutex_unlock(rt, a) == 0);
        for (i = 0; i < N; ++i) REQUIRE(pthread_join(threads[i], NULL) == 0);
        for (i = 0; i < N; ++i) REQUIRE(order[i] == i);
        REQUIRE(ios27_21f_mutex_info(rt, a, &info) == 0 && !info.queued);
    }
    {
        cycle_args cycle = {{rt, a}, b, ATOMIC_VAR_INIT(0)};
        REQUIRE(ios27_21f_mutex_lock(rt, a) == 0);
        REQUIRE(pthread_create(&thread, NULL, cycle_worker, &cycle) == 0);
        {
            uint64_t deadline = now_ms() + 7000;
            while (!atomic_load_explicit(&cycle.held, memory_order_acquire)) {
                REQUIRE(now_ms() < deadline);
                sched_yield();
            }
        }
        await_queue(rt, a, 1);
        REQUIRE(ios27_21f_mutex_lock(rt, b) == EDEADLK);
        REQUIRE(ios27_21f_mutex_unlock(rt, a) == 0);
        REQUIRE(pthread_join(thread, NULL) == 0);
    }
    REQUIRE(ios27_21f_runtime_stats(rt, &stats) == 0);
    REQUIRE(stats.handoffs >= 12u * 12u + 1u);
    REQUIRE(stats.deadlocks >= 3);
    REQUIRE(stats.peak_waiters >= 12);
    REQUIRE(!stats.pending_waiters);
    REQUIRE(ios27_21f_mutex_destroy(rt, a) == 0);
    REQUIRE(ios27_21f_mutex_destroy(rt, b) == 0);
    REQUIRE(ios27_21f_mutex_destroy(rt, c) == 0);
    REQUIRE(ios27_21f_mutex_destroy(rt, d) == 0);
    REQUIRE(ios27_21f_runtime_destroy(rt) == 0);
    puts("phase21f functional: fifo ownership, graph deadlocks, generations, policies passed");
    return 0;
}
