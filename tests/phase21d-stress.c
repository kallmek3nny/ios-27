#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(call) do { int rc_ = (call); if (rc_) { \
    fprintf(stderr, "%s:%d: %s -> %d\n", __FILE__, __LINE__, #call, rc_); abort(); \
} } while (0)
#define REQUIRE(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #cond); abort(); \
} } while (0)
#define NREAD 32
#define NWRITE 16
#define ITERATIONS 1800

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    pthread_barrier_t *start;
    atomic_int reading;
    atomic_int writing;
    int64_t left, right;
    atomic_long read_ops, write_ops;
} shared_data;

typedef struct { shared_data *shared; int is_writer; } worker_arg;

static void *worker(void *opaque) {
    worker_arg *arg = opaque;
    shared_data *s = arg->shared;
    int ready = pthread_barrier_wait(s->start);
    REQUIRE(ready == 0 || ready == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < ITERATIONS; ++i) {
        if (arg->is_writer) {
            CHECK(ios27_21d_wrlock(s->rt, s->lock));
            REQUIRE(atomic_fetch_add(&s->writing, 1) == 0);
            REQUIRE(atomic_load(&s->reading) == 0);
            ++s->left;
            s->right += 3;
            REQUIRE(atomic_fetch_sub(&s->writing, 1) == 1);
            CHECK(ios27_21d_unlock(s->rt, s->lock));
            atomic_fetch_add(&s->write_ops, 1);
        } else {
            CHECK(ios27_21d_rdlock(s->rt, s->lock));
            atomic_fetch_add(&s->reading, 1);
            REQUIRE(atomic_load(&s->writing) == 0);
            REQUIRE(s->right == 3 * s->left);
            REQUIRE(atomic_fetch_sub(&s->reading, 1) >= 1);
            CHECK(ios27_21d_unlock(s->rt, s->lock));
            atomic_fetch_add(&s->read_ops, 1);
        }
    }
    return NULL;
}

static void stress(void) {
    pthread_t threads[NREAD + NWRITE];
    worker_arg args[NREAD + NWRITE];
    pthread_barrier_t start;
    shared_data s = {0};
    size_t objects, waiting, holders;
    CHECK(ios27_21d_runtime_create(&s.rt, 8));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_RWLOCK, &s.lock));
    CHECK(pthread_barrier_init(&start, NULL, NREAD + NWRITE + 1));
    s.start = &start;
    for (int i = 0; i < NREAD + NWRITE; ++i) {
        args[i] = (worker_arg){&s, i >= NREAD};
        CHECK(pthread_create(&threads[i], NULL, worker, &args[i]));
    }
    int ready = pthread_barrier_wait(&start);
    REQUIRE(ready == 0 || ready == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < NREAD + NWRITE; ++i) CHECK(pthread_join(threads[i], NULL));
    CHECK(pthread_barrier_destroy(&start));
    REQUIRE(s.left == (int64_t)NWRITE * ITERATIONS);
    REQUIRE(s.right == 3 * s.left);
    REQUIRE(atomic_load(&s.read_ops) == (long)NREAD * ITERATIONS);
    REQUIRE(atomic_load(&s.write_ops) == (long)NWRITE * ITERATIONS);
    CHECK(ios27_21d_usage(s.rt, &objects, &waiting, &holders));
    REQUIRE(objects == 1 && waiting == 0 && holders == 0);
    CHECK(ios27_21d_destroy(s.rt, s.lock));
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d stress: 86,400 mixed acquisitions, writer/reader exclusion verified");
}

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    atomic_int *sequence;
    int index;
    int *order;
    int result;
} order_arg;

static void *order_worker(void *opaque) {
    order_arg *a = opaque;
    a->result = a->index == 0 ? ios27_21d_wrlock(a->rt, a->lock) :
                                 ios27_21d_rdlock(a->rt, a->lock);
    if (!a->result) {
        int pos = atomic_fetch_add(a->sequence, 1);
        a->order[pos] = a->index;
        CHECK(ios27_21d_unlock(a->rt, a->lock));
    }
    return NULL;
}

static void wait_queued(ios27_21d_runtime *rt, size_t target) {
    size_t waiting = 0;
    for (unsigned i = 0; i < 2000000u; ++i) {
        CHECK(ios27_21d_usage(rt, NULL, &waiting, NULL));
        if (waiting >= target) return;
        (void)sched_yield();
    }
    REQUIRE(waiting >= target);
}

static void test_fifo_and_invalid_owner(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    pthread_t first, second;
    atomic_int sequence = 0;
    int order[2] = {-1, -1};
    order_arg writer, reader;
    CHECK(ios27_21d_runtime_create(&rt, 2));
    CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &lock));
    CHECK(ios27_21d_rdlock(rt, lock));
    REQUIRE(ios27_21d_tryrdlock(rt, lock) == EDEADLK);
    REQUIRE(ios27_21d_wrlock(rt, lock) == EDEADLK);
    REQUIRE(ios27_21d_destroy(rt, lock) == EBUSY);
    writer = (order_arg){rt, lock, &sequence, 0, order, -1};
    reader = (order_arg){rt, lock, &sequence, 1, order, -1};
    CHECK(pthread_create(&first, NULL, order_worker, &writer));
    wait_queued(rt, 1);
    REQUIRE(ios27_21d_tryrdlock(rt, lock) == EDEADLK);
    CHECK(pthread_create(&second, NULL, order_worker, &reader));
    wait_queued(rt, 2);
    CHECK(ios27_21d_unlock(rt, lock));
    CHECK(pthread_join(first, NULL));
    CHECK(pthread_join(second, NULL));
    REQUIRE(writer.result == 0 && reader.result == 0);
    REQUIRE(order[0] == 0 && order[1] == 1);
    REQUIRE(ios27_21d_unlock(rt, lock) == EPERM);
    CHECK(ios27_21d_destroy(rt, lock));
    REQUIRE(ios27_21d_rdlock(rt, lock) == EINVAL);
    CHECK(ios27_21d_runtime_destroy(rt));
    puts("phase21d fairness: queued writer wins before later reader; stale and recursion rejected");
}

static void test_exhaustion(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle h[2], rejected, stale;
    CHECK(ios27_21d_runtime_create(&rt, 2));
    CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &h[0]));
    CHECK(ios27_21d_create(rt, IOS27_21D_ONCE, &h[1]));
    REQUIRE(ios27_21d_create(rt, IOS27_21D_RWLOCK, &rejected) == EAGAIN);
    REQUIRE(ios27_21d_runtime_destroy(rt) == EBUSY);
    stale = h[0];
    for (int i = 0; i < 8000; ++i) {
        CHECK(ios27_21d_destroy(rt, h[0]));
        CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &h[0]));
        REQUIRE(ios27_21d_rdlock(rt, stale) == EINVAL);
        CHECK(ios27_21d_tryrdlock(rt, h[0]));
        REQUIRE(ios27_21d_trywrlock(rt, h[0]) == EDEADLK);
        CHECK(ios27_21d_unlock(rt, h[0]));
    }
    CHECK(ios27_21d_destroy(rt, h[0]));
    CHECK(ios27_21d_destroy(rt, h[1]));
    CHECK(ios27_21d_runtime_destroy(rt));
    puts("phase21d exhaustion: 8000 destroy/recreate cycles and stale handle prevention");
}


/* 2,400 compiler-produced native workers, split across finite waves so host
   pthread resource limits do not become an accidental test dependency. */
typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    int64_t first;
    int64_t second;
    atomic_int writes;
    atomic_int reads;
} churn_data;
typedef struct { churn_data *data; int writer; } churn_arg;
static void *churn_worker(void *opaque) {
    churn_arg *a = opaque;
    churn_data *s = a->data;
    for (int i = 0; i < 160; ++i) {
        if (a->writer) {
            CHECK(ios27_21d_wrlock(s->rt, s->lock));
            ++s->first;
            s->second += 7;
            CHECK(ios27_21d_unlock(s->rt, s->lock));
            atomic_fetch_add(&s->writes, 1);
        } else {
            CHECK(ios27_21d_rdlock(s->rt, s->lock));
            REQUIRE(s->second == s->first * 7);
            CHECK(ios27_21d_unlock(s->rt, s->lock));
            atomic_fetch_add(&s->reads, 1);
        }
    }
    return NULL;
}
static void test_worker_churn(void) {
    enum { BATCH = 60, WAVES = 40 };
    churn_data s = {0};
    churn_arg args[BATCH];
    pthread_t threads[BATCH];
    CHECK(ios27_21d_runtime_create(&s.rt, 1));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_RWLOCK, &s.lock));
    for (int wave = 0; wave < WAVES; ++wave) {
        for (int i = 0; i < BATCH; ++i) {
            args[i] = (churn_arg){.data = &s, .writer = i % 2};
            CHECK(pthread_create(&threads[i], NULL, churn_worker, &args[i]));
        }
        for (int i = 0; i < BATCH; ++i) CHECK(pthread_join(threads[i], NULL));
    }
    REQUIRE(s.second == s.first * 7);
    REQUIRE(s.first == (int64_t)WAVES * (BATCH / 2) * 160);
    REQUIRE(atomic_load(&s.writes) == s.first);
    REQUIRE(atomic_load(&s.reads) == s.first);
    CHECK(ios27_21d_destroy(s.rt, s.lock));
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d churn: 2,400 native workers and 384,000 acquisitions verified");
}

int main(void) {
    test_worker_churn();
    stress();
    test_fifo_and_invalid_owner();
    test_exhaustion();
    return 0;
}
