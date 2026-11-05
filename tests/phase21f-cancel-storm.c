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
    fprintf(stderr,"phase21f cancel storm %s:%d: %s\n",__FILE__,__LINE__,#expr); \
    abort(); } } while (0)
enum { N = 48, ROUNDS = 24 };
typedef struct {
    ios27_21f_runtime *rt;
    ios27_21f_handle mutex;
    atomic_uint *completed;
} task;
static uint64_t now_ms(void) {
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static void await_queue(ios27_21f_runtime *rt, ios27_21f_handle h, size_t n) {
    uint64_t deadline = now_ms() + 10000;
    for (;;) {
        ios27_21f_info info;
        CHECK(ios27_21f_mutex_info(rt, h, &info) == 0);
        if (info.queued == n) return;
        CHECK(now_ms() < deadline);
        sched_yield();
    }
}
static void *worker(void *argument) {
    task *t = argument;
    CHECK(ios27_21f_mutex_lock(t->rt, t->mutex) == 0);
    atomic_fetch_add_explicit(t->completed, 1u, memory_order_relaxed);
    CHECK(ios27_21f_mutex_unlock(t->rt, t->mutex) == 0);
    return (void *)(uintptr_t)1u;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle h;
    unsigned total_canceled = 0, total_completed = 0;
    CHECK(ios27_21f_runtime_create(&rt, 1) == 0);
    CHECK(ios27_21f_mutex_create(rt, IOS27_21F_ERRORCHECK, &h) == 0);
    for (unsigned round = 0; round < ROUNDS; ++round) {
        pthread_t threads[N];
        task jobs[N];
        atomic_uint completed = ATOMIC_VAR_INIT(0);
        size_t waiting = 0;
        unsigned canceled = 0;
        CHECK(ios27_21f_mutex_lock(rt, h) == 0);
        for (unsigned i = 0; i < N; ++i) {
            jobs[i] = (task){rt, h, &completed};
            CHECK(pthread_create(&threads[i], NULL, worker, &jobs[i]) == 0);
            await_queue(rt, h, ++waiting);
        }
        /* Remove queued nodes, including head, middle and tail, before handoff. */
        for (unsigned i = 0; i < N; i += 3) {
            void *result = NULL;
            CHECK(pthread_cancel(threads[i]) == 0);
            CHECK(pthread_join(threads[i], &result) == 0);
            CHECK(result == PTHREAD_CANCELED);
            --waiting;
            await_queue(rt, h, waiting);
            ++canceled;
        }
        CHECK(ios27_21f_mutex_unlock(rt, h) == 0);
        /* Surviving queued workers must all receive a handoff in FIFO order. */
        for (unsigned i = 0; i < N; ++i) {
            void *result = NULL;
            if (i % 3 == 0) continue;
            CHECK(pthread_join(threads[i], &result) == 0);
            CHECK(result == (void *)(uintptr_t)1u);
        }
        CHECK(canceled + atomic_load_explicit(&completed, memory_order_relaxed) == N);
        ios27_21f_info info;
        CHECK(ios27_21f_mutex_info(rt, h, &info) == 0 && !info.owned && !info.queued);
        total_canceled += canceled;
        total_completed += atomic_load_explicit(&completed, memory_order_relaxed);
    }
    ios27_21f_stats stats;
    CHECK(ios27_21f_runtime_stats(rt, &stats) == 0);
    CHECK(stats.canceled == total_canceled && !stats.pending_waiters);
    CHECK(ios27_21f_mutex_destroy(rt, h) == 0);
    CHECK(ios27_21f_runtime_destroy(rt) == 0);
    printf("phase21f cancel storm: %u canceled, %u completed across %u contested workers\n",
           total_canceled, total_completed, N * ROUNDS);
    return 0;
}
