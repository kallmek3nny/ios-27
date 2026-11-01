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
    fprintf(stderr,"phase21f starvation %s:%d: %s\n",__FILE__,__LINE__,#x); \
    abort(); } } while (0)
enum { QUEUED = 48, BARGERS = 4, ROUNDS = 25 };
typedef struct {
    ios27_21f_runtime *rt;
    ios27_21f_handle lock;
    atomic_uint completed, started, stop, barged;
    unsigned order[QUEUED];
} campaign;
typedef struct { campaign *c; unsigned index; } worker_args;
static uint64_t now_ms(void) {
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static void await_queue(campaign *c, unsigned count) {
    uint64_t limit = now_ms() + 10000;
    for (;;) {
        ios27_21f_info info;
        CHECK(ios27_21f_mutex_info(c->rt, c->lock, &info) == 0);
        if (info.queued == count) return;
        CHECK(now_ms() < limit);
        sched_yield();
    }
}
static void *worker(void *ctx) {
    worker_args *a = ctx;
    campaign *c = a->c;
    CHECK(ios27_21f_mutex_lock(c->rt, c->lock) == 0);
    unsigned pos = atomic_fetch_add_explicit(&c->completed, 1u,
                                              memory_order_relaxed);
    CHECK(pos < QUEUED);
    c->order[pos] = a->index;
    CHECK(ios27_21f_mutex_unlock(c->rt, c->lock) == 0);
    return NULL;
}
static void *barger(void *ctx) {
    campaign *c = ctx;
    atomic_fetch_add_explicit(&c->started, 1u, memory_order_release);
    while (!atomic_load_explicit(&c->stop, memory_order_acquire)) {
        int rc = ios27_21f_mutex_trylock(c->rt, c->lock);
        CHECK(rc == 0 || rc == EBUSY);
        if (!rc) {
            if (atomic_load_explicit(&c->completed, memory_order_relaxed) < QUEUED)
                atomic_fetch_add_explicit(&c->barged, 1u, memory_order_relaxed);
            CHECK(ios27_21f_mutex_unlock(c->rt, c->lock) == 0);
        }
        sched_yield();
    }
    return NULL;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle lock;
    CHECK(ios27_21f_runtime_create(&rt, 1) == 0);
    CHECK(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &lock) == 0);
    for (unsigned round = 0; round < ROUNDS; ++round) {
        campaign c = {0};
        worker_args args[QUEUED];
        pthread_t threads[QUEUED], spin[BARGERS];
        c.rt = rt;
        c.lock = lock;
        CHECK(ios27_21f_mutex_lock(rt, lock) == 0);
        for (unsigned i = 0; i < QUEUED; ++i) {
            args[i].c = &c;
            args[i].index = i;
            CHECK(pthread_create(&threads[i], NULL, worker, &args[i]) == 0);
            await_queue(&c, i + 1u);
        }
        for (unsigned i = 0; i < BARGERS; ++i)
            CHECK(pthread_create(&spin[i], NULL, barger, &c) == 0);
        uint64_t deadline = now_ms() + 10000;
        while (atomic_load_explicit(&c.started, memory_order_acquire) < BARGERS) {
            CHECK(now_ms() < deadline);
            sched_yield();
        }
        CHECK(ios27_21f_mutex_unlock(rt, lock) == 0);
        for (unsigned i = 0; i < QUEUED; ++i)
            CHECK(pthread_join(threads[i], NULL) == 0);
        atomic_store_explicit(&c.stop, 1u, memory_order_release);
        for (unsigned i = 0; i < BARGERS; ++i)
            CHECK(pthread_join(spin[i], NULL) == 0);
        CHECK(atomic_load_explicit(&c.completed, memory_order_relaxed) == QUEUED);
        CHECK(atomic_load_explicit(&c.barged, memory_order_relaxed) == 0);
        for (unsigned i = 0; i < QUEUED; ++i)
            CHECK(c.order[i] == i);
    }
    ios27_21f_stats stats;
    CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
    CHECK(stats.pending_waiters == 0 && stats.peak_waiters >= QUEUED);
    CHECK(ios27_21f_mutex_destroy(rt, lock) == 0);
    CHECK(ios27_21f_runtime_destroy(rt) == 0);
    puts("phase21f starvation: 1200 FIFO handoffs with concurrent trylock bargers passed");
    return 0;
}
