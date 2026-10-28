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
    fprintf(stderr,"phase21f graph depth %s:%d: %s\n",__FILE__,__LINE__,#expr); \
    abort(); } } while (0)
enum { CHAIN = 64, ROUNDS = 15 };
typedef struct {
    ios27_21f_runtime *rt;
    ios27_21f_handle own, next;
    atomic_int ready, go;
} link;
static uint64_t now_ms(void) {
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static void await_ready(atomic_int *flag) {
    uint64_t deadline = now_ms() + 10000;
    while (!atomic_load_explicit(flag, memory_order_acquire)) {
        CHECK(now_ms() < deadline);
        sched_yield();
    }
}
static void await_queue(ios27_21f_runtime *rt, ios27_21f_handle h) {
    uint64_t deadline = now_ms() + 10000;
    for (;;) {
        ios27_21f_info info;
        CHECK(ios27_21f_mutex_info(rt, h, &info) == 0);
        if (info.queued) return;
        CHECK(now_ms() < deadline);
        sched_yield();
    }
}
static void *worker(void *arg) {
    link *l = arg;
    CHECK(ios27_21f_mutex_lock(l->rt, l->own) == 0);
    atomic_store_explicit(&l->ready, 1, memory_order_release);
    await_ready(&l->go);
    CHECK(ios27_21f_mutex_lock(l->rt, l->next) == 0);
    CHECK(ios27_21f_mutex_unlock(l->rt, l->next) == 0);
    CHECK(ios27_21f_mutex_unlock(l->rt, l->own) == 0);
    return NULL;
}
int main(void) {
    for (unsigned round = 0; round < ROUNDS; ++round) {
        ios27_21f_runtime *rt = NULL;
        ios27_21f_handle h[CHAIN + 1];
        link links[CHAIN - 1] = {0};
        pthread_t threads[CHAIN - 1];
        ios27_21f_stats stats;
        CHECK(ios27_21f_runtime_create(&rt, CHAIN + 1) == 0);
        for (unsigned i = 0; i < CHAIN + 1; ++i)
            CHECK(ios27_21f_mutex_create(rt, IOS27_21F_ERRORCHECK, &h[i]) == 0);
        CHECK(ios27_21f_mutex_lock(rt, h[0]) == 0);
        for (unsigned i = 1; i < CHAIN; ++i) {
            links[i - 1].rt = rt;
            links[i - 1].own = h[i];
            links[i - 1].next = h[i - 1];
            CHECK(pthread_create(&threads[i - 1], NULL, worker, &links[i - 1]) == 0);
            await_ready(&links[i - 1].ready);
        }
        for (unsigned i = 1; i < CHAIN; ++i) {
            atomic_store_explicit(&links[i - 1].go, 1, memory_order_release);
            await_queue(rt, h[i - 1]);
        }
        /* 63 edges form a legal acyclic chain. The 64th closes it. */
        CHECK(ios27_21f_mutex_lock(rt, h[CHAIN]) == 0);
        CHECK(ios27_21f_mutex_unlock(rt, h[CHAIN]) == 0);
        CHECK(ios27_21f_mutex_trylock(rt, h[CHAIN - 1]) == EBUSY);
        CHECK(ios27_21f_mutex_lock(rt, h[CHAIN - 1]) == EDEADLK);
        CHECK(ios27_21f_mutex_unlock(rt, h[0]) == 0);
        for (unsigned i = 1; i < CHAIN; ++i)
            CHECK(pthread_join(threads[i - 1], NULL) == 0);
        CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
        CHECK(stats.deadlocks == 1 && stats.pending_waiters == 0);
        CHECK(stats.handoffs >= CHAIN - 1);
        for (unsigned i = 0; i < CHAIN + 1; ++i)
            CHECK(ios27_21f_mutex_destroy(rt, h[i]) == 0);
        CHECK(ios27_21f_runtime_destroy(rt) == 0);
    }
    puts("phase21f graph depth: 15 x 64-thread chains, cycles and acyclic controls passed");
    return 0;
}
