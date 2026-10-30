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
    fprintf(stderr, "phase21f stress failure %s:%d: %s\n", \
            __FILE__, __LINE__, #expr); abort(); } } while (0)
typedef struct { ios27_21f_runtime *rt; ios27_21f_handle h; unsigned long *counter; } job;
static uint64_t monotonic_ms(void) {
    struct timespec t;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static void *increment(void *p) {
    job *j = p;
    unsigned i;
    for (i = 0; i < 10000; ++i) {
        int err = ios27_21f_mutex_trylock(j->rt, j->h);
        if (err == EBUSY) err = ios27_21f_mutex_lock(j->rt, j->h);
        REQUIRE(err == 0);
        ++*j->counter;
        REQUIRE(ios27_21f_mutex_unlock(j->rt, j->h) == 0);
    }
    return NULL;
}
static void cleanup_unlock(void *p) {
    job *j = p;
    int err = ios27_21f_mutex_unlock(j->rt, j->h);
    REQUIRE(err == 0 || err == EPERM);
}
static void *cancellable(void *p) {
    job *j = p;
    int err;
    /* If cancellation lands after the handoff, the outer handler
       releases an acquired mutex; otherwise the inner waiter handler does. */
    pthread_cleanup_push(cleanup_unlock, j);
    err = ios27_21f_mutex_lock(j->rt, j->h);
    REQUIRE(err == 0);
    pthread_testcancel();
    REQUIRE(ios27_21f_mutex_unlock(j->rt, j->h) == 0);
    pthread_cleanup_pop(0);
    return NULL;
}
static void wait_for_queue(job *j) {
    uint64_t end = monotonic_ms() + 7000;
    for (;;) {
        ios27_21f_info info;
        REQUIRE(ios27_21f_mutex_info(j->rt, j->h, &info) == 0);
        if (info.queued == 1) return;
        REQUIRE(monotonic_ms() < end);
        sched_yield();
    }
}
int main(void) {
    enum { WORKERS = 10, CANCEL_ROUNDS = 250 };
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle h;
    ios27_21f_stats stats;
    pthread_t threads[WORKERS];
    unsigned long counter = 0;
    job j;
    unsigned i;
    REQUIRE(ios27_21f_runtime_create(&rt, 8) == 0);
    REQUIRE(ios27_21f_mutex_create(rt, IOS27_21F_ERRORCHECK, &h) == 0);
    j = (job){rt, h, &counter};
    for (i = 0; i < WORKERS; ++i)
        REQUIRE(pthread_create(&threads[i], NULL, increment, &j) == 0);
    for (i = 0; i < WORKERS; ++i)
        REQUIRE(pthread_join(threads[i], NULL) == 0);
    REQUIRE(counter == (unsigned long)WORKERS * 10000u);
    for (i = 0; i < CANCEL_ROUNDS; ++i) {
        void *ret = NULL;
        REQUIRE(ios27_21f_mutex_lock(rt, h) == 0);
        REQUIRE(pthread_create(&threads[0], NULL, cancellable, &j) == 0);
        wait_for_queue(&j);
        if (i & 1u) {
            REQUIRE(ios27_21f_mutex_unlock(rt, h) == 0);
            REQUIRE(pthread_cancel(threads[0]) == 0);
        } else {
            REQUIRE(pthread_cancel(threads[0]) == 0);
            REQUIRE(ios27_21f_mutex_unlock(rt, h) == 0);
        }
        REQUIRE(pthread_join(threads[0], &ret) == 0);
        REQUIRE(ret == NULL || ret == PTHREAD_CANCELED);
        REQUIRE(ios27_21f_mutex_trylock(rt, h) == 0);
        REQUIRE(ios27_21f_mutex_unlock(rt, h) == 0);
    }
    REQUIRE(ios27_21f_runtime_stats(rt, &stats) == 0);
    REQUIRE(!stats.pending_waiters);
    REQUIRE(stats.grants >= (uint64_t)WORKERS * 10000u);
    REQUIRE(ios27_21f_mutex_destroy(rt, h) == 0);
    REQUIRE(ios27_21f_runtime_destroy(rt) == 0);
    printf("phase21f stress: %lu increments, %u cancellation races, %llu handoffs passed\n",
           counter, CANCEL_ROUNDS, (unsigned long long)stats.handoffs);
    return 0;
}
