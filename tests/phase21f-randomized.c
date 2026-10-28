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
#define ASSERT(x) do { if (!(x)) { fprintf(stderr, "randomized %d: %s\n", __LINE__, #x); abort(); } } while (0)
enum { LOCKS = 4, THREADS = 12, OPS = 7000 };
typedef struct {
    ios27_21f_runtime *rt;
    ios27_21f_handle h[LOCKS];
    uint64_t count[LOCKS];
    atomic_uint expected[LOCKS];
    atomic_int ready, start;
} shared;
typedef struct { shared *s; uint32_t seed; } actor;
static uint32_t next(uint32_t *seed) {
    uint32_t x = *seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x;
    return x;
}
static uint64_t millis(void) {
    struct timespec t;
    ASSERT(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static void mark(shared *s, unsigned k) {
    ++s->count[k];
    (void)atomic_fetch_add_explicit(&s->expected[k], 1, memory_order_relaxed);
}
static void *worker(void *argument) {
    actor *a = argument;
    shared *s = a->s;
    uint32_t rand = a->seed;
    unsigned i;
    atomic_fetch_add_explicit(&s->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&s->start, memory_order_acquire)) sched_yield();
    for (i = 0; i < OPS; ++i) {
        unsigned first = next(&rand) % LOCKS;
        unsigned second = (first + 1u + (next(&rand) % (LOCKS - 1u))) % LOCKS;
        int result;
        ASSERT(ios27_21f_mutex_lock(s->rt, s->h[first]) == 0);
        mark(s, first);
        if ((next(&rand) & 7u) == 0u) sched_yield();
        result = ios27_21f_mutex_lock(s->rt, s->h[second]);
        ASSERT(result == 0 || result == EDEADLK);
        if (!result) {
            mark(s, second);
            ASSERT(ios27_21f_mutex_unlock(s->rt, s->h[second]) == 0);
        }
        ASSERT(ios27_21f_mutex_unlock(s->rt, s->h[first]) == 0);
    }
    return NULL;
}
int main(void) {
    shared s = {0};
    pthread_t threads[THREADS];
    actor args[THREADS];
    ios27_21f_stats stats;
    uint64_t limit, actual = 0, expected = 0;
    unsigned i;
    ASSERT(ios27_21f_runtime_create(&s.rt, LOCKS) == 0);
    for (i = 0; i < LOCKS; ++i)
        ASSERT(ios27_21f_mutex_create(s.rt, IOS27_21F_ERRORCHECK, &s.h[i]) == 0);
    for (i = 0; i < THREADS; ++i) {
        args[i] = (actor){&s, 0x9e3779b9u + i * 0x45d9f3bu};
        ASSERT(pthread_create(&threads[i], NULL, worker, &args[i]) == 0);
    }
    limit = millis() + 7000;
    while (atomic_load_explicit(&s.ready, memory_order_acquire) < THREADS) {
        ASSERT(millis() < limit);
        sched_yield();
    }
    atomic_store_explicit(&s.start, 1, memory_order_release);
    for (i = 0; i < THREADS; ++i) ASSERT(pthread_join(threads[i], NULL) == 0);
    for (i = 0; i < LOCKS; ++i) {
        actual += s.count[i];
        expected += atomic_load_explicit(&s.expected[i], memory_order_relaxed);
        ASSERT(ios27_21f_mutex_destroy(s.rt, s.h[i]) == 0);
    }
    ASSERT(actual == expected && actual >= (uint64_t)THREADS * OPS);
    ASSERT(ios27_21f_runtime_stats(s.rt, &stats) == 0);
    ASSERT(stats.pending_waiters == 0);
    ASSERT(stats.deadlocks > 0);
    ASSERT(ios27_21f_runtime_destroy(s.rt) == 0);
    printf("phase21f randomized: %llu consistent acquisitions, %llu graph cycles refused\n",
           (unsigned long long)actual, (unsigned long long)stats.deadlocks);
    return 0;
}
