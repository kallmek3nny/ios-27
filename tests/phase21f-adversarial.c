#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21f_mutex.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "phase21f adversarial %d: %s\n", __LINE__, #x); abort(); } } while (0)
typedef struct { ios27_21f_runtime *rt; ios27_21f_handle own, next; atomic_int ready; } node;
static uint64_t tick(void) {
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static void wait_ready(atomic_int *ready) {
    uint64_t stop = tick() + 6000;
    while (!atomic_load_explicit(ready, memory_order_acquire)) {
        CHECK(tick() < stop);
        sched_yield();
    }
}
static void wait_pending(ios27_21f_runtime *rt, ios27_21f_handle h, size_t n) {
    uint64_t stop = tick() + 6000;
    for (;;) {
        ios27_21f_info out;
        CHECK(ios27_21f_mutex_info(rt, h, &out) == 0);
        if (out.queued == n) return;
        CHECK(tick() < stop);
        sched_yield();
    }
}
static void *chain_worker(void *p) {
    node *n = p;
    CHECK(ios27_21f_mutex_lock(n->rt, n->own) == 0);
    atomic_store_explicit(&n->ready, 1, memory_order_release);
    CHECK(ios27_21f_mutex_lock(n->rt, n->next) == 0);
    CHECK(ios27_21f_mutex_unlock(n->rt, n->next) == 0);
    CHECK(ios27_21f_mutex_unlock(n->rt, n->own) == 0);
    return NULL;
}
typedef struct {
    ios27_21f_runtime *rt;
    ios27_21f_handle h;
    atomic_uint *completed;
    unsigned minimum;
} barger;
static void *barger_worker(void *p) {
    barger *b = p;
    uint64_t stop = tick() + 6000;
    int result;
    do {
        result = ios27_21f_mutex_trylock(b->rt, b->h);
        CHECK(result == 0 || result == EBUSY);
        if (result == EBUSY) {
            CHECK(tick() < stop);
            sched_yield();
        }
    } while (result == EBUSY);
    CHECK(atomic_load_explicit(b->completed, memory_order_acquire) >= b->minimum);
    CHECK(ios27_21f_mutex_unlock(b->rt, b->h) == 0);
    return NULL;
}
typedef struct {
    ios27_21f_runtime *rt;
    ios27_21f_handle h;
    unsigned id;
    unsigned *order;
    atomic_uint *completed;
} queue_worker;
static void *queued_worker(void *p) {
    queue_worker *q = p;
    unsigned n;
    CHECK(ios27_21f_mutex_lock(q->rt, q->h) == 0);
    n = atomic_fetch_add_explicit(q->completed, 1, memory_order_relaxed);
    q->order[n] = q->id;
    CHECK(ios27_21f_mutex_unlock(q->rt, q->h) == 0);
    return NULL;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle a, b, c;
    pthread_t x, y;
    ios27_21f_stats stats;
    unsigned cycle;
    CHECK(ios27_21f_runtime_create(&rt, 3) == 0);
    CHECK(ios27_21f_mutex_create(rt, IOS27_21F_ERRORCHECK, &a) == 0);
    CHECK(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &b) == 0);
    CHECK(ios27_21f_mutex_create(rt, IOS27_21F_RECURSIVE, &c) == 0);
    for (cycle = 0; cycle < 125; ++cycle) {
        node one = {rt, b, a, ATOMIC_VAR_INIT(0)};
        node two = {rt, c, b, ATOMIC_VAR_INIT(0)};
        CHECK(ios27_21f_mutex_lock(rt, a) == 0);
        CHECK(pthread_create(&x, NULL, chain_worker, &one) == 0);
        wait_ready(&one.ready);
        wait_pending(rt, a, 1);
        CHECK(pthread_create(&y, NULL, chain_worker, &two) == 0);
        wait_ready(&two.ready);
        wait_pending(rt, b, 1);
        CHECK(ios27_21f_mutex_lock(rt, c) == EDEADLK);
        CHECK(ios27_21f_mutex_unlock(rt, a) == 0);
        CHECK(pthread_join(x, NULL) == 0);
        CHECK(pthread_join(y, NULL) == 0);
    }
    for (cycle = 0; cycle < 45; ++cycle) {
        enum { N = 10 };
        queue_worker q[N];
        pthread_t th[N];
        unsigned order[N], j;
        atomic_uint completed = ATOMIC_VAR_INIT(0);
        CHECK(ios27_21f_mutex_lock(rt, a) == 0);
        for (j = 0; j < N; ++j) {
            q[j] = (queue_worker){rt, a, j, order, &completed};
            CHECK(pthread_create(&th[j], NULL, queued_worker, &q[j]) == 0);
            wait_pending(rt, a, j + 1u);
        }
        /* Cancel the middle and the current first waiter while keeping
           the mutex locked. Surviving waiters must not be overtaken. */
        for (j = 1; j < N; j += 2) CHECK(pthread_cancel(th[j]) == 0);
        CHECK(pthread_cancel(th[0]) == 0);
        for (j = 1; j < N; j += 2) {
            void *ret;
            CHECK(pthread_join(th[j], &ret) == 0 && ret == PTHREAD_CANCELED);
        }
        {
            void *ret;
            CHECK(pthread_join(th[0], &ret) == 0 && ret == PTHREAD_CANCELED);
        }
        wait_pending(rt, a, 4);
        {
            pthread_t outsider;
            barger candidate = {rt, a, &completed, 4};
            CHECK(pthread_create(&outsider, NULL, barger_worker, &candidate) == 0);
            CHECK(ios27_21f_mutex_unlock(rt, a) == 0);
            CHECK(pthread_join(outsider, NULL) == 0);
        }
        for (j = 2; j < N; j += 2) CHECK(pthread_join(th[j], NULL) == 0);
        CHECK(atomic_load_explicit(&completed, memory_order_relaxed) == 4);
        for (j = 0; j < 4; ++j) CHECK(order[j] == 2u * (j + 1u));
    }
    for (cycle = 0; cycle < 32000; ++cycle) {
        ios27_21f_handle old = b;
        CHECK(ios27_21f_mutex_destroy(rt, b) == 0);
        CHECK(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &b) == 0);
        CHECK(old.slot == b.slot && old.generation != b.generation);
        CHECK(ios27_21f_mutex_lock(rt, old) == EINVAL);
    }
    CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
    CHECK(stats.deadlocks >= 125);
    CHECK(stats.canceled >= 45 * 6);
    CHECK(stats.peak_waiters >= 10);
    CHECK(stats.pending_waiters == 0);
    CHECK(ios27_21f_mutex_destroy(rt, a) == 0);
    CHECK(ios27_21f_mutex_destroy(rt, b) == 0);
    CHECK(ios27_21f_mutex_destroy(rt, c) == 0);
    CHECK(ios27_21f_runtime_destroy(rt) == 0);
    puts("phase21f adversarial: three-thread cycles, queue cancellation, 32000 generations passed");
    return 0;
}
