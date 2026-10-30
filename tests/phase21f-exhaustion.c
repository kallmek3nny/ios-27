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
#define VERIFY(x) do { if (!(x)) { fprintf(stderr, "phase21f exhaustion %d: %s\n", __LINE__, #x); abort(); } } while (0)
#if IOS27_21F_MAX_WAITERS != 16
#error "compile exhaustion test with -DIOS27_21F_MAX_WAITERS=16"
#endif
typedef struct {
    ios27_21f_runtime *rt;
    ios27_21f_handle m;
    atomic_uint *completed;
    int result;
} task;
static uint64_t msec(void) {
    struct timespec ts;
    VERIFY(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static void wait_queue(ios27_21f_runtime *rt, ios27_21f_handle m, size_t n) {
    uint64_t bound = msec() + 7000;
    for (;;) {
        ios27_21f_info info;
        VERIFY(ios27_21f_mutex_info(rt, m, &info) == 0);
        if (info.queued == n) return;
        VERIFY(msec() < bound);
        sched_yield();
    }
}
static void *worker(void *data) {
    task *t = data;
    t->result = ios27_21f_mutex_lock(t->rt, t->m);
    if (!t->result) {
        atomic_fetch_add_explicit(t->completed, 1u, memory_order_relaxed);
        VERIFY(ios27_21f_mutex_unlock(t->rt, t->m) == 0);
    }
    return NULL;
}
int main(void) {
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle m;
    ios27_21f_stats stats;
    pthread_t threads[17];
    task work[17];
    atomic_uint completed = ATOMIC_VAR_INIT(0);
    unsigned i;
    VERIFY(ios27_21f_runtime_create(&rt, 2) == 0);
    VERIFY(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &m) == 0);
    VERIFY(ios27_21f_mutex_lock(rt, m) == 0);
    for (i = 0; i < 16; ++i) {
        work[i] = (task){rt, m, &completed, -1};
        VERIFY(pthread_create(&threads[i], NULL, worker, &work[i]) == 0);
        wait_queue(rt, m, i + 1u);
    }
    VERIFY(ios27_21f_runtime_stats(rt, &stats) == 0);
    VERIFY(stats.pending_waiters == 16);
    VERIFY(stats.peak_waiters == 16);
    work[16] = (task){rt, m, &completed, -1};
    VERIFY(pthread_create(&threads[16], NULL, worker, &work[16]) == 0);
    VERIFY(pthread_join(threads[16], NULL) == 0);
    VERIFY(work[16].result == EAGAIN);
    /* Cancel a queued waiter, then verify capacity is reclaimed. */
    VERIFY(pthread_cancel(threads[7]) == 0);
    {
        void *ret;
        VERIFY(pthread_join(threads[7], &ret) == 0);
        VERIFY(ret == PTHREAD_CANCELED);
    }
    wait_queue(rt, m, 15);
    work[16].result = -1;
    VERIFY(pthread_create(&threads[16], NULL, worker, &work[16]) == 0);
    wait_queue(rt, m, 16);
    VERIFY(ios27_21f_mutex_destroy(rt, m) == EBUSY);
    VERIFY(ios27_21f_mutex_unlock(rt, m) == 0);
    for (i = 0; i < 16; ++i)
        if (i != 7) {
            VERIFY(pthread_join(threads[i], NULL) == 0);
            VERIFY(work[i].result == 0);
        }
    VERIFY(pthread_join(threads[16], NULL) == 0);
    VERIFY(work[16].result == 0);
    VERIFY(atomic_load_explicit(&completed, memory_order_relaxed) == 16);
    VERIFY(ios27_21f_runtime_stats(rt, &stats) == 0);
    VERIFY(stats.pending_waiters == 0);
    VERIFY(stats.canceled >= 1);
    VERIFY(ios27_21f_mutex_destroy(rt, m) == 0);
    VERIFY(ios27_21f_runtime_destroy(rt) == 0);
    puts("phase21f exhaustion: quota denied 17th waiter, cancellation reclaimed capacity");
    return 0;
}
