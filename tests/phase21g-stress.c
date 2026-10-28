#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21g_cond.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "phase21g:%d %s failed\n", __LINE__, #expr); abort(); \
} } while (0)
#define OK(expr) CHECK((expr) == 0)
#define MAX_WAIT_YIELDS 15000000
static void until_queued(ios27_21g_runtime *rt, ios27_21g_handle c,
                         size_t count) {
    for (int i = 0; i < MAX_WAIT_YIELDS; ++i) {
        ios27_21g_stats s;
        OK(ios27_21g_cond_stats(rt, c, &s));
        if (s.queued == count) return;
        CHECK(s.queued <= count);
        (void)sched_yield();
    }
    CHECK(0 && "queue registration timeout");
}
static void deadline(enum ios27_21g_clock clock, struct timespec *out,
                     long add_ns) {
    OK(clock_gettime(clock == IOS27_21G_REALTIME ? CLOCK_REALTIME :
                     CLOCK_MONOTONIC, out) ? errno : 0);
    out->tv_nsec += add_ns;
    while (out->tv_nsec >= 1000000000L) {
        ++out->tv_sec;
        out->tv_nsec -= 1000000000L;
    }
}
enum { P = 6, C = 6, ITEMS = 3300, CAP = 11 };
typedef struct {
    ios27_21g_runtime *rt;
    ios27_21g_handle mutex, has_items, has_space;
    unsigned values[CAP];
    int head, tail, count, complete;
    uint64_t sum;
} queue;
typedef struct { queue *q; int id; } producer_args;
static void *produce(void *arg) {
    producer_args *a = arg;
    queue *q = a->q;
    for (int i = 0; i < ITEMS; ++i) {
        OK(ios27_21g_mutex_lock(q->rt, q->mutex));
        while (q->count == CAP)
            OK(ios27_21g_cond_wait(q->rt, q->has_space, q->mutex));
        q->values[q->tail] = (unsigned)(a->id * ITEMS + i + 1);
        q->tail = (q->tail + 1) % CAP;
        ++q->count;
        OK(ios27_21g_cond_signal(q->rt, q->has_items));
        OK(ios27_21g_mutex_unlock(q->rt, q->mutex));
    }
    return NULL;
}
static void *consume(void *arg) {
    queue *q = arg;
    for (;;) {
        OK(ios27_21g_mutex_lock(q->rt, q->mutex));
        while (!q->count && !q->complete)
            OK(ios27_21g_cond_wait(q->rt, q->has_items, q->mutex));
        if (!q->count && q->complete) {
            OK(ios27_21g_mutex_unlock(q->rt, q->mutex));
            return NULL;
        }
        q->sum += q->values[q->head];
        q->head = (q->head + 1) % CAP;
        --q->count;
        OK(ios27_21g_cond_signal(q->rt, q->has_space));
        OK(ios27_21g_mutex_unlock(q->rt, q->mutex));
    }
}
static void test_queue(void) {
    queue q = {0};
    pthread_t producers[P], consumers[C];
    producer_args args[P];
    OK(ios27_21g_runtime_create(&q.rt, 5));
    OK(ios27_21g_create(q.rt, IOS27_21G_MUTEX, &q.mutex));
    OK(ios27_21g_create(q.rt, IOS27_21G_COND, &q.has_items));
    OK(ios27_21g_create(q.rt, IOS27_21G_COND, &q.has_space));
    for (int i = 0; i < C; ++i) OK(pthread_create(&consumers[i], NULL, consume, &q));
    for (int i = 0; i < P; ++i) {
        args[i] = (producer_args){&q, i};
        OK(pthread_create(&producers[i], NULL, produce, &args[i]));
    }
    for (int i = 0; i < P; ++i) OK(pthread_join(producers[i], NULL));
    OK(ios27_21g_mutex_lock(q.rt, q.mutex));
    q.complete = 1;
    OK(ios27_21g_cond_broadcast(q.rt, q.has_items));
    OK(ios27_21g_mutex_unlock(q.rt, q.mutex));
    for (int i = 0; i < C; ++i) OK(pthread_join(consumers[i], NULL));
    CHECK(q.count == 0);
    CHECK(q.sum == ((uint64_t)P * ITEMS * ((uint64_t)P * ITEMS + 1u)) / 2u);
    OK(ios27_21g_destroy(q.rt, q.has_items));
    OK(ios27_21g_destroy(q.rt, q.has_space));
    OK(ios27_21g_destroy(q.rt, q.mutex));
    OK(ios27_21g_runtime_destroy(q.rt));
    puts("producer-consumer: 19,800 items and no missed wakeups");
}
typedef struct {
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c;
    int ticket, *turn, *done;
} fifo_args;
static void *fifo_worker(void *arg) {
    fifo_args *a = arg;
    OK(ios27_21g_mutex_lock(a->rt, a->m));
    while (*a->turn <= a->ticket)
        OK(ios27_21g_cond_wait(a->rt, a->c, a->m));
    CHECK(*a->done == a->ticket);
    ++*a->done;
    OK(ios27_21g_mutex_unlock(a->rt, a->m));
    return NULL;
}
static void test_fifo(void) {
    enum { N = 48 };
    ios27_21g_runtime *rt = NULL;
    ios27_21g_handle m, c;
    pthread_t th[N];
    fifo_args args[N];
    int turn = 0, done = 0;
    OK(ios27_21g_runtime_create(&rt, 3));
    OK(ios27_21g_create(rt, IOS27_21G_MUTEX, &m));
    OK(ios27_21g_create(rt, IOS27_21G_COND, &c));
    for (int i = 0; i < N; ++i) {
        args[i] = (fifo_args){rt, m, c, i, &turn, &done};
        OK(pthread_create(&th[i], NULL, fifo_worker, &args[i]));
        until_queued(rt, c, (size_t)(i + 1));
    }
    CHECK(ios27_21g_destroy(rt, c) == EBUSY);
    CHECK(ios27_21g_destroy(rt, m) == EBUSY);
    for (int i = 0; i < N; ++i) {
        OK(ios27_21g_mutex_lock(rt, m));
        ++turn;
        OK(ios27_21g_cond_signal(rt, c));
        OK(ios27_21g_mutex_unlock(rt, m));
        OK(pthread_join(th[i], NULL));
        CHECK(done == i + 1);
    }
    ios27_21g_stats stats;
    OK(ios27_21g_cond_stats(rt, c, &stats));
    CHECK(stats.queued == 0 && stats.outstanding == 0 && stats.wakeups == N);
    OK(ios27_21g_destroy(rt, c));
    OK(ios27_21g_destroy(rt, m));
    OK(ios27_21g_runtime_destroy(rt));
    puts("fifo: 48 independently enqueued waiters served in registration order");
}
typedef struct {
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c, ready, finished;
    int arrived, completed, epoch, phase;
} waves;
static void *wave_worker(void *arg) {
    waves *w = arg;
    for (int i = 0; i < 42; ++i) {
        OK(ios27_21g_mutex_lock(w->rt, w->m));
        while (w->phase != i)
            OK(ios27_21g_cond_wait(w->rt, w->ready, w->m));
        ++w->arrived;
        OK(ios27_21g_cond_signal(w->rt, w->ready));
        while (w->epoch == i)
            OK(ios27_21g_cond_wait(w->rt, w->c, w->m));
        CHECK(w->epoch == i + 1);
        ++w->completed;
        OK(ios27_21g_cond_signal(w->rt, w->finished));
        OK(ios27_21g_mutex_unlock(w->rt, w->m));
    }
    return NULL;
}
static void test_broadcast(void) {
    enum { N = 128 };
    waves w = {0};
    pthread_t th[N];
    OK(ios27_21g_runtime_create(&w.rt, 5));
    OK(ios27_21g_create(w.rt, IOS27_21G_MUTEX, &w.m));
    OK(ios27_21g_create(w.rt, IOS27_21G_COND, &w.c));
    OK(ios27_21g_create(w.rt, IOS27_21G_COND, &w.ready));
    OK(ios27_21g_create(w.rt, IOS27_21G_COND, &w.finished));
    for (int i = 0; i < N; ++i) OK(pthread_create(&th[i], NULL, wave_worker, &w));
    for (int epoch = 0; epoch < 42; ++epoch) {
        OK(ios27_21g_mutex_lock(w.rt, w.m));
        while (w.arrived < N) OK(ios27_21g_cond_wait(w.rt, w.ready, w.m));
        ++w.epoch;
        OK(ios27_21g_cond_broadcast(w.rt, w.c));
        while (w.completed < N)
            OK(ios27_21g_cond_wait(w.rt, w.finished, w.m));
        w.arrived = w.completed = 0;
        ++w.phase;
        OK(ios27_21g_cond_broadcast(w.rt, w.ready));
        OK(ios27_21g_mutex_unlock(w.rt, w.m));
    }
    for (int i = 0; i < N; ++i) OK(pthread_join(th[i], NULL));
    CHECK(w.epoch == 42);
    OK(ios27_21g_destroy(w.rt, w.finished));
    OK(ios27_21g_destroy(w.rt, w.ready));
    OK(ios27_21g_destroy(w.rt, w.c));
    OK(ios27_21g_destroy(w.rt, w.m));
    OK(ios27_21g_runtime_destroy(w.rt));
    puts("broadcast: 5,376 synchronized wait/reacquire transitions");
}
typedef struct {
    ios27_21g_runtime *rt;
    ios27_21g_handle mutex, cond;
    int rounds, success, timedout;
} timer_args;
static void *timer_worker(void *arg) {
    timer_args *a = arg;
    for (int i = 0; i < a->rounds; ++i) {
        struct timespec t;
        enum ios27_21g_clock clock = (i & 1) ? IOS27_21G_REALTIME :
                                               IOS27_21G_MONOTONIC;
        deadline(clock, &t, 1500000L);
        OK(ios27_21g_mutex_lock(a->rt, a->mutex));
        int ret = ios27_21g_cond_timedwait(a->rt, a->cond, a->mutex, clock, &t);
        CHECK(ret == 0 || ret == ETIMEDOUT);
        CHECK(ios27_21g_mutex_trylock(a->rt, a->mutex) == EBUSY);
        if (ret == 0) ++a->success;
        else ++a->timedout;
        OK(ios27_21g_mutex_unlock(a->rt, a->mutex));
    }
    return NULL;
}
static void test_timeout_races(void) {
    enum { N = 24, ROUNDS = 90 };
    ios27_21g_runtime *rt = NULL;
    ios27_21g_handle m, c;
    pthread_t th[N];
    timer_args args[N];
    struct timespec bad = {1, 1000000000L}, expired = {0, 0};
    OK(ios27_21g_runtime_create(&rt, 2));
    OK(ios27_21g_create(rt, IOS27_21G_MUTEX, &m));
    OK(ios27_21g_create(rt, IOS27_21G_COND, &c));
    OK(ios27_21g_mutex_lock(rt, m));
    CHECK(ios27_21g_cond_timedwait(rt, c, m, IOS27_21G_REALTIME, &bad) == EINVAL);
    CHECK(ios27_21g_cond_timedwait(rt, c, m, 99, &expired) == EINVAL);
    CHECK(ios27_21g_cond_timedwait(rt, c, m, IOS27_21G_REALTIME, &expired) == ETIMEDOUT);
    CHECK(ios27_21g_cond_timedwait(rt, c, m, IOS27_21G_MONOTONIC, &expired) == ETIMEDOUT);
    OK(ios27_21g_mutex_unlock(rt, m));
    for (int i = 0; i < N; ++i) {
        args[i] = (timer_args){rt, m, c, ROUNDS, 0, 0};
        OK(pthread_create(&th[i], NULL, timer_worker, &args[i]));
    }
    for (int i = 0; i < 2400; ++i) {
        if (i % 7 == 0) OK(ios27_21g_cond_broadcast(rt, c));
        else OK(ios27_21g_cond_signal(rt, c));
        (void)sched_yield();
    }
    int received = 0, expired_count = 0;
    for (int i = 0; i < N; ++i) {
        OK(pthread_join(th[i], NULL));
        received += args[i].success;
        expired_count += args[i].timedout;
    }
    CHECK(received + expired_count == N * ROUNDS);
    ios27_21g_stats stats;
    OK(ios27_21g_cond_stats(rt, c, &stats));
    CHECK(stats.queued == 0 && stats.outstanding == 0);
    CHECK(stats.timeouts >= (uint64_t)expired_count);
    OK(ios27_21g_destroy(rt, c));
    OK(ios27_21g_destroy(rt, m));
    OK(ios27_21g_runtime_destroy(rt));
    printf("timeout races: %d signals, %d timeouts, both clocks\n", received, expired_count);
}
typedef struct {
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c;
} cancel_args;
static void unlock_on_cancel(void *arg) {
    cancel_args *a = arg;
    OK(ios27_21g_mutex_unlock(a->rt, a->m));
}
static void *cancel_worker(void *arg) {
    cancel_args *a = arg;
    OK(ios27_21g_mutex_lock(a->rt, a->m));
    pthread_cleanup_push(unlock_on_cancel, a);
    for (;;) OK(ios27_21g_cond_wait(a->rt, a->c, a->m));
    pthread_cleanup_pop(1);
    return NULL;
}
static void test_cancellation_and_errors(void) {
    ios27_21g_runtime *rt = NULL;
    ios27_21g_handle m, c, extra, stale, fresh, invalid = {0, 0};
    cancel_args a;
    pthread_t th;
    void *exit_status;
    OK(ios27_21g_runtime_create(&rt, 3));
    OK(ios27_21g_create(rt, IOS27_21G_MUTEX, &m));
    OK(ios27_21g_create(rt, IOS27_21G_COND, &c));
    OK(ios27_21g_create(rt, IOS27_21G_MUTEX, &extra));
    CHECK(ios27_21g_create(rt, IOS27_21G_COND, &fresh) == EAGAIN);
    CHECK(ios27_21g_cond_signal(rt, invalid) == EINVAL);
    CHECK(ios27_21g_runtime_destroy(rt) == EBUSY);
    CHECK(ios27_21g_cond_wait(rt, c, m) == EPERM);
    OK(ios27_21g_mutex_lock(rt, m));
    CHECK(ios27_21g_mutex_lock(rt, m) == EDEADLK);
    CHECK(ios27_21g_mutex_trylock(rt, m) == EBUSY);
    CHECK(ios27_21g_destroy(rt, m) == EBUSY);
    OK(ios27_21g_mutex_unlock(rt, m));
    CHECK(ios27_21g_mutex_unlock(rt, m) == EPERM);
    a = (cancel_args){rt, m, c};
    OK(pthread_create(&th, NULL, cancel_worker, &a));
    until_queued(rt, c, 1);
    CHECK(ios27_21g_destroy(rt, c) == EBUSY);
    CHECK(ios27_21g_destroy(rt, m) == EBUSY);
    OK(pthread_cancel(th));
    OK(pthread_join(th, &exit_status));
    CHECK(exit_status == PTHREAD_CANCELED);
    ios27_21g_stats stats;
    OK(ios27_21g_cond_stats(rt, c, &stats));
    CHECK(stats.queued == 0 && stats.outstanding == 0);
    OK(ios27_21g_mutex_lock(rt, m));
    OK(ios27_21g_mutex_unlock(rt, m));
    stale = extra;
    OK(ios27_21g_destroy(rt, extra));
    OK(ios27_21g_create(rt, IOS27_21G_COND, &fresh));
    CHECK(fresh.slot == stale.slot && fresh.generation != stale.generation);
    CHECK(ios27_21g_mutex_lock(rt, stale) == EINVAL);
    OK(ios27_21g_destroy(rt, fresh));
    OK(ios27_21g_destroy(rt, c));
    OK(ios27_21g_destroy(rt, m));
    OK(ios27_21g_runtime_destroy(rt));
    puts("cancellation, ownership, quotas, stale handles: passed");
}
int main(void) {
    test_queue();
    test_fifo();
    test_broadcast();
    test_timeout_races();
    test_cancellation_and_errors();
    puts("phase21g: all compiled synchronization stress checks passed");
    return 0;
}
