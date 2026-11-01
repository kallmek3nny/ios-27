#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21f_mutex.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"phase21f lifecycle race %s:%d: %s\n", __FILE__,__LINE__,#x); \
    abort(); } } while (0)
enum { WORKERS = 16, ITERATIONS = 2400 };
typedef struct {
    ios27_21f_runtime *rt;
    atomic_uint *start, *ready;
    atomic_uint_fast64_t *denied, *success;
} job;
static void *worker(void *p) {
    job *j = p;
    atomic_fetch_add_explicit(j->ready, 1u, memory_order_release);
    while (!atomic_load_explicit(j->start, memory_order_acquire)) sched_yield();
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        ios27_21f_handle handle;
        int rc;
        do {
            rc = ios27_21f_mutex_create(j->rt, (i & 1u) ? IOS27_21F_RECURSIVE :
                                         IOS27_21F_ERRORCHECK, &handle);
            CHECK(rc == 0 || rc == EAGAIN);
            if (rc == EAGAIN) {
                atomic_fetch_add_explicit(j->denied, 1u, memory_order_relaxed);
                sched_yield();
            }
        } while (rc == EAGAIN);
        CHECK(ios27_21f_mutex_lock(j->rt, handle) == 0);
        if (i & 1u) CHECK(ios27_21f_mutex_trylock(j->rt, handle) == 0);
        else CHECK(ios27_21f_mutex_trylock(j->rt, handle) == EBUSY);
        if (i & 1u) CHECK(ios27_21f_mutex_unlock(j->rt, handle) == 0);
        CHECK(ios27_21f_mutex_unlock(j->rt, handle) == 0);
        CHECK(ios27_21f_mutex_destroy(j->rt, handle) == 0);
        CHECK(ios27_21f_mutex_lock(j->rt, handle) == EINVAL);
        CHECK(ios27_21f_mutex_destroy(j->rt, handle) == EINVAL);
        atomic_fetch_add_explicit(j->success, 1u, memory_order_relaxed);
    }
    return NULL;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    pthread_t threads[WORKERS];
    atomic_uint start = ATOMIC_VAR_INIT(0), ready = ATOMIC_VAR_INIT(0);
    atomic_uint_fast64_t denied = ATOMIC_VAR_INIT(0), success = ATOMIC_VAR_INIT(0);
    job j;
    CHECK(ios27_21f_runtime_create(&rt, 2) == 0);
    j = (job){rt, &start, &ready, &denied, &success};
    for (unsigned i = 0; i < WORKERS; ++i)
        CHECK(pthread_create(&threads[i], NULL, worker, &j) == 0);
    while (atomic_load_explicit(&ready, memory_order_acquire) < WORKERS)
        sched_yield();
    atomic_store_explicit(&start, 1u, memory_order_release);
    for (unsigned i = 0; i < WORKERS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(atomic_load_explicit(&success, memory_order_relaxed) ==
          (uint64_t)WORKERS * ITERATIONS);
    /* An allowed single-core schedule can serialize the workers entirely;
     * the separate full-capacity test guarantees EAGAIN coverage. */
    CHECK(ios27_21f_runtime_destroy(rt) == 0);
    printf("phase21f lifecycle: %u concurrent reuse cycles passed, %llu quota refusals observed\n",
           WORKERS * ITERATIONS,
           (unsigned long long)atomic_load_explicit(&denied, memory_order_relaxed));
    return 0;
}
