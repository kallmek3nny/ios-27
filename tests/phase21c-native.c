#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define REQUIRE(expr) do { if (!(expr)) { \
    fprintf(stderr, "phase21c failed at %s:%d (%s)\n", \
            __FILE__, __LINE__, #expr); abort(); } } while (0)
#define OK(expr) REQUIRE((expr) == 0)
enum { PRODUCERS = 4, CONSUMERS = 4, ITEMS = 2400, QUEUE_SIZE = 13 };
typedef struct {
    ios27_21c_runtime *rt;
    ios27_21c_handle mutex, not_empty, not_full;
    unsigned queue[QUEUE_SIZE];
    int head, tail, length, consumed;
    uint64_t sum;
} queue_state;
typedef struct { queue_state *queue; int id; } producer_arg;
static void *producer(void *arg) {
    producer_arg *p = arg;
    queue_state *q = p->queue;
    for (int i = 0; i < ITEMS; ++i) {
        OK(ios27_21c_mutex_lock(q->rt, q->mutex));
        while (q->length == QUEUE_SIZE)
            OK(ios27_21c_cond_wait(q->rt, q->not_full, q->mutex));
        q->queue[q->tail] = (unsigned)(p->id * ITEMS + i + 1);
        q->tail = (q->tail + 1) % QUEUE_SIZE;
        ++q->length;
        OK(ios27_21c_cond_signal(q->rt, q->not_empty));
        OK(ios27_21c_mutex_unlock(q->rt, q->mutex));
    }
    return NULL;
}
static void *consumer(void *arg) {
    queue_state *q = arg;
    for (;;) {
        OK(ios27_21c_mutex_lock(q->rt, q->mutex));
        while (!q->length && q->consumed != PRODUCERS * ITEMS)
            OK(ios27_21c_cond_wait(q->rt, q->not_empty, q->mutex));
        if (!q->length && q->consumed == PRODUCERS * ITEMS) {
            OK(ios27_21c_mutex_unlock(q->rt, q->mutex));
            return NULL;
        }
        q->sum += q->queue[q->head];
        q->head = (q->head + 1) % QUEUE_SIZE;
        --q->length;
        ++q->consumed;
        OK(ios27_21c_cond_signal(q->rt, q->not_full));
        if (q->consumed == PRODUCERS * ITEMS)
            OK(ios27_21c_cond_broadcast(q->rt, q->not_empty));
        OK(ios27_21c_mutex_unlock(q->rt, q->mutex));
    }
}
static void test_queue(void) {
    queue_state q = {0};
    pthread_t producers[PRODUCERS], consumers[CONSUMERS];
    producer_arg args[PRODUCERS];
    OK(ios27_21c_runtime_create(&q.rt, 8));
    OK(ios27_21c_sync_create(q.rt, IOS27_21C_NORMAL_MUTEX, &q.mutex));
    OK(ios27_21c_sync_create(q.rt, IOS27_21C_CONDITION, &q.not_empty));
    OK(ios27_21c_sync_create(q.rt, IOS27_21C_CONDITION, &q.not_full));
    for (int i = 0; i < CONSUMERS; ++i)
        OK(pthread_create(&consumers[i], NULL, consumer, &q));
    for (int i = 0; i < PRODUCERS; ++i) {
        args[i] = (producer_arg){&q, i};
        OK(pthread_create(&producers[i], NULL, producer, &args[i]));
    }
    for (int i = 0; i < PRODUCERS; ++i) OK(pthread_join(producers[i], NULL));
    for (int i = 0; i < CONSUMERS; ++i) OK(pthread_join(consumers[i], NULL));
    REQUIRE(q.consumed == PRODUCERS * ITEMS && q.length == 0);
    REQUIRE(q.sum == (uint64_t)(PRODUCERS * ITEMS) *
                     (uint64_t)(PRODUCERS * ITEMS + 1) / 2u);
    OK(ios27_21c_sync_destroy(q.rt, q.not_full));
    OK(ios27_21c_sync_destroy(q.rt, q.not_empty));
    OK(ios27_21c_sync_destroy(q.rt, q.mutex));
    OK(ios27_21c_runtime_destroy(q.rt));
}
typedef struct {
    ios27_21c_runtime *rt;
    ios27_21c_handle mutex;
    int count;
} counter_state;
static void *counter(void *arg) {
    counter_state *s = arg;
    for (int i = 0; i < 6000; ++i) {
        OK(ios27_21c_mutex_lock(s->rt, s->mutex));
        ++s->count;
        OK(ios27_21c_mutex_unlock(s->rt, s->mutex));
    }
    return NULL;
}
static void *foreign_unlock(void *arg) {
    counter_state *s = arg;
    REQUIRE(ios27_21c_mutex_unlock(s->rt, s->mutex) == EPERM);
    REQUIRE(ios27_21c_mutex_trylock(s->rt, s->mutex) == EBUSY);
    return NULL;
}
static void test_mutexes(void) {
    ios27_21c_runtime *rt = NULL;
    ios27_21c_handle m, rec, errcheck, c;
    counter_state s = {0};
    pthread_t threads[10], other;
    struct timespec ts;
    OK(ios27_21c_runtime_create(&rt, 6));
    OK(ios27_21c_sync_create(rt, IOS27_21C_NORMAL_MUTEX, &m));
    OK(ios27_21c_sync_create(rt, IOS27_21C_RECURSIVE_MUTEX, &rec));
    OK(ios27_21c_sync_create(rt, IOS27_21C_ERRORCHECK_MUTEX, &errcheck));
    OK(ios27_21c_sync_create(rt, IOS27_21C_CONDITION, &c));
    s.rt = rt; s.mutex = m;
    for (int i = 0; i < 10; ++i)
        OK(pthread_create(&threads[i], NULL, counter, &s));
    for (int i = 0; i < 10; ++i) OK(pthread_join(threads[i], NULL));
    REQUIRE(s.count == 60000);
    OK(ios27_21c_mutex_lock(rt, m));
    REQUIRE(ios27_21c_mutex_lock(rt, m) == EDEADLK);
    REQUIRE(ios27_21c_sync_destroy(rt, m) == EBUSY);
    OK(pthread_create(&other, NULL, foreign_unlock, &s));
    OK(pthread_join(other, NULL));
    OK(ios27_21c_mutex_unlock(rt, m));
    REQUIRE(ios27_21c_mutex_unlock(rt, m) == EPERM);
    OK(ios27_21c_mutex_lock(rt, rec));
    OK(ios27_21c_mutex_trylock(rt, rec));
    REQUIRE(ios27_21c_cond_wait(rt, c, rec) == EINVAL);
    OK(ios27_21c_mutex_unlock(rt, rec));
    OK(ios27_21c_mutex_unlock(rt, rec));
    OK(ios27_21c_mutex_lock(rt, errcheck));
    REQUIRE(ios27_21c_mutex_lock(rt, errcheck) == EDEADLK);
    OK(ios27_21c_mutex_unlock(rt, errcheck));
    REQUIRE(ios27_21c_cond_wait(rt, c, m) == EPERM);
    OK(ios27_21c_mutex_lock(rt, m));
    OK(clock_gettime(CLOCK_REALTIME, &ts) ? errno : 0);
    ts.tv_sec -= 2;
    REQUIRE(ios27_21c_cond_timedwait(rt, c, m, IOS27_21C_REALTIME, &ts)
            == ETIMEDOUT);
    REQUIRE(ios27_21c_mutex_trylock(rt, m) == EBUSY);
    OK(ios27_21c_mutex_unlock(rt, m));
    OK(ios27_21c_sync_destroy(rt, c));
    OK(ios27_21c_sync_destroy(rt, errcheck));
    OK(ios27_21c_sync_destroy(rt, rec));
    OK(ios27_21c_sync_destroy(rt, m));
    OK(ios27_21c_runtime_destroy(rt));
}
enum { WAITER_COUNT = 12 };
typedef struct {
    ios27_21c_runtime *rt;
    ios27_21c_handle mutex, ready_cond, release_cond;
    int ready, released, go;
} wake_state;
static void *waiter(void *arg) {
    wake_state *s = arg;
    OK(ios27_21c_mutex_lock(s->rt, s->mutex));
    ++s->ready;
    OK(ios27_21c_cond_signal(s->rt, s->ready_cond));
    while (!s->go)
        OK(ios27_21c_cond_wait(s->rt, s->release_cond, s->mutex));
    ++s->released;
    OK(ios27_21c_mutex_unlock(s->rt, s->mutex));
    return NULL;
}
static void test_wakeups(void) {
    wake_state s = {0};
    pthread_t threads[WAITER_COUNT];
    struct timespec ts;
    size_t used = 0;
    OK(ios27_21c_runtime_create(&s.rt, 5));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_ERRORCHECK_MUTEX, &s.mutex));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_CONDITION, &s.ready_cond));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_CONDITION, &s.release_cond));
    for (int i = 0; i < WAITER_COUNT; ++i)
        OK(pthread_create(&threads[i], NULL, waiter, &s));
    OK(ios27_21c_mutex_lock(s.rt, s.mutex));
    while (s.ready != WAITER_COUNT)
        OK(ios27_21c_cond_wait(s.rt, s.ready_cond, s.mutex));
    REQUIRE(ios27_21c_sync_destroy(s.rt, s.release_cond) == EBUSY);
    REQUIRE(ios27_21c_sync_destroy(s.rt, s.mutex) == EBUSY);
    OK(clock_gettime(CLOCK_MONOTONIC, &ts) ? errno : 0);
    ts.tv_sec -= 1;
    REQUIRE(ios27_21c_cond_timedwait(s.rt, s.release_cond, s.mutex,
                                      IOS27_21C_MONOTONIC, &ts)
            == ETIMEDOUT);
    s.go = 1;
    OK(ios27_21c_cond_broadcast(s.rt, s.release_cond));
    OK(ios27_21c_mutex_unlock(s.rt, s.mutex));
    for (int i = 0; i < WAITER_COUNT; ++i)
        OK(pthread_join(threads[i], NULL));
    REQUIRE(s.released == WAITER_COUNT);
    ios27_21c_usage(s.rt, &used);
    REQUIRE(used == 3);
    OK(ios27_21c_sync_destroy(s.rt, s.ready_cond));
    OK(ios27_21c_sync_destroy(s.rt, s.release_cond));
    OK(ios27_21c_sync_destroy(s.rt, s.mutex));
    OK(ios27_21c_runtime_destroy(s.rt));
}
static void test_invalid_and_quotas(void) {
    ios27_21c_runtime *rt = NULL;
    ios27_21c_handle a, b, fresh, invalid = {0, 0};
    struct timespec invalid_time = {1, 1000000000L};
    size_t used = 0;
    REQUIRE(ios27_21c_runtime_create(NULL, 3) == EINVAL);
    REQUIRE(ios27_21c_runtime_create(&rt, 0) == EINVAL);
    OK(ios27_21c_runtime_create(&rt, 2));
    OK(ios27_21c_sync_create(rt, IOS27_21C_NORMAL_MUTEX, &a));
    OK(ios27_21c_sync_create(rt, IOS27_21C_CONDITION, &b));
    REQUIRE(ios27_21c_sync_create(rt, IOS27_21C_CONDITION, &fresh) == EAGAIN);
    REQUIRE(ios27_21c_mutex_lock(rt, b) == EINVAL);
    REQUIRE(ios27_21c_mutex_lock(rt, invalid) == EINVAL);
    REQUIRE(ios27_21c_cond_signal(rt, a) == EINVAL);
    REQUIRE(ios27_21c_runtime_destroy(rt) == EBUSY);
    OK(ios27_21c_mutex_lock(rt, a));
    REQUIRE(ios27_21c_cond_timedwait(rt, b, a, IOS27_21C_REALTIME,
                                      &invalid_time) == EINVAL);
    OK(ios27_21c_mutex_unlock(rt, a));
    OK(ios27_21c_sync_destroy(rt, a));
    OK(ios27_21c_sync_create(rt, IOS27_21C_ERRORCHECK_MUTEX, &fresh));
    REQUIRE(fresh.slot == a.slot && fresh.generation != a.generation);
    REQUIRE(ios27_21c_mutex_lock(rt, a) == EINVAL);
    ios27_21c_usage(rt, &used);
    REQUIRE(used == 2);
    OK(ios27_21c_sync_destroy(rt, fresh));
    OK(ios27_21c_sync_destroy(rt, b));
    OK(ios27_21c_runtime_destroy(rt));
}
int main(void) {
    test_queue();
    test_mutexes();
    test_wakeups();
    test_invalid_and_quotas();
    puts("phase21c: producer-consumer, contention, owner validation, wakeups, "
         "timeouts, destruction, quotas and stale handles passed");
    return 0;
}
